#pragma once

#include <cmath>

// Math for fixed-step simulation code (docs/GameServicesDesign.md, FD-48). Level-1 determinism
// (same build, same machine) holds with the standard functions, which these wrap today. Standard
// library results can differ between machines and runtime versions, so lockstep across PCs
// (level 2) or platforms (level 3) will replace the bodies with bit-exact versions; simulation code
// that calls these needs no change then.
namespace Raven::SimulationMath
{
	inline float Sin(float value) noexcept { return std::sin(value); }
	inline float Cos(float value) noexcept { return std::cos(value); }
	inline float Tan(float value) noexcept { return std::tan(value); }
	inline float Asin(float value) noexcept { return std::asin(value); }
	inline float Acos(float value) noexcept { return std::acos(value); }
	inline float Atan2(float y, float x) noexcept { return std::atan2(y, x); }
	inline float Sqrt(float value) noexcept { return std::sqrt(value); }
	inline float Exp(float value) noexcept { return std::exp(value); }
	inline float Log(float value) noexcept { return std::log(value); }
	inline float Pow(float base, float exponent) noexcept { return std::pow(base, exponent); }
}
