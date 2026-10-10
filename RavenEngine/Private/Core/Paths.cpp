#include "Raven/Core/Paths.hpp"

#include "Core/Paths.hpp"
#include "Core/PathText.hpp"
#include "Platform/PlatformPaths.hpp"

#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace Raven
{
	namespace
	{
		struct PathState
		{
			std::mutex Mutex;
			std::optional<std::filesystem::path> ContentRoot;
		};

		PathState& GetPathState()
		{
			static PathState state;
			return state;
		}

		std::filesystem::path ResolveRelativePath(const std::filesystem::path& root,
			const std::filesystem::path& relativePath)
		{
			if (relativePath.has_root_path())
				throw std::invalid_argument("Expected a relative content path: " +
					PathToUtf8(relativePath));
			const auto normalized = relativePath.lexically_normal();
			for (const auto& component : normalized)
			{
				if (component == "..")
					throw std::invalid_argument("Content path escapes its root: " +
						PathToUtf8(relativePath));
			}
			if (normalized.empty() || normalized == ".")
				return root;
			return (root / normalized).lexically_normal();
		}
	}

	ApplicationPathsScope::ApplicationPathsScope(const std::filesystem::path& contentRoot)
	{
		if (contentRoot.has_root_path() && !contentRoot.is_absolute())
			throw std::invalid_argument("Content root must be absolute or executable-relative: " +
				PathToUtf8(contentRoot));
		auto root = contentRoot.empty() ? Paths::GetExecutableDirectory() / "Content" :
			(contentRoot.is_absolute() ? contentRoot : Paths::GetExecutableDirectory() / contentRoot);
		root = root.lexically_normal();

		auto& state = GetPathState();
		std::scoped_lock lock(state.Mutex);
		if (state.ContentRoot.has_value())
			throw std::logic_error("Only one Application::Run can be active at a time");
		state.ContentRoot.emplace(std::move(root));
	}

	ApplicationPathsScope::~ApplicationPathsScope()
	{
		auto& state = GetPathState();
		std::scoped_lock lock(state.Mutex);
		state.ContentRoot.reset();
	}

	namespace Paths
	{
		std::filesystem::path GetExecutablePath()
		{
			static const auto path = GetPlatformExecutablePath().lexically_normal();
			return path;
		}

		std::filesystem::path GetExecutableDirectory()
		{
			return GetExecutablePath().parent_path();
		}

		std::filesystem::path GetContentPath(const std::filesystem::path& relativePath)
		{
			std::filesystem::path root;
			{
				auto& state = GetPathState();
				std::scoped_lock lock(state.Mutex);
				root = state.ContentRoot.value_or(GetExecutableDirectory() / "Content");
			}
			return ResolveRelativePath(root, relativePath);
		}

		std::filesystem::path GetShaderPath(const std::filesystem::path& relativePath)
		{
			return ResolveRelativePath(GetContentPath() / "Shaders", relativePath);
		}
	}
}
