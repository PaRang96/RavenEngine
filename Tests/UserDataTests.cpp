// Guards the user-data store of docs/GameServicesDesign.md (FD-33): records by key, both formats,
// field migration on read, crash-safe saves with a backup, recovery from damaged files, and saved
// input bindings. World saves, compression and signing (later stages) build on these rules.

#include "../RavenEngine/Private/Core/UserData.hpp"
#include "../RavenEngine/Private/Input/InputBindingsFile.hpp"
#include "../RavenEngine/Private/Input/InputRouter.hpp"
#include "Raven/Core/Files.hpp"
#include "Raven/Core/GameSystems.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Support/Check.hpp"

#include <chrono>
#include <filesystem>
#include <string>

namespace RavenTests
{
	using namespace Raven;
	namespace fs = std::filesystem;

	struct Progress : ECS::Component
	{
		std::int32_t Stage = 1;
		float BestScore = 0.0f;
		RAVEN_COMPONENT(RavenTests::Progress, Stage, BestScore);
	};

	namespace Next
	{
		// Progress after a rename: Stage became Level, and Coins is new.
		struct Progress : ECS::Component
		{
			std::int32_t Level = 1;
			float BestScore = 0.0f;
			std::int32_t Coins = 7;
			RAVEN_COMPONENT(RavenTests::Next::Progress, Level, BestScore, Coins);
			RAVEN_COMPONENT_ALIASES("RavenTests::Progress", "Stage=Level");
		};
	}

