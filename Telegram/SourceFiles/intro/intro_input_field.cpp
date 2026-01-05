// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "intro/intro_input_field.h"

#include "base/qt/qt_common_adapters.h"
#include "ui/painter.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"
#include "ui/integration.h"
#include "styles/palette.h"
#include "styles/style_widgets.h"
#include "styles/style_intro.h"

#include <QtWidgets/QCommonStyle>
#include <QtWidgets/QApplication>
#include <QtGui/QClipboard>

namespace Ui {
namespace {

class IntroInputStyle final : public QCommonStyle {
public:
	IntroInputStyle() {
		setParent(QCoreApplication::instance());
	}

	void drawPrimitive(
		PrimitiveElement element,
		const QStyleOption *option,
		QPainter *painter,
		const QWidget *widget = nullptr) const override {
	}

	static IntroInputStyle *instance() {
		if (!_instance) {
			if (!QGuiApplication::instance()) {
				return nullptr;
			}
			_instance = new IntroInputStyle();
		}
		return _instance;
	}

	~IntroInputStyle() {
		_instance = nullptr;
	}

private:
	static IntroInputStyle *_instance;

};

IntroInputStyle *IntroInputStyle::_instance = nullptr;

} // namespace

IntroInputField::IntroInputField(
	QWidget *parent,
	const style::IntroInputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: Parent(val, parent)
, _st(st)
, _oldtext(val)
, _placeholderFull(std::move(placeholder))
, _clearButton(this, st::introFieldCancel) {
	resize(_st.width, _st.heightMin);

	setFont(_st.style.font);
	setAlignment(_st.textAlign);
	if (_st.maxLength > 0) {
		setMaxLength(_st.maxLength);
	}

	_placeholderFull.value(
	) | rpl::on_next([=](const QString &text) {
		refreshPlaceholder(text);
		setAccessibleName(text);
	}, lifetime());

	style::PaletteChanged(
	) | rpl::on_next([=] {
		updatePalette();
	}, lifetime());
	updatePalette();

	if (_st.textBg->c.alphaF() >= 1. && !_st.borderRadius) {
		setAttribute(Qt::WA_OpaquePaintEvent);
	}

	connect(this, SIGNAL(textChanged(QString)), this, SLOT(onTextChange(QString)));
	connect(this, SIGNAL(cursorPositionChanged(int,int)), this, SLOT(onCursorPositionChanged(int,int)));

	connect(this, SIGNAL(textEdited(QString)), this, SLOT(onTextEdited()));
	connect(this, &IntroInputField::selectionChanged, [] {
		Integration::Instance().textActionsUpdated();
	});

	setStyle(IntroInputStyle::instance());
	QLineEdit::setTextMargins(0, 0, 0, 0);
	setContentsMargins(_textMargins + QMargins(-2, -1, -2, -1));
	setFrame(false);

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));

	setTextMargins(_st.textMargins);

	_clearButton->hide(anim::type::instant);
	_clearButton->addClickHandler([=] {
		clear();
		setFocus();
	});

	startPlaceholderAnimation();
	startBorderAnimation();
	finishAnimating();

	updateClearButtonVisibility();
}

void IntroInputField::updatePalette() {
	auto p = palette();
	p.setColor(QPalette::Text, _st.textFg->c);
	p.setColor(QPalette::Highlight, st::msgInBgSelected->c);
	p.setColor(QPalette::HighlightedText, st::historyTextInFgSelected->c);
	setPalette(p);
}

void IntroInputField::setCorrectedText(QString &now, int &nowCursor, const QString &newText, int newPos) {
	if (newPos < 0 || newPos > newText.size()) {
		newPos = newText.size();
	}
	auto updateText = (newText != now);
	if (updateText) {
		now = newText;
		setText(now);
		startPlaceholderAnimation();
	}
	auto updateCursorPosition = (newPos != nowCursor) || updateText;
	if (updateCursorPosition) {
		nowCursor = newPos;
		setCursorPosition(nowCursor);
	}
}

void IntroInputField::customUpDown(bool custom) {
	_customUpDown = custom;
}

int IntroInputField::borderAnimationStart() const {
	return _borderAnimationStart;
}

