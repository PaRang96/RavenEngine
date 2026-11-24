// The Windows 3D smoke test: the Samples game on the engine session, its scene and module reloads,
// and the hierarchy edge cases (docs/DataModelDesign.md).

#include "Core/GameSession.hpp"
#include "SampleComponents.hpp"

#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Platform/Input.hpp"
#include "Raven/Renderer/RenderWorld.hpp"
#include "Raven/Scene/SceneFile.hpp"
#include "Raven/Core/Files.hpp"
#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/GameInstance.hpp"
#include "Raven/Core/GameModule.hpp"
#include "Raven/Core/Paths.hpp"
#include "Raven/Platform/Window.hpp"
#include "Raven/Renderer/Vulkan/VulkanContext.hpp"
#include "Raven/Scene/ScenePicking.hpp"
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
		Check(scene.GetEntities().empty() && !registry.IsAlive(child) && !registry.IsAlive(pivot) &&
			registry.IsAlive(replacement), "Cascade failed to remove owned descendants");
	}

	// The engine's Extract stage, then one presented frame through the session.
	void Render(Window& window, VulkanContext& renderer, GameSession& session, RenderWorld& world)
	{
		auto& scene = session.GetScene();
		scene.UpdateWorldTransforms();
		world.Sync(scene, [&](ECS::Entity, const ECS::MaterialComponent& material) -> const VulkanTexture*
		{
			return material.BaseColorTexture.IsValid() ? &scene.GetAsset(material.BaseColorTexture.Id).GetTexture() : nullptr;
		}, [&](ECS::Entity, AssetRef<MeshAsset> mesh) { return scene.GetAsset(mesh.Id).GetMesh(); });
		Check(world.GetObjectCount() > 0, "Expected live scene meshes");
		const InputState idle;
		WaitFor(window, [&] { return session.Render(renderer, world, idle); }, "No 3D frame presented");
	}

	ECS::Entity ActiveCamera(SceneAccess scene)
	{
		ECS::Entity camera;
		scene.View<const ECS::ActiveCameraComponent>().Each([&camera](ECS::Entity entity, const ECS::ActiveCameraComponent&)
		{
			camera = entity;
			return false;
		});
		Check(!camera.IsNull(), "The scene has no active camera");
		return camera;
	}

	void Run(const fs::path& scratch, const fs::path& editedModule)
	{
		const auto runtime = Paths::GetExecutableDirectory();
		const auto content = scratch / "Content";
		fs::copy(runtime / "Games" / "Samples" / "Content", content, fs::copy_options::recursive);
		const auto activeDll = scratch / "Samples.dll";
		fs::copy_file(runtime / "Samples.dll", activeDll);
		const auto originalDll = Files::ReadBinary(activeDll);
		const auto collectionFile = content / "Levels" / "Collection.ravenscene";
		const auto collectionBytes = Files::ReadBinary(collectionFile);
		// Encoding is deterministic: every tracked scene re-encodes to its own bytes.
		for (const auto& entry : fs::directory_iterator(content / "Levels"))
		{
			const auto bytes = Files::ReadBinary(entry.path(), std::size_t{ 1 } << 30);
			Check(SceneFile::Decode(bytes).Encode() == bytes, "A scene failed its binary round trip");
		}

		const auto window = Window::Create({"RavenEngine 3D smoke", 720, 480});
		VulkanContext renderer(*window);
		GameInstance instance;
		auto& registry = instance.GetRegistry();
		{
			SceneProject project(renderer, content);
			CheckHierarchy(registry, project.GetAssets());
		}
		RenderWorld world;
		GameModule game(instance, activeDll);
		ApplicationConfig config;
		config.ContentRoot = content;
		config.WatchContent = true;
		GameSession session(renderer, instance, config);
		const auto scene = [&] { return session.GetScene().GetAccess(); };
		const InputState idle;
		const auto tick = [&](std::span<const PickHit> picks = {})
		{
			SystemContext context{ scene(), idle, FrameTime{ 0, .1 }, picks, session.GetCommands(), session.GetServices() };
			game.Update(context);
			session.ApplyCommands();
		};
		const auto attempt = [&]
		{
			const auto* state = scene().Single<Samples::AttemptComponent>();
			Check(state && scene().Count<Samples::AttemptComponent>() == 1, "Expected one scene-owned attempt");
			return *state;
		};
		const auto approach = [&](std::string_view target)
		{
			const auto camera = ActiveCamera(scene());
			auto pose = Read<ECS::TransformComponent>(scene(), camera);
			// The puzzle's authored camera is parented, while the collection camera is a root.
			pose.Position = pose.Position + Position(scene().GetWorldTransform(scene().Find(target))) -
				Position(scene().GetWorldTransform(camera));
			Check(scene().Write(camera, pose), "Could not move the scoped camera");
			tick();
		};
		const auto select = [&](const char* file)
		{
			for (std::size_t i = 0; i < 16; ++i)
			{
				if (session.GetSession().GetActiveDescription().SourcePath.filename() == file)
					return;
				Check(session.Advance(false, true), "Could not advance scene");
			}
			throw std::runtime_error("Project does not register the smoke scene");
		};
		const auto collect = [&](std::string_view id)
		{
			const auto entity = scene().Find(id);
			const auto origin = Position(scene().GetWorldTransform(ActiveCamera(scene())));
			const auto target = Position(scene().GetWorldTransform(entity));
			const auto hit = Pick(session.GetScene(), renderer, {origin, Normalize(target - origin)}, 100.0f);
			Check(hit && hit->Entity == entity, "A pickup could not be reached by mesh picking");
			tick(std::array{ *hit, *hit });
			Check(Read<Samples::PickupComponent>(scene(), entity).Collected, "Picked mesh did not collect");
		};
		const auto pick = [&](std::string_view id)
		{
			return PickHit{ scene().Find(id), 1.0f };
		};

		select("Puzzle.ravenscene");
		Render(*window, renderer, session, world);
		approach("goal");
		Check(!attempt().Completed, "Puzzle completed with the gate closed");
		tick(std::array{ pick("switch-a"), pick("switch-b") });
		for (int frame = 0; frame < 30; ++frame)
			tick();
		Check(attempt().Completed && attempt().CompletionNotified, "Puzzle completion was not reported");
		Check(session.Advance(true) && !attempt().Completed, "Puzzle did not restart");

		select("Collection.ravenscene");
		Render(*window, renderer, session, world);
		const auto originalCamera = ActiveCamera(scene());
		const auto spawn = Read<ECS::TransformComponent>(scene(), originalCamera);
		approach("exit");
		Check(!attempt().Completed, "Collection completed without any pickups");
		Check(scene().Write(originalCamera, spawn), "Could not restore collection spawn");
		const auto foreign = registry.Create();
		Check(registry.Add(foreign, Samples::PickupComponent{}) && scene().Query<Samples::PickupComponent>().size() == 3 &&
			!scene().IsAlive(foreign), "Scene query included a foreign entity");
		collect("pickup-left");
		const auto left = scene().Find("pickup-left");
		const std::array badBytes{ std::byte{ 0 } };
		Files::WriteAtomic(collectionFile, badBytes);
		const auto settle = std::chrono::steady_clock::now() + 1500ms;
		while (std::chrono::steady_clock::now() < settle)
		{
			window->PollEvents();
			Check(!session.Advance(), "Malformed scene replaced the active world");
			std::this_thread::sleep_for(10ms);
		}
		Check(Read<Samples::PickupComponent>(scene(), left).Collected, "Bad scene save erased progress");
		collect("pickup-center");
		approach("exit");
		Check(!attempt().Completed, "Collection completed before its last pickup");
		Check(scene().Write(originalCamera, spawn), "Could not return for the last pickup");
		collect("pickup-right");
		approach("exit");
		Check(attempt().Completed && attempt().CompletionNotified, "Collection completion was not reported");

		Files::WriteAtomic(collectionFile, collectionBytes);
		WaitFor(*window, [&] { return session.Advance(); }, "Valid scene save did not recover");
		Check(!registry.IsAlive(left) && !attempt().Completed && scene().Query<Samples::PickupComponent>().size() == 3,
			"Replaced collection did not get a fresh attempt");
		collect("pickup-left");
		const auto retainedPickup = scene().Find("pickup-left");
		const auto retainedCamera = ActiveCamera(scene());
		Files::WriteAtomic(activeDll, Files::ReadBinary(editedModule));
		Check(game.Reload(), "Compatible gameplay edit was rejected");
		Check(scene().Find("pickup-left") == retainedPickup &&
			Read<Samples::PickupComponent>(scene(), retainedPickup).Collected &&
			ActiveCamera(scene()) == retainedCamera, "DLL reload lost identity or pickup progress");

		const auto generation = game.GetGeneration();
		Files::WriteAtomic(activeDll, badBytes);
		Check(!game.Reload() && game.GetGeneration() == generation, "Invalid game DLL replaced active code");
		collect("pickup-center");
		approach("exit");
		Check(attempt().Completed && !Read<Samples::PickupComponent>(scene(), scene().Find("pickup-right")).Collected,
			"Revised two-pickup rule was not active after rejecting the bad DLL");

		Files::WriteAtomic(activeDll, originalDll);
		Check(game.Reload(), "Could not restore the original game DLL");
		Check(session.Advance(true) && !attempt().Completed &&
			!Read<Samples::PickupComponent>(scene(), scene().Find("pickup-left")).Collected,
			"Collection restart did not clear runtime pickup state");
		Check(registry.Destroy(foreign), "Could not retire the unrelated entity");
		Render(*window, renderer, session, world);
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
