#include "overlaywidget.h"
#include "ui_overlaywidget.h"

// application namespace
namespace miccontrol {

OverlayWidget::OverlayWidget(QWidget *parent) :	
		QWidget(parent), ui(new Ui::OverlayWidget) {
	ui->setupUi(this);
}

} // namespace miccontrol