void IntroInputField::setTextMargins(const QMargins &mrg) {
	_textMargins = mrg;
	setContentsMargins(_textMargins + QMargins(-2, -1, -2, -1));
	refreshPlaceholder(_placeholderFull.current());
}

void IntroInputField::onTouchTimer() {
	_touchRightButton = true;
}

bool IntroInputField::eventHook(QEvent *e) {
	auto type = e->type();
	if (type == QEvent::TouchBegin
		|| type == QEvent::TouchUpdate
		|| type == QEvent::TouchEnd
		|| type == QEvent::TouchCancel) {
		auto event = static_cast<QTouchEvent*>(e);
		if (event->device()->type() == base::TouchDevice::TouchScreen) {
			touchEvent(event);
		}
	}
	return Parent::eventHook(e);
}

void IntroInputField::touchEvent(QTouchEvent *e) {
	switch (e->type()) {
	case QEvent::TouchBegin: {
		if (_touchPress || e->touchPoints().isEmpty()) {
			return;
		}
		_touchTimer.start(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = _mousePressedInTouch = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
	} break;

	case QEvent::TouchUpdate: {
		if (!e->touchPoints().isEmpty()) {
			touchUpdate(e->touchPoints().cbegin()->screenPos().toPoint());
		}
	} break;

	case QEvent::TouchEnd: {
		touchFinish();
	} break;

	case QEvent::TouchCancel: {
		_touchPress = false;
		_touchTimer.stop();
	} break;
	}
}

void IntroInputField::touchUpdate(QPoint globalPosition) {
	if (_touchPress
		&& !_touchMove
		&& ((globalPosition - _touchStart).manhattanLength()
			>= QApplication::startDragDistance())) {
		_touchMove = true;
	}
}

void IntroInputField::touchFinish() {
	if (!_touchPress) {
		return;
	}
	const auto weak = base::make_weak(this);
	if (!_touchMove && window()) {
		QPoint mapped(mapFromGlobal(_touchStart));

		if (_touchRightButton) {
			QContextMenuEvent contextEvent(
				QContextMenuEvent::Mouse,
				mapped,
				_touchStart);
			contextMenuEvent(&contextEvent);
		} else {
			QGuiApplication::inputMethod()->show();
		}
	}
	if (weak) {
		_touchTimer.stop();
		_touchPress
			= _touchMove
			= _touchRightButton
			= _mousePressedInTouch = false;
	}
}



void IntroInputField::paintRoundSurrounding(QPainter& p, QRect clip, float64 errorDegree, float64 focusedDegree) {
	const auto divide = _st.borderDenominator ? _st.borderDenominator : 1;
	const auto border = _st.border / float64(divide);
	const auto borderHalf = border / 2.;
	auto pen = anim::pen(_st.borderFg, _st.borderFgActive, focusedDegree);
	pen.setWidthF(border);
	p.setPen(pen);
	p.setBrush(anim::brush(_st.textBg, _st.textBgActive, focusedDegree));

	PainterHighQualityEnabler hq(p);
	const auto radius = _st.borderRadius - borderHalf;
	p.drawRoundedRect( QRectF(0, 0, width(), height()).marginsRemoved(QMarginsF(borderHalf, borderHalf, borderHalf, borderHalf)), radius, radius);
}

void IntroInputField::paintFlatSurrounding(QPainter& p, QRect clip, float64 errorDegree, float64 focusedDegree) {
	if (_st.textBg->c.alphaF() > 0.) {
		p.fillRect(clip, _st.textBg);
	}
	if (_st.border) {
		p.fillRect(0, height() - _st.border, width(), _st.border, _st.borderFg);
	}
	const auto borderShownDegree = _a_borderShown.value(1.);
	const auto borderOpacity = _a_borderOpacity.value(_borderVisible ? 1. : 0.);
	if (_st.borderActive && (borderOpacity > 0.)) {
		auto borderStart = std::clamp(_borderAnimationStart, 0, width());
		auto borderFrom = qRound(borderStart * (1. - borderShownDegree));
		auto borderTo = borderStart + qRound((width() - borderStart) * borderShownDegree);
		if (borderTo > borderFrom) {
			auto borderFg = anim::brush(_st.borderFgActive, _st.borderFgError, errorDegree);
			p.setOpacity(borderOpacity);
			p.fillRect(borderFrom, height() - _st.borderActive, borderTo - borderFrom, _st.borderActive, borderFg);
			p.setOpacity(1);
		}
	}
}

void IntroInputField::paintSurrounding(QPainter& p, QRect clip, float64 errorDegree, float64 focusedDegree) {
	if (_st.borderRadius > 0) {
		paintRoundSurrounding(p, clip, errorDegree, focusedDegree);
	}
	else {
		paintFlatSurrounding(p, clip, errorDegree, focusedDegree);
	}
}



void IntroInputField::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto r = rect().intersected(e->rect());
	const auto errorDegree = _a_error.value(_error ? 1. : 0.);
	const auto focusedDegree = _a_focused.value(_focused ? 1. : 0.);
	paintSurrounding(p, r, errorDegree, focusedDegree);

	// Paint normalIcon on the left side, vertically centered


	if (_st.placeholderScale > 0. && !_placeholderPath.isEmpty()) {
		auto placeholderShiftDegree = _a_placeholderShifted.value(_placeholderShifted ? 1. : 0.);
		p.save();
		p.setClipRect(r);

		auto placeholderTop = anim::interpolate(0, _st.placeholderShift, placeholderShiftDegree);

		QRect r(rect().marginsRemoved(_textMargins + _st.placeholderMargins));
		r.moveTop(r.top() + placeholderTop);
		if (style::RightToLeft()) r.moveLeft(width() - r.left() - r.width());

		auto placeholderScale = 1. - (1. - _st.placeholderScale) * placeholderShiftDegree;
		auto placeholderFg = anim::color(_st.placeholderFg, _st.placeholderFgActive, focusedDegree);
		placeholderFg = anim::color(placeholderFg, _st.placeholderFgError, errorDegree);

		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(placeholderFg);
		p.translate(r.topLeft());
		p.scale(placeholderScale, placeholderScale);
		p.drawPath(_placeholderPath);

		p.restore();
	} else if (!_placeholder.isEmpty()) {
		auto placeholderHiddenDegree = _a_placeholderShifted.value(_placeholderShifted ? 1. : 0.);
		if (placeholderHiddenDegree < 1.) {
			p.setOpacity(1. - placeholderHiddenDegree);
			p.save();
			p.setClipRect(r);

			auto placeholderLeft = anim::interpolate(0, -_st.placeholderShift, placeholderHiddenDegree);

			QRect r(rect().marginsRemoved(_textMargins + _st.placeholderMargins));
			r.moveLeft(r.left() + placeholderLeft);
			if (style::RightToLeft()) r.moveLeft(width() - r.left() - r.width());

			p.setFont(_st.placeholderFont);
			p.setPen(anim::pen(_st.placeholderFg, _st.placeholderFgActive, focusedDegree));
			p.drawText(r, _placeholder, _st.placeholderAlign);

			p.restore();
			p.setOpacity(1.);
		}
	}
    if (!_st.normalIcon.empty()) {
		const auto iconTop = (height() - _st.normalIcon.height()) / 2;
		const auto iconLeft = st::introInputIconSkip;
		if (focusedDegree > 0.5 && !_st.activeIcon.empty()) {
			_st.activeIcon.paint(p, iconLeft, iconTop, width());
		} else {
			if (errorDegree > 0.5 && !_st.errorIcon.empty()) {
				_st.errorIcon.paint(p, iconLeft, iconTop, width());
			} else {
				_st.normalIcon.paint(p, iconLeft, iconTop, width());
			}
		}
	}
	paintAdditionalPlaceholder(p);
	QLineEdit::paintEvent(e);
 
}

