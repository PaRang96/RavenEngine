#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace Raven
{
	class AssetId;
	template<typename T> struct AssetRef;
	// Specialized beside each asset type with a stable Name; the field table tags AssetRef fields with it.
	template<typename T> struct AssetTypeTraits;
	struct Vec3;
	struct Vec4;
	struct Mat4;
}

namespace Raven::ECS
{
	class Entity;
	struct EntityRef;

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
	template<> struct ComponentValue<EntityRef> : std::true_type {};
	template<> struct ComponentValue<AssetId> : std::true_type {};
	template<typename T> struct ComponentValue<AssetRef<T>> : std::true_type {};

	// Components only the host writes (docs/DataModelDesign.md, FD-07). SceneAccess rejects mutable
	// access to them at compile time; game code changes them through Scene or Commands.
	template<typename T>
	inline constexpr bool HostOwned = false;

	// Components derived from others each frame (WorldTransform). Scene files never store them.
	template<typename T>
	inline constexpr bool Transient = false;

	// ComponentInfo::Flags.
	inline constexpr std::uint32_t ComponentIsTransient = 1;
	// HostOwned<T>: type-erased callers (SceneCommands, SceneAccess) reject writes as the templates do.
	inline constexpr std::uint32_t ComponentIsHostOwned = 2;

	// Every component derives from this empty, non-virtual base. Registry stores component
	// objects in their native layout, so a component is a standard-layout, trivially copyable
	// value without virtual functions. Per-type behavior is registered, never virtual.
	struct Component
	{
	};

	// The value kind of one component field (docs/DataModelDesign.md, FD-08). Arrays keep the
	// element kind and record their extents. Entity is a runtime handle that no file may store.
	// File format; never change: scenes and player profiles store these values, so a new kind is
	// appended with the next value and no value is ever reused (FD-10, version policy).
	enum class FieldKind : std::uint8_t
	{
		Bool = 0, Char = 1, Int8 = 2, UInt8 = 3, Int16 = 4, UInt16 = 5, Int32 = 6, UInt32 = 7, Int64 = 8,
		UInt64 = 9, Float32 = 10, Float64 = 11, Vec3 = 12, Vec4 = 13, Mat4 = 14, Entity = 15, AssetId = 16,
		AssetRef = 17, EntityRef = 18,
		// An approved custom value, compared by size only.
		Opaque = 19,
	};

	// FieldInfo::Flags.
	inline constexpr std::uint8_t FieldIsEnum = 1;

	// One row of a component's field table. Name refers to static storage in the declaring
	// module; Registry copies the table before retaining anything. Editor metadata (display
	// names, ranges) belongs in a separate list, so it never changes the layout hash.
	struct FieldInfo
	{
		std::string_view Name;
		std::uint32_t Offset = 0;
		// The whole field, including every array element.
		std::uint32_t Size = 0;
		FieldKind Kind = FieldKind::Opaque;
		std::uint8_t Flags = 0;
		// Array extents, outer first; 1 for each dimension the field does not have.
		std::array<std::uint32_t, 2> Extent{ 1, 1 };
		// AssetRef fields: the hash of the asset type name. Zero otherwise.
		std::uint64_t Tag = 0;
	};

	// Type-erased description used by Registry and generic tools. Name and Fields refer to
	// static storage in the declaring module; Registry copies them before retaining anything.
	struct ComponentInfo
	{
		std::uint64_t Id = 0;
		std::string_view Name;
		std::size_t Size = 0;
		std::size_t Alignment = 0;
		std::uint64_t LayoutHash = 0;
		const FieldInfo* Fields = nullptr;
		std::size_t FieldCount = 0;
		std::uint32_t Flags = 0;
		// RAVEN_COMPONENT_ALIASES entries (docs/DataModelDesign.md, FD-16): an old type name, or
		// "Old=New" for a renamed field.
		const std::string_view* Aliases = nullptr;
		std::size_t AliasCount = 0;
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

		struct FieldType
		{
			FieldKind Kind = FieldKind::Opaque;
			std::uint8_t Flags = 0;
			std::array<std::uint32_t, 2> Extent{ 1, 1 };
			std::uint64_t Tag = 0;
		};

