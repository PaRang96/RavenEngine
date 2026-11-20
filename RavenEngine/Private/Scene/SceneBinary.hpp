#pragma once
#include "Raven/Assets/AssetId.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace Raven::SceneBinary
{
	inline constexpr std::size_t Limit = 4 * 1024 * 1024;
	inline std::uint32_t Checksum(std::span<const std::byte> bytes)
	{
		std::uint32_t value = 2166136261u;
		for (auto b : bytes)
		{
			value ^= std::to_integer<std::uint8_t>(b);
			value *= 16777619u;
		}
		return value;
	}
	struct Writer
	{
		std::vector<std::byte> Bytes;
		void U8(std::uint8_t n)
		{
			Bytes.push_back(static_cast<std::byte>(n));
		}
		void U32(std::uint32_t n)
		{
			for (int i = 0; i < 4; ++i)
				U8(static_cast<std::uint8_t>(n >> (i * 8)));
		}
		void Float(float n)
		{
			static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
			U32(std::bit_cast<std::uint32_t>(n));
		}
		void Text(const std::string& text)
		{
			U32(static_cast<std::uint32_t>(text.size()));
			for (unsigned char c : text)
				U8(c);
		}
		void Vector(Vec3 v)
		{
			Float(v.X);
			Float(v.Y);
			Float(v.Z);
		}
		void Color(Vec4 v)
		{
			Float(v.X);
			Float(v.Y);
			Float(v.Z);
			Float(v.W);
		}
		void Transform(const ECS::TransformComponent& t)
		{
			Vector(t.Position);
			Vector(t.Rotation);
			Vector(t.Scale);
		}
		void Id(AssetId id)
		{
			const auto text = id.ToString();
			int high = -1;
			for (char c : text)
				if (c != '-')
				{
					const int n = c <= '9' ? c - '0' : c - 'a' + 10;
					if (high < 0)
						high = n;
					else
					{
						U8(static_cast<std::uint8_t>(high * 16 + n));
						high = -1;
					}
				}
		}
	};
	struct Reader
	{
		std::span<const std::byte> Bytes;
		std::uint8_t U8()
		{
			if (Bytes.empty())
				throw std::runtime_error("Truncated binary scene");
			const auto n = std::to_integer<std::uint8_t>(Bytes.front());
			Bytes = Bytes.subspan(1);
			return n;
		}
		std::uint32_t U32()
		{
			std::uint32_t n = 0;
			for (int i = 0; i < 4; ++i)
				n |= static_cast<std::uint32_t>(U8()) << (i * 8);
			return n;
		}
		std::uint32_t Count(std::uint32_t limit = 4096)
		{
			const auto n = U32();
			if (n > limit)
				throw std::runtime_error("Binary scene count exceeds limit");
			return n;
		}
		float Float()
		{
			return std::bit_cast<float>(U32());
		}
		std::string Text()
		{
			const auto n = Count(128);
			if (n > Bytes.size())
				throw std::runtime_error("Truncated scene string");
			std::string s(reinterpret_cast<const char*>(Bytes.data()), n);
			Bytes = Bytes.subspan(n);
			return s;
		}
		Vec3 Vector()
		{
			const float x = Float(), y = Float(), z = Float();
			return {x, y, z};
		}
		Vec4 Color()
		{
			const float x = Float(), y = Float(), z = Float(), w = Float();
			return {x, y, z, w};
		}
		ECS::TransformComponent Transform()
		{
			ECS::TransformComponent t;
			t.Position = Vector();
			t.Rotation = Vector();
			t.Scale = Vector();
			return t;
		}
		AssetId Id()
		{
			constexpr char hex[] = "0123456789abcdef";
			std::string s;
			bool nonzero = false;
			for (int i = 0; i < 16; ++i)
			{
				if (i == 4 || i == 6 || i == 8 || i == 10)
					s += '-';
				const auto b = U8();
				nonzero |= b != 0;
				s += hex[b >> 4];
				s += hex[b & 15];
			}
			return nonzero ? AssetId::Parse(s) : AssetId{};
		}
	};
}
