/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "mtproto/sender.h"
#include "ui/text/text_variant.h"
#include "ui/rp_widget.h"
#include "ui/effects/animations.h"

namespace style {
struct RoundButton;
} // namespace style;

namespace Main {
class Account;
} // namespace Main;

namespace Ui {
class SlideAnimation;
class CrossFadeAnimation;
class FlatLabel;
template <typename Widget>
class FadeWrap;
} // namespace Ui

namespace Intro {
namespace details {

struct Data;
enum class StackAction;
enum class Animate;

class Step : public Ui::RpWidget {
public:
	Step(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data);
	~Step();

	QAccessible::Role accessibilityRole() override {
		return QAccessible::Role::Dialog;
	}
	QString accessibilityName() override {
		return QString();
	}
	QString accessibilityDescription() override {
		return QString();
	}

	[[nodiscard]] Main::Account &account() const {
		return *_account;
	}

	// It should not be called in StartWidget, in other steps it should be
	// present and not changing.
	[[nodiscard]] MTP::Sender &api() const;
	void apiClear();

	virtual void finishInit() {
	}
	virtual void setInnerFocus() {
		setFocus();
	}

	void setGoCallback(
		Fn<void(Step *step, StackAction action, Animate animate)> callback);
	void setShowResetCallback(Fn<void()> callback);
	void setShowTermsCallback(Fn<void()> callback);
	void setCancelNearestDcCallback(Fn<void()> callback);
	void setAcceptTermsCallback(
		Fn<void(Fn<void()> callback)> callback);

	void prepareShowAnimated(Step *after);
	void showAnimated(Animate animate);
	void showFast();
	[[nodiscard]] bool animating() const;

	[[nodiscard]] virtual bool hasBack() const;
	virtual void activate();
	virtual void cancelled();
	virtual void finished();

	virtual void submit() = 0;
	[[nodiscard]] virtual rpl::producer<QString> nextButtonText() const;
	[[nodiscard]] virtual auto nextButtonStyle() const
		-> rpl::producer<const style::RoundButton*>;
	[[nodiscard]] virtual rpl::producer<> nextButtonFocusRequests() const;

	[[nodiscard]] int contentLeft() const;
	[[nodiscard]] int contentTop() const;

	[[nodiscard]] virtual int contentBottomOffset() const;

	void setErrorCentered(bool centered);
	void showError(rpl::producer<QString> text);
	void hideError() {
		showError(rpl::single(QString()));
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	bool paintAnimated(QPainter &p, QRect clip);

	void fillSentCodeData(const MTPDauth_sentCode &type);


	[[nodiscard]] not_null<Data*> getData() const {
		return _data;
	}
	void finish(const MTPauth_Authorization &auth, QImage &&photo = {});
	void finish(const MTPUser &user, QImage &&photo = {});
	void createSession(
		const MTPUser &user,
		QImage photo,
		const QVector<MTPDialogFilter> &filters,
		bool tagsEnabled);

	void goBack();

	template <typename StepType>
	void goNext() {
		goNext(new StepType(parentWidget(), _account, _data));
	}

	template <typename StepType>
	void goReplace(Animate animate) {
		goReplace(new StepType(parentWidget(), _account, _data), animate);
	}

	void showResetButton() {
		if (_showResetCallback) _showResetCallback();
	}
	void showTerms() {
		if (_showTermsCallback) _showTermsCallback();
	}
	void acceptTerms(Fn<void()> callback) {
		if (_acceptTermsCallback) {
			_acceptTermsCallback(callback);
		}
	}
	void cancelNearestDcRequest() {
		if (_cancelNearestDcCallback) _cancelNearestDcCallback();
	}

	virtual int errorTop() const;

private:
	void updateLabelsPosition();
	void refreshError(const QString &text);

	void goNext(Step *step);
	void goReplace(Step *step, Animate animate);

	[[nodiscard]] QPixmap prepareSlideAnimation();
	void showFinished();

	const not_null<Main::Account*> _account;
	const not_null<Data*> _data;
	mutable std::optional<MTP::Sender> _api;

	Fn<void(Step *step, StackAction action, Animate animate)> _goCallback;
	Fn<void()> _showResetCallback;
	Fn<void()> _showTermsCallback;
	Fn<void()> _cancelNearestDcCallback;
	Fn<void(Fn<void()> callback)> _acceptTermsCallback;


	bool _errorCentered = false;
	rpl::variable<QString> _errorText;
	object_ptr<Ui::FadeWrap<Ui::FlatLabel>> _error = { nullptr };

	Ui::Animations::Simple _a_show;
	std::unique_ptr<Ui::SlideAnimation> _slideAnimation;
	

};


} // namespace details
} // namespace Intro
