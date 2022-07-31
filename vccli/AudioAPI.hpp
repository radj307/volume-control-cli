#pragma once
#include "util.hpp"
#include "Volume.hpp"

#include <make_exception.hpp>
#include <math.hpp>

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>

namespace vccli {
	struct ProcessInfoLookup {
		using pInfo_t = std::pair<DWORD, std::string>;
		using pInfo_list_t = std::vector<pInfo_t>;
		pInfo_list_t vec;

		constexpr ProcessInfoLookup(pInfo_list_t&& vec) : vec{ std::move(vec) } {}
		constexpr ProcessInfoLookup(pInfo_list_t const& vec) : vec{ vec } {}

		constexpr std::optional<pInfo_t> operator()(std::string pName, const bool ignoreCase = true) const
		{
			if (ignoreCase)
				pName = str::tolower(pName);
			for (const auto& it : vec)
				if ((!ignoreCase && it.second == pName) || (ignoreCase && str::tolower(it.second) == pName))
					return it;
			return std::nullopt;
		}
		constexpr std::optional<pInfo_t> operator()(DWORD const& pid) const
		{
			for (const auto& it : vec)
				if (it.first == pid)
					return it;
			return std::nullopt;
		}
	};

	class AudioAPI {
	#pragma region Internal
		static IMMDeviceEnumerator* getDeviceEnumerator()
		{
			IMMDeviceEnumerator* deviceEnumerator{};
			if (const auto& hr{ CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (void**)&deviceEnumerator) }; hr != S_OK)
				throw make_exception(GetErrorMessageFrom(hr), " (code ", hr, ')');
			return{ deviceEnumerator };
		}
		static IMMDevice* getDevice(const std::string& device_id)
		{
			const auto& deviceEnumerator{ getDeviceEnumerator() };

			IMMDevice* dev;
			IMMDeviceCollection* devices;
			deviceEnumerator->EnumAudioEndpoints(EDataFlow::eRender, ERole::eMultimedia, &devices);

			UINT count;
			devices->GetCount(&count);

			LPWSTR sbuf{};

			for (UINT i{ 0u }; i < count; ++i) {
				devices->Item(i, &dev);

				if (dev->GetId(&sbuf) == S_OK && w_converter.to_bytes(sbuf) == device_id) {
					devices->Release();
					deviceEnumerator->Release();

					return dev;
				}
			}

			devices->Release();
			deviceEnumerator->Release();

			return nullptr;
		}
		static IMMDevice* getDefaultDevice()
		{
			auto* deviceEnumerator{ getDeviceEnumerator() };

			IMMDevice* dev;
			deviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eMultimedia, &dev);

			deviceEnumerator->Release();

