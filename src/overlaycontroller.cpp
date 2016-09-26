#include "overlaycontroller.h"
#include "overlaywidget.h"
#include "ui_overlaywidget.h"
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <QApplication>
#include <QtWidgets/QWidget>
#include <QMouseEvent>
#include <QtWidgets/QGraphicsSceneMouseEvent>
#include <QtWidgets/QApplication>
#include <QOpenGLExtraFunctions>
#include <QCursor>
#include <QProcess>
#include <QMessageBox>
#include <exception>
#include <iostream>
#include <array>
#include <cmath>
#include <openvr.h>
#include "logging.h"



// application namespace
namespace miccontrol {

OverlayController::~OverlayController() {
	appSettings.sync();
	m_pPumpEventsTimer.reset();
	vr::VR_Shutdown();
	m_pScene.reset();
	m_pFbo.reset();
	m_pOffscreenSurface.reset();
	m_pOpenGLContext.reset();
}

void OverlayController::Init(std::shared_ptr<AudioManager> audioManager) {
	// Loading the OpenVR Runtime
	auto initError = vr::VRInitError_None;
	vr::VR_Init(&initError, vr::VRApplication_Overlay);
	if (initError != vr::VRInitError_None) {
		throw std::runtime_error(std::string("Failed to initialize OpenVR: " + std::string(vr::VR_GetVRInitErrorAsEnglishDescription(initError))));
	}

	QSurfaceFormat format;
	// Qt's QOpenGLPaintDevice is not compatible with OpenGL versions >= 3.0
	// NVIDIA does not care, but unfortunately AMD does
	// Are subtle changes to the semantics of OpenGL functions actually covered by the compatibility profile,
	// and this is an AMD bug?
	format.setVersion(2, 1);
	//format.setProfile( QSurfaceFormat::CompatibilityProfile );
	format.setDepthBufferSize(16);
	format.setStencilBufferSize(8);
	format.setSamples(16);

	m_pOpenGLContext.reset(new QOpenGLContext());
	m_pOpenGLContext->setFormat( format );
	if (!m_pOpenGLContext->create()) {
		throw std::runtime_error("Could not create OpenGL context");
	}

	// create an offscreen surface to attach the context and FBO to
	m_pOffscreenSurface.reset(new QOffscreenSurface());
	m_pOffscreenSurface->setFormat(m_pOpenGLContext->format());
	m_pOffscreenSurface->create();
	m_pOpenGLContext->makeCurrent( m_pOffscreenSurface.get() );

	m_pScene.reset(new QGraphicsScene());
	connect( m_pScene.get(), SIGNAL(changed(const QList<QRectF>&)), this, SLOT( OnSceneChanged(const QList<QRectF>&)) );

	this->audioManager = audioManager;
	this->audioManager->init(this);

	pttEnabled = appSettings.value("pttEnabled", false).toBool();
	pttNotifyEnabled = appSettings.value("pttNotifyEnabled", true).toBool();
	pttLeftControllerEnabled = appSettings.value("pttLeftControllerEnabled", false).toBool();
	pttRightControllerEnabled = appSettings.value("pttRightControllerEnabled", false).toBool();
	pttDigitalButtonMask = appSettings.value("pttDigitalButtonMask", 0).toULongLong();
	pttTriggerModus = appSettings.value("pttTriggerModus", 0).toInt();
	pttPadModus = appSettings.value("pttPadModus", 0).toInt();
	pttPadArea = appSettings.value("pttPadArea", 0).toInt();
}


void OverlayController::SetWidget(OverlayWidget *pWidget, const std::string& name, const std::string& key) {
	// all of the mouse handling stuff requires that the widget be at 0,0
	pWidget->move(0, 0);
	m_pScene->addWidget(pWidget);
	m_pWidget = pWidget;
	//pWidget->ui->VersionLabel->setText(OverlayController::applicationVersionString);

	if (!vr::VROverlay()) {
		QMessageBox::critical(nullptr, "Microphone Control Overlay", "Is OpenVR running?");
		throw std::runtime_error(std::string("No Overlay interface"));
	}
	vr::VROverlayError overlayError = vr::VROverlay()->CreateDashboardOverlay(key.c_str(), name.c_str(), &m_ulOverlayHandle, &m_ulOverlayThumbnailHandle);
	if (overlayError != vr::VROverlayError_None) {
		if (overlayError == vr::VROverlayError_KeyInUse) {
			QMessageBox::critical(nullptr, "Microphone Control Overlay", "Another instance is already running.");
		}
		throw std::runtime_error(std::string("Failed to create Overlay: " + std::string(vr::VROverlay()->GetOverlayErrorNameFromEnum(overlayError))));
	}
	vr::VROverlay()->SetOverlayWidthInMeters(m_ulOverlayHandle, 2.5f);
	vr::VROverlay()->SetOverlayInputMethod(m_ulOverlayHandle, vr::VROverlayInputMethod_Mouse);
	std::string thumbIconPath = QApplication::applicationDirPath().toStdString() + "/res/thumbicon.png";
	if (QFile::exists(QString::fromStdString(thumbIconPath))) {
		vr::VROverlay()->SetOverlayFromFile(m_ulOverlayThumbnailHandle, thumbIconPath.c_str());
	} else {
		LOG(ERROR) << "Could not find thumbnail icon \"" << thumbIconPath << "\"";
	}

	std::string notifKey = key + ".pptnotification";
	overlayError = vr::VROverlay()->CreateOverlay(notifKey.c_str(), notifKey.c_str(), &m_ulNotificationOverlayHandle);
	std::string notifIconPath = QApplication::applicationDirPath().toStdString() + "/res/notificationicon.png";
	if (QFile::exists(QString::fromStdString(notifIconPath))) {
		vr::VROverlay()->SetOverlayFromFile(m_ulNotificationOverlayHandle, notifIconPath.c_str());
		vr::VROverlay()->SetOverlayWidthInMeters(m_ulNotificationOverlayHandle, 0.02f);
		vr::HmdMatrix34_t notificationTransform = {
			1.0f, 0.0f, 0.0f, 0.12f,
			0.0f, 1.0f, 0.0f, 0.08f,
			0.0f, 0.0f, 1.0f, -0.3f
		};
		vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(m_ulNotificationOverlayHandle, vr::k_unTrackedDeviceIndex_Hmd, &notificationTransform);
	} else {
		LOG(ERROR) << "Could not find notification icon \"" << notifIconPath << "\"";
	}

	m_pPumpEventsTimer.reset(new QTimer(this));
	connect(m_pPumpEventsTimer.get(), SIGNAL(timeout()), this, SLOT(OnTimeoutPumpEvents()));
	m_pPumpEventsTimer->setInterval(20);
	m_pPumpEventsTimer->start();

	QOpenGLFramebufferObjectFormat fboFormat;
	fboFormat.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
	fboFormat.setTextureTarget(GL_TEXTURE_2D);

	m_pFbo.reset(new QOpenGLFramebufferObject(pWidget->width(), pWidget->height(), fboFormat));

	vr::HmdVector2_t vecWindowSize = {
		(float)pWidget->width(),
		(float)pWidget->height()
	};
	vr::VROverlay()->SetOverlayMouseScale(m_ulOverlayHandle, &vecWindowSize);

	connect(m_pWidget->ui->pttLeftControllerToggle, SIGNAL(toggled(bool)), this, SLOT(pttLeftControllerToggled(bool)));
	connect(m_pWidget->ui->pttRightControllerToggle, SIGNAL(toggled(bool)), this, SLOT(pttRightControllerToggled(bool)));
	connect(m_pWidget->ui->pttGripButtonToggle, SIGNAL(toggled(bool)), this, SLOT(pttGripButtonToggled(bool)));
	connect(m_pWidget->ui->pttMenuButtonToggle, SIGNAL(toggled(bool)), this, SLOT(pttMenuButtonToggled(bool)));
	connect(m_pWidget->ui->pttTriggerButtonToggle, SIGNAL(toggled(bool)), this, SLOT(pttTriggerButtonToggled(bool)));
	connect(m_pWidget->ui->pttPadTouchedToggle, SIGNAL(toggled(bool)), this, SLOT(pttPadTouchedToggled(bool)));
	connect(m_pWidget->ui->pttPadPressedToggle, SIGNAL(toggled(bool)), this, SLOT(pttPadPressedToggled(bool)));
	connect(m_pWidget->ui->pttPadAreaLeftToggle, SIGNAL(toggled(bool)), this, SLOT(pttPadAreaLeftToggled(bool)));
	connect(m_pWidget->ui->pttPadAreaRightToggle, SIGNAL(toggled(bool)), this, SLOT(pttPadAreaRightToggled(bool)));
	connect(m_pWidget->ui->pttPadAreaTopToggle, SIGNAL(toggled(bool)), this, SLOT(pttPadAreaTopToggled(bool)));
	connect(m_pWidget->ui->pttPadAreaBottomToggle, SIGNAL(toggled(bool)), this, SLOT(pttPadAreaBottomToggled(bool)));
	connect(m_pWidget->ui->pttNotifyToggle, SIGNAL(toggled(bool)), this, SLOT(pttNotifyToggled(bool)));
	connect(m_pWidget->ui->pttToggleButton, SIGNAL(toggled(bool)), this, SLOT(pttEnableToggled(bool)));
	connect(m_pWidget->ui->micMuteToggle, SIGNAL(toggled(bool)), this, SLOT(MicMuteToggled(bool)));
	connect(m_pWidget->ui->micVolumeSlider, SIGNAL(valueChanged(int)), this, SLOT(MicVolumeChanged(int)));
}


void OverlayController::OnSceneChanged( const QList<QRectF>& ) {
	// skip rendering if the overlay isn't visible
	if (!vr::VROverlay() || !vr::VROverlay()->IsOverlayVisible(m_ulOverlayHandle) && !vr::VROverlay()->IsOverlayVisible(m_ulOverlayThumbnailHandle))
		return;

	m_pOpenGLContext->makeCurrent(m_pOffscreenSurface.get());
	m_pFbo->bind();

	QOpenGLPaintDevice device(m_pFbo->size());
	QPainter painter(&device);
	painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

	m_pScene->render(&painter);

	m_pFbo->release();

	GLuint unTexture = m_pFbo->texture();
	if (unTexture != 0) {
#if defined _WIN64 || defined _LP64
		// To avoid any compiler warning because of cast to a larger pointer type (warning C4312 on VC)
		vr::Texture_t texture = { (void*)((uint64_t)unTexture), vr::API_OpenGL, vr::ColorSpace_Auto };
#else
		vr::Texture_t texture = { (void*)unTexture, vr::API_OpenGL, vr::ColorSpace_Auto };
#endif
		vr::VROverlay()->SetOverlayTexture(m_ulOverlayHandle, &texture);
	}
	m_pOpenGLContext->functions()->glFlush(); // We need to flush otherwise the texture may be empty.
}



void logControllerState(const vr::VRControllerState_t& state, const std::string& prefix) {
	if (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_ApplicationMenu) << " pressed";
	} else if (state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_ApplicationMenu) << " touched";
	}
	if (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_Grip)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Grip) << " pressed";
	} else if (state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_Grip)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Grip) << " touched";
	}
	if (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_DPad_Left)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_DPad_Left) << " pressed";
	} else if (state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_DPad_Left)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_DPad_Left) << " touched";
	}
	if (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_DPad_Up)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_DPad_Up) << " pressed";
	} else if (state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_DPad_Up)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_DPad_Up) << " touched";
	}
	if (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_DPad_Right)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_DPad_Right) << " pressed";
	} else if (state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_DPad_Right)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_DPad_Right) << " touched";
	}
	if (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_DPad_Down)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_DPad_Down) << " pressed";
	} else if (state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_DPad_Down)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_DPad_Down) << " touched";
	}
	if (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_A)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_A) << " pressed";
	} else if (state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_A)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_A) << " touched";
	}
	if (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_Axis0)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis0) << " pressed";
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis0) << " x: " << state.rAxis[0].x << "  y: " << state.rAxis[0].y;
	} else if (state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_Axis0)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis0) << " touched";
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis0) << " x: " << state.rAxis[0].x << "  y: " << state.rAxis[0].y;
	}
	if (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_Axis1)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis1) << " pressed";
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis1) << " x: " << state.rAxis[1].x << "  y: " << state.rAxis[0].y;
	} else if (state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_Axis1)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis1) << " touched";
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis1) << " x: " << state.rAxis[1].x << "  y: " << state.rAxis[0].y;
	}
	if (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_Axis2)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis2) << " pressed";
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis2) << " x: " << state.rAxis[2].x << "  y: " << state.rAxis[0].y;
	} else if (state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_Axis2)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis2) << " touched";
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis2) << " x: " << state.rAxis[2].x << "  y: " << state.rAxis[0].y;
	}
	if (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_Axis3)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis3) << " pressed";
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis3) << " x: " << state.rAxis[3].x << "  y: " << state.rAxis[0].y;
	} else if (state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_Axis3)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis3) << " touched";
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis3) << " x: " << state.rAxis[3].x << "  y: " << state.rAxis[0].y;
	}
	if (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_Axis4)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis4) << " pressed";
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis4) << " x: " << state.rAxis[4].x << "  y: " << state.rAxis[0].y;
	} else if (state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_Axis4)) {
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis4) << " touched";
		LOG(INFO) << prefix << vr::VRSystem()->GetButtonIdNameFromEnum(vr::k_EButton_Axis4) << " x: " << state.rAxis[4].x << "  y: " << state.rAxis[0].y;
	}
}



