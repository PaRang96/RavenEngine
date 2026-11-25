// FD-32 measurements 1 and 2, FD-47 measurement 3 and FD-34 measurement 4
// (docs/GameServicesDesign.md): the cost of a physics step in Release and in x64-debug, where Jolt is
// compiled with optimization (RAVEN_JOLT_OPTIMIZE_DEBUG), of movers on the character backend, and of
// picking among 50,000 renderables. Run on the minimum-spec
// laptop and record the output in the design note:
//   out\build\x64-release\RavenEngine\PhysicsBenchmark.exe
//   out\build\x64-debug\RavenEngine\PhysicsBenchmark.exe
// A pile of boxes and spheres falls onto a floor and settles; each step includes the component
// sync, Jolt's simulation and the write-back.

#include "../RavenEngine/Private/Physics/PhysicsWorld.hpp"
#include "../RavenEngine/Private/Physics/PickWorld.hpp"
#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Raven/Scene/SceneProject.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <exception>
#include <memory>
#include <numbers>
#include <vector>

namespace
{
	using namespace Raven;

	struct Result
	{
		double Mean = 0.0;
		double P95 = 0.0;
		double Max = 0.0;
		std::uint32_t Active = 0;
	};

	Result Run(int side, int steps)
	{
		ECS::Registry registry;
		Scene scene(registry);
		PhysicsWorld world(PhysicsLimits{});
		const auto floor = scene.CreateEntity("floor");
		ECS::TransformComponent ground;
		ground.Position = { 0.0f, 0.0f, -0.5f };
		registry.Add(floor, ground);
		ECS::ColliderComponent slab;
		slab.Size = { 100.0f, 100.0f, 0.5f };
		registry.Add(floor, slab);
		for (int z = 0; z < side; ++z)
			for (int y = 0; y < side; ++y)
				for (int x = 0; x < side; ++x)
				{
					const auto entity = scene.CreateEntity();
					ECS::TransformComponent transform;
					transform.Position = { static_cast<float>(x - side / 2) * 1.2f, static_cast<float>(y - side / 2) * 1.2f,
						1.0f + static_cast<float>(z) * 1.2f };
					registry.Add(entity, transform);
					ECS::ColliderComponent collider;
					collider.Shape = (x + y + z) % 2 ? ECS::ColliderShape::Sphere : ECS::ColliderShape::Box;
					collider.Size = { 0.45f, 0.45f, 0.45f };
					registry.Add(entity, collider);
					registry.Add(entity, ECS::RigidBodyComponent{});
				}
		std::vector<double> times;
		times.reserve(steps);
		for (int step = 0; step < steps; ++step)
		{
			const auto start = std::chrono::steady_clock::now();
			world.Step(scene, registry, 1.0 / 60.0);
			times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
		}
		Result result;
		result.Active = world.GetStats().ActiveBodies;
		// The first step builds every body; it is reported as the maximum only.
		result.Max = *std::max_element(times.begin(), times.end());
		times.erase(times.begin());
		for (const double time : times)
			result.Mean += time;
		result.Mean /= static_cast<double>(times.size());
		std::sort(times.begin(), times.end());
		result.P95 = times[static_cast<std::size_t>(static_cast<double>(times.size()) * 0.95)];
		return result;
	}
}

namespace
{
	struct Mesh
	{
		std::vector<Vec3> Vertices;
		std::vector<std::uint32_t> Indices;
	};

	// A UV sphere 1 m across; 16 segments and 8 rings match the sample Sphere.fbx.
	std::shared_ptr<Mesh> Sphere(int segments, int rings)
	{
		auto mesh = std::make_shared<Mesh>();
		for (int ring = 0; ring <= rings; ++ring)
			for (int segment = 0; segment <= segments; ++segment)
			{
				const float theta = std::numbers::pi_v<float> * static_cast<float>(ring) / static_cast<float>(rings);
				const float phi = 2.0f * std::numbers::pi_v<float> * static_cast<float>(segment) / static_cast<float>(segments);
				mesh->Vertices.push_back({ 0.5f * std::sin(theta) * std::cos(phi), 0.5f * std::sin(theta) * std::sin(phi),
					0.5f * std::cos(theta) });
			}
		const auto at = [&](int ring, int segment) { return static_cast<std::uint32_t>(ring * (segments + 1) + segment); };
		for (int ring = 0; ring < rings; ++ring)
			for (int segment = 0; segment < segments; ++segment)
				for (const auto index : { at(ring, segment), at(ring + 1, segment), at(ring + 1, segment + 1),
					at(ring, segment), at(ring + 1, segment + 1), at(ring, segment + 1) })
					mesh->Indices.push_back(index);
		return mesh;
	}

