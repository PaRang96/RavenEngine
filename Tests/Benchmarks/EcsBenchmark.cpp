// M0 baseline for the M1 ECS budgets (docs/ECSDesign.md). Run the Release build on the
// minimum-spec laptop and record the output in docs/ECSDesign.md:
//   out\build\x64-release\RavenEngine\EcsBenchmark.exe
// The layout mirrors the 3D benchmark scenes: row roots own pivots, each pivot owns one
// mesh entity, and one pivot in five has a rotator.

#include "Raven/Components/InteractionComponents.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Support/TestWorld.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <random>
#include <vector>

namespace
{
	using Raven::Scene;
	using Raven::ECS::Entity;
	using Raven::ECS::RotatorComponent;
	using Raven::ECS::TransformComponent;

	volatile float Sink = 0.0f;

	template<typename Work>
	double MedianMilliseconds(Work&& work)
	{
		constexpr int Repetitions = 9;
		work();
		std::vector<double> samples;
		for (int index = 0; index < Repetitions; ++index)
		{
			const auto start = std::chrono::steady_clock::now();
			work();
			samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
		}
		std::sort(samples.begin(), samples.end());
		return samples[samples.size() / 2];
	}

	// Runs setup before each timed run of work; only work is measured.
	template<typename Setup, typename Work>
	double MedianMillisecondsAfter(Setup&& setup, Work&& work)
	{
		constexpr int Repetitions = 9;
		setup();
		work();
		std::vector<double> samples;
		for (int index = 0; index < Repetitions; ++index)
		{
			setup();
			const auto start = std::chrono::steady_clock::now();
			work();
			samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
		}
		std::sort(samples.begin(), samples.end());
		return samples[samples.size() / 2];
	}

	void Report(const char* name, std::size_t entities, std::size_t operations, double milliseconds)
	{
		std::printf("| %-48s | %7zu | %9.3f | %8.1f |\n", name, entities, milliseconds,
			milliseconds * 1.0e6 / static_cast<double>(operations));
	}

