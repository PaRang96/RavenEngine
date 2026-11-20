#include "Raven/Math/Collision2D.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Raven
{
	namespace
	{
		struct SweepAxis
		{
			double Entry;
			double Exit;
			double Contact;
		};

		struct SweepHit
		{
			double Time = 1.0;
			bool BlocksX = false;
			bool BlocksZ = false;
			double ContactX = 0.0;
			double ContactZ = 0.0;
		};

		float RoundOutward(double contact, double delta)
		{
			const float position = static_cast<float>(contact);
			const float infinity = std::numeric_limits<float>::infinity();
			if (delta > 0.0 && position > contact)
				return std::nextafter(position, -infinity);
			if (delta < 0.0 && position < contact)
				return std::nextafter(position, infinity);
			return position;
		}

		bool Sweep(double origin, double delta, double center, double extent, SweepAxis& axis)
		{
			// A float center that rounds to a face must remain outside the solid.
			const double lower = RoundOutward(center - extent, 1.0);
			const double upper = RoundOutward(center + extent, -1.0);
			if (delta == 0.0)
			{
				const double infinity = std::numeric_limits<double>::infinity();
				axis = { -infinity, infinity, origin };
				return origin > lower && origin < upper;
			}
			if (delta > 0.0)
				axis = { (lower - origin) / delta, (upper - origin) / delta, lower };
			else
				axis = { (upper - origin) / delta, (lower - origin) / delta, upper };
			return true;
		}

		bool SweepSolid(double x, double z, double dx, double dz, XzHalfExtents halfExtents,
			const XzBounds& solid, SweepHit& hit)
		{
			SweepAxis xAxis;
			SweepAxis zAxis;
			if (!Sweep(x, dx, solid.Center.X, static_cast<double>(halfExtents.X) + solid.HalfExtents.X, xAxis) ||
				!Sweep(z, dz, solid.Center.Z, static_cast<double>(halfExtents.Z) + solid.HalfExtents.Z, zAxis))
				return false;

			const double entry = std::max(xAxis.Entry, zAxis.Entry);
			const double exit = std::min(xAxis.Exit, zAxis.Exit);
			if (entry < 0.0 || entry > 1.0 || entry >= exit)
				return false;

			hit = { entry, xAxis.Entry == entry, zAxis.Entry == entry, xAxis.Contact, zAxis.Contact };
			return true;
		}
	}

	bool OverlapsXZ(const XzBounds& first, const XzBounds& second) noexcept
	{
		return std::abs(static_cast<double>(first.Center.X) - second.Center.X) <=
			static_cast<double>(first.HalfExtents.X) + second.HalfExtents.X &&
			std::abs(static_cast<double>(first.Center.Z) - second.Center.Z) <=
			static_cast<double>(first.HalfExtents.Z) + second.HalfExtents.Z;
	}

	bool PenetratesXZ(const XzBounds& first, const XzBounds& second) noexcept
	{
		return std::abs(static_cast<double>(first.Center.X) - second.Center.X) <
			static_cast<double>(first.HalfExtents.X) + second.HalfExtents.X &&
			std::abs(static_cast<double>(first.Center.Z) - second.Center.Z) <
			static_cast<double>(first.HalfExtents.Z) + second.HalfExtents.Z;
	}

	Vec3 ResolveMovementXZ(const XzBounds& moving, Vec3 displacement,
		std::span<const XzBounds> solids) noexcept
	{
		double x = moving.Center.X;
		double z = moving.Center.Z;
		double dx = displacement.X;
		double dz = displacement.Z;
		// Each impact removes at least one axis from the remaining motion.
		while (dx != 0.0 || dz != 0.0)
		{
			SweepHit nearest;
			for (const auto& solid : solids)
			{
				SweepHit hit;
				if (!SweepSolid(x, z, dx, dz, moving.HalfExtents, solid, hit) || hit.Time > nearest.Time)
					continue;
				if (hit.Time < nearest.Time)
					nearest = hit;
				else
				{
					if (hit.BlocksX)
					{
						nearest.BlocksX = true;
						nearest.ContactX = hit.ContactX;
					}
					if (hit.BlocksZ)
					{
						nearest.BlocksZ = true;
						nearest.ContactZ = hit.ContactZ;
					}
				}
			}
			x += dx * nearest.Time;
			z += dz * nearest.Time;
			if (nearest.BlocksX)
				x = nearest.ContactX;
			if (nearest.BlocksZ)
				z = nearest.ContactZ;
			dx = nearest.BlocksX ? 0.0 : dx * (1.0 - nearest.Time);
			dz = nearest.BlocksZ ? 0.0 : dz * (1.0 - nearest.Time);
		}
		return { static_cast<float>(x), moving.Center.Y, static_cast<float>(z) };
	}
}
