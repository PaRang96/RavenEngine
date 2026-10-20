#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace Raven
{
	class AssetId;
	template<typename T> struct AssetRef;
	struct Vec3;
	struct Vec4;
	struct Mat4;
	class MeshHandle;
}

namespace Raven::ECS
{
	class Entity;

	// Custom value structs must explicitly opt in after reviewing their members.
	// Approval never bypasses the structural checks applied to each field below.
	template<typename T>
	struct ComponentValue : std::bool_constant<std::is_arithmetic_v<T> || std::is_enum_v<T>>
	{
	};

	template<typename T, std::size_t N>
	struct ComponentValue<std::array<T, N>> : ComponentValue<std::remove_cv_t<T>> {};
	template<typename T, std::size_t N>
	struct ComponentValue<T[N]> : ComponentValue<std::remove_cv_t<T>> {};

	template<> struct ComponentValue<Vec3> : std::true_type {};
	template<> struct ComponentValue<Vec4> : std::true_type {};
	template<> struct ComponentValue<Mat4> : std::true_type {};
	template<> struct ComponentValue<Entity> : std::true_type {};
	template<> struct ComponentValue<AssetId> : std::true_type {};
	template<typename T> struct ComponentValue<AssetRef<T>> : std::true_type {};
	template<> struct ComponentValue<MeshHandle> : std::true_type {};

	// Component objects are local C++ objects. Registry retains their field values only.
	class Component
	{
	public:
		virtual ~Component() = default;
		virtual std::string_view GetTypeName() const noexcept = 0;
		virtual std::size_t GetDataSize() const noexcept = 0;
		// Calls borrow the buffer synchronously; no object or callback is retained.
		// A size mismatch returns false without modifying the buffer or component.
		virtual bool CopyDataTo(std::span<std::byte> bytes) const noexcept = 0;
		virtual bool CopyDataFrom(std::span<const std::byte> bytes) noexcept = 0;

	protected:
		Component() = default;
		Component(const Component&) = default;
		Component& operator=(const Component&) = default;
		Component(Component&&) = default;
		Component& operator=(Component&&) = default;
	};

	namespace Detail
	{
		template<typename T>
		struct ComponentFields
		{
			static constexpr bool Mutable = false;
		};

		template<typename... T>
		struct ComponentFields<std::tuple<T&...>>
		{
			static constexpr bool Valid =
				((ComponentValue<std::remove_cv_t<T>>::value &&
					std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T> &&
					std::is_trivially_destructible_v<T> && !std::is_pointer_v<T> &&
					!std::is_member_pointer_v<T> && !std::is_volatile_v<T>) && ...);
			static constexpr bool Mutable = Valid && ((!std::is_const_v<T>) && ...);
			static constexpr std::size_t Size = (sizeof(T) + ... + 0);
			using ConstFields = std::tuple<const T&...>;
		};

		template<typename... T>
		auto MakeComponentFields(T&... fields) noexcept
		{
			static_assert(ComponentFields<std::tuple<T&...>>::Valid,
				"Component fields must be approved, standard-layout, trivially copyable/destructible values or IDs");
			return std::tie(fields...);
		}

		template<typename... T>
		bool CopyComponentFields(const std::tuple<T&...>& fields, std::span<std::byte> bytes) noexcept
		{
			if (bytes.size() != ComponentFields<std::tuple<T&...>>::Size)
				return false;
			[[maybe_unused]] std::size_t offset = 0;
			std::apply([&](const auto&... field)
			{
				((std::memcpy(bytes.data() + offset, std::addressof(field), sizeof(field)),
					offset += sizeof(field)), ...);
			}, fields);
			return true;
		}

		template<typename... T>
		bool RestoreComponentFields(std::tuple<T&...> fields, std::span<const std::byte> bytes) noexcept
		{
			if (bytes.size() != ComponentFields<std::tuple<T&...>>::Size)
				return false;
			[[maybe_unused]] std::size_t offset = 0;
			std::apply([&](auto&... field)
			{
				((std::memcpy(std::addressof(field), bytes.data() + offset, sizeof(field)),
					offset += sizeof(field)), ...);
			}, fields);
			return true;
		}
	}

	template<typename T>
	concept ComponentType = std::derived_from<T, Component> &&
		(!std::is_const_v<T>) && (!std::is_volatile_v<T>) && (!std::is_abstract_v<T>) &&
		requires(T& value, const T& constant)
	{
		typename T::ComponentSelf;
		requires std::same_as<T, typename T::ComponentSelf>;
		{ T::TypeName } -> std::convertible_to<std::string_view>;
		{ value.GetFields() } noexcept;
		{ constant.GetFields() } noexcept;
		requires Detail::ComponentFields<decltype(value.GetFields())>::Mutable;
		requires std::same_as<decltype(constant.GetFields()),
			typename Detail::ComponentFields<decltype(value.GetFields())>::ConstFields>;
	} && (!T::TypeName.empty());
}

// Place in the public section after declaring the fields, using the qualified type.
// List all retained fields; virtual tables and other object bookkeeping are excluded.
#define RAVEN_COMPONENT(Type, ...) \
	using ComponentSelf = Type; \
	static constexpr ::std::string_view TypeName = #Type; \
	::std::string_view GetTypeName() const noexcept override { return TypeName; } \
	auto GetFields() noexcept { return ::Raven::ECS::Detail::MakeComponentFields(__VA_ARGS__); } \
	auto GetFields() const noexcept { return ::Raven::ECS::Detail::MakeComponentFields(__VA_ARGS__); } \
	::std::size_t GetDataSize() const noexcept override \
	{ return ::Raven::ECS::Detail::ComponentFields<decltype(GetFields())>::Size; } \
	bool CopyDataTo(::std::span<::std::byte> bytes) const noexcept override \
	{ return ::Raven::ECS::Detail::CopyComponentFields(GetFields(), bytes); } \
	bool CopyDataFrom(::std::span<const ::std::byte> bytes) noexcept override \
	{ return ::Raven::ECS::Detail::RestoreComponentFields(GetFields(), bytes); }
