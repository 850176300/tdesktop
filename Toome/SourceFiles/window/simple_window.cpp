/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/simple_window.h"

#include "api/api_updates.h"
#include "storage/localstorage.h"
#include "platform/platform_specific.h"
#include "ui/platform/ui_platform_window.h"
#include "platform/platform_window_title.h"
#include "window/window_lock_widgets.h"
#include "window/window_controller.h"
#include "core/application.h"
#include "core/sandbox.h"
#include "core/shortcuts.h"
#include "data/data_session.h"
#include "base/crc32hash.h"
#include "ui/widgets/shadow.h"
#include "ui/controls/window_outdated_bar.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "tray.h"
#include "styles/style_window.h"
#include <QtGui/QWindow>
#include <QtGui/QScreen>
#include <QtGui/QDrag>

namespace Window {
	
using Core::WindowPosition;
SimpleWindow::SimpleWindow()
:_outdated(Ui::CreateOutdatedBar(body(), cWorkingDir()))
, _body(body()) {
	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		updatePalette();
	}, lifetime());
	
	windowActiveValue(
	) | rpl::skip(1) | rpl::start_with_next([=](bool active) {
		InvokeQueued(this, [=] {
			handleActiveChanged(active);
		});
	}, lifetime());

	shownValue(
	) | rpl::skip(1) | rpl::start_with_next([=](bool visible) {
		InvokeQueued(this, [=] {
			handleVisibleChanged(visible);
		});
	}, lifetime());

	body()->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		updateControlsGeometry();
	}, lifetime());

	if (_outdated) {
		_outdated->heightValue(
		) | rpl::start_with_next([=](int height) {
			if (!height) {
				crl::on_main(this, [=] { _outdated.destroy(); });
			}
			updateControlsGeometry();
		}, _outdated->lifetime());
	}
}

void SimpleWindow::quitFromTray() {
	Core::Quit();
}
	
void SimpleWindow::showFromTray() {
	InvokeQueued(this, [=] {
		updateGlobalMenu();
	});
	activate();
	unreadCounterChangedHook();
	Core::App().tray().updateIconCounters();
}
	
void SimpleWindow::updateIsActive() {
	const auto isActive = computeIsActive();
	if (_isActive != isActive) {
		_isActive = isActive;
	}
}

bool SimpleWindow::computeIsActive() const {
	return isActiveWindow() && isVisible() && !(windowState() & Qt::WindowMinimized);
}

QRect SimpleWindow::desktopRect() const {
	const auto now = crl::now();
	if (!_monitorLastGot || now >= _monitorLastGot + crl::time(1000)) {
		_monitorLastGot = now;
		_monitorRect = computeDesktopRect();
	}
	return _monitorRect;
}

void SimpleWindow::init() {
	initHook();
	updatePalette();
	updateWindowIcon();
}

void SimpleWindow::handleStateChanged(Qt::WindowState state) {
	stateChangedHook(state);
	updateControlsGeometry();
}

void SimpleWindow::handleActiveChanged(bool active) {
	checkActivation();
}

void SimpleWindow::handleVisibleChanged(bool visible) {
	if (visible) {
		if (_maximizedBeforeHide) {
			DEBUG_LOG(("Window Pos: Window was maximized before hidding, setting maximized."));
			setWindowState(Qt::WindowMaximized);
		}
	} else {
		_maximizedBeforeHide = Core::App().settings().windowPosition().maximized;
	}

	handleVisibleChangedHook(visible);
}

void SimpleWindow::activate() {
	setWindowState(windowState() & ~Qt::WindowMinimized);
	setVisible(true);
	Platform::ActivateThisProcess();
	raise();
	activateWindow();
}

void SimpleWindow::updatePalette() {
	Ui::ForceFullRepaint(this);
	auto p = palette();
	p.setColor(QPalette::Window, st::windowBg->c);
	setPalette(p);
}

int SimpleWindow::computeMinWidth() const {
	return st::windowMinWidth;
}

int SimpleWindow::computeMinHeight() const {
	const auto outdated = [&] {
		if (!_outdated) {
			return 0;
		}
		_outdated->resizeToWidth(st::windowMinWidth);
		return _outdated->height();
	}();
	return outdated + st::windowMinHeight;
}