void OverlayController::OnTimeoutPumpEvents() {
    if( !vr::VRSystem() )
		return;

	/*
	// tell OpenVR to make some events for us
	for( vr::TrackedDeviceIndex_t unDeviceId = 1; unDeviceId < vr::k_unControllerStateAxisCount; unDeviceId++ ) {
        if( vr::VROverlay()->HandleControllerOverlayInteractionAsMouse( m_ulOverlayHandle, unDeviceId ) ) {
			break;
		}
	}
	*/
	
	static auto handleControllerState = [](const vr::VRControllerState_t& state, OverlayController* controller) -> bool {
		if (state.ulButtonPressed & controller->pttDigitalButtonMask) {
			return true;
		}
		if (controller->pttTriggerModus) {
			if ( state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger) 
				|| state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger) ) {
				return true;
			}
		}
		if (controller->pttPadModus) {
			if ( ( (controller->pttPadModus & 1) && (state.ulButtonTouched & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad)) )
					|| ( (controller->pttPadModus & 2) && (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Touchpad)) ) ) {
				if (controller->pttPadArea == (PAD_AREA_LEFT + PAD_AREA_TOP + PAD_AREA_RIGHT + PAD_AREA_BOTTOM)) {
					return true;
				} else {
					float x = state.rAxis[0].x;
					float y = state.rAxis[0].y;
					if (std::abs(x) >= 0.2 || std::abs(y) >= 0.2) { // deadzone in the middle
						if (x < 0 && std::abs(y) < -x && (controller->pttPadArea & PAD_AREA_LEFT)) {
							return true;
						} else if (y > 0 && std::abs(x) < y && (controller->pttPadArea & PAD_AREA_TOP)) {
							return true;
						} else if (x > 0 && std::abs(y) < x && (controller->pttPadArea & PAD_AREA_RIGHT)) {
							return true;
						} else if (y < 0 && std::abs(x) < -y && (controller->pttPadArea & PAD_AREA_BOTTOM)) {
							return true;
						}
					}
				}
			}
		}
		return false;
	};
	if (pttEnabled) {
		bool newState = false;
		if (pttLeftControllerEnabled) {
			auto leftId = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
			if (leftId != vr::k_unTrackedDeviceIndexInvalid) {
				vr::VRControllerState_t state;
				if (vr::VRSystem()->GetControllerState(leftId, &state)) {
					//logControllerState(state, "Left ");
					newState |= handleControllerState(state, this);
				}
			}
		}

		if (pttRightControllerEnabled) {
			auto rightId = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);
			if (rightId != vr::k_unTrackedDeviceIndexInvalid) {
				vr::VRControllerState_t state;
				if (vr::VRSystem()->GetControllerState(rightId, &state)) {
					//logControllerState(state, "Right ");
					newState |= handleControllerState(state, this);
				}
			}
		}

		if (newState && !pttActive) {
			if (audioManager && audioManager->isValid() && audioManager->setMuted(false)) {
				pttActive = true;
				vr::VROverlay()->ShowOverlay(m_ulNotificationOverlayHandle);
				m_pWidget->ui->micMuteToggle->setChecked((pttEnabled && !pttActive) || micUserMute);
			}
		} else if (!newState && pttActive) {
			if (audioManager && audioManager->isValid() && audioManager->setMuted(true)) {
				pttActive = false;
				vr::VROverlay()->HideOverlay(m_ulNotificationOverlayHandle);
				m_pWidget->ui->micMuteToggle->setChecked((pttEnabled && !pttActive) || micUserMute);
			}
		}
	}

	vr::VREvent_t vrEvent;
    while( vr::VROverlay()->PollNextOverlayEvent( m_ulOverlayHandle, &vrEvent, sizeof( vrEvent )  ) ) {
		switch( vrEvent.eventType ) {
			case vr::VREvent_MouseMove: {
				QPointF ptNewMouse( vrEvent.data.mouse.x, vrEvent.data.mouse.y );
				QPoint ptGlobal = ptNewMouse.toPoint();
				QGraphicsSceneMouseEvent mouseEvent( QEvent::GraphicsSceneMouseMove );
				mouseEvent.setWidget( NULL );
				mouseEvent.setPos( ptNewMouse );
				mouseEvent.setScenePos( ptGlobal );
				mouseEvent.setScreenPos( ptGlobal );
				mouseEvent.setLastPos( m_ptLastMouse );
				mouseEvent.setLastScenePos( m_pWidget->mapToGlobal( m_ptLastMouse.toPoint() ) );
				mouseEvent.setLastScreenPos( m_pWidget->mapToGlobal( m_ptLastMouse.toPoint() ) );
				mouseEvent.setButtons( m_lastMouseButtons );
				mouseEvent.setButton( Qt::NoButton );
				mouseEvent.setModifiers( 0 );
				mouseEvent.setAccepted( false );

				m_ptLastMouse = ptNewMouse;
				QApplication::sendEvent( m_pScene.get(), &mouseEvent );

				OnSceneChanged( QList<QRectF>() );
			}
			break;

			case vr::VREvent_MouseButtonDown: {
				Qt::MouseButton button = vrEvent.data.mouse.button == vr::VRMouseButton_Right ? Qt::RightButton : Qt::LeftButton;

				m_lastMouseButtons |= button;

				QPoint ptGlobal = m_ptLastMouse.toPoint();
				QGraphicsSceneMouseEvent mouseEvent( QEvent::GraphicsSceneMousePress );
				mouseEvent.setWidget( NULL );
				mouseEvent.setPos( m_ptLastMouse );
				mouseEvent.setButtonDownPos( button, m_ptLastMouse );
				mouseEvent.setButtonDownScenePos( button, ptGlobal);
				mouseEvent.setButtonDownScreenPos( button, ptGlobal );
				mouseEvent.setScenePos( ptGlobal );
				mouseEvent.setScreenPos( ptGlobal );
				mouseEvent.setLastPos( m_ptLastMouse );
				mouseEvent.setLastScenePos( ptGlobal );
				mouseEvent.setLastScreenPos( ptGlobal );
				mouseEvent.setButtons( m_lastMouseButtons );
				mouseEvent.setButton( button );
				mouseEvent.setModifiers( 0 );
				mouseEvent.setAccepted( false );

				QApplication::sendEvent( m_pScene.get(), &mouseEvent );
			}
			break;

			case vr::VREvent_MouseButtonUp: {
				Qt::MouseButton button = vrEvent.data.mouse.button == vr::VRMouseButton_Right ? Qt::RightButton : Qt::LeftButton;
				m_lastMouseButtons &= ~button;

				QPoint ptGlobal = m_ptLastMouse.toPoint();
				QGraphicsSceneMouseEvent mouseEvent( QEvent::GraphicsSceneMouseRelease );
				mouseEvent.setWidget( NULL );
				mouseEvent.setPos( m_ptLastMouse );
				mouseEvent.setScenePos( ptGlobal );
				mouseEvent.setScreenPos( ptGlobal );
				mouseEvent.setLastPos( m_ptLastMouse );
				mouseEvent.setLastScenePos( ptGlobal );
				mouseEvent.setLastScreenPos( ptGlobal );
				mouseEvent.setButtons( m_lastMouseButtons );
				mouseEvent.setButton( button );
				mouseEvent.setModifiers( 0 );
				mouseEvent.setAccepted( false );

				QApplication::sendEvent( m_pScene.get(), &mouseEvent );
			}
			break;

			case vr::VREvent_OverlayShown: {
				m_pWidget->repaint();
				UpdateWidget();
			}
			break;

			case vr::VREvent_Quit: {
				LOG(INFO) << "Received quit request.";
				vr::VRSystem()->AcknowledgeQuit_Exiting(); // Let us buy some time just in case
				m_pPumpEventsTimer->stop();
				MicMuteToggled(micUserMute);
				QApplication::exit();
			}
			break;

			case vr::VREvent_DashboardActivated: {
				LOG(INFO) << "Dashboard activated";
				dashboardVisible = true;
			}
			break;

			case vr::VREvent_DashboardDeactivated: {
				LOG(INFO) << "Dashboard deactivated";
				dashboardVisible = false;
			}
			break;
		}
	}

    if( m_ulOverlayThumbnailHandle != vr::k_ulOverlayHandleInvalid ) {
        while( vr::VROverlay()->PollNextOverlayEvent( m_ulOverlayThumbnailHandle, &vrEvent, sizeof( vrEvent)  ) ) {
            switch( vrEvent.eventType ) {
            case vr::VREvent_OverlayShown: {
                    m_pWidget->repaint();
					UpdateWidget();
                }
                break;
            }
        }
    }
}


