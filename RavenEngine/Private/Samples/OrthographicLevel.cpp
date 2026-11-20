#include "OrthographicLevel.hpp"
#include "LevelFile.hpp"
#include "SampleAssets.hpp"

#include "Game/GameComponents.hpp"
#include "Raven/Components/SpriteComponent.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Core/Files.hpp"
#include <sstream>
#include "Raven/ECS/Registry.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace Raven
{
	namespace
	{
		struct SpriteDefinition
		{
			std::string Name;
			ECS::TransformComponent Transform;
			ECS::SpriteComponent Sprite;
		};

		struct CameraDefinition
		{
			std::string Name;
			ECS::TransformComponent Transform;
			ECS::CameraComponent Lens;
		};

		struct LevelDefinition
		{
			std::vector<SpriteDefinition> Sprites;
			std::vector<CameraDefinition> Cameras;
			std::string InitialCamera;
			std::string AlternateCamera;
			std::string MovingSprite;
			std::string PlayerSprite;
			std::string GoalSprite;
			std::map<std::string, XzHalfExtents> Bounds;
			std::map<std::string, XzHalfExtents> Solids;
			std::string OrderSprite;
			SampleGame::Velocity Velocity;
			AssetRef<AudioAsset> WinSound;
			AssetRef<AudioAsset> LossSound;
		};

		using LevelFile::ReadVector;

		SpriteDefinition ReadSprite(std::istream& input)
		{
			SpriteDefinition definition;
			std::string textureId;
			std::string blend;
			auto& sprite = definition.Sprite;
			auto& transform = definition.Transform;
			if (!(input >> definition.Name >> textureId >> blend) ||
				!ReadVector(input, transform.Position) || !ReadVector(input, transform.Rotation) ||
				!ReadVector(input, transform.Scale) ||
				!(input >> sprite.Tint.X >> sprite.Tint.Y >> sprite.Tint.Z >> sprite.Tint.W
					>> sprite.SortLayer >> sprite.SortOrder))
				throw std::runtime_error("expected sprite name, texture UUID, blend, position, rotation, scale, tint, layer and order");

			if (blend == "opaque")
				sprite.BlendMode = ECS::SpriteBlendMode::Opaque;
			else if (blend == "alpha")
				sprite.BlendMode = ECS::SpriteBlendMode::Alpha;
			else
				throw std::runtime_error("unknown sprite blend '" + blend + "'");

			if (!std::isfinite(sprite.Tint.X) || !std::isfinite(sprite.Tint.Y) ||
				!std::isfinite(sprite.Tint.Z) || !std::isfinite(sprite.Tint.W))
				throw std::runtime_error("sprite tint must be finite");

			const Mat4 model = transform.GetMatrix();
			for (const auto& column : model.Columns)
			{
				for (const float value : column)
				{
					if (!std::isfinite(value))
						throw std::runtime_error("sprite transform exceeds the supported numeric range");
				}
			}
			sprite.Texture.Id = AssetId::Parse(textureId);
			return definition;
		}

		CameraDefinition ReadCamera(std::istream& input)
		{
			CameraDefinition definition;
			auto& lens = definition.Lens;
			if (!(input >> definition.Name) ||
				!ReadVector(input, definition.Transform.Position) || !ReadVector(input, definition.Transform.Rotation) ||
				!(input >> lens.OrthographicHeight >> lens.NearPlane >> lens.FarPlane))
				throw std::runtime_error("expected camera name, position, rotation, height, near and far");

			lens.ProjectionMode = ECS::CameraProjectionMode::Orthographic;
			ECS::GetCameraView(definition.Transform, lens, 1.0f);
			return definition;
		}

		void ReadRecord(LevelDefinition& level, std::set<std::string>& names,
			const std::string& record, std::istream& input)
		{
			if (record == "sprite")
			{
				auto sprite = ReadSprite(input);
				if (!names.insert(sprite.Name).second)
					throw std::runtime_error("duplicate entity name '" + sprite.Name + "'");
				level.Sprites.push_back(std::move(sprite));
			}
			else if (record == "camera")
			{
				auto camera = ReadCamera(input);
				if (!names.insert(camera.Name).second)
					throw std::runtime_error("duplicate entity name '" + camera.Name + "'");
				level.Cameras.push_back(std::move(camera));
			}
			else if (record == "cameras")
			{
				if (!level.InitialCamera.empty() || !(input >> level.InitialCamera >> level.AlternateCamera))
					throw std::runtime_error("expected one cameras record with initial and alternate camera names");
			}
			else if (record == "moving")
			{
				if (!level.MovingSprite.empty() || !(input >> level.MovingSprite) ||
					!ReadVector(input, level.Velocity.Linear))
					throw std::runtime_error("expected one moving record with sprite name and velocity");
			}
			else if (record == "player")
			{
				if (!level.PlayerSprite.empty() || !(input >> level.PlayerSprite))
					throw std::runtime_error("expected one player record with a sprite name");
			}
			else if (record == "goal")
			{
				if (!level.GoalSprite.empty() || !(input >> level.GoalSprite))
					throw std::runtime_error("expected one goal record with a sprite name");
			}
			else if (record == "bounds" || record == "solid")
			{
				std::string name;
				XzHalfExtents bounds;
				if (!(input >> name >> bounds.X >> bounds.Z) ||
					!std::isfinite(bounds.X) || !std::isfinite(bounds.Z) ||
					bounds.X <= 0.0f || bounds.Z <= 0.0f)
					throw std::runtime_error("expected " + record + " with a sprite name and positive finite X and Z half-extents");
				auto& colliders = record == "solid" ? level.Solids : level.Bounds;
				if (!colliders.emplace(name, bounds).second)
					throw std::runtime_error("duplicate " + record + " for '" + name + "'");
			}
			else if (record == "win-sound" || record == "loss-sound")
			{
				auto& sound = record == "win-sound" ? level.WinSound : level.LossSound;
				std::string identity;
				if (sound.IsValid() || !(input >> identity))
					throw std::runtime_error("expected one sound UUID per outcome");
				sound.Id = AssetId::Parse(identity);
				if (!sound.IsValid())
					throw std::runtime_error("sound UUID must be non-nil");
			}
			else if (record == "order")
			{
				if (!level.OrderSprite.empty() || !(input >> level.OrderSprite))
					throw std::runtime_error("expected one order record with a sprite name");
			}
			else
			{
				throw std::runtime_error("unknown record '" + record + "'");
			}
		}

		LevelDefinition ReadLevel(std::span<const std::byte> bytes)
		{
			LevelDefinition level;
			std::set<std::string> names;
			const auto* text = bytes.empty() ? "" : reinterpret_cast<const char*>(bytes.data());
			std::istringstream file(std::string(text, bytes.size()));
			LevelFile::ReadRecords(file, [&](const std::string& record, std::istream& input)
			{
				ReadRecord(level, names, record, input);
			});
			return level;
		}

		const CameraDefinition& FindCamera(const LevelDefinition& level, const std::string& name)
		{
			if (name.empty())
				throw std::runtime_error("missing cameras binding");
			const auto camera = std::find_if(level.Cameras.begin(), level.Cameras.end(),
				[&name](const CameraDefinition& definition) { return definition.Name == name; });
			if (camera == level.Cameras.end())
				throw std::runtime_error("unknown camera '" + name + "'");
			return *camera;
		}

		const SpriteDefinition& FindSprite(const LevelDefinition& level, const std::string& name,
			const char* binding)
		{
			if (name.empty())
				throw std::runtime_error(std::string("missing ") + binding + " binding");
			const auto sprite = std::find_if(level.Sprites.begin(), level.Sprites.end(),
				[&name](const SpriteDefinition& definition) { return definition.Name == name; });
			if (sprite == level.Sprites.end())
				throw std::runtime_error(std::string(binding) + " refers to unknown sprite '" + name + "'");
			return *sprite;
		}

		XzHalfExtents FindBounds(const LevelDefinition& level, const SpriteDefinition& sprite)
		{
			const auto bounds = level.Bounds.find(sprite.Name);
			if (bounds == level.Bounds.end())
				throw std::runtime_error("missing bounds for '" + sprite.Name + "'");
			return bounds->second;
		}
	}

	OrthographicLevel::OrthographicLevel(ECS::Registry& registry, SampleAssets& assets,
		const std::filesystem::path& path)
		: OrthographicLevel(registry, assets.GetProject(), Files::ReadBinary(path), path, assets.GetCatalog()) {}

	OrthographicLevel::OrthographicLevel(ECS::Registry& registry, SceneProject& project, std::span<const std::byte> bytes,
		const std::filesystem::path&, const AssetCatalog& catalog)
		: Scene(registry, project.GetAssets())
	{
		const LevelDefinition level = ReadLevel(bytes);
		const auto& initial = FindCamera(level, level.InitialCamera);
		const auto& alternate = FindCamera(level, level.AlternateCamera);
		if (&initial == &alternate || level.Cameras.size() != 2)
			throw std::runtime_error("level must bind two distinct cameras");
		const auto& moving = FindSprite(level, level.MovingSprite, "moving");
		const auto& order = FindSprite(level, level.OrderSprite, "order");
		const SpriteDefinition* player = nullptr;
		const SpriteDefinition* goal = nullptr;
		if (!level.PlayerSprite.empty())
		{
			player = &FindSprite(level, level.PlayerSprite, "player");
			goal = &FindSprite(level, level.GoalSprite, "goal");
			if (player == &moving || player == goal || goal == &moving)
				throw std::runtime_error("player, moving and goal bindings must name three distinct sprites");
			m_Player.Bounds = FindBounds(level, *player);
			m_Courier.Bounds = FindBounds(level, moving);
			m_Goal.Bounds = FindBounds(level, *goal);
			if (level.Bounds.size() != 3)
				throw std::runtime_error("bounds are only supported for the player, moving sprite and goal");
		}
		else if (!level.GoalSprite.empty() || !level.Bounds.empty())
		{
			throw std::runtime_error("goal and bounds require a player binding");
		}
		if (order.Sprite.BlendMode != ECS::SpriteBlendMode::Alpha)
			throw std::runtime_error("order binding requires an alpha sprite");

		for (const auto& [name, bounds] : level.Solids)
		{
			const auto& sprite = FindSprite(level, name, "solid");
			if (&sprite == player || &sprite == &moving || &sprite == goal)
				throw std::runtime_error("solid '" + name + "' must bind a stationary sprite");
			const XzBounds solid{ sprite.Transform.Position, bounds };
			if (player && PenetratesXZ({ player->Transform.Position, m_Player.Bounds }, solid))
				throw std::runtime_error("player starts inside solid '" + name + "'");
			m_Solids.push_back(solid);
		}

		std::set<AssetId> textures;
		for (const auto& sprite : level.Sprites)
			textures.insert(sprite.Sprite.Texture.Id);
		for (const auto& id : textures)
			UseAsset(catalog, id, AssetKind::Texture);

		m_WinSound = level.WinSound.IsValid() ? level.WinSound.Id : SampleAssets::GetWinSound().Id;
		m_LossSound = level.LossSound.IsValid() ? level.LossSound.Id : SampleAssets::GetLossSound().Id;
		UseAsset(catalog, m_WinSound, AssetKind::Audio);
		UseAsset(catalog, m_LossSound, AssetKind::Audio);
		for (const auto& sprite : level.Sprites)
		{
			const auto entity = CreateEntity(sprite.Name);
			if (!m_Registry.Add(entity, sprite.Transform) || !m_Registry.Add(entity, sprite.Sprite))
				throw std::runtime_error("could not initialize sprite '" + sprite.Name + "'");
			if (&sprite == &moving)
			{
				if (!m_Registry.Add(entity, level.Velocity))
					throw std::runtime_error("could not initialize velocity for '" + sprite.Name + "'");
				m_Courier.Entity = entity;
			}
			if (&sprite == &order)
				m_OrderSprite = entity;
			if (&sprite == player)
				m_Player.Entity = entity;
			if (&sprite == goal)
				m_Goal.Entity = entity;
		}

		const std::array definitions{ &initial, &alternate };
		for (std::size_t index = 0; index < definitions.size(); ++index)
		{
			const auto& definition = *definitions[index];
			const auto entity = CreateEntity(definition.Name);
			if (!m_Registry.Add(entity, definition.Transform) || !m_Registry.Add(entity, definition.Lens))
				throw std::runtime_error("could not initialize camera '" + definition.Name + "'");
			m_Cameras[index] = { entity, definition.Transform, definition.Lens };
		}
		SelectCamera(m_Cameras[0].Entity);
		SetBindings({ .Player = m_Player.Entity });
	}

	const OrthographicLevel::Camera& OrthographicLevel::GetSelectedCamera() const
	{
		return m_Cameras[m_Cameras[0].Entity == Scene::GetSelectedCamera() ? 0 : 1];
	}

	void OrthographicLevel::SwitchCamera()
	{
		SelectCamera(m_Cameras[m_Cameras[0].Entity == Scene::GetSelectedCamera() ? 1 : 0].Entity);
	}

	ECS::Entity OrthographicLevel::GetOrderSprite() const
	{
		return m_OrderSprite;
	}

	ECS::Entity OrthographicLevel::GetPlayer() const noexcept
	{
		return m_Player.Entity;
	}

	OrthographicLevel::AttemptResult OrthographicLevel::GetAttemptResult() const noexcept
	{
		return m_AttemptResult;
	}

	bool OrthographicLevel::OverlapsPlayer(Vec3 playerPosition, const Collider& other) const
	{
		ECS::TransformComponent transform;
		if (!m_Registry.Read(other.Entity, transform))
			return false;
		return OverlapsXZ({ playerPosition, m_Player.Bounds }, { transform.Position, other.Bounds });
	}

	std::optional<OrthographicLevel::AttemptResult> OrthographicLevel::Step(Vec3 displacement)
	{
		if (m_AttemptResult != AttemptResult::Playing || m_Player.Entity.IsNull())
			return std::nullopt;

		ECS::TransformComponent player;
		if (!m_Registry.Read(m_Player.Entity, player))
			return std::nullopt;

		if (std::isfinite(displacement.X) && std::isfinite(displacement.Z) &&
			(displacement.X != 0.0f || displacement.Z != 0.0f))
		{
			player.Position = ResolveMovementXZ({ player.Position, m_Player.Bounds }, displacement, m_Solids);
			m_Registry.Write(m_Player.Entity, player);
		}

		if (OverlapsPlayer(player.Position, m_Courier))
		{
			m_AttemptResult = AttemptResult::Lost;
			Log(LogLevel::Info, "Attempt lost: courier hit. F3 restarts.");
		}
		else if (OverlapsPlayer(player.Position, m_Goal))
		{
			m_AttemptResult = AttemptResult::Won;
			Log(LogLevel::Info, "Attempt won: goal reached. F3 restarts.");
		}
		else
		{
			return std::nullopt;
		}
		return m_AttemptResult;
	}
}
