/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/intro_step.h"

#include "intro/intro_widget.h"
#include "intro/intro_signup.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "lang/lang_cloud_manager.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "apiwrap.h"
#include "api/api_peer_photo.h"
#include "mainwindow.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/effects/slide_animation.h"
#include "ui/ui_utility.h"
#include "data/data_user.h"
#include "data/data_auto_download.h"
#include "data/data_session.h"
#include "data/data_chat_filters.h"
#include "window/window_controller.h"
#include "styles/style_intro.h"
#include "styles/style_window.h"

namespace Intro {
namespace details {
namespace {

void PrepareSupportMode(not_null<Main::Session*> session) {
	using ::Data::AutoDownload::Full;

	anim::SetDisabled(true);
	Core::App().settings().setDesktopNotify(false);
	Core::App().settings().setSoundNotify(false);
	Core::App().settings().setFlashBounceNotify(false);
	Core::App().saveSettings();

	session->settings().autoDownload() = Full::FullDisabled();
	session->saveSettings();
}

} // namespace

Step::Step(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Data*> data)
: RpWidget(parent)
, _account(account)
, _data(data) {
	hide();

	_errorText.value(
	) | rpl::on_next([=](const QString &text) {
		refreshError(text);
	}, lifetime());
}

Step::~Step() = default;

MTP::Sender &Step::api() const {
	if (!_api) {
		_api.emplace(&_account->mtp());
	}
	return *_api;
}

void Step::apiClear() {
	_api.reset();
}

rpl::producer<QString> Step::nextButtonText() const {
	return tr::lng_intro_next();
}

rpl::producer<const style::RoundButton*> Step::nextButtonStyle() const {
	return rpl::single((const style::RoundButton*)(nullptr));
}

rpl::producer<> Step::nextButtonFocusRequests() const {
	return rpl::never();
}

void Step::goBack() {
	if (_goCallback) {
		_goCallback(nullptr, StackAction::Back, Animate::Back);
	}
}

void Step::goNext(Step *step) {
	if (_goCallback) {
		_goCallback(step, StackAction::Forward, Animate::Forward);
	}
}

void Step::goReplace(Step *step, Animate animate) {
	if (_goCallback) {
		_goCallback(step, StackAction::Replace, animate);
	}
}

void Step::finish(const MTPauth_Authorization &auth, QImage &&photo) {
	auth.match([&](const MTPDauth_authorization &data) {
		if (data.vuser().type() != mtpc_user
			|| !data.vuser().c_user().is_self()) {
			showError(rpl::single(Lang::Hard::ServerError())); // wtf?
			return;
		}
		finish(data.vuser(), std::move(photo));
	}, [&](const MTPDauth_authorizationSignUpRequired &data) {
		if (const auto terms = data.vterms_of_service()) {
			terms->match([&](const MTPDhelp_termsOfService &data) {
				getData()->termsLock = Window::TermsLock::FromMTP(
					nullptr,
					data);
			});
		} else {
			getData()->termsLock = Window::TermsLock();
		}
		goReplace<SignupWidget>(Animate::Forward);
	});
}

void Step::finish(const MTPUser &user, QImage &&photo) {
	if (user.type() != mtpc_user
		|| !user.c_user().is_self()
		|| !user.c_user().vid().v) {
		// No idea what to do here.
		// We could've reset intro and MTP, but this really should not happen.
		Ui::show(Ui::MakeInformBox(
			"Internal error: bad user.is_self() after sign in."));
		return;
	}

	// Check if such account is authorized already.
	for (const auto &[index, existing] : Core::App().domain().accounts()) {
		const auto raw = existing.get();
		if (const auto session = raw->maybeSession()) {
			if (raw->mtp().environment() == _account->mtp().environment()
				&& UserId(user.c_user().vid()) == session->userId()) {
				_account->logOut();
				crl::on_main(raw, [=] {
					Core::App().domain().activate(raw);
					Local::sync();
				});
				return;
			}
		}
	}

	api().request(MTPmessages_GetDialogFilters(
	)).done([=](const MTPmessages_DialogFilters &result) {
		const auto &d = result.data();
		createSession(user, photo, d.vfilters().v, d.is_tags_enabled());
	}).fail([=] {
		createSession(user, photo, QVector<MTPDialogFilter>(), false);
	}).send();
}

void Step::createSession(
		const MTPUser &user,
		QImage photo,
		const QVector<MTPDialogFilter> &filters,
		bool tagsEnabled) {
	// Save the default language if we've suggested some other and user ignored it.
	const auto currentId = Lang::Id();
	const auto defaultId = Lang::DefaultLanguageId();
	const auto suggested = Lang::CurrentCloudManager().suggestedLanguage();
	if (currentId.isEmpty() && !suggested.isEmpty() && suggested != defaultId) {
		Lang::GetInstance().switchToId(Lang::DefaultLanguage());
		Local::writeLangPack();
	}

	auto settings = std::make_unique<Main::SessionSettings>();
	const auto hasFilters = ranges::contains(
		filters,
		mtpc_dialogFilter,
		&MTPDialogFilter::type);
	settings->setDialogsFiltersEnabled(hasFilters);

	const auto account = _account;
	account->createSession(user, std::move(settings));

	// "this" is already deleted here by creating the main widget.
	account->local().enforceModernStorageIdBots();
	account->local().writeMtpData();
	auto &session = account->session();
	session.data().chatsFilters().setPreloaded(filters, tagsEnabled);
	if (hasFilters) {
		session.saveSettingsDelayed();
	}
	if (!photo.isNull()) {
		session.api().peerPhoto().upload(
			session.user(),
			{ std::move(photo) });
	}
	account->appConfig().refresh();
	if (session.supportMode()) {
		PrepareSupportMode(&session);
	}
	Local::sync();
}

void Step::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	paintAnimated(p, e->rect());
}

void Step::resizeEvent(QResizeEvent *e) {
	updateLabelsPosition();
}

void Step::updateLabelsPosition() {
	if (_error) {
		if (_errorCentered) {
			_error->entity()->resizeToWidth(width());
		}
		Ui::SendPendingMoveResizeEvents(_error->entity());
		auto errorLeft = _errorCentered ? 0 : (contentLeft() + st::buttonRadius);
		_error->moveToLeft(errorLeft, errorTop());
	}
}

int Step::errorTop() const {
	return contentTop() + st::introErrorTop;
}


void Step::showFinished() {
	_a_show.stop();
	_slideAnimation.reset();
	activate();
}

bool Step::paintAnimated(QPainter &p, QRect clip) {
	if (_slideAnimation) {
		_slideAnimation->paintFrame(p, (width() - st::introStepWidth) / 2, contentTop(), width());
		if (!_slideAnimation->animating()) {
			showFinished();
			return false;
		}
		return true;
	}

	if (!_a_show.animating()) {
		if (!QRect(0, contentTop(), width(), st::introStepHeight).intersects(clip)) {
			return true;
		}
		return false;
	}

	return true;
}

void Step::fillSentCodeData(const MTPDauth_sentCode &data) {
	const auto bad = [](const char *type) {
		LOG(("API Error: Should not be '%1'.").arg(type));
	};
	getData()->codeByTelegram = false;
	getData()->codeByFragmentUrl = QString();
	data.vtype().match([&](const MTPDauth_sentCodeTypeApp &data) {
		getData()->codeByTelegram = true;
		getData()->codeLength = data.vlength().v;
	}, [&](const MTPDauth_sentCodeTypeSms &data) {
		getData()->codeLength = data.vlength().v;
	}, [&](const MTPDauth_sentCodeTypeFragmentSms &data) {
		getData()->codeByFragmentUrl = qs(data.vurl());
		getData()->codeLength = data.vlength().v;
	}, [&](const MTPDauth_sentCodeTypeCall &data) {
		getData()->codeLength = data.vlength().v;
	}, [&](const MTPDauth_sentCodeTypeFlashCall &) {
		bad("FlashCall");
	}, [&](const MTPDauth_sentCodeTypeMissedCall &) {
		bad("MissedCall");
	}, [&](const MTPDauth_sentCodeTypeFirebaseSms &) {
		bad("FirebaseSms");
	}, [&](const MTPDauth_sentCodeTypeEmailCode &data) {
		getData()->emailPatternLogin = qs(data.vemail_pattern());
		getData()->codeLength = data.vlength().v;
	}, [&](const MTPDauth_sentCodeTypeSmsWord &) {
		bad("SmsWord");
	}, [&](const MTPDauth_sentCodeTypeSmsPhrase &) {
		bad("SmsPhrase");
	}, [&](const MTPDauth_sentCodeTypeSetUpEmailRequired &) {
		getData()->emailStatus = EmailStatus::SetupRequired;
	});
}



int Step::contentLeft() const {
	return (width() - st::introNextButton.width) / 2;
}

int Step::contentTop() const {
	auto result = (height() - st::introHeight) / 2;
	accumulate_max(result, st::introStepTopMin);
	return result;
}

void Step::setErrorCentered(bool centered) {
	_errorCentered = centered;
	_error.destroy();
}

void Step::showError(rpl::producer<QString> text) {
	_errorText = std::move(text);
}

void Step::refreshError(const QString &text) {
	if (text.isEmpty()) {
		if (_error) _error->hide(anim::type::normal);
	} else {
		if (!_error) {
			_error.create(
				this,
				object_ptr<Ui::FlatLabel>(
					this,
					_errorCentered
						? st::introErrorCentered
						: st::introError));
			_error->hide(anim::type::instant);
		}
		_error->entity()->setText(text);
		updateLabelsPosition();
		_error->show(anim::type::normal);
	}
}

void Step::prepareShowAnimated(Step *after) {
	setInnerFocus();
	auto leftSnapshot = after->prepareSlideAnimation();
	auto rightSnapshot = prepareSlideAnimation();
	_slideAnimation = std::make_unique<Ui::SlideAnimation>();
	_slideAnimation->setSnapshots(std::move(leftSnapshot), std::move(rightSnapshot));
	_slideAnimation->setOverflowHidden(false);
}

QPixmap Step::prepareSlideAnimation() {
	auto grabLeft = (width() - st::introStepWidth) / 2;
	auto grabTop = contentTop();
	return Ui::GrabWidget(
		this,
		QRect(grabLeft, grabTop, st::introStepWidth, st::introStepHeight));
}

void Step::showAnimated(Animate animate) {
	setFocus();
	show();
	hideChildren();
	if (_slideAnimation) {
		auto slideLeft = (animate == Animate::Back);
		_slideAnimation->start(
			slideLeft,
			[=] { update(0, contentTop(), width(), st::introStepHeight); },
			st::introSlideDuration);
	}
}

void Step::setGoCallback(
		Fn<void(Step *step, StackAction action, Animate animate)> callback) {
	_goCallback = std::move(callback);
}

void Step::setShowResetCallback(Fn<void()> callback) {
	_showResetCallback = std::move(callback);
}

void Step::setShowTermsCallback(Fn<void()> callback) {
	_showTermsCallback = std::move(callback);
}

void Step::setCancelNearestDcCallback(Fn<void()> callback) {
	_cancelNearestDcCallback = std::move(callback);
}

void Step::setAcceptTermsCallback(
		Fn<void(Fn<void()> callback)> callback) {
	_acceptTermsCallback = std::move(callback);
}

void Step::showFast() {
	show();
	showFinished();
}

bool Step::animating() const {
	return (_slideAnimation && _slideAnimation->animating())
		|| _a_show.animating();
}

bool Step::hasBack() const {
	return false;
}

void Step::activate() {
	if (!_errorText.current().isEmpty()) {
		_error->show(anim::type::instant);
	}
}

void Step::cancelled() {
}

void Step::finished() {
	hide();
}

} // namespace details
} // namespace Intro
