/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/widgets/rp_window.h"
#include "base/timer.h"
#include "base/object_ptr.h"
#include "core/core_settings.h"


namespace Ui {
class BoxContent;
class PlainShadow;
} // namespace Ui

namespace Core {
struct WindowPosition;
enum class QuitReason;
} // namespace Core

namespace Window {

class SimpleWindow : public Ui::RpWindow {
public:
	explicit SimpleWindow();
	virtual ~SimpleWindow();

	void activate();

	[[nodiscard]] QRect desktopRect() const;
	[[nodiscard]] Core::WindowPosition withScreenInPosition(
		Core::WindowPosition position) const;

	void init();

	void updateIsActive();
	void showFromTray();
	void quitFromTray();
	[[nodiscard]] bool isActive() const {
		return !isHidden() && _isActive;
	}

	virtual void fixOrder() {
	}
	virtual void setInnerFocus() {
		setFocus();
	}

	Ui::RpWidget *bodyWidget() {
		return _body.data();
	}

	[[nodiscard]] rpl::producer<> leaveEvents() const;
	[[nodiscard]] rpl::producer<> imeCompositionStarts() const;

	virtual void updateWindowIcon() = 0;

	virtual int computeMinWidth() const;
	virtual int computeMinHeight() const;

	void recountGeometryConstraints();
	virtual void updateControlsGeometry();

	void firstShow();
	virtual bool minimizeToTray();
	void updateGlobalMenu() {
		updateGlobalMenuHook();
	}

	[[nodiscard]] QRect countInitialGeometry(
		Core::WindowPosition position,
		Core::WindowPosition initial,
		QSize minSize) const;

	[[nodiscard]] virtual rpl::producer<QPoint> globalForceClicks() {
		return rpl::never<QPoint>();
	}

	void positionUpdated()
	{
		
	}
protected:
	void leaveEventHook(QEvent *e) override;

	void handleStateChanged(Qt::WindowState state);
	void handleActiveChanged(bool active);
	void handleVisibleChanged(bool visible);

	virtual void checkActivation() {
	}
	virtual void initHook() {
	}

	virtual void handleVisibleChangedHook(bool visible) {
	}

	virtual void clearWidgetsHook() {
	}

	virtual void stateChangedHook(Qt::WindowState state) {
	}

	virtual void unreadCounterChangedHook() {
	}

	virtual void closeWithoutDestroy() {
		hide();
	}

	virtual void updateGlobalMenuHook() {
	}

	virtual void workmodeUpdated(Core::Settings::WorkMode mode) {
	}

	virtual void createGlobalMenu() {
	}

	virtual bool initGeometryFromSystem() {
		return false;
	}

	void imeCompositionStartReceived();
	void setPositionInited();

	virtual QRect computeDesktopRect() const;

private:
	void updateMinimumSize();
	void updatePalette();

	[[nodiscard]] Core::WindowPosition initialPosition() const;
	[[nodiscard]] QRect countInitialGeometry(Core::WindowPosition position);

	bool computeIsActive() const;

	bool _positionInited = false;

	object_ptr<Ui::PlainShadow> _titleShadow = { nullptr };
	object_ptr<Ui::RpWidget> _outdated;
	object_ptr<Ui::RpWidget> _body;

	bool _isActive = false;

	rpl::event_stream<> _leaveEvents;
	rpl::event_stream<> _imeCompositionStartReceived;

	bool _maximizedBeforeHide = false;

	QPoint _lastMyChildCreatePosition;
	int _lastChildIndex = 0;

	mutable QRect _monitorRect;
	mutable crl::time _monitorLastGot = 0;

};

[[nodiscard]] int32 DefaultScreenNameChecksum(const QString &name);

[[nodiscard]] Core::WindowPosition PositionWithScreen(
	Core::WindowPosition position,
	const QScreen *chosen,
	QSize minimal);
[[nodiscard]] Core::WindowPosition PositionWithScreen(
	Core::WindowPosition position,
	not_null<const QWidget*> widget,
	QSize minimal);

} // namespace Window
