#include "Input/InputBindingsFile.hpp"

#include "Input/InputRouter.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Core/UserData.hpp"

#include <charconv>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Raven
{
	void LoadBindings(InputRouter& router, const UserDataStore& store)
	{
		// (context, action) -> slot -> binding
		std::map<std::pair<std::string, std::string>, std::map<std::size_t, InputBinding>> saved;
		for (const auto& key : store.GetKeys(BindingsFile))
		{
			const auto slash = key.find('/');
			const auto hash = key.rfind('#');
			std::size_t slot = 0;
			if (slash == std::string::npos || hash == std::string::npos || hash < slash ||
				std::from_chars(key.data() + hash + 1, key.data() + key.size(), slot).ec != std::errc{})
			{
				Log(LogLevel::Warning, "Saved binding key is not <context>/<action>#<slot>: " + key);
				continue;
			}
			const auto record = store.Get<InputBindingRecord>(BindingsFile, key);
			saved[{ key.substr(0, slash), key.substr(slash + 1, hash - slash - 1) }][slot] =
				InputBinding{ InputCode{ record.Device, record.Code }, record.Modifiers, record.Scale };
		}
		for (const auto& [name, slots] : saved)
		{
			std::vector<InputBinding> bindings;
			for (const auto& [slot, binding] : slots)
				bindings.push_back(binding);
			if (!router.SetBindings(name.first, name.second, bindings))
				Log(LogLevel::Warning, "Saved bindings for " + name.first + "/" + name.second +
					" were not applied: no such action, or an invalid binding");
		}
	}

	void StoreBindings(const InputRouter& router, UserDataStore& store)
	{
		for (const auto& key : store.GetKeys(BindingsFile))
			store.Remove(BindingsFile, key);
		for (const auto& changed : router.GetOverrides())
			for (std::size_t slot = 0; slot < changed.Bindings.size(); ++slot)
			{
				const auto& binding = changed.Bindings[slot];
				InputBindingRecord record;
				record.Device = binding.Code.Device;
				record.Code = binding.Code.Value;
				record.Modifiers = binding.Modifiers;
				record.Scale = binding.Scale;
				store.Set(BindingsFile, changed.Context + "/" + changed.Action + "#" + std::to_string(slot), record);
			}
	}
}