	// Movers walking circles that cross each other, on a floor with pillars and a scattering of crates.
	void Movers(int count, int steps)
	{
		ECS::Registry registry;
		Scene scene(registry);
		PhysicsWorld world(PhysicsLimits{});
		const auto add = [&](Vec3 position, Vec3 half, bool dynamic)
		{
			const auto entity = scene.CreateEntity();
			ECS::TransformComponent transform;
			transform.Position = position;
			registry.Add(entity, transform);
			ECS::ColliderComponent collider;
			collider.Size = half;
			registry.Add(entity, collider);
			if (dynamic)
				registry.Add(entity, ECS::RigidBodyComponent{});
		};
		const float half = 4.0f + std::sqrt(static_cast<float>(count)) * 2.0f;
		add({ 0.0f, 0.0f, -0.5f }, { half, half, 0.5f }, false);
		for (int pillar = 0; pillar < 16; ++pillar)
			add({ (static_cast<float>(pillar % 4) - 1.5f) * half * 0.45f, (static_cast<float>(pillar / 4) - 1.5f) * half * 0.45f, 1.0f },
				{ 0.4f, 0.4f, 1.0f }, false);
		for (int crate = 0; crate < 32; ++crate)
			add({ (static_cast<float>(crate % 8) - 3.5f) * half * 0.2f, (static_cast<float>(crate / 8) - 1.5f) * half * 0.3f, 0.5f },
				{ 0.4f, 0.4f, 0.4f }, true);
		std::vector<ECS::Entity> movers;
		const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count))));
		for (int index = 0; index < count; ++index)
		{
			const auto entity = scene.CreateEntity();
			ECS::TransformComponent transform;
			transform.Position = { (static_cast<float>(index % side) - 0.5f * static_cast<float>(side)) * 2.0f,
				(static_cast<float>(index / side) - 0.5f * static_cast<float>(side)) * 2.0f, 1.0f };
			registry.Add(entity, transform);
			registry.Add(entity, ECS::MoverComponent{});
			movers.push_back(entity);
		}
		std::vector<double> times;
		for (int step = 0; step < steps; ++step)
		{
			// Steering, as a game's fixed-step system would write it.
			const float time = static_cast<float>(step) / 60.0f;
			for (std::size_t index = 0; index < movers.size(); ++index)
			{
				auto mover = *registry.TryGet<const ECS::MoverComponent>(movers[index]);
				const float angle = time * 0.8f + static_cast<float>(index) * 0.37f;
				mover.DesiredVelocity = { 4.0f * std::cos(angle), 4.0f * std::sin(angle), 0.0f };
				registry.Write(movers[index], mover);
			}
			const auto start = std::chrono::steady_clock::now();
			world.Step(scene, registry, 1.0 / 60.0);
			times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
		}
		times.erase(times.begin(), times.begin() + 30);
		double mean = 0.0;
		for (const double time : times)
			mean += time;
		mean /= static_cast<double>(times.size());
		std::sort(times.begin(), times.end());
		const double p95 = times[static_cast<std::size_t>(static_cast<double>(times.size()) * 0.95)];
		std::printf("%d,%.3f,%.3f,%.2f\n", count, mean, p95, mean * 1000.0 / count);
	}

	double Milliseconds(std::int64_t nanoseconds) { return static_cast<double>(nanoseconds) / 1e6; }

	// 50,000 renderables in the 50k benchmark scene's layout (250 x 200, spacing 1.55 m), alternating
	// cube and sphere meshes; one in five moves between picks, like its rotors.
	void Picking()
	{
		ECS::Registry registry;
		Scene scene(registry);
		PickWorld picking;
		const auto cubeId = AssetId::Generate();
		const auto sphereId = AssetId::Generate();
		auto cube = std::make_shared<Mesh>();
		cube->Vertices = { { -.5f, -.5f, -.5f }, { .5f, -.5f, -.5f }, { .5f, .5f, -.5f }, { -.5f, .5f, -.5f },
			{ -.5f, -.5f, .5f }, { .5f, -.5f, .5f }, { .5f, .5f, .5f }, { -.5f, .5f, .5f } };
		cube->Indices = { 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0,
			4, 3, 4, 7 };
		auto sphere = Sphere(16, 8);
		const PickMeshResolver meshes = [&](AssetId id) -> PickMesh
		{
			const auto& mesh = id == cubeId ? cube : sphere;
			return { mesh, mesh->Vertices, mesh->Indices };
		};
		std::vector<ECS::Entity> moving;
		for (int row = 0; row < 200; ++row)
			for (int column = 0; column < 250; ++column)
			{
				const auto entity = scene.CreateEntity();
				ECS::TransformComponent transform;
				transform.Position = { (static_cast<float>(column) - 124.5f) * 1.55f, 0.0f,
					(static_cast<float>(row) - 99.5f) * 1.55f };
				transform.Scale = { .68f, .68f, .68f };
				registry.Add(entity, transform);
				ECS::StaticMeshComponent mesh;
				mesh.Mesh.Id = (row * 250 + column) % 2 ? sphereId : cubeId;
				registry.Add(entity, mesh);
				if ((row * 250 + column) % 5 == 0)
					moving.push_back(entity);
			}
		scene.UpdateWorldTransforms();
		std::printf("Picking (50,000 renderables)\ncase,sync_ms,query_ms,shapes_ms\n");
		const auto report = [&](const char* name, const Ray& ray)
		{
			picking.Pick(scene, registry, meshes, ray, 300.0f);
			const auto stats = picking.GetStats();
			std::printf("%s,%.3f,%.3f,%.3f\n", name, Milliseconds(stats.SyncNanoseconds), Milliseconds(stats.QueryNanoseconds),
				Milliseconds(stats.ShapeNanoseconds));
		};
		const Ray center{ { 0.775f, -120.0f, 0.775f }, { 0.0f, 1.0f, 0.0f } };
		const Ray graze{ { -200.0f, 0.0f, 0.775f }, { 1.0f, 0.0f, 0.0f } };
		report("first pick (builds every body)", center);
		report("unchanged", center);
		report("ray along a row", graze);
		for (int frame = 1; frame <= 3; ++frame)
		{
			for (const auto entity : moving)
			{
				auto transform = *registry.TryGet<const ECS::TransformComponent>(entity);
				transform.Rotation.Y += 0.05f;
				registry.Write(entity, transform);
			}
			scene.UpdateWorldTransforms();
			report("after 10,000 moved", center);
		}
		// A reloaded mesh: one shape rebuilt and 25,000 bodies given the new shape.
		sphere = Sphere(64, 32);
		report("after a sphere reload (4,096 triangles)", center);
	}
}

int main()
{
	try
	{
#ifdef NDEBUG
		const char* configuration = "Release";
#else
		const char* configuration = "Debug";
#endif
		std::printf("Physics benchmark (%s), 300 steps at 60 Hz\n", configuration);
		std::printf("bodies,mean_ms,p95_ms,first_step_ms,active_after\n");
		for (const int side : { 5, 10, 16 })
		{
			const auto result = Run(side, 300);
			std::printf("%d,%.3f,%.3f,%.3f,%u\n", side * side * side, result.Mean, result.P95, result.Max, result.Active);
		}
		std::printf("Movers (character backend), 300 steps at 60 Hz with 16 pillars and 32 crates\n");
		std::printf("movers,mean_ms,p95_ms,mean_us_per_mover\n");
		for (const int count : { 10, 100, 500 })
			Movers(count, 300);
		Picking();
		return 0;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
