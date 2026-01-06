/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/lang_cloud_manager.h"

#include "lang/lang_instance.h"
#include "lang/lang_file_parser.h"
#include "lang/lang_text_entity.h"
#include "mtproto/mtp_instance.h"
#include "storage/localstorage.h"
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/text/text_utilities.h"
#include "core/file_utilities.h"
#include "core/click_handler_types.h"
#include "boxes/abstract_box.h" // Ui::hideLayer().
#include "styles/style_layers.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

namespace Lang {
namespace {

class ConfirmSwitchBox : public Ui::BoxContent {
public:
	ConfirmSwitchBox(
		QWidget*,
		const Language &data,
		Fn<void()> apply);

protected:
	void prepare() override;

private:
	QString _name;
	int _percent = 0;
	bool _official = false;
	Fn<void()> _apply;

};

class NotReadyBox : public Ui::BoxContent {
public:
	NotReadyBox(
		QWidget*,
		const Language &data);

protected:
	void prepare() override;

private:
	QString _name;

};

ConfirmSwitchBox::ConfirmSwitchBox(
	QWidget*,
	const Language &data,
	Fn<void()> apply)
: _name(data.nativeName)
, _percent(1.)
, _official(true)
, _apply(std::move(apply)) {
}

void ConfirmSwitchBox::prepare() {
	setTitle(tr::lng_language_switch_title());

	auto text = (_official
		? tr::lng_language_switch_about_official
		: tr::lng_language_switch_about_unofficial)(
			lt_lang_name,
			rpl::single(tr::bold(_name)),
			lt_percent,
			rpl::single(tr::bold(QString::number(_percent))),
			tr::marked);
	const auto content = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			std::move(text),
			st::boxLabel),
		QMargins{ st::boxPadding.left(), 0, st::boxPadding.right(), 0 });
	content->entity()->setLinksTrusted();

	addButton(tr::lng_language_switch_apply(), [=] {
		const auto apply = _apply;
		closeBox();
		apply();
	});
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	content->resizeToWidth(st::boxWideWidth);
	content->heightValue(
	) | rpl::on_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, lifetime());
}

NotReadyBox::NotReadyBox(
	QWidget*,
	const Language &data)
: _name(data.nativeName)
{
}

void NotReadyBox::prepare() {
	setTitle(tr::lng_language_not_ready_title());

	auto text = tr::lng_language_not_ready_about(
		lt_lang_name,
		rpl::single(tr::marked(_name)),
		tr::marked);
	const auto content = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			std::move(text),
			st::boxLabel),
		QMargins{ st::boxPadding.left(), 0, st::boxPadding.right(), 0 });
	content->entity()->setLinksTrusted();

	addButton(tr::lng_box_ok(), [=] { closeBox(); });

	content->resizeToWidth(st::boxWidth);
	content->heightValue(
	) | rpl::on_next([=](int height) {
		setDimensions(st::boxWidth, height);
	}, lifetime());
}

} // namespace


CloudManager::CloudManager(Instance &langpack)
: _langpack(langpack) {
	const auto mtpLifetime = _lifetime.make_state<rpl::lifetime>();
	Core::App().domain().activeValue(
	) | rpl::filter([=](Main::Account *account) {
		return (account != nullptr);
	}) | rpl::on_next_done([=](Main::Account *account) {
		*mtpLifetime = account->mtpMainSessionValue(
		) | rpl::on_next([=](not_null<MTP::Instance*> instance) {
			_api.emplace(instance);
			resendRequests();
		});
	}, [=] {
		_api.reset();
	}, _lifetime);
}

Pack CloudManager::packTypeFromId(const QString &id) const {
	if (id == LanguageIdOrDefault(_langpack.id())) {
		return Pack::Current;
	} else if (id == _langpack.baseId()) {
		return Pack::Base;
	}
	return Pack::None;
}

rpl::producer<> CloudManager::languageListChanged() const {
	return _languageListChanged.events();
}

rpl::producer<> CloudManager::firstLanguageSuggestion() const {
	return _firstLanguageSuggestion.events();
}

void CloudManager::requestLangPackDifference(const QString &langId) {
	Expects(!langId.isEmpty());

	if (langId == LanguageIdOrDefault(_langpack.id())) {
		requestLangPackDifference(Pack::Current);
	} else {
		requestLangPackDifference(Pack::Base);
	}
}