void OverlayController::UpdateWidget() {
	if (m_pWidget) {
		static QWidget* pptElements[] = {
			m_pWidget->ui->pttLeftControllerToggle,
			m_pWidget->ui->pttRightControllerToggle,
			m_pWidget->ui->pttGripButtonToggle,
			m_pWidget->ui->pttMenuButtonToggle,
			m_pWidget->ui->pttTriggerButtonToggle,
			m_pWidget->ui->pttPadTouchedToggle,
			m_pWidget->ui->pttPadPressedToggle,
			m_pWidget->ui->pttPadAreaLeftToggle,
			m_pWidget->ui->pttPadAreaTopToggle,
			m_pWidget->ui->pttPadAreaRightToggle,
			m_pWidget->ui->pttPadAreaBottomToggle,
			m_pWidget->ui->pttNotifyToggle,
			m_pWidget->ui->pttToggleButton
		};
		static QWidget* micElements[] = {
			m_pWidget->ui->micMuteToggle,
			m_pWidget->ui->micVolumeSlider
		};
		static auto _setEnabled = [](bool enable, QWidget** widget, unsigned size) {
			for (unsigned i = 0; i < size; i++) {
				widget[i]->setEnabled(enable);
			}
		};
		static auto _blockSignals = [](bool block, QWidget** widget, unsigned size) {
			for (unsigned i = 0; i < size; i++) {
				widget[i]->blockSignals(block);
			}
		};
		_blockSignals(true, pptElements, 13);
		_blockSignals(true, micElements, 2);
		m_pWidget->ui->pttLeftControllerToggle->setChecked(pttLeftControllerEnabled);
		m_pWidget->ui->pttRightControllerToggle->setChecked(pttRightControllerEnabled);
		m_pWidget->ui->pttGripButtonToggle->setChecked(pttDigitalButtonMask & vr::ButtonMaskFromId(vr::k_EButton_Grip));
		m_pWidget->ui->pttMenuButtonToggle->setChecked(pttDigitalButtonMask & vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu));
		m_pWidget->ui->pttTriggerButtonToggle->setChecked(pttTriggerModus >= 1);
		m_pWidget->ui->pttPadTouchedToggle->setChecked(pttPadModus & 1);
		m_pWidget->ui->pttPadPressedToggle->setChecked(pttPadModus & 2);
		m_pWidget->ui->pttPadAreaLeftToggle->setChecked(pttPadArea & PAD_AREA_LEFT);
		m_pWidget->ui->pttPadAreaTopToggle->setChecked(pttPadArea & PAD_AREA_TOP);
		m_pWidget->ui->pttPadAreaRightToggle->setChecked(pttPadArea & PAD_AREA_RIGHT);
		m_pWidget->ui->pttPadAreaBottomToggle->setChecked(pttPadArea & PAD_AREA_BOTTOM);
		m_pWidget->ui->pttNotifyToggle->setChecked(pttNotifyEnabled);
		m_pWidget->ui->pttToggleButton->setChecked(pttEnabled);
		if (pttEnabled) {
			_setEnabled(true, pptElements, 12); // don't touch the last element
			m_pWidget->ui->micMuteToggle->setEnabled(false);
			if (audioManager && audioManager->isValid()) {
				pttActive = !audioManager->isMuted();
			} else {
				pttActive = false;
			}
		} else {
			_setEnabled(false, pptElements, 12); // don't touch the last element
			m_pWidget->ui->micMuteToggle->setEnabled(true);
			if (audioManager && audioManager->isValid()) {
				micUserMute = audioManager->isMuted();
			} else {
				micUserMute = false;
			}
		}
		m_pWidget->ui->micMuteToggle->setChecked((pttEnabled && !pttActive) || micUserMute);
		m_pWidget->ui->micVolumeSlider->setValue(micVolume);
		_blockSignals(false, pptElements, 13);
		_blockSignals(false, micElements, 2);
	}
}


