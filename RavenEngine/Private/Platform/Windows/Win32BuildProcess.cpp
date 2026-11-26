#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "Core/PathText.hpp"
#include "Platform/BuildProcess.hpp"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace Raven::Platform
{
	namespace
	{
		[[noreturn]] void Fail(const char* operation)
		{
			throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
		}

		struct CloseFile
		{
			void operator()(void* handle) const noexcept { CloseHandle(handle); }
		};

		class InheritedHandles
		{
		public:
			InheritedHandles(HANDLE input, HANDLE output)
				: m_Handles{ input, output }
			{
				SIZE_T size = 0;
				InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
				m_Storage.resize(size);
				if (!InitializeProcThreadAttributeList(Get(), 1, 0, &size))
					Fail("Initialize build handle list");
				if (!UpdateProcThreadAttribute(Get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
					m_Handles, sizeof(m_Handles), nullptr, nullptr))
				{
					const auto error = GetLastError();
					DeleteProcThreadAttributeList(Get());
					SetLastError(error);
					Fail("Set build handle list");
				}
			}

			~InheritedHandles() { DeleteProcThreadAttributeList(Get()); }

			LPPROC_THREAD_ATTRIBUTE_LIST Get() noexcept
			{
				return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_Storage.data());
			}

		private:
			HANDLE m_Handles[2];
			std::vector<std::byte> m_Storage;
		};

		std::wstring Quote(const std::wstring& value)
		{
			std::wstring result = L"\"";
			std::size_t slashes = 0;
			for (wchar_t c : value)
			{
				if (c == L'\\')
				{
					++slashes;
					continue;
				}
				result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
				slashes = 0;
				if (c == L'"')
					result += L'\\';
				result += c;
			}
			result.append(slashes * 2, L'\\');
			return result + L'"';
		}
	}

	struct BuildProcess
	{
		HANDLE Job = nullptr;
		HANDLE Process = nullptr;
		~BuildProcess()
		{
			if (Job)
				CloseHandle(Job);
			if (Process)
				CloseHandle(Process);
		}
	};

	bool SupportsBuildProcesses() noexcept
	{
		return true;
	}

	BuildProcess* StartBuildProcess(const std::filesystem::path& executable,
		const std::vector<std::string>& arguments, const std::filesystem::path& log)
	{
		auto process = std::make_unique<BuildProcess>();
		process->Job = CreateJobObjectW(nullptr, nullptr);
		if (!process->Job)
			Fail("Create build process job");
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (!SetInformationJobObject(process->Job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
			Fail("Configure build process job");

		SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
		const HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (output == INVALID_HANDLE_VALUE)
			Fail("Open build process log");
		const std::unique_ptr<void, CloseFile> outputOwner(output);
		const HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			&security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (input == INVALID_HANDLE_VALUE)
			Fail("Open build process input");
		const std::unique_ptr<void, CloseFile> inputOwner(input);
		InheritedHandles handles(input, output);
		std::wstring command = Quote(executable.wstring());
		for (const auto& argument : arguments)
		{
			command += L' ';
			// Arguments are UTF-8; the command line is UTF-16.
			command += Quote(PathFromUtf8(argument).wstring());
		}
		STARTUPINFOEXW startup{};
		startup.StartupInfo.cb = sizeof(startup);
		startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
		startup.StartupInfo.hStdInput = input;
		startup.StartupInfo.hStdOutput = output;
		startup.StartupInfo.hStdError = output;
		startup.lpAttributeList = handles.Get();
		PROCESS_INFORMATION info{};
		if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
			nullptr, nullptr, &startup.StartupInfo, &info))
			Fail("Start build process");
		process->Process = info.hProcess;
		if (!AssignProcessToJobObject(process->Job, info.hProcess))
		{
			const DWORD assignError = GetLastError();
			TerminateProcess(info.hProcess, 1);
			CloseHandle(info.hThread);
			SetLastError(assignError);
			Fail("Assign build process job");
		}
		const DWORD resumed = ResumeThread(info.hThread);
		CloseHandle(info.hThread);
		if (resumed == static_cast<DWORD>(-1))
			Fail("Resume build process");
		return process.release();
	}

	bool PollBuildProcess(BuildProcess& process, int& exitCode)
	{
		const DWORD result = WaitForSingleObject(process.Process, 0);
		if (result == WAIT_TIMEOUT) return false;
		if (result != WAIT_OBJECT_0)
			Fail("Poll build process");
		DWORD code = 0;
		if (!GetExitCodeProcess(process.Process, &code))
			Fail("Read build process result");
		exitCode = static_cast<int>(code);
		return true;
	}

	void DestroyBuildProcess(BuildProcess* process) noexcept { delete process; }
}
