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

#include <doctest/doctest.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <endpointvolume.h>
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

	TEST_CASE("GetErrorMessageFrom")
	{
		CHECK(GetErrorMessageFrom(0) == "The operation completed successfully.\r\n");
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

	inline std::optional<std::string> GetProcessNameFrom(DWORD const& pid)
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

	inline std::string getSessionInstanceIdentifier(IAudioSessionControl2* session)
	{
		LPWSTR sbuf;
		session->GetSessionInstanceIdentifier(&sbuf);
		return w_converter.to_bytes(sbuf);
	}
	inline std::string getSessionIdentifier(IAudioSessionControl2* session)
	{
		LPWSTR sbuf;
		session->GetSessionIdentifier(&sbuf);
		return w_converter.to_bytes(sbuf);
	}

	inline std::string getDeviceID(IMMDevice* dev)
	{
		LPWSTR sbuf;
		dev->GetId(&sbuf);
		return w_converter.to_bytes(sbuf);
	}
	/**
	 * @brief		Retrieves the specified property value from the given device's property store.
	 * @param dev	The IMMDevice to retrieve properties from.
	 * @param pkey	The PROPERTYKEY structure to target.
	 * @returns		PROPVARIANT
	 */
	inline PROPVARIANT getDeviceProperty(IMMDevice* dev, const PROPERTYKEY& pkey)
	{
		PROPVARIANT pv{};
		if (IPropertyStore* properties; dev->OpenPropertyStore(STGM_READ, &properties) == S_OK) {
			properties->GetValue(pkey, &pv);
			properties->Release();
		}
		return pv;
	}
	/**
	 * @brief		Retrieve the name of the given device from its properties.
	 *\n			PKEY_DeviceInterface_FriendlyName
	 * @param dev	The IMMDevice to retrieve properties from.
	 * @returns		std::string
	 */
	inline std::string getDeviceFriendlyName(IMMDevice* dev)
	{
		return w_converter.to_bytes(getDeviceProperty(dev, PKEY_DeviceInterface_FriendlyName).pwszVal);
	}
	/**
	 * @brief		Retrieve the name of the given device from its properties.
	 *\n			PKEY_Device_FriendlyName
	 * @param dev	The IMMDevice to retrieve properties from.
	 * @returns		std::string
	 */
	inline std::string getDeviceName(IMMDevice* dev)
	{
		return w_converter.to_bytes(getDeviceProperty(dev, PKEY_Device_FriendlyName).pwszVal);
	}
	/**
	 * @brief		Retrieve the description of the given device from its properties.
	 *\n			PKEY_Device_DeviceDesc
	 * @param dev	The IMMDevice to retrieve properties from.
	 * @returns		std::string
	 */
	inline std::string getDeviceDesc(IMMDevice* dev)
	{
		return w_converter.to_bytes(getDeviceProperty(dev, PKEY_Device_DeviceDesc).pwszVal);
	}
	/**
	 * @brief		Queries the given device to determine whether it is an input or output device.
	 * @param dev	The IMMDevice to query.
	 * @returns		EDataFlow
	 */
	inline EDataFlow getDeviceDataFlow(IMMDevice* dev)
	{
		IMMEndpoint* endpoint;
		dev->QueryInterface<IMMEndpoint>(&endpoint);
		EDataFlow flow;
		endpoint->GetDataFlow(&flow);
		endpoint->Release();
		return flow;
	}
	/**
	 * @brief			Convert the given EDataFlow enumeration to a string representation.
	 * @param dataflow	An EDataFlow enum value.
	 * @returns			std::string
	 */
	constexpr std::string DataFlowToString(EDataFlow const& dataflow)
	{
		switch (dataflow) {
		case EDataFlow::eRender:
			return "Output";
		case EDataFlow::eCapture:
			return "Input";
		case EDataFlow::eAll:
			return "Input/Output";
		default:
			return{};
		}
	}
}
