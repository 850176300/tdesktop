/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "intro/intro_step.h"
#include "ui/wrap/fade_wrap.h"

namespace Ui {
class FlatLabel;
class LinkButton;
class RoundButton;
class MultiSelect;
class IntroAccountInput;
class IntroPasswordInput;
class RpWidget;
class Checkbox;

} // namespace Ui

namespace Intro {
namespace details {

class StartWidget : public Step {
public:
	StartWidget(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Data*> data);

	void submit() override;
	rpl::producer<QString> nextButtonText() const override;
	rpl::producer<> nextButtonFocusRequests() const override;
	void activate() override;
	void setInnerFocus() override;
	[[nodiscard]] int contentBottomOffset() const override;

private:
	void resizeEvent(QResizeEvent *e) override;
	not_null<Ui::RpWidget*> inputControls();

	rpl::event_stream<> _nextButtonFocusRequests;
	object_ptr<Ui::FadeWrap<Ui::RpWidget>> _inputFieldWrap;
	object_ptr<Ui::IntroAccountInput> _accountInput = nullptr;
	object_ptr<Ui::IntroPasswordInput> _passwordInput = nullptr;
	object_ptr<Ui::Checkbox> _rememberMeCheckbox = nullptr;
	object_ptr<Ui::Checkbox> _rememberPasswordCheckbox = nullptr;
	object_ptr<Ui::FadeWrap<Ui::LinkButton>> _registerNowButton = nullptr;
};

} // namespace details
} // namespace Intro
