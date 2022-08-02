#pragma once
#include "util.hpp"
#include "Volume.hpp"

#include <make_exception.hpp>
#include <math.hpp>

#define $release(var) var->Release(); var = nullptr;

namespace vccli {
	struct basic_info {
		virtual ~basic_info() = default;
		friend std::ostream& operator<<(std::ostream& os, const basic_info&) { return os; }
	};

	struct DeviceInfo : basic_info {
		std::string dname, dguid;
		EDataFlow flow;
		bool isDefault;

		constexpr DeviceInfo(std::string const& DNAME, std::string const& DGUID, const EDataFlow flow, const bool isDefault) : dname{ DNAME }, dguid{ DGUID }, flow{ flow }, isDefault{ isDefault } {}

		std::optional<std::string> type_name() const { return "Device"; }
	};
	struct ProcessInfo : DeviceInfo {
		DWORD pid;
		std::string pname, suid, sguid;

		constexpr ProcessInfo(std::string const& PNAME, const DWORD PID, const EDataFlow flow, std::string const& SUID, std::string const& SGUID, std::string const& DGUID, std::string const& DNAME, const bool isDefaultDevice)
			: DeviceInfo(DNAME, DGUID, flow, false), pid{ PID }, pname{ PNAME }, suid{ SUID }, sguid{ SGUID } {}

	};

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
	#pragma endregion Internal

		static ProcessInfoLookup::pInfo_list_t GetAudioProcessLookup(EDataFlow flow = EDataFlow::eAll, ERole role = ERole::eMultimedia)
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
		static ProcessInfoLookup::pInfo_list_t GetAudioProcessLookupSorted(const std::function<bool(std::pair<DWORD, std::string>, std::pair<DWORD, std::string>)>& sorting_predicate, EDataFlow flow = EDataFlow::eAll, ERole role = ERole::eMultimedia)
		{
			std::vector<std::pair<DWORD, std::string>> vec{ GetAudioProcessLookup(flow, role) };
			std::sort(vec.begin(), vec.end(), sorting_predicate);
			return vec;
		}
		static ProcessInfoLookup::pInfo_list_t GetAudioProcessLookupSorted(EDataFlow flow = EDataFlow::eAll, ERole role = ERole::eMultimedia)
		{
			return GetAudioProcessLookupSorted(std::less<std::pair<DWORD, std::string>>{}, flow, role);
		}

	public:
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
		static std::string getDeviceName(std::string const& devID)
		{
			auto* deviceEnumerator{ getDeviceEnumerator() };
			IMMDevice* dev;
			deviceEnumerator->GetDevice(w_converter.from_bytes(devID).c_str(), &dev);
			$release(deviceEnumerator);
			std::string name{ getDeviceFriendlyName(dev) };
			$release(dev);
			return name;
		}

