#pragma once

#include "Raven/Components/CameraComponent.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Raven/Math/Collision2D.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace Raven
{
	namespace ECS { class Registry; }
	class SampleAssets;
	class SceneProject;


	class OrthographicLevel final : public Scene
	{
	public:
		struct Camera
		{
			ECS::Entity Entity;
			ECS::TransformComponent Transform;
			ECS::CameraComponent Lens;
		};

		enum class AttemptResult
		{
			Playing,
			Won,
			Lost
		};

		OrthographicLevel(ECS::Registry& registry, SampleAssets& assets, const std::filesystem::path& path);
		OrthographicLevel(ECS::Registry& registry, SceneProject& project, std::span<const std::byte> bytes,
			const std::filesystem::path& path, const AssetCatalog& catalog);
		~OrthographicLevel() override = default;
		const AssetLease& GetWinSound() const { return GetAsset(m_WinSound); }
		const AssetLease& GetLossSound() const { return GetAsset(m_LossSound); }
		OrthographicLevel(const OrthographicLevel&) = delete;
		OrthographicLevel& operator=(const OrthographicLevel&) = delete;

		const Camera& GetSelectedCamera() const;
		void SwitchCamera();
		ECS::Entity GetOrderSprite() const;
		ECS::Entity GetPlayer() const noexcept;
		AttemptResult GetAttemptResult() const noexcept;
		// Returns the outcome on the tick when the attempt finishes.
		std::optional<AttemptResult> Step(Vec3 displacement);

	private:
		struct Collider
		{
			ECS::Entity Entity;
			XzHalfExtents Bounds{};
		};

		bool OverlapsPlayer(Vec3 playerPosition, const Collider& other) const;
		AssetId m_WinSound;
		AssetId m_LossSound;
		std::array<Camera, 2> m_Cameras;
		ECS::Entity m_OrderSprite;
		Collider m_Player;
		Collider m_Courier;
		Collider m_Goal;
		std::vector<XzBounds> m_Solids;
		AttemptResult m_AttemptResult = AttemptResult::Playing;
	};
}
