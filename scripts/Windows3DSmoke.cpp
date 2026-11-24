#include "Raven/Renderer/RenderWorld.hpp"
#include "PerspectiveLevel.hpp"
#include "Raven/Components/InteractionComponents.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Core/Files.hpp"
#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/GameInstance.hpp"
#include "Raven/Core/GameModule.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/Platform/Window.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"
#include "Raven/Scene/SceneSession.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	using namespace Raven;
	using namespace std::chrono_literals;
	namespace fs = std::filesystem;

	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	template<class T>
	T Read(SceneAccess scene, ECS::Entity entity)
	{
		T value;
		Check(scene.Read(entity, value), "Expected a live scene component");
		return value;
	}

	template<class Predicate>
	void WaitFor(Window& window, Predicate condition, const char* message)
	{
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		do
		{
			window.PollEvents();
			if (condition())
				return;
			std::this_thread::sleep_for(10ms);
		} while (std::chrono::steady_clock::now() < deadline);
		throw std::runtime_error(message);
	}

	Vec3 Position(const Mat4& world)
	{
		return {world(0, 3), world(1, 3), world(2, 3)};
	}

	void CheckHierarchy(ECS::Registry& registry, AssetStore& assets)
	{
		Scene scene(registry, assets);
		Scene other(registry, assets);
		const auto root = scene.CreateEntity("root");
		const auto pivot = scene.CreateEntity("pivot");
		const auto child = scene.CreateEntity("child");
		const auto sibling = scene.CreateEntity("sibling");
		const auto foreign = other.CreateEntity("foreign");
		ECS::TransformComponent rootLocal, pivotLocal, childLocal, siblingLocal;
		rootLocal.Scale = {-2, 1.5f, .5f};
		pivotLocal.Rotation.Z = .7f;
		childLocal.Position = {1, 2, 3};
		siblingLocal.Position = {-4, 3, 2};
		Check(registry.Add(root, rootLocal) && registry.Add(pivot, pivotLocal) &&
			registry.Add(child, childLocal) && registry.Add(sibling, siblingLocal), "Could not prepare hierarchy");
		scene.SetParent(pivot, root);
		scene.SetParent(child, pivot);
		scene.SetParent(sibling, pivot);
		const auto expected = rootLocal.GetMatrix() * pivotLocal.GetMatrix() * childLocal.GetMatrix();
		const auto CheckWorld = [](const Mat4& actual, const Mat4& desired)
		{
			for (std::size_t row = 0; row < 4; ++row)
				for (std::size_t column = 0; column < 4; ++column)
					Check(std::abs(actual(row, column) - desired(row, column)) < .0001f,
						"Full affine ancestor composition changed");
		};
		CheckWorld(scene.GetWorldTransform(child), expected);
		const auto Cached = [&registry](ECS::Entity entity)
		{
			ECS::WorldTransformComponent world;
			Check(registry.Read(entity, world), "Expected a cached world transform");
			return world.World;
		};
		scene.UpdateWorldTransforms();
		CheckWorld(Cached(child), expected);
		CheckWorld(Cached(sibling), scene.GetWorldTransform(sibling));
		for (const float scale : { 1e-30f, 1e30f })
		{
			ECS::TransformComponent parentScale, childScale;
			parentScale.Scale.X = scale;
			childScale.Scale.X = 1 / scale;
			Check(registry.Write(root, parentScale) && registry.Write(pivot, parentScale) &&
				registry.Write(child, childScale), "Could not prepare scaled hierarchy");
			scene.UpdateWorldTransforms();
			const auto legacy = scene.GetWorldTransform(child);
			const auto current = Cached(child);
			Check(legacy(0, 0) != 0 && current(0, 0) == legacy(0, 0),
				"An intermediate scale lost the representable world transform");
		}
		Check(registry.Write(root, rootLocal) && registry.Write(pivot, pivotLocal) &&
			registry.Write(child, childLocal), "Could not restore hierarchy");
		const auto RejectParent = [&](ECS::Entity entity, ECS::Entity parent)
		{
			try { scene.SetParent(entity, parent); }
			catch (const std::invalid_argument&) { return; }
			throw std::runtime_error("Invalid parent was accepted");
		};
		RejectParent(root, child);
		RejectParent(child, foreign);
		Check(scene.GetParent(root).IsNull() && scene.GetParent(child) == pivot,
			"Rejected reparent changed the hierarchy");
		scene.SetParent(child);
		const auto detached = Position(scene.GetWorldTransform(child));
		Check(detached.X == 1 && detached.Y == 2 && detached.Z == 3, "Detach changed local position");
		pivotLocal.Rotation.Z = -.3f;
		Check(registry.Write(pivot, pivotLocal), "Could not update parent rotation");
		scene.UpdateWorldTransforms();
		CheckWorld(Cached(child), childLocal.GetMatrix());
		CheckWorld(Cached(sibling), scene.GetWorldTransform(sibling));
		scene.SetParent(child, pivot);
		scene.SelectCamera(child);
		GameBindings bindings;
		bindings.Camera = child;
		scene.SetBindings(bindings);
		Check(registry.Destroy(root), "Could not externally destroy the root");
		const auto replacement = other.CreateEntity("replacement");
		Check(!scene.GetAccess().IsAlive(root) && !scene.GetAccess().IsAlive(replacement),
			"Stale or foreign handle was accepted");
		// Descendants of an externally destroyed root become detached roots below it (pre-M6
		// hardening, 2026-11-24), and the other scene's entity never receives a world from this scene.
		scene.UpdateWorldTransforms();
		CheckWorld(Cached(pivot), pivotLocal.GetMatrix());
		CheckWorld(Cached(child), pivotLocal.GetMatrix() * childLocal.GetMatrix());
		CheckWorld(Cached(sibling), scene.GetWorldTransform(sibling));
		CheckWorld(scene.GetWorldTransform(child), Cached(child));
		Check(!registry.Has<ECS::WorldTransformComponent>(replacement), "A foreign entity received a world transform");
		scene.DestroyEntity(root);
		Check(scene.GetEntities().empty() && scene.GetSelectedCamera().IsNull() && scene.GetBindings().Camera.IsNull() &&
			!registry.IsAlive(child) && !registry.IsAlive(pivot) && registry.IsAlive(replacement),
			"Cascade failed to remove owned descendants and bindings");
	}

	void Render(Window& window, VulkanContext& renderer, PerspectiveLevel& level)
	{
		level.UpdateWorldTransforms();
		RenderWorld world;
		world.Sync(level, [&](ECS::Entity, const ECS::MaterialComponent& material) -> const VulkanTexture*
		{
			return material.BaseColorTexture.IsValid() ? &level.GetAsset(material.BaseColorTexture.Id).GetTexture() : nullptr;
		});
		Check(world.GetObjectCount() > 0, "Expected live scene meshes");
		WaitFor(window, [&]
		{
			return renderer.DrawFrame([&](std::uint32_t width, std::uint32_t height)
			{
				return level.GetCameraView(level.GetSelectedCamera(),
					static_cast<float>(width) / static_cast<float>(height));
			}, &world, {}, {}, level.GetLighting()).has_value();
		}, "No 3D frame presented");
	}

	void Run(const fs::path& scratch, const fs::path& editedModule)
	{
		const auto runtime = Paths::GetExecutableDirectory();
		const auto content = scratch / "Content";
		fs::copy(runtime / "Content", content, fs::copy_options::recursive);
		const auto activeDll = scratch / "RavenGame.dll";
		fs::copy_file(runtime / "RavenGame.dll", activeDll);
		const auto originalDll = Files::ReadBinary(activeDll);
		const auto collectionFile = content / "Levels" / "Collection.ravenscene";
		for (const auto* file : {"Perspective", "Gallery", "Assemblies", "Puzzle", "Collection"})
		{
			const auto bytes = Files::ReadBinary(content / "Levels" / (std::string(file) + ".ravenscene"));
			Check(SceneDocument::Decode(bytes).Encode() == bytes, "Authored scene failed binary round trip");
		}

		const auto window = Window::Create({"RavenEngine 3D smoke", 720, 480});
		VulkanContext renderer(*window);
		GameInstance instance;
		auto& registry = instance.GetRegistry();
		SceneProject project(renderer, content, true);
		CheckHierarchy(registry, project.GetAssets());
		unsigned factoryCalls = 0;
		SceneSession session(project, SceneType::Perspective,
			[&](std::span<const std::byte> bytes, const fs::path&, const AssetCatalog& catalog)
			{
				++factoryCalls;
				return std::make_unique<PerspectiveLevel>(registry, project, SceneDocument::Decode(bytes), catalog);
			});
		GameModule game(activeDll);
		const auto level = [&]() -> PerspectiveLevel& { return static_cast<PerspectiveLevel&>(session.GetActive()); };
		const auto scene = [&] { return level().GetAccess(); };
		const auto tick = [&](std::span<const ECS::Entity> hits = {})
		{
			auto bindings = level().GetBindings();
			bindings.Interactions = hits;
			game.Update(instance, {}, FrameTime{0, .1}, bindings);
		};
		const auto completed = [&]
		{
			const auto attempts = scene().Query<ECS::AttemptComponent>();
			Check(attempts.size() == 1, "Expected one scene-owned attempt");
			return Read<ECS::AttemptComponent>(scene(), attempts.front()).Completed;
		};
		const auto approach = [&](std::string_view target)
		{
			const auto camera = level().GetSelectedCamera();
			auto pose = Read<ECS::TransformComponent>(scene(), camera);
			// The puzzle's authored camera is parented, while the collection camera is a root.
			pose.Position = pose.Position + Position(scene().GetWorldTransform(scene().Find(target))) -
				Position(scene().GetWorldTransform(camera));
			Check(scene().Write(camera, pose), "Could not move the scoped camera");
			tick();
		};
		const auto select = [&](const char* file)
		{
			for (std::size_t i = 0; i < project.GetScenes().size(); ++i)
			{
				if (session.GetActiveDescription().SourcePath.filename() == file)
					return;
				Check(session.Update(false, true), "Could not advance scene");
			}
			throw std::runtime_error("Project does not register the smoke scene");
		};
		const auto collect = [&](std::string_view id)
		{
			const auto entity = scene().Find(id);
			const auto origin = Position(scene().GetWorldTransform(level().GetSelectedCamera()));
			const auto target = Position(scene().GetWorldTransform(entity));
			const auto hit = level().Pick(renderer, {origin, Normalize(target - origin)}, 100.0f);
			Check(hit && hit->Entity == entity, "A pickup could not be reached by mesh picking");
			tick(std::array{hit->Entity, hit->Entity});
			Check(Read<ECS::PickupComponent>(scene(), entity).Collected, "Picked mesh did not collect");
		};

		select("Puzzle.ravenscene");
		Render(*window, renderer, level());
		approach("goal");
		Check(!completed(), "Puzzle completed with the gate closed");
		tick(std::array{scene().Find("switch-a"), scene().Find("switch-b")});
		for (int frame = 0; frame < 30; ++frame)
			tick();
		Check(completed() && level().ConsumeCompletion() && !level().ConsumeCompletion(),
			"Puzzle completion was not delivered exactly once");
		Check(session.Update(true, false) && !completed(), "Puzzle did not restart");

		select("Collection.ravenscene");
		Render(*window, renderer, level());
		const auto originalCamera = level().GetSelectedCamera();
		const auto spawn = Read<ECS::TransformComponent>(scene(), originalCamera);
		approach("exit");
		Check(!completed(), "Collection completed without any pickups");
		Check(scene().Write(originalCamera, spawn), "Could not restore collection spawn");
		const auto foreign = registry.Create();
		Check(registry.Add(foreign, ECS::PickupComponent{}) && scene().Query<ECS::PickupComponent>().size() == 3 &&
			!scene().IsAlive(foreign), "Scene query included a foreign entity");
		collect("pickup-left");
		const auto left = scene().Find("pickup-left");
		const auto beforeInvalid = factoryCalls;
		const std::array badBytes{std::byte{0}};
		Files::WriteAtomic(collectionFile, badBytes);
		WaitFor(*window, [&]
		{
			Check(!session.Update(), "Malformed scene replaced the active world");
			return factoryCalls != beforeInvalid;
		}, "Malformed scene was not observed");
		Check(Read<ECS::PickupComponent>(scene(), left).Collected, "Bad scene save erased progress");
		collect("pickup-center");
		approach("exit");
		Check(!completed(), "Collection completed before its last pickup");
		Check(scene().Write(originalCamera, spawn), "Could not return for the last pickup");
		collect("pickup-right");
		approach("exit");
		Check(completed() && level().ConsumeCompletion() && !level().ConsumeCompletion(),
			"Collection completion was not delivered exactly once");

		level().GetAuthoringDefinition().Save(collectionFile, project.GetCatalog(), content);
		WaitFor(*window, [&] { return session.Update(); }, "Valid scene save did not recover");
		Check(!registry.IsAlive(left) && !completed() && scene().Query<ECS::PickupComponent>().size() == 3,
			"Replaced collection did not get a fresh attempt");
		collect("pickup-left");
		const auto retainedPickup = scene().Find("pickup-left");
		const auto retainedCamera = level().GetSelectedCamera();
		Files::WriteAtomic(activeDll, Files::ReadBinary(editedModule));
		Check(game.Reload(), "Compatible gameplay edit was rejected");
		Check(scene().Find("pickup-left") == retainedPickup &&
			Read<ECS::PickupComponent>(scene(), retainedPickup).Collected &&
			level().GetSelectedCamera() == retainedCamera, "DLL reload lost identity or pickup progress");

		const auto generation = game.GetGeneration();
		Files::WriteAtomic(activeDll, badBytes);
		Check(!game.Reload() && game.GetGeneration() == generation, "Invalid game DLL replaced active code");
		collect("pickup-center");
		approach("exit");
		Check(completed() && !Read<ECS::PickupComponent>(scene(), scene().Find("pickup-right")).Collected,
			"Revised two-pickup rule was not active after rejecting the bad DLL");

		Files::WriteAtomic(activeDll, originalDll);
		Check(game.Reload(), "Could not restore the original game DLL");
		Check(session.Update(true, false) && !completed() &&
			!Read<ECS::PickupComponent>(scene(), scene().Find("pickup-left")).Collected,
			"Collection restart did not clear runtime pickup state");
		Check(registry.Destroy(foreign), "Could not retire the unrelated entity");
		Render(*window, renderer, level());
	}
}

int main(int argc, char** argv)
{
	try
	{
		if (argc != 3)
			throw std::invalid_argument("Usage: Raven3DSmoke <empty-scratch-directory> <edited-game-dll>");
		Run(argv[1], argv[2]);
		std::cout << "Windows 3D smoke passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Windows 3D smoke failed: " << error.what() << '\n';
		return 1;
	}
}
