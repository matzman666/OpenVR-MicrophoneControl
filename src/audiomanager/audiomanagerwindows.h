#pragma once

#include "../audiomanager.h"

#include <Mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <Endpointvolume.h>


// application namespace
namespace miccontrol {

class AudioManagerWindows : public AudioManager {
friend class AudioNotificationClient;
private:
	OverlayController* controller = nullptr;
	IMMDeviceEnumerator* audioDeviceEnumerator = nullptr;
	IMMDevice* audioDevice = nullptr;
	IAudioEndpointVolume* audioEndpointVolume = nullptr;

public:
	~AudioManagerWindows();

	void init(OverlayController* controller) override;
	bool isValid() override;

	bool isMuted() override;
	bool setMuted(const bool& mute) override;

	float getMasterVolume() override;
	bool setMasterVolume(float value) override;

private:
	IMMDeviceEnumerator* getAudioDeviceEnumerator();
	IMMDevice* getDefaultRecordingDevice(IMMDeviceEnumerator* deviceEnumerator);
	IAudioEndpointVolume* getAudioEndpointVolume(IMMDevice* device);
};

}

