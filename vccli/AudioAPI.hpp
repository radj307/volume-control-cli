#pragma once
#include "util.hpp"

#include <make_exception.hpp>

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

namespace vccli {
	inline static constexpr GUID default_context{};

	class ApplicationVolume {
		ISimpleAudioVolume* vol;

	public:
		ApplicationVolume(ISimpleAudioVolume* vol) : vol{ vol } { if (this->vol == nullptr) throw make_exception(""); }
		~ApplicationVolume() { if (this->vol) this->vol->Release(); }

	#pragma region Mute
		bool getMuted() const
		{
			BOOL muted;
			vol->GetMute(&muted);
			return static_cast<bool>(muted);
		}
		void setMuted(const bool state) const
		{
			vol->SetMute(static_cast<BOOL>(state), &default_context);
		}
		void mute() const
		{
			vol->SetMute(static_cast<BOOL>(true), &default_context);
		}
		void unmute() const
		{
			vol->SetMute(static_cast<BOOL>(false), &default_context);
		}
	#pragma endregion Mute

		float getVolume() const
		{
			float level;
			vol->GetMasterVolume(&level);
			return level;
		}
		void setVolume(const float& level) const
		{
			vol->SetMasterVolume(level, &default_context);
		}
		void incrementVolume(const float& amount) const
		{
			auto level{ getVolume() + amount };
			if (level > 1.0f)
				level = 0.0f;
			setVolume(level);
		}
		void decrementVolume(const float& amount) const
		{
			auto level{ getVolume() - amount };
			if (level < 0.0f)
				level = 0.0f;
			setVolume(level);
		}
	};

	class AudioAPI {
		static ISimpleAudioVolume* getVolumeObject(const DWORD pid, IMMDevice* device)
		{
			static constexpr IID iidIAudioSessionManager2 = __uuidof(IAudioSessionManager2);
			IAudioSessionManager2* mgr{};
			device->Activate(iidIAudioSessionManager2, 0, NULL, (void**)&mgr);

			IAudioSessionEnumerator* sessionEnumerator;
			mgr->GetSessionEnumerator(&sessionEnumerator);

			int count;
			sessionEnumerator->GetCount(&count);


			for (int i{ 0 }; i < count; ++i) {
				IAudioSessionControl* sessionControl;
				sessionEnumerator->GetSession(i, &sessionControl);

				IAudioSessionControl2* session;
				sessionControl->QueryInterface<IAudioSessionControl2>(&session);

				if (session) {
					DWORD sessionID;

					if (session->GetProcessId(&sessionID) == S_OK && pid == sessionID) {
						ISimpleAudioVolume* volumeControl;
						session->QueryInterface<ISimpleAudioVolume>(&volumeControl);

						session->Release();
						sessionControl->Release();

						return volumeControl;
					}

					session->Release();
				}
				sessionControl->Release();
			}

			return nullptr;
		}
		static ISimpleAudioVolume* getVolumeObject(const DWORD pid)
		{
			IMMDeviceEnumerator* deviceEnumerator{};
			if (const auto& hr{ CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (void**)&deviceEnumerator) }; hr != S_OK)
				throw make_exception(GetErrorMessageFrom(hr), " (code ", hr, ')');

			IMMDevice* dev;
			deviceEnumerator->GetDefaultAudioEndpoint(EDataFlow::eRender, ERole::eMultimedia, &dev);

			if (auto* volumeControl = getVolumeObject(pid, dev)) {
				deviceEnumerator->Release();
				return volumeControl;
			}
			else dev->Release();

			IMMDeviceCollection* devices;
			deviceEnumerator->EnumAudioEndpoints(EDataFlow::eRender, ERole::eMultimedia, &devices);

			UINT devicesCount;
			devices->GetCount(&devicesCount);

			for (UINT i{ 0u }; i < devicesCount; ++i) {
				devices->Item(i, &dev);
				if (auto* volumeControl = getVolumeObject(0, dev)) {

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

	public:
		static ApplicationVolume getApplicationVolumeObject(const std::string& identifier)
		{
			return{ getVolumeObject(ResolveProcessIdentifier(identifier)) };
		}
		static float getVolume(const std::string& identifier) noexcept(false)
		{
			if (auto* vol = getVolumeObject(ResolveProcessIdentifier(identifier))) {
				float level{ 0.0f };
				vol->GetMasterVolume(&level);
				vol->Release();
				return level;
			}
			throw make_exception("Couldn't find a process with identifier '", identifier, "'!");
		}
		static void setVolume(const std::string& identifier, const float& level) noexcept(false)
		{
			if (auto* vol = getVolumeObject(ResolveProcessIdentifier(identifier))) {
				vol->SetMasterVolume(level, &default_context);
				vol->Release();
				return;
			}
			throw make_exception("Couldn't find a process with identifier '", identifier, "'!");
		}
		static bool getIsMuted(const std::string& identifier) noexcept(false)
		{
			if (auto* vol = getVolumeObject(ResolveProcessIdentifier(identifier))) {
				BOOL isMuted{ 0 };
				vol->GetMute(&isMuted);
				vol->Release();
				return static_cast<bool>(isMuted);
			}
			throw make_exception("Couldn't find a process with identifier '", identifier, "'!");
		}
		static void setIsMuted(const std::string& identifier, const bool state) noexcept(false)
		{
			if (auto* vol = getVolumeObject(ResolveProcessIdentifier(identifier))) {
				vol->SetMute(static_cast<BOOL>(state), &default_context);
				vol->Release();
				return;
			}
			throw make_exception("Couldn't find a process with identifier '", identifier, "'!");
		}
	};
}
