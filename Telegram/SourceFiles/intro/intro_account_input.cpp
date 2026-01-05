// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "intro/intro_account_input.h"
#include "styles/style_intro.h"

namespace Ui {

IntroAccountInput::IntroAccountInput(
	QWidget *parent,
	const style::IntroInputField &st,
	rpl::producer<QString> placeholder,
	const QString &value)
: IntroInputField(parent, st, std::move(placeholder), value) {
	
}

void IntroAccountInput::correctValue(
	const QString &was,
	int wasCursor,
	QString &now,
	int &nowCursor) {
	QString newText;
	newText.reserve(now.size());
	auto newPos = nowCursor;
	for (auto i = 0, l = int(now.size()); i < l; ++i) {
		const auto ch = now.at(i);
		if (ch >= 'A' && ch <= 'Z' || ch >= 'a' && ch <= 'z' || ch >= '0' && ch <= '9') {
			newText.append(ch);
		} else if (i < nowCursor) {
			--newPos;
		}
	}
	setCorrectedText(now, nowCursor, newText, newPos);
}

} // namespace Ui