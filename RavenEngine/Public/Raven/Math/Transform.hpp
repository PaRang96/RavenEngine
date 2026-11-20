#pragma once

#include <cmath>
#include <array>
#include <cstddef>
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

	inline Mat4 RotationY(float radians)
	{
		Mat4 result = Mat4::Identity();
		const float sine = std::sin(radians);
		const float cosine = std::cos(radians);
		result(0, 0) = cosine;
		result(0, 2) = sine;
		result(2, 0) = -sine;
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
	// Inverse transpose of the affine model's linear part, including reflected scale.
	inline Mat4 NormalMatrix(const Mat4& model)
	{
		if (model(3, 0) != 0.0f || model(3, 1) != 0.0f || model(3, 2) != 0.0f || model(3, 3) != 1.0f)
			throw std::invalid_argument("Lit mesh model must be affine");
		const Vec3 x{ model(0, 0), model(1, 0), model(2, 0) };
		const Vec3 y{ model(0, 1), model(1, 1), model(2, 1) };
		const Vec3 z{ model(0, 2), model(1, 2), model(2, 2) };
		const float determinant = Dot(x, Cross(y, z));
		if (!std::isfinite(determinant) || determinant == 0.0f)
			throw std::invalid_argument("Lit mesh transform must be nonsingular");
		const std::array columns{ Cross(y, z), Cross(z, x), Cross(x, y) };
		Mat4 result = Mat4::Identity();
		for (std::size_t column = 0; column < columns.size(); ++column)
		{
			const Vec3 value = columns[column] * (1.0f / determinant);
			if (!std::isfinite(value.X) || !std::isfinite(value.Y) || !std::isfinite(value.Z))
				throw std::invalid_argument("Mesh normal transform exceeds the supported numeric range");
			result(0, column) = value.X;
			result(1, column) = value.Y;
			result(2, column) = value.Z;
		}
		return result;
	}

}