void IntroInputField::mousePressEvent(QMouseEvent *e) {
	if (_touchPress && e->button() == Qt::LeftButton) {
		_mousePressedInTouch = true;
		_touchStart = e->globalPos();
	}
	return QLineEdit::mousePressEvent(e);
}

void IntroInputField::mouseReleaseEvent(QMouseEvent *e) {
	if (_mousePressedInTouch) {
		touchFinish();
	}
	return QLineEdit::mouseReleaseEvent(e);
}

void IntroInputField::mouseMoveEvent(QMouseEvent *e) {
	if (_mousePressedInTouch) {
		touchUpdate(e->globalPos());
	}
	return QLineEdit::mouseMoveEvent(e);
}

QString IntroInputField::getDisplayedText() const {
	auto result = getLastText();
	if (!_lastPreEditText.isEmpty()) {
		result = result.mid(0, _oldcursor)
			+ _lastPreEditText
			+ result.mid(_oldcursor);
	}
	return result;
}

void IntroInputField::startBorderAnimation() {
	auto borderVisible = (_error || _focused);
	if (_borderVisible != borderVisible) {
		_borderVisible = borderVisible;
		if (_borderVisible) {
			if (_a_borderOpacity.animating()) {
				_a_borderOpacity.start([this] { update(); }, 0., 1., _st.duration);
			} else {
				_a_borderShown.start([this] { update(); }, 0., 1., _st.duration);
			}
		} else if (qFuzzyCompare(_a_borderShown.value(1.), 0.)) {
			_a_borderShown.stop();
			_a_borderOpacity.stop();
		} else {
			_a_borderOpacity.start([this] { update(); }, 1., 0., _st.duration);
		}
	}
}

