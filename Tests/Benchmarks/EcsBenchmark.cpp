// M0 baseline for the M1 ECS budgets (docs/ECSDesign.md). Run the Release build on the
// minimum-spec laptop and record the output in docs/ECSDesign.md:
//   out\build\x64-release\RavenEngine\EcsBenchmark.exe
// The layout mirrors the 3D benchmark scenes: row roots own pivots, each pivot owns one
// mesh entity, and one pivot in five has a rotator.

#include "Raven/Components/TransformComponent.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/Scene.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <array>
#include <random>
#include <span>
#include <vector>

namespace RavenTests
{
	struct BenchmarkSpin : Raven::ECS::Component
	{
		float Rate = 0.5f;
		RAVEN_COMPONENT(RavenTests::BenchmarkSpin, Rate);
	};

	// The layout of the Samples game's rotator (Games/Samples/Source/SampleComponents.hpp), so the
	// recorded numbers stay comparable now that sample components leave the engine.
	struct RotatorComponent : Raven::ECS::Component
	{
		Raven::ECS::EntityRef Switch;
		Raven::Vec3 BaseRotation{};
		Raven::Vec3 Rotation{};
		float Duration = 1.0f;
		float Progress = 0.0f;
		Raven::Vec3 OffsetRotation{};
		RAVEN_COMPONENT(RavenTests::RotatorComponent, Switch, BaseRotation, Rotation, Duration, Progress, OffsetRotation);
	};
}

namespace
{
	using Raven::Scene;
	using Raven::ECS::Entity;
	using RavenTests::RotatorComponent;
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

	void RunSize(std::size_t pivotCount)
	{
		Raven::ECS::Registry registry;
		Scene scene(registry);
		std::vector<Entity> meshes;
		meshes.reserve(pivotCount);
		std::vector<Entity> pivots;
		pivots.reserve(pivotCount);
		std::vector<Entity> rows;
		Entity row;
		for (std::size_t index = 0; index < pivotCount; ++index)
		{
			if (index % 100 == 0)
			{
				row = scene.CreateEntity();
				rows.push_back(row);
				TransformComponent rowTransform;
				rowTransform.Position = { 0.0f, static_cast<float>(index / 100) * 2.0f, 0.0f };
				registry.Add(row, rowTransform);
			}
			const auto pivot = scene.CreateEntity();
			TransformComponent pivotTransform;
			pivotTransform.Position = { static_cast<float>(index % 100) * 2.0f, 0.0f, 0.0f };
			registry.Add(pivot, pivotTransform);
			scene.SetParent(pivot, row);
			pivots.push_back(pivot);
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
			registry.Add(mesh, RavenTests::BenchmarkSpin{});
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

		// Budget case: half of all entities match (every mesh has Spin and Transform).
		std::size_t spins = 0;
		access.View<const RavenTests::BenchmarkSpin, TransformComponent>().Each(
			[&spins](Entity, const RavenTests::BenchmarkSpin&, TransformComponent&) { ++spins; });
		Report("Scene View<Spin, Transform> mutable, half match", entities, spins, MedianMilliseconds([&]
		{
			access.View<const RavenTests::BenchmarkSpin, TransformComponent>().Each(
				[](Entity, const RavenTests::BenchmarkSpin& spin, TransformComponent& transform)
				{
					transform.Rotation.Z += spin.Rate * 0.016f;
				});
		}));
		Report("Registry View<Spin, Transform> mutable, half match", entities, spins, MedianMilliseconds([&]
		{
			registry.View<const RavenTests::BenchmarkSpin, TransformComponent>().Each(
				[](Entity, const RavenTests::BenchmarkSpin& spin, TransformComponent& transform)
				{
					transform.Rotation.Z += spin.Rate * 0.016f;
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

		// Structural changes (ARCH-ECS-01 step 0): one gameplay frame that spawns, despawns or
		// reparents 1k entities, including the world update that follows. Each run restores the
		// scene, so every size measures the same steady population.
		constexpr std::size_t Batch = 1'000;
		std::vector<Entity> spawned;
		spawned.reserve(Batch);
		const auto spawn = [&]
		{
			for (std::size_t index = 0; index < Batch; ++index)
			{
				const auto entity = scene.CreateEntity();
				TransformComponent transform;
				transform.Position = { 0.0f, 0.0f, 2.0f };
				registry.Add(entity, transform);
				scene.SetParent(entity, pivots[(index * 7919) % pivots.size()]);
				spawned.push_back(entity);
			}
			scene.UpdateWorldTransforms();
		};
		const auto despawn = [&]
		{
			for (const auto entity : spawned)
				scene.DestroyEntity(entity);
			spawned.clear();
			scene.UpdateWorldTransforms();
		};
		scene.UpdateWorldTransforms();
		Report("Spawn 1k leaves + world update", entities, Batch, MedianMillisecondsAfter([&]
		{
			if (!spawned.empty())
				despawn();
		}, spawn));
		Report("Despawn 1k leaves + world update", entities, Batch, MedianMillisecondsAfter([&]
		{
			if (spawned.empty())
				spawn();
		}, despawn));
		if (!spawned.empty())
			despawn();
		// 500 pivots move with their meshes (1k entities) between the first two rows.
		bool moved = false;
		Report("Reparent 500 pivots (1k entities) + world", entities, Batch, MedianMilliseconds([&]
		{
			moved = !moved;
			for (std::size_t index = 0; index < Batch / 2; ++index)
				scene.SetParent(pivots[index], rows[moved ? 1 : 0]);
			scene.UpdateWorldTransforms();
		}));

		// Field migration of every Transform (docs/HotReloadDesign.md, H6): a layout with one
		// added field, then back. The budget is 20 ms for 100k values (docs/DataModelDesign.md).
		const auto* current = registry.FindSchema(TransformComponent::TypeId);
		auto widened = *current;
		widened.Fields.push_back({ "Extra", static_cast<std::uint32_t>(current->Size), 4, Raven::ECS::FieldKind::Float32,
			0, { 1, 1 }, 0, {} });
		widened.Size += 4;
		widened.LayoutHash ^= 1;
		widened.Defaults.resize(widened.Size);
		const std::array layouts{ *current, widened };
		std::size_t layout = 0;
		Report("Migrate every Transform (one added field)", entities, registry.Count<TransformComponent>(), MedianMilliseconds([&]
		{
			layout = 1 - layout;
			registry.Migrate(std::span(&layouts[layout], 1));
		}));
		if (layout == 1)
			registry.Migrate(std::span(&layouts[0], 1));
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
		for (const std::size_t pivots : { 5'000u, 25'000u, 50'000u })
			RunSize(pivots);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "EcsBenchmark failed: %s\n", error.what());
		return 1;
	}
}
