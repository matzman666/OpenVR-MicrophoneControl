#include "audiomanagerwindows.h"
#include <exception>
#include "../logging.h"


// application namespace
namespace miccontrol {

AudioManagerWindows::~AudioManagerWindows() {
	if (audioEndpointVolume) {
		audioEndpointVolume->Release();
	}
	if (audioDevice) {
		audioDevice->Release();
	}
	audioDeviceEnumerator->Release();
}

void AudioManagerWindows::init(OverlayController* controller) {
	audioDeviceEnumerator = getAudioDeviceEnumerator();
	if (!audioDeviceEnumerator) {
		throw std::exception("Could not create audio device enumerator");
	}
	audioDevice = getDefaultRecordingDevice(audioDeviceEnumerator);
	if (audioDevice) {
		audioEndpointVolume = getAudioEndpointVolume(audioDevice);
	} else {
		LOG(WARNING) << "Could not find a default recording device.";
	}
	this->controller = controller;
}

bool AudioManagerWindows::isValid() {
	return audioEndpointVolume != nullptr;
}

bool AudioManagerWindows::isMuted() {
	BOOL value;
	if (audioEndpointVolume && audioEndpointVolume->GetMute(&value) >= 0) {
		return value;
	} else {
		return false;
	}
}

bool AudioManagerWindows::setMuted(const bool & mute) {
	if (audioEndpointVolume) {
		if (audioEndpointVolume->SetMute(mute, nullptr) >= 0) {
			return true;
		}
	}
	return false;
}

float AudioManagerWindows::getMasterVolume() {
	float value;
	if (audioEndpointVolume && audioEndpointVolume->GetMasterVolumeLevelScalar(&value) >= 0) {
		return value;
	} else {
		return 0.0;
	}
}

bool AudioManagerWindows::setMasterVolume(float value) {
	if (audioEndpointVolume) {
		if (audioEndpointVolume->SetMasterVolumeLevelScalar(value, nullptr) >= 0) {
			return true;
		}
	}
	return false;
}

IMMDeviceEnumerator* AudioManagerWindows::getAudioDeviceEnumerator() {
	IMMDeviceEnumerator* pEnumerator;
	if (CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator) < 0) {
		return nullptr;
	}
	return pEnumerator;
}

IMMDevice* AudioManagerWindows::getDefaultRecordingDevice(IMMDeviceEnumerator* deviceEnumerator) {
	IMMDevice* pDefCapture;
	if (deviceEnumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &pDefCapture) < 0) {
		return nullptr;
	}
	return pDefCapture;
}

IAudioEndpointVolume * AudioManagerWindows::getAudioEndpointVolume(IMMDevice* device) {
	IAudioEndpointVolume * pEndpointVolume;
	if (device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (void**)&pEndpointVolume) < 0) {
		return nullptr;
	}
	return pEndpointVolume;
}

}