void IntroInputField::focusInEvent(QFocusEvent *e) {
	_borderAnimationStart = (e->reason() == Qt::MouseFocusReason) ? mapFromGlobal(QCursor::pos()).x() : (width() / 2);
	setFocused(true);
	QLineEdit::focusInEvent(e);
	focused();
}

void IntroInputField::focusOutEvent(QFocusEvent *e) {
	setFocused(false);
	QLineEdit::focusOutEvent(e);
	blurred();
}

void IntroInputField::setFocused(bool focused) {
	if (_focused != focused) {
		_focused = focused;
		_a_focused.start([this] { update(); }, _focused ? 0. : 1., _focused ? 1. : 0., _st.duration);
		startPlaceholderAnimation();
		startBorderAnimation();
		updateClearButtonVisibility();
	}
}

void IntroInputField::resizeEvent(QResizeEvent *e) {
	refreshPlaceholder(_placeholderFull.current());
	_borderAnimationStart = width() / 2;

	const auto buttonTop = (height() - _clearButton->height()) / 2;
	_clearButton->moveToRight(0, buttonTop);

	QLineEdit::resizeEvent(e);
}

void IntroInputField::refreshPlaceholder(const QString &text) {
	const auto availableWidth = width() - _textMargins.left() - _textMargins.right() - _st.placeholderMargins.left() - _st.placeholderMargins.right() - 1;
	if (_st.placeholderScale > 0.) {
		auto placeholderFont = _st.placeholderFont->f;
		placeholderFont.setStyleStrategy(QFont::PreferMatch);
		const auto metrics = QFontMetrics(placeholderFont);
		_placeholder = metrics.elidedText(text, Qt::ElideRight, availableWidth);
		_placeholderPath = QPainterPath();
		if (!_placeholder.isEmpty()) {
			const auto result = style::FindAdjustResult(placeholderFont);
			const auto ascent = result ? result->iascent : metrics.ascent();
			_placeholderPath.addText(0, ascent, placeholderFont, _placeholder);
		}
	} else {
		_placeholder = _st.placeholderFont->elided(text, availableWidth);
	}
	update();
}

void IntroInputField::setPlaceholder(rpl::producer<QString> placeholder) {
	_placeholderFull = std::move(placeholder);
}

void IntroInputField::contextMenuEvent(QContextMenuEvent *e) {
	if (const auto menu = createStandardContextMenu()) {
		_contextMenu = base::make_unique_q<PopupMenu>(this, menu);
		_contextMenu->popup(e->globalPos());
	}
}

void IntroInputField::inputMethodEvent(QInputMethodEvent *e) {
	QLineEdit::inputMethodEvent(e);
	_lastPreEditText = e->preeditString();
	update();
}

void IntroInputField::showError() {
	showErrorNoFocus();
	if (!hasFocus()) {
		setFocus();
	}
}

void IntroInputField::showErrorNoFocus() {
	setErrorShown(true);
}

