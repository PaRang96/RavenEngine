#pragma once

#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <type_traits>

namespace Raven
{
	struct Vec3
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
	};

	struct Vec4
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 0.0f;
	};

	constexpr Vec3 operator+(Vec3 left, Vec3 right)
	{
		return { left.X + right.X, left.Y + right.Y, left.Z + right.Z };
	}

	constexpr Vec3 operator-(Vec3 left, Vec3 right)
	{
		return { left.X - right.X, left.Y - right.Y, left.Z - right.Z };
	}

	constexpr Vec3 operator*(Vec3 value, float scale)
	{
		return { value.X * scale, value.Y * scale, value.Z * scale };
	}

	constexpr float Dot(Vec3 left, Vec3 right)
	{
		return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
	}

	constexpr Vec3 Cross(Vec3 left, Vec3 right)
	{
		return { left.Y * right.Z - left.Z * right.Y,
			left.Z * right.X - left.X * right.Z,
			left.X * right.Y - left.Y * right.X };
	}

	inline Vec3 Normalize(Vec3 value)
	{
		const float squaredLength = Dot(value, value);
		if (!std::isfinite(squaredLength) || squaredLength <= 1.0e-12f)
			throw std::invalid_argument("Cannot normalize a zero or non-finite direction");
		return value * (1.0f / std::sqrt(squaredLength));
	}

	// Column-major storage and column vectors: clip = projection * view * model * local.
	struct alignas(16) Mat4
	{
		float Columns[4][4]{};

		constexpr float& operator()(std::size_t row, std::size_t column)
		{
			return Columns[column][row];
		}

		constexpr float operator()(std::size_t row, std::size_t column) const
		{
			return Columns[column][row];
		}

		static constexpr Mat4 Identity()
		{
			Mat4 result;
			for (std::size_t index = 0; index < 4; ++index)
				result(index, index) = 1.0f;
			return result;
		}
	};

	static_assert(std::is_standard_layout_v<Mat4>);
	static_assert(std::is_trivially_copyable_v<Mat4>);
	static_assert(sizeof(Mat4) == 64);
	static_assert(sizeof(Vec4) == 16);

	constexpr Mat4 operator*(const Mat4& left, const Mat4& right)
	{
		Mat4 result;
		for (std::size_t column = 0; column < 4; ++column)
			for (std::size_t row = 0; row < 4; ++row)
				for (std::size_t index = 0; index < 4; ++index)
					result(row, column) += left(row, index) * right(index, column);
		return result;
	}

	constexpr Vec4 operator*(const Mat4& matrix, Vec4 value)
	{
		return {
			matrix(0, 0) * value.X + matrix(0, 1) * value.Y + matrix(0, 2) * value.Z + matrix(0, 3) * value.W,
			matrix(1, 0) * value.X + matrix(1, 1) * value.Y + matrix(1, 2) * value.Z + matrix(1, 3) * value.W,
			matrix(2, 0) * value.X + matrix(2, 1) * value.Y + matrix(2, 2) * value.Z + matrix(2, 3) * value.W,
			matrix(3, 0) * value.X + matrix(3, 1) * value.Y + matrix(3, 2) * value.Z + matrix(3, 3) * value.W
		};
	}

	constexpr Mat4 Translation(Vec3 position)
	{
		Mat4 result = Mat4::Identity();
		result(0, 3) = position.X;
		result(1, 3) = position.Y;
		result(2, 3) = position.Z;
		return result;
	}

	constexpr Mat4 Scale(Vec3 scale)
	{
		Mat4 result = Mat4::Identity();
		result(0, 0) = scale.X;
		result(1, 1) = scale.Y;
		result(2, 2) = scale.Z;
		return result;
	}

	inline Mat4 RotationX(float radians)
	{
		Mat4 result = Mat4::Identity();
		const float sine = std::sin(radians);
		const float cosine = std::cos(radians);
		result(1, 1) = cosine;
		result(1, 2) = -sine;
		result(2, 1) = sine;
		result(2, 2) = cosine;
		return result;
	}

	inline Mat4 RotationZ(float radians)
	{
		Mat4 result = Mat4::Identity();
		const float sine = std::sin(radians);
		const float cosine = std::cos(radians);
		result(0, 0) = cosine;
		result(0, 1) = -sine;
		result(1, 0) = sine;
		result(1, 1) = cosine;
		return result;
	}

	inline Mat4 LookAtRH(Vec3 eye, Vec3 target, Vec3 up)
	{
		const Vec3 forward = Normalize(target - eye);
		const Vec3 right = Normalize(Cross(forward, up));
		const Vec3 cameraUp = Cross(right, forward);
		Mat4 result = Mat4::Identity();
		result(0, 0) = right.X;
		result(0, 1) = right.Y;
		result(0, 2) = right.Z;
		result(0, 3) = -Dot(right, eye);
		result(1, 0) = cameraUp.X;
		result(1, 1) = cameraUp.Y;
		result(1, 2) = cameraUp.Z;
		result(1, 3) = -Dot(cameraUp, eye);
		result(2, 0) = -forward.X;
		result(2, 1) = -forward.Y;
		result(2, 2) = -forward.Z;
		result(2, 3) = Dot(forward, eye);
		return result;
	}

	inline Mat4 PerspectiveVulkanRH(float verticalFovRadians, float aspect,
		float nearPlane, float farPlane)
	{
		if (!std::isfinite(verticalFovRadians) || !std::isfinite(aspect) ||
			!std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
			verticalFovRadians <= 0.0f || verticalFovRadians >= std::numbers::pi_v<float> ||
			aspect <= 0.0f || nearPlane <= 0.0f || farPlane <= nearPlane)
			throw std::invalid_argument("Invalid perspective field of view, aspect, or clipping planes");

		const float focalScale = 1.0f / std::tan(verticalFovRadians * 0.5f);
		Mat4 result;
		result(0, 0) = focalScale / aspect;
		// Positive-height Vulkan viewport: world/camera up must map toward the screen top.
		result(1, 1) = -focalScale;
		result(2, 2) = farPlane / (nearPlane - farPlane);
		result(2, 3) = nearPlane * result(2, 2);
		result(3, 2) = -1.0f;
		for (const auto& column : result.Columns)
			for (const float value : column)
				if (!std::isfinite(value))
					throw std::invalid_argument("Perspective parameters exceed the supported numeric range");
		return result;
	}
}
