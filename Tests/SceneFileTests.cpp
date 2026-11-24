// Guards the generic scene format that replaces the sample's positional codec at M6-6
// (docs/DataModelDesign.md, FD-10, FD-11): save/load and text round trips, deterministic
// bytes, and loading a file written with another component layout.

#include "Raven/Components/TransformComponent.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Raven/Scene/SceneFile.hpp"
#include "Support/Check.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace RavenTests
{
	enum class Mood : std::uint8_t
	{
		Calm,
		Alert
	};

	struct Watcher : Raven::ECS::Component
	{
		std::array<Raven::ECS::EntityRef, 2> Targets{};
		std::uint32_t Count = 0;
		Mood State = Mood::Calm;
		float Range = 2.5f;
		RAVEN_COMPONENT(RavenTests::Watcher, Targets, Count, State, Range);
	};
}

namespace
{
	using Raven::Scene;
	using Raven::SceneFile;
	using Raven::ECS::Entity;
	using Raven::ECS::Registry;
	using Raven::ECS::TransformComponent;
	using RavenTests::Watcher;

	struct Authored
	{
		Entity Root, Child, Guard;
	};

	Authored Author(Registry& registry, Scene& scene)
	{
		Authored authored{ scene.CreateEntity("Root"), scene.CreateEntity("Child"), scene.CreateEntity() };
		TransformComponent transform;
		transform.Position = { 1.0f, 2.0f, 3.0f };
		RAVEN_CHECK(registry.Add(authored.Root, transform));
		transform.Position = { 0.1f, -0.2f, 1e-7f };
		transform.Scale = { 2.0f, 2.0f, 2.0f };
		RAVEN_CHECK(registry.Add(authored.Child, transform));
		scene.SetParent(authored.Child, authored.Root);
		Watcher watcher;
		watcher.Targets = { scene.GetRef(authored.Root), scene.GetRef(authored.Child) };
		watcher.Count = 7;
		watcher.State = RavenTests::Mood::Alert;
		RAVEN_CHECK(registry.Add(authored.Guard, watcher));
		scene.UpdateWorldTransforms();
		return authored;
	}

	void SaveAndLoad()
	{
		Registry registry;
		Scene scene(registry);
		const auto authored = Author(registry, scene);
		const auto file = Raven::CaptureScene(scene);
		const auto bytes = file.Encode();
		// Transient world transforms are not stored; the hierarchy is, as ParentComponent.
		RAVEN_CHECK(std::none_of(file.Components.begin(), file.Components.end(),
			[](const SceneFile::Component& component) { return component.Type == "Raven::ECS::WorldTransformComponent"; }));
		RAVEN_CHECK(Raven::CaptureScene(scene).Encode() == bytes);

		Registry other;
		other.Register<Watcher>();
		Scene loaded(other);
		Raven::InstantiateScene(SceneFile::Decode(bytes), loaded);
		const auto root = loaded.Resolve(scene.GetRef(authored.Root));
		const auto child = loaded.Resolve(scene.GetRef(authored.Child));
		const auto guard = loaded.Resolve(scene.GetRef(authored.Guard));
		RAVEN_CHECK(!root.IsNull() && !child.IsNull() && !guard.IsNull());
		RAVEN_CHECK(loaded.GetParent(child) == root && loaded.FindEntity("Root") == root);
		TransformComponent transform;
		RAVEN_CHECK(other.Read(child, transform) && transform.Position.Z == 1e-7f && transform.Scale.X == 2.0f);
		Watcher watcher;
		RAVEN_CHECK(other.Read(guard, watcher) && watcher.Count == 7 && watcher.State == RavenTests::Mood::Alert);
		RAVEN_CHECK(loaded.Resolve(watcher.Targets[1]) == child && watcher.Range == 2.5f);
		RAVEN_CHECK(Raven::CaptureScene(loaded).Encode() == bytes);

		// The text form is lossless, including floats.
		const auto text = SceneFile::Decode(bytes).ToText();
		RAVEN_CHECK(SceneFile::FromText(text).Encode() == bytes);
		RAVEN_CHECK(SceneFile::FromText(text).ToText() == text);
	}

	void OtherLayouts()
	{
		Registry registry;
		registry.Register<Watcher>();
		Scene scene(registry);
		const auto authored = Author(registry, scene);
		auto file = Raven::CaptureScene(scene);
		auto watcher = std::find_if(file.Components.begin(), file.Components.end(),
			[](const SceneFile::Component& component) { return component.Type == "RavenTests::Watcher"; });
		RAVEN_CHECK(watcher != file.Components.end());
		if (watcher == file.Components.end())
			return;
		// An older writer: Count was a float, and a field the current type no longer has.
		for (auto& field : watcher->Fields)
			if (field.Name == "Count")
				field.Kind = Raven::ECS::FieldKind::Float32;
		watcher->Fields.push_back({ "Retired", Raven::ECS::FieldKind::UInt8, 0, { 1, 1 }, 0, 1 });
		watcher->Values.push_back(std::byte{ 9 });
		// A component type and a section kind this build does not know are kept in the file.
		SceneFile::Component unknown;
		unknown.Type = "Elsewhere::Marker";
		unknown.Fields.push_back({ "Value", Raven::ECS::FieldKind::Int32, 0, { 1, 1 }, 0, 4 });
		unknown.Entities.push_back(scene.GetRef(authored.Guard).Id);
		unknown.Values.resize(4, std::byte{ 1 });
		file.Components.push_back(unknown);
		file.Unknown.push_back({ 42, { std::byte{ 1 }, std::byte{ 2 } } });

		const auto decoded = SceneFile::Decode(file.Encode());
		RAVEN_CHECK(decoded.Unknown.size() == 1 && decoded.Unknown[0].Kind == 42 && decoded.Components.size() == file.Components.size());
		RAVEN_CHECK(SceneFile::FromText(decoded.ToText()).Encode() == decoded.Encode());

		Registry other;
		other.Register<Watcher>();
		Scene loaded(other);
		Raven::InstantiateScene(decoded, loaded);
		Watcher value;
		RAVEN_CHECK(other.Read(loaded.Resolve(scene.GetRef(authored.Guard)), value));
		RAVEN_CHECK(value.Count == 0 && value.State == RavenTests::Mood::Alert && value.Range == 2.5f);

		// A stored ID already used in the scene is rejected.
		RAVEN_CHECK_THROWS(Raven::InstantiateScene(decoded, loaded));
		RAVEN_CHECK_THROWS(SceneFile::Decode(std::vector<std::byte>(8, std::byte{ 0 })));
	}
}

int main()
{
	return RavenTests::Run("SceneFileTests", []
	{
		SaveAndLoad();
		OtherLayouts();
	});
}
