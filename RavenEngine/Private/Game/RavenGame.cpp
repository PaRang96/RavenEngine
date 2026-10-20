#include "Raven/Core/Api.hpp"
#include "Raven/Core/ApplicationClient.hpp"
#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/GameInstance.hpp"
#include "Raven/Core/Log.hpp"
#include "GameComponents.hpp"
#include "GameContract.hpp"

#include <algorithm>
#include <exception>

namespace
{
	void Update(Raven::GameInstance& instance, const Raven::InputState&,
		const Raven::FrameTime& frame) noexcept
	{
		try
		{
			constexpr float MovementScale = 1.0f;
			const float delta = static_cast<float>(std::clamp(frame.DeltaSeconds, 0.0, 0.1));
			auto& registry = instance.GetRegistry();
			for (const auto entity : registry.GetEntities())
			{
				Raven::SampleGame::Position position{};
				Raven::SampleGame::Velocity velocity{};
				if (registry.Read(entity, position) && registry.Read(entity, velocity))
				{
					position.Location = position.Location + velocity.Linear * (delta * MovementScale);
					if (position.Location.X > 3.0f || position.Location.X < -3.0f)
					{
						position.Location.X = std::clamp(position.Location.X, -3.0f, 3.0f);
						velocity.Linear.X = -velocity.Linear.X;
						registry.Write(entity, velocity);
					}
					registry.Write(entity, position);
				}
			}
		}
		catch (const std::exception& error)
		{
			Raven::Log(Raven::LogLevel::Error, error.what());
		}
		catch (...)
		{
			Raven::Log(Raven::LogLevel::Error, "Unknown game update failure");
		}
	}
}

// These two C-linkage signatures stay fixed. Check the contract before fetching Update.
extern "C" RAVEN_GAME_API const char* RavenGetGameContract() noexcept
{
	return Raven::GameBuild::Contract;
}

extern "C" RAVEN_GAME_API Raven::GameUpdateFunction RavenGetGameUpdate() noexcept
{
	return &Update;
}
