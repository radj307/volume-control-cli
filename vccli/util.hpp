#pragma once
#include <sysarch.h>
#include <str.hpp>

#include <concepts>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include <Windows.h>
#include <tlhelp32.h>

namespace vccli {
	template<std::integral T>
	WINCONSTEXPR std::string GetErrorMessageFrom(T const& err)
	{
		LPVOID lpMsgBuf{};
		FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, static_cast<DWORD>(err), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
		return{ (char*)lpMsgBuf };
	}

	bool CompareProcessName(std::string const& l, std::string const& r)
	{
		return str::equalsAny<true>(
			std::filesystem::path{ l }.replace_extension().generic_string(), 
			std::filesystem::path{ r }.replace_extension().generic_string()
			);
	}

	/**
	 * @brief				Resolves the given identifier to a valid process ID.
	 * @param identifier	A process name or process ID.
	 * @returns				The process ID of the target process; or 0 if the process doesn't exist.
	 */
	DWORD ResolveProcessIdentifier(const std::string& identifier, const std::function<bool(std::string, std::string)>& comp = CompareProcessName)
	{
		if (std::all_of(identifier.begin(), identifier.end(), str::stdpred::isdigit))
			return str::stoul(identifier); //< return ID casted to a number
		else { // find process by name:
			PROCESSENTRY32 processInfo{};
			processInfo.dwSize = sizeof(processInfo);

			HANDLE snapshot;
			if ((snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL)) == INVALID_HANDLE_VALUE)
				return 0u;

			Process32First(snapshot, &processInfo);
			if (comp(identifier, std::string{ processInfo.szExeFile })) {
				CloseHandle(snapshot);
				DWORD id{ processInfo.th32ProcessID };
				if (HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id)) {
					CloseHandle(hProc);
					return id;
				}
			}

			while (Process32Next(snapshot, &processInfo)) {
				if (comp(identifier, std::string{ processInfo.szExeFile })) {
					CloseHandle(snapshot);
					DWORD id{ processInfo.th32ProcessID };
					if (HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id)) {
						CloseHandle(hProc);
						return id;
					}
				}
			}

			CloseHandle(snapshot);
			return 0u;
		}
		return 0u;
	}
}
