// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "intro/intro_input_field.h"

namespace style {
struct IntroInputField;
} // namespace style

namespace Ui {

class IntroAccountInput final : public IntroInputField {
public:
	IntroAccountInput(
		QWidget *parent,
		const style::IntroInputField &st,
		rpl::producer<QString> placeholder = nullptr,
		const QString &val = QString());

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

};

} // namespace Ui