void SimpleWindow::updateMinimumSize() {
	setMinimumSize(QSize(computeMinWidth(), computeMinHeight()));
}

void SimpleWindow::recountGeometryConstraints() {
	updateMinimumSize();
	updateControlsGeometry();
	fixOrder();
}

WindowPosition SimpleWindow::initialPosition() const {
	const auto primaryScreen = QGuiApplication::primaryScreen();
	const auto primaryAvailable = primaryScreen
		? primaryScreen->availableGeometry()
		: QRect(0, 0, st::windowDefaultWidth, st::windowDefaultHeight);
	return {
		.scale = cScale(),
		.x = 0,
		.y = 0,
		.w = primaryAvailable.width(),
		.h = primaryAvailable.height(),
	};
}

QRect SimpleWindow::countInitialGeometry(WindowPosition position) {
	const auto primaryScreen = QGuiApplication::primaryScreen();
	const auto primaryAvailable = primaryScreen
		? primaryScreen->availableGeometry()
		: QRect(0, 0, st::windowDefaultWidth, st::windowDefaultHeight);
	const auto initialWidth = Core::Settings::ThirdColumnByDefault()
		? st::windowBigDefaultWidth
		: st::windowDefaultWidth;
	const auto initialHeight = Core::Settings::ThirdColumnByDefault()
		? st::windowBigDefaultHeight
		: st::windowDefaultHeight;
	const auto initial = WindowPosition{
		.x = (primaryAvailable.x()
			+ std::max((primaryAvailable.width() - initialWidth) / 2, 0)),
		.y = (primaryAvailable.y()
			+ std::max((primaryAvailable.height() - initialHeight) / 2, 0)),
		.w = initialWidth,
		.h = initialHeight,
	};
	return countInitialGeometry(
		position,
		initial,
		{ st::windowMinWidth, st::windowMinHeight });
}

QRect SimpleWindow::countInitialGeometry(
		WindowPosition position,
		WindowPosition initial,
		QSize minSize) const {
	if (!position.w || !position.h) {
		return initial.rect();
	}
	const auto screen = [&]() -> QScreen* {
		for (const auto screen : QGuiApplication::screens()) {
			const auto sum = Platform::ScreenNameChecksum(screen->name());
			if (position.moncrc == sum) {
				return screen;
			}
		}
		return nullptr;
	}();
	if (!screen) {
		return initial.rect();
	}
	const auto frame = frameMargins();
	const auto screenGeometry = screen->geometry();
	const auto availableGeometry = screen->availableGeometry();
	const auto spaceForInner = availableGeometry.marginsRemoved(frame);
	DEBUG_LOG(("Window Pos: "
		"Screen found, screen geometry: %1, %2, %3, %4, "
		"available: %5, %6, %7, %8"
		).arg(screenGeometry.x()
		).arg(screenGeometry.y()
		).arg(screenGeometry.width()
		).arg(screenGeometry.height()
		).arg(availableGeometry.x()
		).arg(availableGeometry.y()
		).arg(availableGeometry.width()
		).arg(availableGeometry.height()));
	DEBUG_LOG(("Window Pos: "
		"Window frame margins: %1, %2, %3, %4, "
		"available space for inner geometry: %5, %6, %7, %8"
		).arg(frame.left()
		).arg(frame.top()
		).arg(frame.right()
		).arg(frame.bottom()
		).arg(spaceForInner.x()
		).arg(spaceForInner.y()
		).arg(spaceForInner.width()
		).arg(spaceForInner.height()));

	const auto x = spaceForInner.x() - screenGeometry.x();
	const auto y = spaceForInner.y() - screenGeometry.y();
	const auto w = spaceForInner.width();
	const auto h = spaceForInner.height();
	if (w < st::windowMinWidth || h < st::windowMinHeight) {
		return initial.rect();
	}
	if (position.x < x) position.x = x;
	if (position.y < y) position.y = y;
	if (position.w > w) position.w = w;
	if (position.h > h) position.h = h;
	const auto rightPoint = position.x + position.w;
	const auto screenRightPoint = x + w;
	if (rightPoint > screenRightPoint) {
		const auto distance = rightPoint - screenRightPoint;
		const auto newXPos = position.x - distance;
		if (newXPos >= x) {
			position.x = newXPos;
		} else {
			position.x = x;
			const auto newRightPoint = position.x + position.w;
			const auto newDistance = newRightPoint - screenRightPoint;
			position.w -= newDistance;
		}
	}
	const auto bottomPoint = position.y + position.h;
	const auto screenBottomPoint = y + h;
	if (bottomPoint > screenBottomPoint) {
		const auto distance = bottomPoint - screenBottomPoint;
		const auto newYPos = position.y - distance;
		if (newYPos >= y) {
			position.y = newYPos;
		} else {
			position.y = y;
			const auto newBottomPoint = position.y + position.h;
			const auto newDistance = newBottomPoint - screenBottomPoint;
			position.h -= newDistance;
		}
	}
	position.x += screenGeometry.x();
	position.y += screenGeometry.y();
	if ((position.x + st::windowMinWidth
		> screenGeometry.x() + screenGeometry.width())
		|| (position.y + st::windowMinHeight
			> screenGeometry.y() + screenGeometry.height())) {
		return initial.rect();
	}
	DEBUG_LOG(("Window Pos: Resulting geometry is %1, %2, %3, %4"
		).arg(position.x
		).arg(position.y
		).arg(position.w
		).arg(position.h));
	return position.rect();
}