		template<typename T>
		constexpr FieldKind ScalarKind() noexcept
		{
			if constexpr (std::is_same_v<T, bool>)
				return FieldKind::Bool;
			else if constexpr (std::is_same_v<T, char>)
				return FieldKind::Char;
			else if constexpr (std::is_floating_point_v<T>)
			{
				static_assert(sizeof(T) == 4 || sizeof(T) == 8, "Component floats must be 32 or 64 bits");
				return sizeof(T) == 4 ? FieldKind::Float32 : FieldKind::Float64;
			}
			else
			{
				static_assert(std::is_integral_v<T> && !std::is_same_v<T, wchar_t> && !std::is_same_v<T, char8_t> &&
					!std::is_same_v<T, char16_t> && !std::is_same_v<T, char32_t>,
					"Component text uses char; other character types have no field kind");
				constexpr bool Signed = std::is_signed_v<T>;
				if constexpr (sizeof(T) == 1)
					return Signed ? FieldKind::Int8 : FieldKind::UInt8;
				else if constexpr (sizeof(T) == 2)
					return Signed ? FieldKind::Int16 : FieldKind::UInt16;
				else if constexpr (sizeof(T) == 4)
					return Signed ? FieldKind::Int32 : FieldKind::UInt32;
				else
					return Signed ? FieldKind::Int64 : FieldKind::UInt64;
			}
		}

		template<typename T>
		constexpr FieldType MakeFieldType() noexcept
		{
			if constexpr (std::is_enum_v<T>)
				return FieldType{ ScalarKind<std::underlying_type_t<T>>(), FieldIsEnum };
			else if constexpr (std::is_arithmetic_v<T>)
				return FieldType{ ScalarKind<T>() };
			else
				return FieldType{ FieldKind::Opaque };
		}

		template<typename T>
		struct FieldTypeOf
		{
			static constexpr FieldType Value = MakeFieldType<T>();
		};
		template<> struct FieldTypeOf<Vec3> { static constexpr FieldType Value{ FieldKind::Vec3 }; };
		template<> struct FieldTypeOf<Vec4> { static constexpr FieldType Value{ FieldKind::Vec4 }; };
		template<> struct FieldTypeOf<Mat4> { static constexpr FieldType Value{ FieldKind::Mat4 }; };
		template<> struct FieldTypeOf<Entity> { static constexpr FieldType Value{ FieldKind::Entity }; };
		template<> struct FieldTypeOf<AssetId> { static constexpr FieldType Value{ FieldKind::AssetId }; };
		template<> struct FieldTypeOf<EntityRef> { static constexpr FieldType Value{ FieldKind::EntityRef }; };
		template<typename T> struct FieldTypeOf<AssetRef<T>>
		{
			static constexpr FieldType Value{ FieldKind::AssetRef, 0, { 1, 1 }, HashText(AssetTypeTraits<T>::Name) };
		};

		template<typename T, std::size_t N>
		constexpr FieldType ArrayFieldType() noexcept
		{
			constexpr FieldType element = FieldTypeOf<std::remove_cv_t<T>>::Value;
			static_assert(element.Extent[1] == 1, "Component arrays may have at most two dimensions");
			FieldType result = element;
			result.Extent = { static_cast<std::uint32_t>(N), element.Extent[0] };
			return result;
		}
		template<typename T, std::size_t N> struct FieldTypeOf<std::array<T, N>>
		{
			static constexpr FieldType Value = ArrayFieldType<T, N>();
		};
		template<typename T, std::size_t N> struct FieldTypeOf<T[N]>
		{
			static constexpr FieldType Value = ArrayFieldType<T, N>();
		};

		constexpr std::uint64_t HashFieldType(const FieldType& type, std::uint64_t hash) noexcept
		{
			hash = HashNumber(static_cast<std::uint64_t>(type.Kind), hash);
			hash = HashNumber(type.Flags, hash);
			hash = HashNumber(type.Extent[1], HashNumber(type.Extent[0], hash));
			return HashNumber(type.Tag, hash);
		}

		constexpr bool IsFieldNameSpace(char character) noexcept
		{
			return character == ' ' || character == '\t' || character == '\n' || character == '\r';
		}

