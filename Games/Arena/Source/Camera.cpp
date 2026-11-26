// The follow camera: behind and above the craft, turned by Q and E, easing toward its place.

#include "ArenaSystems.hpp"

#include "Raven/Components/TransformComponent.hpp"

namespace Arena
{
	void CameraSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		const auto camera = First<FollowCameraComponent>(scene);
		const auto craft = First<CraftComponent>(scene);
		auto* rig = scene.TryGet<FollowCameraComponent>(camera);
		const auto* target = scene.TryGet<const ECS::TransformComponent>(craft);
		ECS::TransformComponent transform;
		if (!rig || !target || !scene.Read(camera, transform))
			return;
		const float seconds = FrameSeconds(context);
		rig->Yaw = std::remainder(rig->Yaw + context.Actions.GetAxis(Turn) * rig->TurnSpeed * seconds, 6.2831853f);
		const Vec3 place = target->Position - Forward(rig->Yaw) * rig->Distance + Vec3{ 0.0f, 0.0f, rig->Height };
		const Vec3 offset = place - transform.Position;
		// Far away (a new run, a teleport): jump there instead of sweeping across the arena.
		if (Dot(offset, offset) > 100.0f)
			transform.Position = place;
		else
			transform.Position = transform.Position + offset * (1.0f - std::exp(-rig->Follow * seconds));
		transform.Rotation = { rig->Pitch, 0.0f, rig->Yaw };
		scene.Write(camera, transform);
	}
}