	void RunSize(RavenTests::TestWorld& world, std::size_t pivotCount)
	{
		auto& registry = world.Registry;
		Scene scene(registry, world.Store);
		std::vector<Entity> meshes;
		meshes.reserve(pivotCount);
		Entity row;
		for (std::size_t index = 0; index < pivotCount; ++index)
		{
			if (index % 100 == 0)
			{
				row = scene.CreateEntity();
				TransformComponent rowTransform;
				rowTransform.Position = { 0.0f, static_cast<float>(index / 100) * 2.0f, 0.0f };
				registry.Add(row, rowTransform);
			}
			const auto pivot = scene.CreateEntity();
			TransformComponent pivotTransform;
			pivotTransform.Position = { static_cast<float>(index % 100) * 2.0f, 0.0f, 0.0f };
			registry.Add(pivot, pivotTransform);
			scene.SetParent(pivot, row);
			if (index % 5 == 0)
			{
				RotatorComponent rotor;
				rotor.Rotation = { 0.0f, 0.0f, 0.65f };
				registry.Add(pivot, rotor);
			}

			const auto mesh = scene.CreateEntity();
			TransformComponent meshTransform;
			meshTransform.Position = { 0.0f, 0.0f, 1.0f };
			meshTransform.Scale = { 0.5f, 0.5f, 0.5f };
			registry.Add(mesh, meshTransform);
			scene.SetParent(mesh, pivot);
			meshes.push_back(mesh);
		}
		const std::size_t entities = scene.GetEntities().size();

		// Gameplay pattern over every entity: snapshot, then copy in and out.
		Report("Transform read+write, all entities", entities, entities, MedianMilliseconds([&]
		{
			for (const auto entity : registry.GetEntities())
			{
				TransformComponent transform;
				if (!registry.Read(entity, transform))
					continue;
				transform.Position.Z += 0.001f;
				registry.Write(entity, transform);
			}
		}));

		// The same pass through a borrowed Registry view.
		Report("View<Transform> mutable, all entities", entities, entities, MedianMilliseconds([&]
		{
			registry.View<TransformComponent>().Each([](Entity, TransformComponent& transform)
			{
				transform.Position.Z += 0.001f;
			});
		}));

		// The interaction game's rotator loop: scene query, then copies.
		const auto access = scene.GetAccess();
		std::size_t rotors = 0;
		const double rotorTime = MedianMilliseconds([&]
		{
			rotors = 0;
			for (const auto entity : access.Query<RotatorComponent, TransformComponent>())
			{
				RotatorComponent rotor;
				TransformComponent transform;
				if (!access.Read(entity, rotor) || !access.Read(entity, transform))
					continue;
				transform.Rotation.Z += rotor.Rotation.Z * 0.016f;
				access.Write(entity, rotor);
				access.Write(entity, transform);
				++rotors;
			}
		});
		Report("Query<Rotator, Transform> + read/write", entities, rotors, rotorTime);

		// The ported rotator loop: a scene-scoped view with borrowed references.
		Report("Scene View<Rotator, Transform> mutable", entities, rotors, MedianMilliseconds([&]
		{
			access.View<RotatorComponent, TransformComponent>().Each(
				[](Entity, RotatorComponent& rotor, TransformComponent& transform)
				{
					transform.Rotation.Z += rotor.Rotation.Z * 0.016f;
				});
		}));

		// Cached world transforms (M1 step 6). The first update builds every world.
		scene.UpdateWorldTransforms();
		Report("World update, nothing changed", entities, entities, MedianMilliseconds([&]
		{
			scene.UpdateWorldTransforms();
		}));
		// One pivot in five turns, as in the 3D benchmark; its mesh child follows.
		Report("World update, rotating pivots changed", entities, entities, MedianMillisecondsAfter([&]
		{
			access.View<RotatorComponent, TransformComponent>().Each(
				[](Entity, RotatorComponent& rotor, TransformComponent& transform)
				{
					transform.Rotation.Z += rotor.Rotation.Z * 0.016f;
				});
		}, [&]
		{
			scene.UpdateWorldTransforms();
		}));
		Report("Read cached worlds (view, per mesh)", entities, meshes.size(), MedianMilliseconds([&]
		{
			float sum = 0.0f;
			access.View<const Raven::ECS::WorldTransformComponent>().Each(
				[&sum](Entity, const Raven::ECS::WorldTransformComponent& world)
				{
					sum += world.World(0, 3);
				});
			Sink = sum;
		}));

		// Gameplay world queries compose the full ancestor chain each time.
		Report("World transforms, GetWorldTransform (per mesh)", entities, meshes.size(), MedianMilliseconds([&]
		{
			float sum = 0.0f;
			for (const auto mesh : meshes)
				sum += scene.GetWorldTransform(mesh)(0, 3);
			Sink = sum;
		}));

		// Random single-component access.
		std::vector<Entity> shuffled = meshes;
		std::shuffle(shuffled.begin(), shuffled.end(), std::mt19937(12345));
		Report("Single Read<Transform>, random order", entities, shuffled.size(), MedianMilliseconds([&]
		{
			float sum = 0.0f;
			for (const auto entity : shuffled)
			{
				TransformComponent transform;
				registry.Read(entity, transform);
				sum += transform.Position.X;
			}
			Sink = sum;
		}));
	}
}

int main()
{
	try
	{
#ifdef NDEBUG
		std::printf("EcsBenchmark (Release). Median of 9 runs after one warm-up run.\n\n");
#else
		std::printf("EcsBenchmark (Debug build: numbers are not comparable to budgets).\n\n");
#endif
		std::printf("| %-48s | %7s | %9s | %8s |\n", "Case", "Entities", "Median ms", "ns/op");
		std::printf("| %-48s | %7s | %9s | %8s |\n", "---", "---:", "---:", "---:");
		RavenTests::TestWorld world("RavenEngine EcsBenchmark");
		for (const std::size_t pivots : { 5'000u, 25'000u, 50'000u })
			RunSize(world, pivots);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "EcsBenchmark failed: %s\n", error.what());
		return 1;
	}
}
