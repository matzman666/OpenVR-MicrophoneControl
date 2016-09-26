
#pragma once

#include <openvr.h>
#include <QtCore/QtCore>
// because of incompatibilities with QtOpenGL and GLEW we need to cherry pick includes
#include <QVector2D>
#include <QMatrix4x4>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QSettings>
#include <QtGui/QOpenGLContext>
#include <QtWidgets/QGraphicsScene>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <memory>
#include "audiomanager.h"
#include "logging.h"


// application namespace
namespace miccontrol {

// forward declaration
class OverlayWidget;

class OverlayController : public QObject {
	Q_OBJECT

public:
	static constexpr const char* applicationKey = "matzman666.MicrophoneControl";
	static constexpr const char* applicationName = "Mic Control";
	static constexpr const char* applicationVersionString = "v1.0";

private:
	OverlayWidget *m_pWidget;

	vr::VROverlayHandle_t m_ulOverlayHandle = vr::k_ulOverlayHandleInvalid;
	vr::VROverlayHandle_t m_ulOverlayThumbnailHandle = vr::k_ulOverlayHandleInvalid;
	vr::VROverlayHandle_t m_ulNotificationOverlayHandle = vr::k_ulOverlayHandleInvalid;

	std::unique_ptr<QOpenGLContext> m_pOpenGLContext;
	std::unique_ptr<QGraphicsScene> m_pScene;
	std::unique_ptr<QOpenGLFramebufferObject> m_pFbo;
	std::unique_ptr<QOffscreenSurface> m_pOffscreenSurface;

	std::unique_ptr<QTimer> m_pPumpEventsTimer;
	bool dashboardVisible = false;

	QPointF m_ptLastMouse;
	Qt::MouseButtons m_lastMouseButtons = 0;

	bool micUserMute = false;
	unsigned micVolume = 100;

	bool pttEnabled = false;
	bool pttActive = false;
	bool pttNotifyEnabled = true;
	bool pttLeftControllerEnabled = false;
	bool pttRightControllerEnabled = false;
	uint64_t pttDigitalButtonMask = 0;
	int pttTriggerModus = 0; // 0 .. disabled, 1 .. enabled
	int pttPadModus = 0; // disabled, 1 .. only touch, 2 .. only press, 3 .. both
	enum PadArea {
		PAD_AREA_LEFT = (1 << 0),
		PAD_AREA_TOP = (1 << 1),
		PAD_AREA_RIGHT = (1 << 2),
		PAD_AREA_BOTTOM = (1 << 3),
	};
	int pttPadArea = 0;
	std::shared_ptr<AudioManager> audioManager;

	QSettings appSettings;

public:
    OverlayController() : QObject(), appSettings("matzman666", "microphonecontrol") {}
	virtual ~OverlayController();

	void Init(std::shared_ptr<AudioManager> audioManager);

	bool isDashboardVisible() {
		return dashboardVisible;
	}

	void SetWidget(OverlayWidget *pWidget, const std::string& name, const std::string& key = "");

public slots:
	void OnSceneChanged( const QList<QRectF>& );
	void OnTimeoutPumpEvents();

	void UpdateWidget();

	void MicMuteToggled(bool value);
	void MicVolumeChanged(int value);

	void pttEnableToggled(bool value);
	void pttNotifyToggled(bool value);
	void pttLeftControllerToggled(bool value);
	void pttRightControllerToggled(bool value);
	void pttGripButtonToggled(bool value);
	void pttMenuButtonToggled(bool value);
	void pttTriggerButtonToggled(bool value);
	void pttPadTouchedToggled(bool value);
	void pttPadPressedToggled(bool value);
	void pttPadAreaLeftToggled(bool value);
	void pttPadAreaTopToggled(bool value);
	void pttPadAreaRightToggled(bool value);
	void pttPadAreaBottomToggled(bool value);
};

} // namespace miccontrol
