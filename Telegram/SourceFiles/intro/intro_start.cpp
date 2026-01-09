/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_start.h"

#include "lang/lang_keys.h"
#include "intro/intro_qr.h"
#include "intro/intro_phone.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/ui_utility.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "styles/style_boxes.h"
#include "styles/style_widgets.h"
#include "styles/style_intro.h"
#include "styles/style_settings.h"
#include "intro/intro_password_input.h"
#include "intro/intro_account_input.h"
#include "styles/style_media_player.h"
#include "ui/widgets/checkbox.h"

namespace Intro {
namespace details {

StartWidget::StartWidget(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: Step(parent, account, data)
, _inputFieldWrap(this, object_ptr<Ui::RpWidget>(this)) 
, _accountInput(static_cast<QWidget*>(inputControls().get()), st::introAccountInputField, tr::lng_chat_intro_input_account())
, _passwordInput(static_cast<QWidget*>(inputControls().get()), st::introPasswordInputField, tr::lng_chat_intro_input_password())
, _rememberMeCheckbox(static_cast<QWidget*>(inputControls().get()), tr::lng_login_remember_account(), false, st::introCheckbox)
, _rememberPasswordCheckbox(static_cast<QWidget*>(inputControls().get()), tr::lng_login_remember_password(), false, st::introCheckbox) {
	
	_accountInput->move(st::introLeftSkip, 0);
	_accountInput->show();
	_passwordInput->move(st::introLeftSkip, _accountInput->height() + st::introInputSpace);
	_passwordInput->show();
	auto inputHeight = _accountInput->height() + _passwordInput->height() + st::introInputSpace + st::introCheckBoxHeightSkip;
	_rememberMeCheckbox->move(st::introLeftSkip - st::introCheckboxLeftSkip, inputHeight);
	_rememberMeCheckbox->show();
	_rememberPasswordCheckbox->widthValue() | rpl::on_next([=](int width) {
		_rememberPasswordCheckbox->move(st::introLeftSkip + _accountInput->width() - width, inputHeight);
	}, _rememberPasswordCheckbox->lifetime());
	_rememberPasswordCheckbox->show();
	inputControls()->resize(_accountInput->width() + st::introLeftSkip*2, _accountInput->height() + _passwordInput->height() + st::introInputSpace + st::introCheckBoxHeightSkip + _rememberMeCheckbox->height());
	_inputFieldWrap->show(anim::type::instant);
	setMouseTracking(true);
	show();
}

not_null<Ui::RpWidget*> StartWidget::inputControls(){
	return _inputFieldWrap->entity();
}

void StartWidget::submit() {
	account().destroyStaleAuthorizationKeys();
	goNext<QrWidget>();
}

rpl::producer<QString> StartWidget::nextButtonText() const {
	return tr::lng_start_msgs();
}
rpl::producer<> StartWidget::nextButtonFocusRequests() const {
	return _nextButtonFocusRequests.events();
}

void StartWidget::activate() {
	Step::activate();
	showChildren();
	_inputFieldWrap->show(anim::type::normal);
	setInnerFocus();
}

void StartWidget::setInnerFocus() {
	_nextButtonFocusRequests.fire({});
}

void StartWidget::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	const auto fieldWidth = _accountInput->width();
	const auto left = (width() - fieldWidth - st::introLeftSkip*2) / 2;
	const auto top = st::introAccountInputSkipTop;
	_inputFieldWrap->move(left, top);
}

int StartWidget::contentBottomOffset() const {
	return st::introLoginBottomOffset;
}

} // namespace details
} // namespace Intro
