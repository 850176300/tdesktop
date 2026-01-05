// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "intro/intro_password_input.h"
#include "styles/style_intro.h"

namespace Ui {

IntroPasswordInput::IntroPasswordInput(
	QWidget *parent,
	const style::IntroInputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: IntroInputField(parent, st, std::move(placeholder), val) {
	QLineEdit::setEchoMode(QLineEdit::Password);
}

} // namespace Ui
