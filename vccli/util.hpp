#pragma once
#include <sysarch.h>
#include <str.hpp>

#include <algorithm>
#include <codecvt>
#include <concepts>
#include <filesystem>
#include <functional>
#include <locale>
#include <optional>
#include <string>

#include <Windows.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>

namespace vccli {
	/// @brief	( std::wstring <=> std::string ) converter object with UTF8/UTF16 (Windows) encoding.
	inline static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> w_converter;

	/**
	 * @brief		Uses the FormatMessage function to get a description of the given error code.
	 * @param err	The error ID number; either an HRESULT or another type of windows system error code.
	 * @returns		String containing a description of the error.
	 */
	template<std::integral T>
	WINCONSTEXPR std::string GetErrorMessageFrom(T const& err)
	{
		LPVOID lpMsgBuf{};
		FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, static_cast<DWORD>(err), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);
		return{ (char*)lpMsgBuf };
	}

	/**
	 * @brief		Compares two given strings by comparing them as extentionless filenames using case-insensitive matching.
	 * @param l		Left-side comparison string
	 * @param r		Right-side comparison string
	 * @returns		true when the extentionless filename of l is equal to the extentionless filename of r; otherwise false.
	 */
	bool CompareProcessName(std::string const& l, std::string const& r)
	{
		return str::tolower(std::filesystem::path{ l }.replace_extension().generic_string()) == str::tolower(std::filesystem::path{ r }.replace_extension().generic_string());
	}

	inline std::optional<std::string> GetProcessNameFrom(DWORD pid)
	{
		if (HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
			DWORD len{ 260 };
			CHAR sbuf[260];
			if (QueryFullProcessImageNameA(hProc, PROCESS_NAME_NATIVE, sbuf, &len) != 0) {
				CloseHandle(hProc);
				return std::filesystem::path{ sbuf }.filename().replace_extension().generic_string();
			}
			else {
				const auto hr{ GetLastError() };
				CloseHandle(hProc);
				throw make_exception("GetProcessName failed:  ", GetErrorMessageFrom(hr), " (code: ", hr, ')');
			}
		}
		return std::nullopt;
	}

	inline std::string getDeviceFriendlyName(IMMDevice* dev)
	{
		if (IPropertyStore* properties; dev->OpenPropertyStore(STGM_READ, &properties) == S_OK) {
			PROPVARIANT pv;
			properties->GetValue(PKEY_DeviceInterface_FriendlyName, &pv);
			properties->Release();
			return w_converter.to_bytes(pv.pwszVal);
		}
		return{};
	}
	inline std::string getDeviceName(IMMDevice* dev)
	{
		if (IPropertyStore* properties; dev->OpenPropertyStore(STGM_READ, &properties) == S_OK) {
			PROPVARIANT pv;
			properties->GetValue(PKEY_Device_FriendlyName, &pv);
			properties->Release();
			return w_converter.to_bytes(pv.pwszVal);
		}
		return{};
	}
	inline std::string getDeviceDesc(IMMDevice* dev)
	{
		if (IPropertyStore* properties; dev->OpenPropertyStore(STGM_READ, &properties) == S_OK) {
			PROPVARIANT pv;
			properties->GetValue(PKEY_Device_DeviceDesc, &pv);
			properties->Release();
			return w_converter.to_bytes(pv.pwszVal);
		}
		return{};
	}
	inline EDataFlow getDeviceDataFlow(IMMDevice* dev)
	{
		IMMEndpoint* endpoint;
		dev->QueryInterface<IMMEndpoint>(&endpoint);
		EDataFlow flow;
		endpoint->GetDataFlow(&flow);
		endpoint->Release();
		return flow;
	}
	inline std::string DataFlowToString(EDataFlow const& dataflow)
	{
		switch (dataflow) {
		case EDataFlow::eRender:
			return "Output";
		case EDataFlow::eCapture:
			return "Input";
		case EDataFlow::eAll:
		default:
			return{};
		}
	}
}