		// The macro's comma-separated field list, split into names.
		template<std::size_t N>
		constexpr std::array<std::string_view, N> SplitFieldNames(std::string_view text) noexcept
		{
			std::array<std::string_view, N> names{};
			std::size_t position = 0;
			for (std::size_t field = 0; field < N && position <= text.size(); ++field)
			{
				auto end = text.find(',', position);
				if (end == std::string_view::npos)
					end = text.size();
				auto name = text.substr(position, end - position);
				while (!name.empty() && IsFieldNameSpace(name.front()))
					name.remove_prefix(1);
				while (!name.empty() && IsFieldNameSpace(name.back()))
					name.remove_suffix(1);
				names[field] = name;
				position = end + 1;
			}
			return names;
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

			// Sizes, alignments and field types: a type change of equal size changes the hash.
			static constexpr std::uint64_t HashLayout(std::uint64_t hash) noexcept
			{
				((hash = HashFieldType(FieldTypeOf<std::remove_cv_t<T>>::Value,
					HashNumber(alignof(T), HashNumber(sizeof(T), hash)))), ...);
				return hash;
			}

			// Natural-layout offsets. ComponentTraits checks that they add up to the struct, and
			// CaptureDefaults checks them against a real object.
			static constexpr std::array<FieldInfo, sizeof...(T)> Table(std::string_view fieldNames) noexcept
			{
				const auto names = SplitFieldNames<sizeof...(T)>(fieldNames);
				std::array<FieldInfo, sizeof...(T)> table{};
				std::size_t offset = 0;
				std::size_t index = 0;
				const auto add = [&]<typename U>(std::type_identity<U>)
				{
					constexpr FieldType type = FieldTypeOf<std::remove_cv_t<U>>::Value;
					offset = AlignUp(offset, alignof(U));
					table[index] = FieldInfo{ names[index], static_cast<std::uint32_t>(offset),
						static_cast<std::uint32_t>(sizeof(U)), type.Kind, type.Flags, type.Extent, type.Tag };
					offset += sizeof(U);
					++index;
				};
				(add(std::type_identity<T>{}), ...);
				return table;
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

		static constexpr auto FieldTable = []
		{
			if constexpr (Fields::Count == 0)
				return std::array<FieldInfo, 0>{};
			else
				return Fields::Table(T::FieldNames);
		}();

		static constexpr std::span<const std::string_view> AliasTable = []
		{
			if constexpr (requires { T::Aliases; })
				return std::span<const std::string_view>(T::Aliases);
			else
				return std::span<const std::string_view>{};
		}();

		static constexpr ComponentInfo Info
		{
			T::TypeId, T::TypeName, sizeof(T), alignof(T),
			Fields::HashLayout(Detail::HashNumber(sizeof(T),
				Detail::HashText(T::FieldNames, Detail::HashText(T::TypeName)))),
			FieldTable.data(), FieldTable.size(),
			(Transient<T> ? ComponentIsTransient : 0u) | (HostOwned<T> ? ComponentIsHostOwned : 0u),
			AliasTable.data(), AliasTable.size()
		};
	};

	namespace Detail
	{
		// Copies the value-initialized component field by field into zeroed storage, so padding
		// bytes are always zero. False when a field is not at its natural offset (for example
		// after alignas on a member), which the field table cannot describe.
		template<ComponentType T>
		bool CaptureDefaults(std::byte* destination) noexcept
		{
			const T value{};
			const auto* base = reinterpret_cast<const std::byte*>(&value);
			const auto& table = ComponentTraits<T>::FieldTable;
			bool natural = true;
			std::size_t index = 0;
			std::apply([&](const auto&... field)
			{
				((natural = natural &&
					reinterpret_cast<const std::byte*>(&field) - base == static_cast<std::ptrdiff_t>(table[index].Offset),
					++index), ...);
			}, value.GetFields());
			if (!natural)
				return false;
			for (std::size_t byte = 0; byte < sizeof(T); ++byte)
				destination[byte] = std::byte{ 0 };
			for (const auto& field : table)
				for (std::size_t byte = 0; byte < field.Size; ++byte)
					destination[field.Offset + byte] = base[field.Offset + byte];
			return true;
		}
	}
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

// Optional, after RAVEN_COMPONENT: names this component and its fields had before a rename
// (docs/DataModelDesign.md, FD-16). Scene files and field migration accept the old names.
//   RAVEN_COMPONENT_ALIASES("Raven::ECS::DoorComponent", "Required=RequiredRotators");
// An entry without '=' is an old type name; "Old=New" is the old name of field New. Aliases are
// metadata: they never change the layout hash.
#define RAVEN_COMPONENT_ALIASES(...) \
	static constexpr ::std::string_view Aliases[] = { __VA_ARGS__ }
