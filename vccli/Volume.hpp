#pragma once
#include <math.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>

#include <typeinfo>

namespace vccli {
	/// @brief	GUID to use as 'context' parameter in setter functions.
	inline static constexpr GUID default_context{};

	struct Volume {
		std::string resolved_name, identifier;
		EDataFlow flow_type;

		constexpr Volume(std::string const& resolved_name, std::string const& identifier, const EDataFlow flow_type) : resolved_name{ resolved_name }, identifier{ identifier }, flow_type{ flow_type } {}
		virtual ~Volume() = default;

		virtual bool getMuted() const = 0;
		virtual void setMuted(const bool) const = 0;
		virtual void mute() const
		{
			setMuted(true);
		}
		virtual void unmute() const
		{
			setMuted(false);
		}

		virtual float getVolume() const = 0;
		virtual void setVolume(const float&) const = 0;
		virtual void incrementVolume(const float& amount) const
		{
			auto level{ getVolume() + amount };
			if (level > 1.0f)
				level = 0.0f;
			setVolume(level);
		}
		virtual void decrementVolume(const float& amount) const
		{
			auto level{ getVolume() - amount };
			if (level < 0.0f)
				level = 0.0f;
			setVolume(level);
		}

		virtual float getVolumeScaled(const std::pair<float, float>& scale = { 0.0f, 100.0f }) const
		{
			return math::scale(getVolume(), { 0.0f, 1.0f }, scale);
		}
		virtual void setVolumeScaled(const float& level, const std::pair<float, float>& scale = { 0.0f, 100.0f }) const
		{
			setVolume(math::scale(level, scale, { 0.0f, 1.0f }));
		}

		virtual constexpr std::optional<std::string> type_name() const = 0;
		constexpr std::string getFlowTypeName() const { return DataFlowToString(this->flow_type); }

		template<std::derived_from<Volume> T>
		constexpr bool is_derived_type() const
		{
			return typeid(*this) == typeid(T);
		}
	};

	template<std::derived_from<IUnknown> T>
	struct VolumeController : Volume {
	protected:
		using base = VolumeController<T>;

		T* vol;

		constexpr VolumeController(T* vol, std::string const& resolved_name, std::string const& identifier, const EDataFlow flow_type) : Volume(resolved_name, identifier, flow_type), vol{ vol } {}

	public:
		virtual ~VolumeController()
		{
			if (this->vol) ((IUnknown*)this->vol)->Release();
		}
	};

	struct ApplicationVolume : public VolumeController<ISimpleAudioVolume> {
		std::string dev_id, sessionIdentifier, sessionInstanceIdentifier;

		constexpr ApplicationVolume(ISimpleAudioVolume* vol, std::string const& resolved_name, const DWORD pid, const EDataFlow flow_type, std::string const& deviceID, std::string const& sessionIdentifier, std::string const& sessionInstanceIdentifier) : base(vol, resolved_name, std::to_string(pid), flow_type), dev_id{ deviceID }, sessionIdentifier{ sessionIdentifier }, sessionInstanceIdentifier{ sessionInstanceIdentifier } {}

		bool getMuted() const override
		{
			BOOL muted;
			vol->GetMute(&muted);
			return static_cast<bool>(muted);
		}
		void setMuted(const bool state) const override
		{
			vol->SetMute(static_cast<BOOL>(state), &default_context);
		}

		float getVolume() const override
		{
			float level;
			vol->GetMasterVolume(&level);
			return level;
		}
		void setVolume(const float& level) const override
		{
			vol->SetMasterVolume(level, &default_context);
		}
		constexpr std::optional<std::string> type_name() const override
		{
			return{ "Session" };
		}
	};

	struct EndpointVolume : VolumeController<IAudioEndpointVolume> {
		bool isDefault;

		constexpr EndpointVolume(IAudioEndpointVolume* vol, std::string const& resolved_name, std::string const& dGuid, const EDataFlow flow_type, const bool isDefault) : base(vol, resolved_name, dGuid, flow_type), isDefault{ isDefault } {}

		bool getMuted() const override
		{
			BOOL isMuted;
			vol->GetMute(&isMuted);
			return static_cast<bool>(isMuted);
		}
		void setMuted(const bool state) const override
		{
			vol->SetMute(static_cast<BOOL>(state), &default_context);
		}

		float getVolume() const override
		{
			float level;
			vol->GetMasterVolumeLevelScalar(&level);
			return level;
		}
		void setVolume(const float& level) const override
		{
			vol->SetMasterVolumeLevelScalar(level, &default_context);
		}
		constexpr std::optional<std::string> type_name() const override
		{
			return{ "Device" };
		}
	};
}