void CloudManager::requestLangPackDifference(Pack pack) {

	if (_langpack.isCustom()) {
		return;
	}

	const auto version = _langpack.version(pack);
	const auto code = _langpack.cloudLangCode(pack);
	if (code.isEmpty()) {
		return;
	}

	auto language = ranges::find_if(_languages, [=](const Language &language) {
		return language.id == code;
	});
	if (language == _languages.end()) {
		return;
	}
	if (version > 0) {
		auto content = QFile(language->langFile);
		if (!content.open(QIODevice::ReadOnly)) {
			return;
		}
		applyLangPackDifference({ code, content.readAll(), version });
		
	} else {
		auto content = QFile(language->langFile);
		if (!content.open(QIODevice::ReadOnly)) {
			return;
		}
		applyLangPackDifference({ code, content.readAll(), 0});
	}
}

void CloudManager::setSuggestedLanguage(const QString &langCode) {
	if (Lang::LanguageIdOrDefault(langCode) != Lang::DefaultLanguageId()) {
		_suggestedLanguage = langCode;
	} else {
		_suggestedLanguage = QString();
	}

	if (!_languageWasSuggested) {
		_languageWasSuggested = true;
		_firstLanguageSuggestion.fire({});

		if (Core::App().offerLegacyLangPackSwitch()
			&& _langpack.id().isEmpty()
			&& !_suggestedLanguage.isEmpty()) {
			_offerSwitchToId = _suggestedLanguage;
			offerSwitchLangPack();
		}
	}
}

void CloudManager::setCurrentVersions(int version, int baseVersion) {
	const auto check = [&](Pack pack, int version) {
		if (version > _langpack.version(pack)) {
			requestLangPackDifference(pack);
		}
	};
	check(Pack::Current, version);
	check(Pack::Base, baseVersion);
}

void CloudManager::applyLangPackDifference(
		const LanguageData &difference) {

	if (_langpack.isCustom()) {
		return;
	}

	const auto langpackId = difference.langCode;
	const auto pack = packTypeFromId(langpackId);
	if (pack != Pack::None) {
		applyLangPackData(pack, difference);
		if (_restartAfterSwitch) {
			restartAfterSwitch();
		}
	} else {
		LOG(("Lang Warning: "
			"Ignoring update for '%1' because our language is '%2'").arg(
			langpackId,
			_langpack.id()));
	}
}

void CloudManager::requestLanguageList() {
	
	//read content from :/lang/languages.json
	auto content = QFile(":/lang/languages.json");
	if (!content.open(QIODevice::ReadOnly)) {
		return;
	}
	auto data = content.readAll();
	auto json = QJsonDocument::fromJson(data);
	auto languageData = json["languages"].toArray();
	auto languages = Languages();
	for (const auto &language : languageData) {
		auto languageObj = language.toObject();
		auto lang = Language{
			languageObj["id"].toString(),
			languageObj["pluralId"].toString(),
			languageObj["baseId"].toString(),
			languageObj["name"].toString(),
			languageObj["nativeName"].toString(),
			languageObj["langFile"].toString()
		};
		languages.push_back(lang);
	}
	_languages = languages;
	_languageListChanged.fire({});
}

void CloudManager::offerSwitchLangPack() {
	Expects(!_offerSwitchToId.isEmpty());
	Expects(_offerSwitchToId != DefaultLanguageId());

	if (!showOfferSwitchBox()) {
		languageListChanged(
		) | rpl::on_next([=] {
			showOfferSwitchBox();
		}, _lifetime);
		requestLanguageList();
	}
}

Language CloudManager::findOfferedLanguage() const {
	for (const auto &language : _languages) {
		if (language.id == _offerSwitchToId) {
			return language;
		}
	}
	return {};
}

bool CloudManager::showOfferSwitchBox() {
	const auto language = findOfferedLanguage();
	if (language.id.isEmpty()) {
		return false;
	}

	const auto confirm = [=] {
		Ui::hideLayer();
		if (_offerSwitchToId.isEmpty()) {
			return;
		}
		performSwitchAndRestart(language);
	};
	const auto cancel = [=] {
		Ui::hideLayer();
		changeLanguageId(DefaultLanguage());
		Local::writeLangPack();
	};
	Ui::show(
		Ui::MakeConfirmBox({
			.text = QString("Do you want to switch your language to ")
			+ language.nativeName
			+ QString("? You can always change your language in Settings."),
			.confirmed = confirm,
			.cancelled = cancel,
			.confirmText = QString("Change"),
		}),
		Ui::LayerOption::KeepOther);
	return true;
}

