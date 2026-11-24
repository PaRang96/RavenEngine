// The 2D Courtyard game: WASD moves the explorer to the green beacon around the purple wall while
// the gold courier patrols. Arrows pan, the wheel or PageUp/PageDown zooms, Home resets the camera,
// F1 toggles the transparent order and F2 switches the camera.

#include "CourtyardComponents.hpp"
#include "GameContract.hpp"

#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/SpriteComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Api.hpp"
#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/GameSystems.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Math/Collision2D.hpp"
#include "Raven/Platform/Input.hpp"
#include "Raven/Scene/SceneCommands.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
	using namespace Raven;
	using namespace Courtyard;

	float Delta(const SystemContext& context)
	{
		return static_cast<float>(std::clamp(context.Time.DeltaSeconds, 0.0, 0.1));
	}

	float KeyStep(const InputState& input, Key key, float tapStep, float heldStep)
	{
		return (input.WasKeyPressed(key) ? tapStep : 0.0f) + (input.IsKeyDown(key) ? heldStep : 0.0f);
	}

	// A press and release between two ticks still gives one tick of movement.
	float PlayerAxis(const InputState& input, Key positive, Key negative)
	{
		const int positiveActive = input.IsKeyDown(positive) || input.WasKeyPressed(positive);
		const int negativeActive = input.IsKeyDown(negative) || input.WasKeyPressed(negative);
		return static_cast<float>(positiveActive - negativeActive);
	}

	template<typename Tag>
	ECS::Entity First(const SceneAccess& scene)
	{
		ECS::Entity found;
		scene.View<const Tag>().Each([&found](ECS::Entity entity, const Tag&)
		{
			found = entity;
			return false;
		});
		return found;
	}

	ECS::Entity ActiveCamera(const SceneAccess& scene)
	{
		ECS::Entity camera;
		scene.View<const ECS::ActiveCameraComponent, const ECS::CameraComponent>().Each(
			[&camera](ECS::Entity entity, const ECS::ActiveCameraComponent&, const ECS::CameraComponent&)
			{
				camera = entity;
				return false;
			});
		return camera;
	}

	XzBounds BoundsOf(const SceneAccess& scene, ECS::Entity entity, Vec3 position)
	{
		const auto* collider = scene.TryGet<const ColliderComponent>(entity);
		return { position, collider ? XzHalfExtents{ collider->HalfX, collider->HalfZ } : XzHalfExtents{} };
	}

	void MoveSpritesSystem(SystemContext& context)
	{
		const float delta = Delta(context);
		context.World.View<ECS::TransformComponent, VelocityComponent>().Each(
			[delta](ECS::Entity, ECS::TransformComponent& transform, VelocityComponent& velocity)
			{
				transform.Position = transform.Position + velocity.Linear * delta;
				if (transform.Position.X > 3.0f || transform.Position.X < -3.0f)
				{
					transform.Position.X = std::clamp(transform.Position.X, -3.0f, 3.0f);
					velocity.Linear.X = -velocity.Linear.X;
				}
			});
	}

	// Moves the player against solid colliders, then decides the attempt.
	void PlayerSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		auto* attempt = scene.Single<AttemptComponent>();
		const auto player = First<PlayerComponent>(scene);
		ECS::TransformComponent transform;
		if (!attempt || attempt->Result != AttemptResult::Playing || player.IsNull() || !scene.Read(player, transform))
			return;
		const float delta = Delta(context);
		const Vec3 direction{ PlayerAxis(context.Input, Key::D, Key::A), 0.0f, PlayerAxis(context.Input, Key::W, Key::S) };
		const auto self = BoundsOf(scene, player, transform.Position);
		if (delta > 0.0f && (direction.X != 0.0f || direction.Z != 0.0f))
		{
			std::vector<XzBounds> solids;
			scene.View<const ColliderComponent, const ECS::TransformComponent>().Each(
				[&](ECS::Entity entity, const ColliderComponent& collider, const ECS::TransformComponent& other)
				{
					if (collider.Solid && !(entity == player))
						solids.push_back({ other.Position, { collider.HalfX, collider.HalfZ } });
				});
			constexpr float PlayerSpeed = 3.0f;
			transform.Position = ResolveMovementXZ(self, Normalize(direction) * (PlayerSpeed * delta), solids);
			scene.Write(player, transform);
		}
		const XzBounds moved{ transform.Position, self.HalfExtents };
		const auto touches = [&](auto tag)
		{
			bool hit = false;
			scene.View<const decltype(tag), const ECS::TransformComponent>().Each(
				[&](ECS::Entity entity, const decltype(tag)&, const ECS::TransformComponent& other)
				{
					hit = OverlapsXZ(moved, BoundsOf(scene, entity, other.Position));
					return !hit;
				});
			return hit;
		};
		if (touches(HazardComponent{}))
		{
			attempt->Result = AttemptResult::Lost;
			Log(LogLevel::Info, "Attempt lost: courier hit. F3 restarts.");
			context.Services.PlaySound(attempt->LossSound);
		}
		else if (touches(GoalComponent{}))
		{
			attempt->Result = AttemptResult::Won;
			Log(LogLevel::Info, "Attempt won: goal reached. F3 restarts.");
			context.Services.PlaySound(attempt->WinSound);
		}
		else
			return;
		if (auto* sprite = scene.TryGet<ECS::SpriteComponent>(player))
			sprite->Tint = attempt->Result == AttemptResult::Won ? Vec4{ 0.2f, 1.0f, 0.25f, sprite->Tint.W } :
				Vec4{ 1.0f, 0.2f, 0.2f, sprite->Tint.W };
	}

	// Arrows pan, the wheel or PageUp/PageDown zooms, Home resets; F2 switches the camera.
	void CameraSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		const auto& input = context.Input;
		const auto camera = ActiveCamera(scene);
		if (input.WasKeyPressed(Key::F2) && !camera.IsNull())
		{
			ECS::Entity next;
			scene.View<const CameraHomeComponent>().Each([&](ECS::Entity entity, const CameraHomeComponent&)
			{
				if (entity == camera)
					return true;
				next = entity;
				return false;
			});
			if (!next.IsNull())
			{
				// Moving the tag is a structural change, applied after this update.
				context.Commands.Remove<ECS::ActiveCameraComponent>(scene.GetRef(camera));
				context.Commands.Add(scene.GetRef(next), ECS::ActiveCameraComponent{});
			}
			return;
		}
		ECS::TransformComponent transform;
		ECS::CameraComponent lens;
		if (camera.IsNull() || !scene.Read(camera, transform) || !scene.Read(camera, lens))
			return;
		const float delta = Delta(context);
		const bool reset = input.WasKeyPressed(Key::Home);
		const float zoom = input.ScrollY * 0.12f + KeyStep(input, Key::PageUp, 0.05f, delta) -
			KeyStep(input, Key::PageDown, 0.05f, delta);
		const float horizontal = KeyStep(input, Key::Right, 0.05f, delta) - KeyStep(input, Key::Left, 0.05f, delta);
		const float vertical = KeyStep(input, Key::Up, 0.05f, delta) - KeyStep(input, Key::Down, 0.05f, delta);
		const bool pan = horizontal != 0.0f || vertical != 0.0f;
		if (!reset && !pan && zoom == 0.0f)
			return;
		if (reset)
			if (const auto* home = scene.TryGet<const CameraHomeComponent>(camera))
			{
				transform.Position = home->Position;
				transform.Rotation = home->Rotation;
				lens.OrthographicHeight = home->OrthographicHeight;
			}
		if (!std::isfinite(lens.OrthographicHeight) || lens.OrthographicHeight <= 0.0f)
			return;
		if (zoom != 0.0f)
			lens.OrthographicHeight = std::clamp(lens.OrthographicHeight * std::exp(-zoom), 1.0f, 24.0f);
		if (pan)
		{
			const float panSpeed = lens.OrthographicHeight * 0.6f;
			transform.Position = transform.Position + Vec3{ horizontal * panSpeed, 0.0f, vertical * panSpeed };
		}
		scene.Write(camera, transform);
		scene.Write(camera, lens);
	}

	void OrderSystem(SystemContext& context)
	{
		if (!context.Input.WasKeyPressed(Key::F1))
			return;
		if (auto* sprite = context.World.TryGet<ECS::SpriteComponent>(First<OrderToggleComponent>(context.World)))
		{
			sprite->SortOrder = sprite->SortOrder == 0 ? 1 : 0;
			Log(LogLevel::Info, sprite->SortOrder == 0 ? "2D transparent order: automatic camera depth" :
				"2D transparent order: bound sprite order override");
		}
	}
}

extern "C" RAVEN_GAME_API const char* RavenGetGameContract() noexcept
{
	return Raven::GameBuild::Contract;
}

extern "C" RAVEN_GAME_API void RavenRegisterSystems(Raven::SystemTable& systems)
{
	using Raven::SystemPhase;
	systems.Component<VelocityComponent>();
	systems.Component<ColliderComponent>();
	systems.Component<PlayerComponent>();
	systems.Component<HazardComponent>();
	systems.Component<GoalComponent>();
	systems.Component<OrderToggleComponent>();
	systems.Component<CameraHomeComponent>();
	systems.Component<AttemptComponent>();
	systems.Add({ "MoveSprites", SystemPhase::FixedUpdate, 0, &MoveSpritesSystem });
	systems.Add({ "Player", SystemPhase::FixedUpdate, 10, &PlayerSystem });
	systems.Add({ "Camera", SystemPhase::Update, 0, &CameraSystem });
	systems.Add({ "Order", SystemPhase::Update, 10, &OrderSystem });
}
