#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
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

	// Every component derives from this empty, non-virtual base. Registry stores component
	// objects in their native layout, so a component is a standard-layout, trivially copyable
	// value without virtual functions. Per-type behavior is registered, never virtual.
	struct Component
	{
	};

	// Type-erased description used by Registry and generic tools. Name refers to static
	// storage in the declaring module; Registry copies it before retaining anything.
	struct ComponentInfo
	{
		std::uint64_t Id = 0;
		std::string_view Name;
		std::size_t Size = 0;
		std::size_t Alignment = 0;
		std::uint64_t LayoutHash = 0;
	};

	namespace Detail
	{
		inline constexpr std::uint64_t HashOffset = 14695981039346656037ull;
		inline constexpr std::uint64_t HashPrime = 1099511628211ull;

		// 64-bit FNV-1a; stable across compilers, configurations and module generations.
		constexpr std::uint64_t HashText(std::string_view text, std::uint64_t hash = HashOffset) noexcept
		{
			for (const char character : text)
			{
				hash ^= static_cast<unsigned char>(character);
				hash *= HashPrime;
			}
			return hash;
		}

		constexpr std::uint64_t HashNumber(std::uint64_t value, std::uint64_t hash) noexcept
		{
			for (int byte = 0; byte < 8; ++byte)
			{
				hash ^= (value >> (byte * 8)) & 0xffu;
				hash *= HashPrime;
			}
			return hash;
		}

		constexpr std::size_t AlignUp(std::size_t offset, std::size_t alignment) noexcept
		{
			return (offset + alignment - 1) / alignment * alignment;
		}

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
			static constexpr std::size_t Count = sizeof...(T);
			using ConstFields = std::tuple<const T&...>;

			static constexpr std::size_t Alignment() noexcept
			{
				std::size_t alignment = 1;
				((alignment = alignof(T) > alignment ? alignof(T) : alignment), ...);
				return alignment;
			}

			// Size of the listed fields laid out in order with natural alignment.
			static constexpr std::size_t LaidOutSize() noexcept
			{
				std::size_t offset = 0;
				((offset = AlignUp(offset, alignof(T)) + sizeof(T)), ...);
				return AlignUp(offset, Alignment());
			}

			static constexpr std::uint64_t HashLayout(std::uint64_t hash) noexcept
			{
				((hash = HashNumber(alignof(T), HashNumber(sizeof(T), hash))), ...);
				return hash;
			}
		};

		template<typename... T>
		auto MakeComponentFields(T&... fields) noexcept
		{
			static_assert(ComponentFields<std::tuple<T&...>>::Valid,
				"Component fields must be approved, standard-layout, trivially copyable/destructible values or IDs");
			return std::tie(fields...);
		}
	}

	template<typename T>
	concept ComponentType = std::derived_from<T, Component> &&
		(!std::is_const_v<T>) && (!std::is_volatile_v<T>) &&
		requires(T& value, const T& constant)
	{
		typename T::ComponentSelf;
		requires std::same_as<T, typename T::ComponentSelf>;
		{ T::TypeName } -> std::convertible_to<std::string_view>;
		{ T::TypeId } -> std::convertible_to<std::uint64_t>;
		{ T::FieldNames } -> std::convertible_to<std::string_view>;
		{ value.GetFields() } noexcept;
		{ constant.GetFields() } noexcept;
		requires Detail::ComponentFields<decltype(value.GetFields())>::Mutable;
		requires std::same_as<decltype(constant.GetFields()),
			typename Detail::ComponentFields<decltype(value.GetFields())>::ConstFields>;
	} && (!T::TypeName.empty());

	// Checked once per component type at its first Registry use.
	template<ComponentType T>
	struct ComponentTraits
	{
		using Fields = Detail::ComponentFields<decltype(std::declval<T&>().GetFields())>;

		static_assert(!std::is_polymorphic_v<T>, "Components must not declare virtual functions");
		static_assert(std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T> &&
			std::is_trivially_destructible_v<T>,
			"Components must be standard-layout, trivially copyable and trivially destructible");
		static_assert(Fields::Count == 0 ? std::is_empty_v<T> :
			(Fields::LaidOutSize() == sizeof(T) && Fields::Alignment() == alignof(T)),
			"RAVEN_COMPONENT must list every data member, in declaration order");

		static constexpr ComponentInfo Info
		{
			T::TypeId, T::TypeName, sizeof(T), alignof(T),
			Fields::HashLayout(Detail::HashNumber(sizeof(T),
				Detail::HashText(T::FieldNames, Detail::HashText(T::TypeName))))
		};
	};
}

// Place in the public section after declaring the fields, using the qualified type.
// List every data member in declaration order, or omit fields for an empty tag.
#define RAVEN_COMPONENT(Type, ...) \
	using ComponentSelf = Type; \
	static constexpr ::std::string_view TypeName = #Type; \
	static constexpr ::std::uint64_t TypeId = ::Raven::ECS::Detail::HashText(TypeName); \
	static constexpr ::std::string_view FieldNames = "" #__VA_ARGS__; \
	auto GetFields() noexcept { return ::Raven::ECS::Detail::MakeComponentFields(__VA_ARGS__); } \
	auto GetFields() const noexcept { return ::Raven::ECS::Detail::MakeComponentFields(__VA_ARGS__); }