void CloudManager::applyLangPackData(
		Pack pack,
		const LanguageData &data) {
	if (_langpack.version(pack) < data.version) {
		requestLangPackDifference(pack);
	} else if (!data.stringsContent.isEmpty()) {
		_langpack.applyDifference(pack, data);
		Local::writeLangPack();
	} else if (_restartAfterSwitch) {
		Local::writeLangPack();
	} else {
		LOG(("Lang Info: Up to date."));
	}
}

bool CloudManager::canApplyWithoutRestart(const QString &id) const {
	if (id == u"#TEST_X"_q || id == u"#TEST_0"_q) {
		return true;
	}
	return Core::App().canApplyLangPackWithoutRestart();
}

void CloudManager::resetToDefault() {
	performSwitch(DefaultLanguage());
}

void CloudManager::switchToLanguage(const QString &id) {
	requestLanguageAndSwitch(id, false);
}

void CloudManager::switchWithWarning(const QString &id) {
	requestLanguageAndSwitch(id, true);
}

void CloudManager::requestLanguageAndSwitch(
		const QString &id,
		bool warning) {
	Expects(!id.isEmpty());

	if (LanguageIdOrDefault(_langpack.id()) == id) {
		Ui::show(Ui::MakeInformBox(tr::lng_language_already()));
		return;
	} else if (id == u"#custom"_q) {
		performSwitchToCustom();
		return;
	}

	_switchingToLanguageId = id;
	_switchingToLanguageWarning = warning;
	sendSwitchingToLanguageRequest();
}

void CloudManager::sendSwitchingToLanguageRequest() {

	auto language = ranges::find_if(_languages, [=](const Language &language) {
		return language.id == _switchingToLanguageId;
	});
	if (language == _languages.end()) {
		Ui::show(Ui::MakeInformBox(tr::lng_language_not_found()));
		return;
	}
	const auto foundLanguage = *language;
	const auto finalize = [=] {
		if (canApplyWithoutRestart(foundLanguage.id)) {
			performSwitchAndAddToRecent(foundLanguage);
		} else {
			performSwitchAndRestart(foundLanguage);
		}
	};
	if (!_switchingToLanguageWarning) {
		finalize();
		return;
	}
	auto content = Lang::FileParser(language->langFile, { tr::lng_sure_save_language.base });
	if (content.errors().isEmpty()) {
		Ui::show(Box<ConfirmSwitchBox>(foundLanguage, finalize));
	}else{
		Ui::show(Box<NotReadyBox>(foundLanguage));
	}
	
}

void CloudManager::switchToLanguage(const Language &data) {
	if (_langpack.id() == data.id && data.id != u"#custom"_q) {
		return;
	}

	if (data.id == u"#custom"_q) {
		performSwitchToCustom();
	} else if (canApplyWithoutRestart(data.id)) {
		performSwitchAndAddToRecent(data);
	} else {
		auto loader = Lang::FileParser(data.langFile, { tr::lng_sure_save_language.base });
		if (loader.errors().isEmpty()) {
			const auto values = loader.found();
			const auto getValue = [&](ushort key) {
				const auto it = values.find(key);
				return (it == values.cend())
					? GetOriginalValue(key)
					: it.value();
			};
			const auto text = tr::lng_sure_save_language(tr::now)
				+ "\n\n"
				+ getValue(tr::lng_sure_save_language.base);
			Ui::show(
				Ui::MakeConfirmBox({
					.text = text,
					.confirmed = [=] { performSwitchAndRestart(data); },
					.confirmText = tr::lng_box_ok(),
				}),
				Ui::LayerOption::KeepOther);
		} else {
			auto errorText = QString("%1 lang failed :(\n\nError: %2").arg(data.id, loader.errors());
			Ui::show(
				Ui::MakeInformBox({ .text = std::move(errorText) }),
				Ui::LayerOption::KeepOther);
		}
	}
}