void SimpleWindow::firstShow() {
	updateMinimumSize();
	if (initGeometryFromSystem()) {
		show();
		return;
	}
	const auto geometry = countInitialGeometry(initialPosition());
	DEBUG_LOG(("Window Pos: Setting first %1, %2, %3, %4"
		).arg(geometry.x()
		).arg(geometry.y()
		).arg(geometry.width()
		).arg(geometry.height()));
	setGeometry(geometry);
	show();
}

void SimpleWindow::setPositionInited() {
	_positionInited = true;
}

void SimpleWindow::imeCompositionStartReceived() {
	_imeCompositionStartReceived.fire({});
}

rpl::producer<> SimpleWindow::leaveEvents() const {
	return _leaveEvents.events();
}

rpl::producer<> SimpleWindow::imeCompositionStarts() const {
	return _imeCompositionStartReceived.events();
}

void SimpleWindow::leaveEventHook(QEvent *e) {
	_leaveEvents.fire({});
}

void SimpleWindow::updateControlsGeometry() {
	const auto inner = body()->rect();
	auto bodyLeft = inner.x();
	auto bodyTop = inner.y();
	auto bodyWidth = inner.width();
	if (_titleShadow) {
		_titleShadow->setGeometry(inner.x(), bodyTop, inner.width(), st::lineWidth);
	}
	if (_outdated) {
		Ui::SendPendingMoveResizeEvents(_outdated.data());
		_outdated->resizeToWidth(inner.width());
		_outdated->moveToLeft(inner.x(), bodyTop);
		bodyTop += _outdated->height();
	}
	_body->setGeometry(bodyLeft, bodyTop, bodyWidth, inner.height() - (bodyTop - inner.y()));
}

QRect SimpleWindow::computeDesktopRect() const {
	return screen()->availableGeometry();
}

WindowPosition SimpleWindow::withScreenInPosition(
		WindowPosition position) const {
	return PositionWithScreen(
		position,
		this,
		{ st::windowMinWidth, st::windowMinHeight });
}

bool SimpleWindow::minimizeToTray() {
	if (Core::Quitting() || !Core::App().tray().has()) {
		return false;
	}

	closeWithoutDestroy();
	updateGlobalMenu();
	return true;
}


SimpleWindow::~SimpleWindow() {
	// Otherwise:
	// ~QWidget
	// QWidgetPrivate::close_helper
	// QWidgetPrivate::setVisible
	// QWidgetPrivate::hide_helper
	// QWidgetPrivate::hide_sys
	// QWindowPrivate::setVisible
	// QMetaObject::activate
	// Window::BaseWindow::handleVisibleChanged on a destroyed MainWindow.
	hide();
}

int32 DefaultScreenNameChecksum(const QString &name) {
	const auto bytes = name.toUtf8();
	return base::crc32(bytes.constData(), bytes.size());
}



} // namespace Window