		static std::vector<ProcessInfo> GetAllAudioProcesses(EDataFlow flow = EDataFlow::eAll, ERole role = ERole::eMultimedia)
		{
			std::vector<ProcessInfo> vec;

			auto deviceEnumerator{ getDeviceEnumerator() };
			IMMDevice* dev;

			std::string defDevIDIn{}, defDevIDOut{};
			deviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eMultimedia, &dev);
			defDevIDOut = getDeviceID(dev);
			$release(dev);
			deviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eCapture, ERole::eMultimedia, &dev);
			defDevIDIn = getDeviceID(dev);
			$release(dev);

			IMMDeviceCollection* devices;
			deviceEnumerator->EnumAudioEndpoints(flow, role, &devices);

			UINT count;
			devices->GetCount(&count);

			static constexpr IID iid_IAudioSessionManager2{ __uuidof(IAudioSessionManager2) };

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
				const auto& devName{ getDeviceFriendlyName(dev) };
				const auto& devID{ getDeviceID(dev) };
				const auto& devFlow{ getDeviceDataFlow(dev) };

				for (int i{ 0 }; i < sessionCount; ++i) {
					sessionEnumerator->GetSession(i, &sessionControl);

					sessionControl->QueryInterface<IAudioSessionControl2>(&sessionControl2);

					DWORD pid;
					sessionControl2->GetProcessId(&pid);

					if (const auto& pName{ GetProcessNameFrom(pid) }; pName.has_value())
						vec.emplace_back(ProcessInfo{ pName.value(), pid, devFlow, getSessionIdentifier(sessionControl2), getSessionInstanceIdentifier(sessionControl2), devID, devName, devID == defDevIDIn || devID == defDevIDOut });

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
		static std::vector<ProcessInfo> GetAllAudioProcessesSorted(const std::function<bool(ProcessInfo, ProcessInfo)>& sorting_predicate, EDataFlow flow = EDataFlow::eAll, ERole role = ERole::eMultimedia)
		{
			auto vec{ GetAllAudioProcesses(flow, role) };
			std::sort(vec.begin(), vec.end(), sorting_predicate);
			return vec;
		}
		static std::vector<ProcessInfo> GetAllAudioProcessesSorted(EDataFlow flow = EDataFlow::eAll, ERole role = ERole::eMultimedia)
		{
			const auto& nSorter{ std::less<DWORD>() };
			return GetAllAudioProcessesSorted([&nSorter](ProcessInfo const& l, ProcessInfo const& r) -> bool { return static_cast<int>(l.flow) < static_cast<int>(r.flow) && nSorter(l.pid, r.pid); }, flow, role);
		}

		static std::vector<DeviceInfo> GetAllAudioDevices(EDataFlow flow = EDataFlow::eAll, ERole role = ERole::eMultimedia)
		{
			IMMDeviceEnumerator* deviceEnumerator{ getDeviceEnumerator() };
			IMMDevice* dev;

			std::wstring defaultOutputDevID, defaultInputDevID;
			LPWSTR sbuf;

			// Get default output device id
			deviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eRender, role, &dev);
			dev->GetId(&sbuf);
			defaultOutputDevID = sbuf;
			dev->Release();
			// Get default input device id
			deviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eCapture, role, &dev);
			dev->GetId(&sbuf);
			defaultInputDevID = sbuf;
			dev->Release();

			IMMDeviceCollection* devices;
			deviceEnumerator->EnumAudioEndpoints(flow, role, &devices);
			deviceEnumerator->Release();

			UINT count;
			devices->GetCount(&count);

			std::vector<DeviceInfo> vec;
			vec.reserve(count);

			for (UINT i{ 0 }; i < count; ++i) {
				devices->Item(i, &dev);

				dev->GetId(&sbuf);
				const std::wstring devID{ sbuf };

				vec.emplace_back(DeviceInfo{ getDeviceFriendlyName(dev), w_converter.to_bytes(devID), getDeviceDataFlow(dev), devID == defaultInputDevID || devID == defaultOutputDevID });

				dev->Release();
			}

			devices->Release();

			vec.shrink_to_fit();
			return vec;
		}
		static std::vector<DeviceInfo> GetAllAudioDevicesSorted(const std::function<bool(DeviceInfo, DeviceInfo)>& sorting_predicate, EDataFlow flow = EDataFlow::eAll, ERole role = ERole::eMultimedia)
		{
			auto devices{ GetAllAudioDevices(flow, role) };
			std::sort(devices.begin(), devices.end(), sorting_predicate);
			return devices;
		}
		static std::vector<DeviceInfo> GetAllAudioDevicesSorted(EDataFlow flow = EDataFlow::eAll, ERole role = ERole::eMultimedia)
		{
			const auto& sSorter{ std::less<std::string>() };
			return GetAllAudioDevicesSorted([&sSorter](DeviceInfo const& l, DeviceInfo const& r) -> bool { return static_cast<int>(l.flow) < static_cast<int>(r.flow) && sSorter(l.dname, r.dname); }, flow, role);
		}

		/// @brief	Gets the appropriate volume control object for the given string.
		static std::unique_ptr<Volume> getObject(const std::string& target_id, const bool fuzzy, EDataFlow const& deviceFlowFilter, const bool defaultDevIsOutput = true)
		{
			auto target_id_lower{ str::tolower(target_id) };
			if (fuzzy)
				target_id_lower = str::trim(target_id_lower);

			const auto& compare_target_id_to{ [&target_id_lower, &fuzzy](std::string const& s) -> bool {
				return (target_id_lower == (fuzzy ? str::trim(s) : s)) || (fuzzy && s.find(str::trim(target_id_lower)) != std::string::npos);
			} };

			std::unique_ptr<Volume> object{ nullptr };

			IMMDeviceEnumerator* deviceEnumerator{ getDeviceEnumerator() };
			IMMDevice* dev;

			if (target_id.empty()) {
				// DEFAULT DEVICE:
				EDataFlow defaultDevFlow{ deviceFlowFilter };
				if (defaultDevFlow == EDataFlow::eAll) //< we can't request a default 'eAll' device; select input or output
					defaultDevFlow = (defaultDevIsOutput ? EDataFlow::eRender : EDataFlow::eCapture);

				deviceEnumerator->GetDefaultAudioEndpoint(defaultDevFlow, ERole::eMultimedia, &dev);
				$release(deviceEnumerator);
				IAudioEndpointVolume* endpoint{};
				dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (void**)&endpoint);
				const auto& devName{ getDeviceFriendlyName(dev) };
				const auto& deviceID{ getDeviceID(dev) };
				$release(dev);

				return std::make_unique<EndpointVolume>(endpoint, devName, deviceID, defaultDevFlow, true);
			} // Else we have an actual target ID to find

			// Check if we have a valid PID
			std::optional<DWORD> target_pid;
			if (std::all_of(target_id.begin(), target_id.end(), str::stdpred::isdigit))
				target_pid = str::stoul(target_id);

			// Enumerate all devices of the specified I/O type(s):
			IMMDeviceCollection* devices;
			deviceEnumerator->EnumAudioEndpoints(deviceFlowFilter, ERole::eMultimedia, &devices);
			$release(deviceEnumerator)

				UINT count;
			devices->GetCount(&count);

			for (UINT i{ 0u }; object == nullptr && i < count; ++i) {
				devices->Item(i, &dev);

				LPWSTR sbuf;
				dev->GetId(&sbuf);
				std::string deviceID{ w_converter.to_bytes(sbuf) };

				const auto& deviceName{ getDeviceFriendlyName(dev) };
				const auto& deviceFlow{ getDeviceDataFlow(dev) };

				// Check if this device is a match
				if (!target_pid.has_value() && (compare_target_id_to(str::tolower(deviceID)) || compare_target_id_to(str::tolower(deviceName)))) {
					IAudioEndpointVolume* endpointVolume{};
					dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (void**)&endpointVolume);
					object = std::make_unique<EndpointVolume>(endpointVolume, deviceName, deviceID, deviceFlow, isDefaultDevice(dev));
				}
				else { // Check for matching sessions on this device:
					IAudioSessionManager2* mgr{};
					dev->Activate(__uuidof(IAudioSessionManager2), 0, NULL, (void**)&mgr);

					IAudioSessionEnumerator* sessionEnumerator;
					mgr->GetSessionEnumerator(&sessionEnumerator);
					$release(mgr);

					IAudioSessionControl* sessionControl;
					IAudioSessionControl2* sessionControl2;
					ISimpleAudioVolume* sessionVolumeControl;

					// Enumerate all audio sessions on this device:
					int sessionCount;
					sessionEnumerator->GetCount(&sessionCount);

					for (int j{ 0 }; j < sessionCount; ++j) {
						sessionEnumerator->GetSession(j, &sessionControl);

						sessionControl->QueryInterface<IAudioSessionControl2>(&sessionControl2);
						$release(sessionControl);

						DWORD pid;
						sessionControl2->GetProcessId(&pid);

						const auto& pname{ GetProcessNameFrom(pid) };
						const auto& suid{ getSessionIdentifier(sessionControl2) }, & sguid{ getSessionInstanceIdentifier(sessionControl2) };

						// Check if this session is a match:
						if ((pname.has_value() && compare_target_id_to(str::tolower(pname.value()))) || (target_pid.has_value() && target_pid.value() == pid) || compare_target_id_to(suid) || compare_target_id_to(sguid)) {
							sessionControl2->QueryInterface<ISimpleAudioVolume>(&sessionVolumeControl);
							$release(sessionControl2);
							object = std::make_unique<ApplicationVolume>(sessionVolumeControl, pname.value(), pid, deviceFlow, deviceID, suid, sguid);
							break; //< break from session enumeration loop
						}
						$release(sessionControl2);
					} //< end session enumeration loop
					$release(sessionEnumerator)
				}
				$release(dev);
			}
			$release(devices);

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
				ProcessInfoLookup lookup{ GetAudioProcessLookup(flow, role) };
				if (const auto& pInfo{ lookup(identifier, true) }; pInfo.has_value()) {
					return pInfo.value().first;
				}
			}
			return std::nullopt;
		}
	};
}