	fs::path FreshRoot()
	{
		const auto root = fs::temp_directory_path() /
			("raven-userdata-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::remove_all(root);
		return root;
	}

	void Corrupt(const fs::path& path, std::size_t offset)
	{
		auto bytes = Files::ReadBinary(path);
		bytes.at(offset) = static_cast<std::byte>(static_cast<unsigned char>(bytes.at(offset)) ^ 0x5Au);
		Files::WriteAtomic(path, bytes);
	}

	void RoundTripAndKeys(const fs::path& root)
	{
		ECS::Registry registry;
		registry.Register<Progress>();
		{
			UserDataService store(registry, root);
			RAVEN_CHECK(store.Get<Progress>("Profile").Stage == 1);
			RAVEN_CHECK(!store.Has<Progress>("Profile"));
			store.Set("Profile", Progress{ {}, 3, 1200.5f });
			store.Set("Profile", "Level-2", Progress{ {}, 2, 80.0f });
			RAVEN_CHECK(store.Save("Profile"));
			RAVEN_CHECK(fs::is_regular_file(root / "Profile.ravendata"));
			RAVEN_CHECK_THROWS(store.Get<Progress>("../escape"));
		}
		UserDataService store(registry, root);
		RAVEN_CHECK(store.Get<Progress>("Profile").Stage == 3);
		RAVEN_CHECK(store.Get<Progress>("Profile").BestScore == 1200.5f);
		RAVEN_CHECK(store.Get<Progress>("Profile", "Level-2").BestScore == 80.0f);
		const auto keys = store.GetKeys("Profile");
		RAVEN_CHECK(keys.size() == 2 && keys[0].empty() && keys[1] == "Level-2");
		store.Remove("Profile", "Level-2");
		RAVEN_CHECK(!store.Has<Progress>("Profile", "Level-2"));
	}

	void TextFormatAndMigration(const fs::path& root)
	{
		{
			ECS::Registry registry;
			registry.Register<Progress>();
			UserDataService store(registry, root);
			store.SetFormat("Settings", UserDataFormat::Text);
			store.Set("Settings", Progress{ {}, 9, 3.5f });
			RAVEN_CHECK(store.Save("Settings"));
			const auto text = Files::ReadText(root / "Settings.ravendata.txt");
			RAVEN_CHECK(text.find("RavenTests::Progress") != std::string::npos);
		}
		// A later build renames the type and a field and adds one; the stored values follow.
		ECS::Registry registry;
		registry.Register<Next::Progress>();
		UserDataService store(registry, root);
		store.SetFormat("Settings", UserDataFormat::Text);
		const auto migrated = store.Get<Next::Progress>("Settings");
		RAVEN_CHECK(migrated.Level == 9 && migrated.BestScore == 3.5f && migrated.Coins == 7);
		// Writing replaces the old-name record instead of keeping both.
		store.Set("Settings", migrated);
		RAVEN_CHECK(store.GetKeys("Settings").size() == 1);
	}

	void DamagedFilesRecover(const fs::path& root)
	{
		ECS::Registry registry;
		registry.Register<Progress>();
		{
			UserDataService store(registry, root);
			store.Set("Save", Progress{ {}, 4, 10.0f });
			RAVEN_CHECK(store.Save("Save"));
			store.Set("Save", Progress{ {}, 5, 20.0f });
			RAVEN_CHECK(store.Save("Save"));
			RAVEN_CHECK(fs::is_regular_file(root / "Save.ravendata.bak"));
		}
		// A flipped payload byte fails the checksum: the file is set aside and the backup is used.
		Corrupt(root / "Save.ravendata", 40);
		{
			UserDataService store(registry, root);
			RAVEN_CHECK(store.Get<Progress>("Save").Stage == 4);
			RAVEN_CHECK(!fs::exists(root / "Save.ravendata"));
			RAVEN_CHECK(store.Save("Save"));
		}
		// With the main file and its backup both damaged, records take their defaults.
		Corrupt(root / "Save.ravendata", 0);
		Corrupt(root / "Save.ravendata.bak", 0);
		UserDataService store(registry, root);
		RAVEN_CHECK(store.Get<Progress>("Save").Stage == 1);
		std::size_t damaged = 0;
		for (const auto& entry : fs::directory_iterator(root))
			damaged += entry.path().filename().string().find(".damaged-") != std::string::npos;
		// Every damaged file is kept, even when two are set aside within the same second.
		RAVEN_CHECK(damaged == 3);
	}

	void BindingsPersist(const fs::path& root)
	{
		ECS::Registry registry;
		registry.Register<InputBindingRecord>();
		SystemTable table;
		table.InputContext({ "Gameplay" });
		const InputBinding jump[]{ { InputCode::FromKey(Key::Space) } };
		table.Action({ "Gameplay", "Jump", ActionKind::Button, jump });
		{
			InputRouter router(true);
			router.SetGame(table.GetInputContexts(), table.GetActions());
			RAVEN_CHECK(router.Rebind("Gameplay", "Jump", 1, { InputCode::FromMouse(MouseButton::Right), Modifier::Shift }));
			RAVEN_CHECK(router.Rebind(DeveloperInput::Context, DeveloperInput::ReloadGame, 0, { InputCode::FromKey(Key::F6) }));
			UserDataService store(registry, root);
			store.SetFormat(BindingsFile, UserDataFormat::Text);
			StoreBindings(router, store);
			RAVEN_CHECK(store.Save(BindingsFile));
		}
		InputRouter router(true);
		router.SetGame(table.GetInputContexts(), table.GetActions());
		UserDataService store(registry, root);
		store.SetFormat(BindingsFile, UserDataFormat::Text);
		LoadBindings(router, store);
		const auto bindings = router.GetBindings("Gameplay", "Jump");
		RAVEN_CHECK(bindings.size() == 2 && bindings[0].Code == InputCode::FromKey(Key::Space) &&
			bindings[1].Code == InputCode::FromMouse(MouseButton::Right) && bindings[1].Modifiers == Modifier::Shift);
		RAVEN_CHECK(router.GetBindings(DeveloperInput::Context, DeveloperInput::ReloadGame)[0].Code ==
			InputCode::FromKey(Key::F6));
		// A reset rewrites the file without the reset action.
		router.ResetBindings("Gameplay");
		StoreBindings(router, store);
		RAVEN_CHECK(store.GetKeys(BindingsFile).size() == 1);
	}
}

int main()
{
	const auto root = RavenTests::FreshRoot();
	const int result = RavenTests::Run("UserDataTests", [&]
	{
		RavenTests::RoundTripAndKeys(root / "a");
		RavenTests::TextFormatAndMigration(root / "b");
		RavenTests::DamagedFilesRecover(root / "c");
		RavenTests::BindingsPersist(root / "d");
	});
	std::error_code ignored;
	std::filesystem::remove_all(root, ignored);
	return result;
}