			return dev;
		}
		/// @brief	Gets the volume control object for the specified process from the specified device.
		static ISimpleAudioVolume* getVolumeObject(const DWORD pid, IMMDevice* device)
		{
			IAudioSessionManager2* mgr{};
			device->Activate(__uuidof(IAudioSessionManager2), 0, NULL, (void**)&mgr);

			IAudioSessionEnumerator* sessionEnumerator;
			mgr->GetSessionEnumerator(&sessionEnumerator);

			int count;
			sessionEnumerator->GetCount(&count);

			ISimpleAudioVolume* volumeControl{};

			for (int i{ 0 }; i < count; ++i) {
				IAudioSessionControl* sessionControl;
				sessionEnumerator->GetSession(i, &sessionControl);

				IAudioSessionControl2* session;
				sessionControl->QueryInterface<IAudioSessionControl2>(&session);

				if (session) {
					DWORD sessionID;
					session->GetProcessId(&sessionID);
					if (pid == sessionID) {
						session->QueryInterface<ISimpleAudioVolume>(&volumeControl);
						break;
					}

					session->Release();
				}
				sessionControl->Release();
			}

			sessionEnumerator->Release();

			return volumeControl;
		}
		/// @brief	Gets the volume control object for the specified process.
		static ISimpleAudioVolume* getVolumeObject(const DWORD pid)
		{
			auto* deviceEnumerator{ getDeviceEnumerator() };

			IMMDevice* dev{};
			deviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eMultimedia, &dev);

			LPWSTR devIDbuf{};
			std::string defaultDeviceID;
			if (auto* volumeControl = getVolumeObject(pid, dev)) {
				deviceEnumerator->Release();
				return volumeControl;
			}
			else {
				dev->GetId(&devIDbuf);
				// save the default device's GUID string for later
				defaultDeviceID = w_converter.to_bytes(devIDbuf);

				dev->Release();
			}

			IMMDeviceCollection* devices;
			deviceEnumerator->EnumAudioEndpoints(EDataFlow::eRender, ERole::eMultimedia, &devices);

			UINT devicesCount;
			devices->GetCount(&devicesCount);

			for (UINT i{ 0u }; i < devicesCount; ++i) {
				devices->Item(i, &dev);

				dev->GetId(&devIDbuf);

				if (w_converter.to_bytes(devIDbuf) == defaultDeviceID)
					continue;

				if (auto* volumeControl = getVolumeObject(pid, dev)) {

					devices->Release();
					deviceEnumerator->Release();

					return volumeControl;
				}
				dev->Release();
			}

			devices->Release();
			deviceEnumerator->Release();

			return nullptr;
		}
		/// @brief	Gets the audio endpoint volume control object for the specified device.
		static IAudioEndpointVolume* getEndpointObject(const std::string& deviceID, EDataFlow flow = EDataFlow::eRender, ERole role = ERole::eMultimedia)
		{
			auto* deviceEnumerator{ getDeviceEnumerator() };

			IMMDeviceCollection* devices;
			deviceEnumerator->EnumAudioEndpoints(flow, role, &devices);

			UINT devicesCount;
			devices->GetCount(&devicesCount);

			IMMDevice* dev;
			for (UINT i{ 0u }; i < devicesCount; ++i) {
				devices->Item(i, &dev);

				LPWSTR sbuf;
				if (dev->GetId(&sbuf) == S_OK && deviceID == w_converter.to_bytes(sbuf)) {
					if (IAudioEndpointVolume* volumeControl{}; dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (void**)&volumeControl) == S_OK) {
						dev->Release();
						devices->Release();
						deviceEnumerator->Release();

						return volumeControl;
					}
				}

				dev->Release();
			}

			devices->Release();
			deviceEnumerator->Release();

			return nullptr;
		}
		/// @brief	Gets the default audio endpoint volume control object.
		static IAudioEndpointVolume* getEndpointObject(EDataFlow flow = EDataFlow::eRender, ERole role = ERole::eMultimedia)
		{
			auto* deviceEnumerator{ getDeviceEnumerator() };

			IMMDevice* dev;
			deviceEnumerator->GetDefaultAudioEndpoint(flow, role, &dev);

			if (IAudioEndpointVolume* volumeControl{}; dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (void**)&volumeControl) == S_OK) {
				dev->Release();
				deviceEnumerator->Release();

				return volumeControl;
			}
			deviceEnumerator->Release();

			return nullptr;
		}
	#pragma endregion Internal

	public:
		static ProcessInfoLookup::pInfo_list_t GetAllAudioProcesses(EDataFlow flow = EDataFlow::eRender, ERole role = ERole::eMultimedia)
		{
			ProcessInfoLookup::pInfo_list_t vec;

			auto deviceEnumerator{ getDeviceEnumerator() };

			IMMDeviceCollection* devices;
			deviceEnumerator->EnumAudioEndpoints(flow, role, &devices);

			UINT count;
			devices->GetCount(&count);

			static constexpr IID iid_IAudioSessionManager2{ __uuidof(IAudioSessionManager2) };

			IMMDevice* dev;
			IAudioSessionManager2* devManager{};
			IAudioSessionEnumerator* sessionEnumerator{};
			IAudioSessionControl* sessionControl{};
			IAudioSessionControl2* sessionControl2{};

			for (UINT i{ 0u }; i < count; ++i) {
				devices->Item(i, &dev);

				dev->Activate(iid_IAudioSessionManager2, 0, NULL, (void**)&devManager);
				devManager->GetSessionEnumerator(&sessionEnumerator);

				int sessionCount;
				sessionEnumerator->GetCount(&sessionCount);

				vec.reserve(vec.size() + sessionCount);

				for (int i{ 0 }; i < sessionCount; ++i) {
					sessionEnumerator->GetSession(i, &sessionControl);

					sessionControl->QueryInterface<IAudioSessionControl2>(&sessionControl2);

					DWORD pid;
					sessionControl2->GetProcessId(&pid);

					if (std::any_of(vec.begin(), vec.end(), [&pid](auto&& pair) -> bool { return pair.first == pid; }))
						continue;

					if (const auto& pName{ GetProcessNameFrom(pid) }; pName.has_value())
						vec.emplace_back(std::make_pair(pid, pName.value()));

					sessionControl->Release();
					sessionControl2->Release();
				}

				sessionEnumerator->Release();
				devManager->Release();
				dev->Release();
			}

			devices->Release();
			deviceEnumerator->Release();

			vec.shrink_to_fit();
			return vec;
		}
		static ProcessInfoLookup::pInfo_list_t GetAllAudioProcessesSorted(const std::function<bool(std::pair<DWORD, std::string>, std::pair<DWORD, std::string>)>& sorting_predicate, EDataFlow flow = EDataFlow::eRender, ERole role = ERole::eMultimedia)
		{
			std::vector<std::pair<DWORD, std::string>> vec{ GetAllAudioProcesses(flow, role) };
			std::sort(vec.begin(), vec.end(), sorting_predicate);
			return vec;
		}
		static ProcessInfoLookup::pInfo_list_t GetAllAudioProcessesSorted(EDataFlow flow = EDataFlow::eRender, ERole role = ERole::eMultimedia)
		{
			return GetAllAudioProcessesSorted(std::less<std::pair<DWORD, std::string>>{}, flow, role);
		}

		/// @brief	Gets the appropriate volume control object for the given string.
		static std::unique_ptr<Volume> getObject(const std::string& target_id, EDataFlow const& deviceFlowFilter, const bool& defaultDevIsOutput = true)
		{
			const auto& target_id_lower{ str::tolower(target_id) };
			std::unique_ptr<Volume> object{ nullptr };

			IMMDeviceEnumerator* deviceEnumerator{ getDeviceEnumerator() };
			IMMDevice* dev;

			if (target_id.empty()) {
				// DEFAULT DEVICE:
				EDataFlow defaultDevFlow{ deviceFlowFilter };
				if (defaultDevFlow == EDataFlow::eAll)
					defaultDevFlow = (defaultDevIsOutput ? EDataFlow::eRender : EDataFlow::eCapture);

				deviceEnumerator->GetDefaultAudioEndpoint(defaultDevFlow, ERole::eMultimedia, &dev);
				deviceEnumerator->Release();
				IAudioEndpointVolume* endpoint{};
				dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (void**)&endpoint);
				const auto& devName{ str::trim(getDeviceFriendlyName(dev)) };
				dev->Release();

				return std::make_unique<EndpointVolume>(endpoint, devName, defaultDevFlow, true);
			} // Else we have an actual target ID to find

			// Check if we have a valid PID
			std::optional<DWORD> target_pid;
			if (std::all_of(target_id.begin(), target_id.end(), str::stdpred::isdigit))
				target_pid = str::stoul(target_id);

			// Enumerate all devices of the specified I/O type(s):
			IMMDeviceCollection* devices;
			deviceEnumerator->EnumAudioEndpoints(deviceFlowFilter, ERole::eMultimedia, &devices);
			deviceEnumerator->Release();

			UINT count;
			devices->GetCount(&count);

			for (UINT i{ 0u }; object == nullptr && i < count; ++i) {
				devices->Item(i, &dev);

				LPWSTR sbuf;
				dev->GetId(&sbuf);
				std::string deviceID{ w_converter.to_bytes(sbuf) };

				const auto& deviceName{ str::trim(getDeviceFriendlyName(dev)) };

				// Check if this device is a match
				if (!target_pid.has_value() && (target_id_lower == str::tolower(deviceID) || target_id_lower == str::tolower(deviceName))) {
					IAudioEndpointVolume* endpointVolume{};
					dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (void**)&endpointVolume);
					object = std::make_unique<EndpointVolume>(endpointVolume, deviceName, getDeviceDataFlow(dev), isDefaultDevice(dev));
				}
				else { // Check for matching sessions on this device:
					IAudioSessionManager2* mgr{};
					dev->Activate(__uuidof(IAudioSessionManager2), 0, NULL, (void**)&mgr);

					IAudioSessionEnumerator* sessionEnumerator;
					mgr->GetSessionEnumerator(&sessionEnumerator);
					mgr->Release();

					IAudioSessionControl* sessionControl;
					IAudioSessionControl2* sessionControl2;
					ISimpleAudioVolume* sessionVolumeControl;

					// Enumerate all audio sessions on this device:
					int sessionCount;
					sessionEnumerator->GetCount(&sessionCount);

					for (int j{ 0 }; j < sessionCount; ++j) {
						sessionEnumerator->GetSession(j, &sessionControl);

						sessionControl->QueryInterface<IAudioSessionControl2>(&sessionControl2);
						sessionControl->Release();

						DWORD pid;
						sessionControl2->GetProcessId(&pid);

						const auto& pname{ GetProcessNameFrom(pid) };

						// Check if this session is a match:
						if ((pname.has_value() && target_id_lower == str::tolower(pname.value())) || (target_pid.has_value() && target_pid.value() == pid)) {
							sessionControl2->QueryInterface<ISimpleAudioVolume>(&sessionVolumeControl);
							sessionControl2->Release();
							object = std::make_unique<ApplicationVolume>(sessionVolumeControl, pname.value());
							break;
						}
					}
					sessionEnumerator->Release();
				}
				dev->Release();
			}
			devices->Release();

			if (object == nullptr) // use a NullVolume struct instead of returning nullptr
				object = std::make_unique<NullVolume>(target_id);

			return object;
		}

		static bool isDefaultDevice(IMMDevice* dev)
		{
			const auto& devID{ getDeviceID(dev) };
			IMMDeviceEnumerator* deviceEnumerator{ getDeviceEnumerator() };
			bool isDefault{ false };
			IMMDevice* tmp;
			std::string tmpID{};
			deviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eMultimedia, &tmp);
			tmpID = getDeviceID(tmp);
			tmp->Release();
			if (tmpID == devID)
				isDefault = true;
			else {
				deviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eCapture, ERole::eMultimedia, &tmp);
				tmpID = getDeviceID(tmp);
				tmp->Release();
				if (tmpID == devID)
					isDefault = true;
			}
			deviceEnumerator->Release();
			return isDefault;
		}
		/**
		 * @brief				Resolves the given identifier to a process ID by searching for it in a snapshot.
		 * @param identifier	A process name or process ID.
		 * @returns				The process ID of the target process; or 0 if the process doesn't exist. If the process WAS found in the snapshot but is NOT still active, returns 0.
		 */
		static std::optional<DWORD> ResolveProcessIdentifier(const std::string& identifier, const std::function<bool(std::string, std::string)>& comp = CompareProcessName, EDataFlow flow = EDataFlow::eRender, ERole role = ERole::eMultimedia)
		{
			if (std::all_of(identifier.begin(), identifier.end(), str::stdpred::isdigit))
				return str::stoul(identifier); //< return ID casted to a number
			else {
				ProcessInfoLookup lookup{ GetAllAudioProcesses(flow, role) };
				if (const auto& pInfo{ lookup(identifier, true) }; pInfo.has_value()) {
					return pInfo.value().first;
				}
			}
			return std::nullopt;
		}
	};
}
