
![language](https://img.shields.io/badge/Language-C%2B%2B11-green.svg) ![dependencies](https://img.shields.io/badge/Dependencies-OpenVR%2C%20Qt5-green.svg) ![license_gpl3](https://img.shields.io/badge/License-GPL%203.0-green.svg)

**Deprecated:** Has been merged into [OpenVR Advanced Settings](https://github.com/matzman666/OpenVR-AdvancedSettings).

# OpenVR Microphone Control Overlay

Adds an overlay to the OpenVR dashboard that allows to mute the microphone and implements push-to-talk.

# Features
## - Microphone Control

Allows to mute the microphone and set the recording volume level.

## - Push-to-Task:

When push-to-talk is activated, the microphone gets muted in the windows audio settings. This way push-to-talk works with every game/application. 
The Vive controller buttons and the touchpad (which is separated into four regions: left, top, right, botton) can be configured for push-to-talk.
When one of the configured buttons/touchpad regions is pressed then the microphone gets unmuted as long as the button is pressed.

# Notes:

- Autostart settings can be modified in the SteamVR settings (SteamVR->Settings->Applications).

- When the dashboard is active, push-to-talk does not work.

- When the default recording device has been changed this application needs to be restarted to pick up the changes.

# Usage

Just start the executable once. It will register with OpenVR and automatically start whenever OpenVR starts (Can be disabled in the SteamVR settings).

# License

This software is released under GPL 3.0.
