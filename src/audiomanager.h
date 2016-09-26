#pragma once


// application namespace
namespace miccontrol {

class OverlayController;

class AudioManager
{
public:
	virtual ~AudioManager() {};

	virtual void init(OverlayController* controller) = 0;
	virtual bool isValid() = 0;

	virtual bool isMuted() = 0;
	virtual bool setMuted(const bool& mute) = 0;

	virtual float getMasterVolume() = 0;
	virtual bool setMasterVolume(float value) = 0;
};

}