void IntroInputField::hideError() {
	setErrorShown(false);
}

void IntroInputField::setErrorShown(bool error) {
	if (_error != error) {
		_error = error;
		_a_error.start([this] { update(); }, _error ? 0. : 1., _error ? 1. : 0., _st.duration);
		startBorderAnimation();
	}
}

QSize IntroInputField::sizeHint() const {
	return geometry().size();
}

QSize IntroInputField::minimumSizeHint() const {
	return geometry().size();
}

void IntroInputField::setDisplayFocused(bool focused) {
	setFocused(focused);
	finishAnimating();
}

void IntroInputField::finishAnimating() {
	_a_focused.stop();
	_a_error.stop();
	_a_placeholderShifted.stop();
	_a_borderShown.stop();
	_a_borderOpacity.stop();
	update();
}

void IntroInputField::setPlaceholderHidden(bool forcePlaceholderHidden) {
	_forcePlaceholderHidden = forcePlaceholderHidden;
	startPlaceholderAnimation();
}

void IntroInputField::startPlaceholderAnimation() {
	auto placeholderShifted = _forcePlaceholderHidden || (_focused && _st.placeholderScale > 0.) || !getLastText().isEmpty();
	if (_placeholderShifted != placeholderShifted) {
		_placeholderShifted = placeholderShifted;
		_a_placeholderShifted.start([this] { update(); }, _placeholderShifted ? 0. : 1., _placeholderShifted ? 1. : 0., _st.duration);
	}
}

QRect IntroInputField::placeholderRect() const {
	return rect().marginsRemoved(_textMargins + _st.placeholderMargins);
}

style::font IntroInputField::phFont() {
	return _st.style.font;
}

void IntroInputField::placeholderAdditionalPrepare(QPainter &p) {
	p.setFont(_st.style.font);
	p.setPen(_st.placeholderFg);
}

void IntroInputField::keyPressEvent(QKeyEvent *e) {
	QString wasText(_oldtext);
	int32 wasCursor(_oldcursor);

	if (_customUpDown && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down || e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown)) {
		e->ignore();
	} else if (e == QKeySequence::DeleteStartOfWord && hasSelectedText()) {
		e->accept();
		backspace();
	} else {
		QLineEdit::keyPressEvent(e);
	}

	auto newText = text();
	auto newCursor = cursorPosition();
	if (wasText == newText && wasCursor == newCursor) { // call correct manually
		correctValue(wasText, wasCursor, newText, newCursor);
		_oldtext = newText;
		_oldcursor = newCursor;
		if (wasText != _oldtext) changed();
		startPlaceholderAnimation();
	}
	if (e->key() == Qt::Key_Escape) {
		e->ignore();
		cancelled();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		submitted(e->modifiers());
#ifdef Q_OS_MAC
	} else if (e->key() == Qt::Key_E && e->modifiers().testFlag(Qt::ControlModifier)) {
		auto selected = selectedText();
		if (!selected.isEmpty() && echoMode() == QLineEdit::Normal) {
			QGuiApplication::clipboard()->setText(selected, QClipboard::FindBuffer);
		}
#endif // Q_OS_MAC
	}
}

void IntroInputField::onTextEdited() {
	QString wasText(_oldtext), newText(text());
	int32 wasCursor(_oldcursor), newCursor(cursorPosition());

	correctValue(wasText, wasCursor, newText, newCursor);
	_oldtext = newText;
	_oldcursor = newCursor;
	if (wasText != _oldtext) changed();
	startPlaceholderAnimation();
	updateClearButtonVisibility();

	Integration::Instance().textActionsUpdated();
}

void IntroInputField::onTextChange(const QString &text) {
	_oldtext = QLineEdit::text();
	setErrorShown(false);
	updateClearButtonVisibility();
	Integration::Instance().textActionsUpdated();
}

void IntroInputField::onCursorPositionChanged(int oldPosition, int position) {
	_oldcursor = position;
}

void IntroInputField::updateClearButtonVisibility() {
	const auto hasText = !getLastText().isEmpty();
	_clearButton->toggle(hasText && _focused, anim::type::normal);
}

} // namespace Ui
