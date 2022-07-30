#pragma once
#include <math.hpp>

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>

namespace vccli {
	/// @brief	GUID to use as 'context' parameter in setter functions.
	inline static constexpr GUID default_context{};

	struct Volume {
		std::string resolved_name;

		Volume(std::string const& resolved_name) : resolved_name{ resolved_name } {}
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

		virtual constexpr bool isValid() const = 0;

		virtual constexpr std::optional<std::string> type_name() const = 0;
	};
	template<std::derived_from<IUnknown> T>
	struct VolumeController : Volume {
	protected:
		using base = VolumeController<T>;

		T* vol;

		VolumeController(T* vol, std::string const& resolved_name) : Volume(resolved_name), vol{ vol } {}

	public:
		virtual ~VolumeController()
		{
			if (this->vol) ((IUnknown*)this->vol)->Release();
		}

		constexpr bool isValid() const override
		{
			return vol != nullptr;
		}
	};
	struct NullVolume : VolumeController<IUnknown> {
		NullVolume(std::string const& resolved_name) : base(nullptr, resolved_name) {}
		bool getMuted() const override { throw make_exception("Object is null!"); }
		void setMuted(const bool) const override { throw make_exception("Object is null!"); }
		float getVolume() const override { throw make_exception("Object is null!"); }
		void setVolume(const float&) const override { throw make_exception("Object is null!"); }
		constexpr bool isValid() const override { return false; }
		constexpr std::optional<std::string> type_name() const override { return std::nullopt; }
	};

	struct ApplicationVolume : public VolumeController<ISimpleAudioVolume> {
		ApplicationVolume(ISimpleAudioVolume* vol, std::string const& resolved_name) : base(vol, resolved_name) {}

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
		constexpr std::optional<std::string> type_name() const override { return{ "Session" }; }
	};

	struct EndpointVolume : VolumeController<IAudioEndpointVolume> {
		EDataFlow flow;

		EndpointVolume(IAudioEndpointVolume* vol, std::string const& resolved_name, EDataFlow const& flow) : base(vol, resolved_name), flow{ flow } {}

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
		constexpr std::optional<std::string> type_name() const override { return{ DataFlowToString(flow) + " Device"}; }
	};
}