void OverlayController::MicMuteToggled(bool value) {
	if (audioManager && audioManager->isValid()) {
		if (audioManager->setMuted(value) && !pttEnabled) {
			micUserMute = value;
		}
	}
}


void OverlayController::MicVolumeChanged(int value) {
	if (audioManager && audioManager->isValid()) {
		float fval = (float)value / 100.0f;
		if (audioManager->setMasterVolume(fval)) {
			micVolume = value;
		}
	}
}


void OverlayController::pttEnableToggled(bool value) {
	pttEnabled = value;
	m_pWidget->ui->micMuteToggle->setChecked((pttEnabled && !pttActive) || micUserMute);
	UpdateWidget();
	appSettings.setValue("pttEnabled", value);
}


void OverlayController::pttNotifyToggled(bool value) {
	pttNotifyEnabled = value;
	appSettings.setValue("pttNotifyEnabled", value);
}


void OverlayController::pttLeftControllerToggled(bool value) {
	pttLeftControllerEnabled = value;
	appSettings.setValue("pttLeftControllerEnabled", value);
}


void OverlayController::pttRightControllerToggled(bool value) {
	pttRightControllerEnabled = value;
	appSettings.setValue("pttRightControllerEnabled", value);
}


void OverlayController::pttGripButtonToggled(bool value) {
	if (value) {
		pttDigitalButtonMask |= vr::ButtonMaskFromId(vr::k_EButton_Grip);
	} else {
		pttDigitalButtonMask &= ~vr::ButtonMaskFromId(vr::k_EButton_Grip);
	}
	appSettings.setValue("pttDigitalButtonMask", pttDigitalButtonMask);
}