void CloudManager::performSwitchToCustom() {
	auto filter = u"Language files (*.strings)"_q;
	auto title = u"Choose language .strings file"_q;
	FileDialog::GetOpenPath(Core::App().getFileDialogParent(), title, filter, [=, weak = base::make_weak(this)](const FileDialog::OpenResult &result) {
		if (!weak || result.paths.isEmpty()) {
			return;
		}

		const auto filePath = result.paths.front();
		auto loader = Lang::FileParser(
			filePath,
			{ tr::lng_sure_save_language.base });
		if (loader.errors().isEmpty()) {

			if (canApplyWithoutRestart(u"#custom"_q)) {
				_langpack.switchToCustomFile(filePath);
			} else {
				const auto values = loader.found();
				const auto getValue = [&](ushort key) {
					const auto it = values.find(key);
					return (it == values.cend())
						? GetOriginalValue(key)
						: it.value();
				};
				const auto text = tr::lng_sure_save_language(tr::now)
					+ "\n\n"
					+ getValue(tr::lng_sure_save_language.base);
				const auto change = [=] {
					_langpack.switchToCustomFile(filePath);
					Core::Restart();
				};
				Ui::show(
					Ui::MakeConfirmBox({
						.text = text,
						.confirmed = change,
						.confirmText = tr::lng_box_ok(),
					}),
					Ui::LayerOption::KeepOther);
			}
		} else {
			Ui::show(
				Ui::MakeInformBox(
					"Custom lang failed :(\n\nError: " + loader.errors()),
				Ui::LayerOption::KeepOther);
		}
	});
}

void CloudManager::switchToTestLanguage() {
	const auto testLanguageId = (_langpack.id() == u"#TEST_X"_q)
		? u"#TEST_0"_q
		: u"#TEST_X"_q;
	performSwitch({ testLanguageId });
}

void CloudManager::performSwitch(const Language &data) {
	_restartAfterSwitch = false;
	switchLangPackId(data);
	requestLangPackDifference(Pack::Current);
	requestLangPackDifference(Pack::Base);
}

void CloudManager::performSwitchAndAddToRecent(const Language &data) {
	Local::pushRecentLanguage(data);
	performSwitch(data);
}

void CloudManager::performSwitchAndRestart(const Language &data) {
	performSwitchAndAddToRecent(data);
	restartAfterSwitch();
}

void CloudManager::restartAfterSwitch() {
	Core::Restart();
}

void CloudManager::switchLangPackId(const Language &data) {
	const auto currentId = _langpack.id();
	const auto currentBaseId = _langpack.baseId();
	const auto notChanged = (currentId == data.id
		&& currentBaseId == data.baseId)
		|| (currentId.isEmpty()
			&& currentBaseId.isEmpty()
			&& data.id == DefaultLanguageId());
	if (!notChanged) {
		changeLanguageId(data);
	}
}

void CloudManager::changeLanguageId(const Language &data) {
	_langpack.switchToId(data);
}

void CloudManager::getValueForLang(
		const QString &key,
		const QString &langId,
		Fn<void(const QString &)> callback) {
	const auto requestKey = langId + ':' + key;
	auto &request = _getValueForLangRequests[requestKey];
	request.callback = std::move(callback);

	auto language = ranges::find_if(_languages, [=](const Language &language) {
		return language.id == langId;
	});
	if (language == _languages.end()) {
		request.requestId = -1;
		return;
	}
	const auto foundLanguage = *language;

	auto loader = Lang::FileParser(foundLanguage.langFile, { tr::lng_sure_save_language.base });
	if (loader.errors().isEmpty()) {
		const auto it = _getValueForLangRequests.find(requestKey);
		if (it != _getValueForLangRequests.end()) {
			const auto onstack = it->second.callback;
			_getValueForLangRequests.erase(it);
			const auto values = loader.found();
			for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
				const auto value = it.value();
				onstack(value); 
			}
			onstack(QString());
		}
	}else{
		const auto it = _getValueForLangRequests.find(requestKey);
		if (it != _getValueForLangRequests.end()) {
			const auto onstack = it->second.callback;
			_getValueForLangRequests.erase(it);
			onstack(QString());
		}
	}

}

void CloudManager::resendPendingValueRequests() {
	for (const auto &[requestKey, request] : _getValueForLangRequests) {
		if (request.requestId == -1) {
			const auto colonPos = requestKey.indexOf(':');
			if (colonPos > 0) {
				getValueForLang(
					requestKey.mid(colonPos + 1),
					requestKey.left(colonPos),
					request.callback);
			}
		}
	}
}

void CloudManager::resendRequests() {
	
	resendPendingValueRequests();
}

CloudManager &CurrentCloudManager() {
	auto result = Core::App().langCloudManager();
	Assert(result != nullptr);
	return *result;
}

} // namespace Lang
