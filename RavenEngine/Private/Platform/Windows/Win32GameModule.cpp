#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "Platform/GameModulePlatform.hpp"

#include <memory>
#include <stdexcept>
#include <system_error>

namespace Raven::Platform
{
	namespace
	{
		[[noreturn]] void Fail(const char* operation)
		{
			throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
		}

		std::wstring Quote(const std::wstring& value)
		{
			std::wstring result = L"\"";
			std::size_t slashes = 0;
			for (wchar_t c : value)
			{
				if (c == L'\\') { ++slashes; continue; }
				result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
				slashes = 0;
				if (c == L'"') result += L'\\';
				result += c;
			}
			result.append(slashes * 2, L'\\');
			return result + L'"';
		}
	}

	void* LoadGameLibrary(const std::filesystem::path& path)
	{
		auto library = LoadLibraryExW(path.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (!library) Fail("Load game library");
		return library;
	}

	GameEntryPoint FindGameEntryPoint(void* library, const char* name)
	{
		auto entry = GetProcAddress(static_cast<HMODULE>(library), name);
		if (!entry) Fail(name);
		return reinterpret_cast<GameEntryPoint>(entry);
	}

	void UnloadGameLibrary(void* library) noexcept
	{
		if (library) FreeLibrary(static_cast<HMODULE>(library));
	}

	struct GameBuildProcess
	{
		HANDLE Job = nullptr;
		HANDLE Process = nullptr;
		~GameBuildProcess()
		{
			if (Job) CloseHandle(Job);
			if (Process) CloseHandle(Process);
		}
	};

	GameBuildProcess* StartGameBuild(const std::filesystem::path& executable,
		const std::vector<std::wstring>& arguments, const std::filesystem::path& log)
	{
		auto process = std::make_unique<GameBuildProcess>();
		process->Job = CreateJobObjectW(nullptr, nullptr);
		if (!process->Job) Fail("Create game build job");
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (!SetInformationJobObject(process->Job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
			Fail("Configure game build job");

		SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		const HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (output == INVALID_HANDLE_VALUE) Fail("Open game build log");
		const HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			&security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (input == INVALID_HANDLE_VALUE)
		{
			const DWORD error = GetLastError();
			CloseHandle(output);
			SetLastError(error);
			Fail("Open game build input");
		}
		std::wstring command = Quote(executable.wstring());
		for (const auto& argument : arguments) command += L" " + Quote(argument);
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdInput = input;
		startup.hStdOutput = output;
		startup.hStdError = output;
		PROCESS_INFORMATION info{};
		const BOOL created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &info);
		const DWORD error = GetLastError();
		CloseHandle(input);
		CloseHandle(output);
		if (!created) { SetLastError(error); Fail("Start game build"); }
		process->Process = info.hProcess;
		if (!AssignProcessToJobObject(process->Job, info.hProcess))
		{
			const DWORD assignError = GetLastError();
			TerminateProcess(info.hProcess, 1);
			CloseHandle(info.hThread);
			SetLastError(assignError);
			Fail("Assign game build job");
		}
		const DWORD resumed = ResumeThread(info.hThread);
		CloseHandle(info.hThread);
		if (resumed == static_cast<DWORD>(-1)) Fail("Resume game build");
		return process.release();
	}

	bool PollGameBuild(GameBuildProcess& process, int& exitCode)
	{
		const DWORD result = WaitForSingleObject(process.Process, 0);
		if (result == WAIT_TIMEOUT) return false;
		if (result != WAIT_OBJECT_0) Fail("Poll game build");
		DWORD code = 0;
		if (!GetExitCodeProcess(process.Process, &code)) Fail("Read game build result");
		exitCode = static_cast<int>(code);
		return true;
	}

	void DestroyGameBuild(GameBuildProcess* process) noexcept { delete process; }
}