void OverlayController::pttMenuButtonToggled(bool value) {
	if (value) {
		pttDigitalButtonMask |= vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu);
	} else {
		pttDigitalButtonMask &= ~vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu);
	}
	appSettings.setValue("pttDigitalButtonMask", pttDigitalButtonMask);
}


void OverlayController::pttTriggerButtonToggled(bool value) {
	pttTriggerModus = value ? 1 : 0;
	appSettings.setValue("pttTriggerModus", pttTriggerModus);
}


void OverlayController::pttPadTouchedToggled(bool value) {
	if (value) {
		pttPadModus |= 1;
	} else {
		pttPadModus &= ~1;
	}
	appSettings.setValue("pttPadModus", pttPadModus);
}


void OverlayController::pttPadPressedToggled(bool value) {
	if (value) {
		pttPadModus |= 2;
	} else {
		pttPadModus &= ~2;
	}
	appSettings.setValue("pttPadModus", pttPadModus);
}


void OverlayController::pttPadAreaLeftToggled(bool value) {
	if (value) {
		pttPadArea |= PAD_AREA_LEFT;
	} else {
		pttPadArea &= ~PAD_AREA_LEFT;
	}
	appSettings.setValue("pttPadArea", pttPadArea);
}


void OverlayController::pttPadAreaTopToggled(bool value) {
	if (value) {
		pttPadArea |= PAD_AREA_TOP;
	} else {
		pttPadArea &= ~PAD_AREA_TOP;
	}
	appSettings.setValue("pttPadArea", pttPadArea);
}


void OverlayController::pttPadAreaRightToggled(bool value) {
	if (value) {
		pttPadArea |= PAD_AREA_RIGHT;
	} else {
		pttPadArea &= ~PAD_AREA_RIGHT;
	}
	appSettings.setValue("pttPadArea", pttPadArea);
}


void OverlayController::pttPadAreaBottomToggled(bool value) {
	if (value) {
		pttPadArea |= PAD_AREA_BOTTOM;
	} else {
		pttPadArea &= ~PAD_AREA_BOTTOM;
	}
	appSettings.setValue("pttPadArea", pttPadArea);
}


} // namespace advconfig
