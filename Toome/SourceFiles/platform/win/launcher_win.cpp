/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "platform/win/launcher_win.h"

#include "core/crash_reports.h"
//TODO:updater
//#include "core/update_checker.h"
#include "nim_cpp_wrapper/nim_cpp_api.h"
#include <windows.h>
#include <shellapi.h>
#include <VersionHelpers.h>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <winrt/Windows.Data.Xml.Dom.h>
namespace Platform {

Launcher::Launcher(int argc, char *argv[])
: Core::Launcher(argc, argv) {
}

std::optional<QStringList> Launcher::readArgumentsHook(
		int argc,
		char *argv[]) const {
	auto count = 0;
	if (const auto list = CommandLineToArgvW(GetCommandLine(), &count)) {
		const auto guard = gsl::finally([&] { LocalFree(list); });
		if (count > 0) {
			auto result = QStringList();
			result.reserve(count);
			for (auto i = 0; i != count; ++i) {
				result.push_back(QString::fromWCharArray(list[i]));
			}
			return result;
		}
	}
	return std::nullopt;
}

bool Launcher::launchUpdater(UpdaterLaunch action) {
	if (cExeName().isEmpty()) {
		return false;
	}

	const auto operation = (action == UpdaterLaunch::JustRelaunch)
		? QString()
		: (cWriteProtected()
			? u"runas"_q
			: QString());
	const auto binaryPath = (action == UpdaterLaunch::JustRelaunch)
		? (cExeDir() + cExeName())
		: (cWriteProtected()
			? (cWorkingDir() + u"tupdates/temp/Updater.exe"_q)
			: (cExeDir() + u"Updater.exe"_q));

	auto argumentsList = QStringList();
	const auto pushArgument = [&](const QString &argument) {
		argumentsList.push_back(argument.trimmed());
	};
	if (cLaunchMode() == LaunchModeAutoStart) {
		pushArgument(u"-autostart"_q);
	}
	if (Logs::DebugEnabled()) {
		pushArgument(u"-debug"_q);
	}
	if (cStartInTray()) {
		pushArgument(u"-startintray"_q);
	}
	if (customWorkingDir()) {
		pushArgument(u"-workdir"_q);
		pushArgument('"' + cWorkingDir() + '"');
	}
	if (cDataFile() != u"data"_q) {
		pushArgument(u"-key"_q);
		pushArgument('"' + cDataFile() + '"');
	}

	if (action == UpdaterLaunch::JustRelaunch) {
		pushArgument(u"-noupdate"_q);
		if (cRestartingToSettings()) {
			pushArgument(u"-tosettings"_q);
		}
	} else {
		pushArgument(u"-update"_q);
		pushArgument(u"-exename"_q);
		pushArgument('"' + cExeName() + '"');
		if (cWriteProtected()) {
			pushArgument(u"-writeprotected"_q);
			pushArgument('"' + cExeDir() + '"');
		}
	}
	return launch(operation, binaryPath, argumentsList);
}

bool Launcher::launch(
		const QString &operation,
		const QString &binaryPath,
		const QStringList &argumentsList) {
	const auto convertPath = [](const QString &path) {
		return QDir::toNativeSeparators(path).toStdWString();
	};
	const auto nativeBinaryPath = convertPath(binaryPath);
	const auto nativeWorkingDir = convertPath(cWorkingDir());
	const auto arguments = argumentsList.join(' ');

	DEBUG_LOG(("Application Info: executing %1 %2"
		).arg(binaryPath
		).arg(arguments
		));

	Logs::closeMain();
	CrashReports::Finish();

	const auto hwnd = HWND(0);
	const auto result = ShellExecute(
		hwnd,
		operation.isEmpty() ? nullptr : operation.toStdWString().c_str(),
		nativeBinaryPath.c_str(),
		arguments.toStdWString().c_str(),
		nativeWorkingDir.empty() ? nullptr : nativeWorkingDir.c_str(),
		SW_SHOWNORMAL);
	if (int64(result) < 32) {
		DEBUG_LOG(("Application Error: failed to execute %1, working directory: '%2', result: %3"
			).arg(binaryPath
			).arg(cWorkingDir()
			).arg(int64(result)
			));
		return false;
	}
	return true;
}

void Launcher::initIMSdk() {
	QFile nim_config(u":/im_configs/config/nim_server.conf"_q);
	nim::SDKConfig config;
	QString app_key;
	if (nim_config.open(QIODevice::ReadOnly | QIODevice::Text)) {
		QByteArray fileContent = nim_config.readAll();
		nim_config.close();
		QJsonDocument jsonDoc = QJsonDocument::fromJson(fileContent);

		if (!jsonDoc.isNull() && jsonDoc.isObject()) {
			QJsonObject jsonObject = jsonDoc.object();
			if (jsonObject.contains(nim::kNIMAppKey) && jsonObject[nim::kNIMAppKey].isString()) {
				app_key = jsonObject[nim::kNIMAppKey].toString();
			}
			if (jsonObject.contains(nim::kNIMPreloadAttach) && jsonObject[nim::kNIMPreloadAttach].isString())
				config.preload_attach_ = jsonObject[nim::kNIMPreloadAttach].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMDataBaseEncryptKey) && jsonObject[nim::kNIMDataBaseEncryptKey].isString())
				config.database_encrypt_key_ = jsonObject[nim::kNIMDataBaseEncryptKey].toString().toStdString();
			if (jsonObject.contains(nim::kNIMPreloadImageQuality) && jsonObject[nim::kNIMPreloadImageQuality].isString())
				config.preload_image_quality_ = std::atoi(jsonObject[nim::kNIMPreloadImageQuality].toString().toStdString().c_str());
			if (jsonObject.contains(nim::kNIMPreloadImageResize) && jsonObject[nim::kNIMPreloadImageResize].isString())
				config.preload_image_resize_ = jsonObject[nim::kNIMPreloadImageResize].toString().toStdString();
			if (jsonObject.contains(nim::kNIMPreloadAttachImageNameTemplate) && jsonObject[nim::kNIMPreloadAttachImageNameTemplate].isString())
				config.preload_image_name_template_ = jsonObject[nim::kNIMPreloadAttachImageNameTemplate].toString().toStdString();
			if (jsonObject.contains(nim::kNIMSDKLogLevel) && jsonObject[nim::kNIMSDKLogLevel].isString())
				config.sdk_log_level_ = (nim::NIMSDKLogLevel)jsonObject[nim::kNIMSDKLogLevel].toString().toInt();
			if (jsonObject.contains(nim::kNIMSyncSessionAck) && jsonObject[nim::kNIMSyncSessionAck].isString())
				config.sync_session_ack_ = jsonObject[nim::kNIMSyncSessionAck].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMLoginRetryMaxTimes) && jsonObject[nim::kNIMLoginRetryMaxTimes].isString())
				config.login_max_retry_times_ = jsonObject[nim::kNIMLoginRetryMaxTimes].toString().toInt();
			if (jsonObject.contains(nim::kNIMUseHttps) && jsonObject[nim::kNIMUseHttps].isString())
				config.use_https_ = jsonObject[nim::kNIMUseHttps].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMTeamNotificationUnreadCount) && jsonObject[nim::kNIMTeamNotificationUnreadCount].isString())
				config.team_notification_unread_count_ = jsonObject[nim::kNIMTeamNotificationUnreadCount].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMVChatMissUnreadCount) && jsonObject[nim::kNIMVChatMissUnreadCount].isString())
				config.vchat_miss_unread_count_ = jsonObject[nim::kNIMVChatMissUnreadCount].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMResetUnreadCountWhenRecall) && jsonObject[nim::kNIMResetUnreadCountWhenRecall].isString())
				config.reset_unread_count_when_recall_ = jsonObject[nim::kNIMResetUnreadCountWhenRecall].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMUploadSDKEventsAfterLogin) && jsonObject[nim::kNIMUploadSDKEventsAfterLogin].isString())
				config.upload_sdk_events_after_login_ = jsonObject[nim::kNIMUploadSDKEventsAfterLogin].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMAnimatedImageThumbnailEnabled) && jsonObject[nim::kNIMAnimatedImageThumbnailEnabled].isString())
				config.animated_image_thumbnail_enabled_ =
				jsonObject[nim::kNIMAnimatedImageThumbnailEnabled].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMClientAntispam) && jsonObject[nim::kNIMClientAntispam].isString())
				config.client_antispam_ = jsonObject[nim::kNIMClientAntispam].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMTeamMessageAckEnabled) && jsonObject[nim::kNIMTeamMessageAckEnabled].isString())
				config.team_msg_ack_ = jsonObject[nim::kNIMTeamMessageAckEnabled].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMNeedUpdateLBSBeforRelogin) && jsonObject[nim::kNIMNeedUpdateLBSBeforRelogin].isString())
				config.need_update_lbs_befor_relogin_ = jsonObject[nim::kNIMNeedUpdateLBSBeforRelogin].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMCachingMarkreadEnabled) && jsonObject[nim::kNIMCachingMarkreadEnabled].isString())
				config.caching_markread_ = jsonObject[nim::kNIMCachingMarkreadEnabled].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMCachingMarkreadTime) && jsonObject[nim::kNIMCachingMarkreadTime].isString())
				config.caching_markread_time_ = jsonObject[nim::kNIMCachingMarkreadTime].toString().toInt();
			if (jsonObject.contains(nim::kNIMCachingMarkreadCount) && jsonObject[nim::kNIMCachingMarkreadCount].isString())
				config.caching_markread_count_ = jsonObject[nim::kNIMCachingMarkreadCount].toString().toInt();
			if (jsonObject.contains(nim::kNIMUserDataFileLocalBackupFolder) && jsonObject[nim::kNIMUserDataFileLocalBackupFolder].isString())
				config.user_datafile_localbackup_folder_ = jsonObject[nim::kNIMUserDataFileLocalBackupFolder].toString().toStdString();
			if (jsonObject.contains(nim::kNIMEnableUserDataFileLocalBackup) && jsonObject[nim::kNIMEnableUserDataFileLocalBackup].isString())
				config.enable_user_datafile_backup_ = jsonObject[nim::kNIMEnableUserDataFileLocalBackup].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMEnableUserDataFileLocalRestore) && jsonObject[nim::kNIMEnableUserDataFileLocalRestore].isString())
				config.enable_user_datafile_restore_ = jsonObject[nim::kNIMEnableUserDataFileLocalRestore].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMEnableUserDataFileDefRestoreProc) && jsonObject[nim::kNIMEnableUserDataFileDefRestoreProc].isString())
				config.enable_user_datafile_defrestoreproc_ =
				jsonObject[nim::kNIMEnableUserDataFileDefRestoreProc].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMDedicatedClusteFlag) && jsonObject[nim::kNIMDedicatedClusteFlag].isString())
				config.dedicated_cluste_flag_ = jsonObject[nim::kNIMDedicatedClusteFlag].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMNegoKeyNECA) && jsonObject[nim::kNIMNegoKeyNECA].isString())
				config.nego_key_neca_ = jsonObject[nim::kNIMNegoKeyNECA].toString().toInt();
			if (jsonObject.contains(nim::kNIMCommNECA) && jsonObject[nim::kNIMCommNECA].isString())
				config.comm_neca_ = jsonObject[nim::kNIMCommNECA].toString().toInt();
			if (jsonObject.contains(nim::kNIMIPProtVersion) && jsonObject[nim::kNIMIPProtVersion].isString())
				config.ip_protocol_version_ = jsonObject[nim::kNIMIPProtVersion].toString().toInt();
			if (jsonObject.contains(nim::kNIMHandShakeType) && jsonObject[nim::kNIMHandShakeType].isString())
				config.hand_shake_type_ = jsonObject[nim::kNIMHandShakeType].toString().toInt();
			if (jsonObject.contains(nim::kNIMPriorityUseCdnHost) && jsonObject[nim::kNIMPriorityUseCdnHost].isString())
				config.hand_shake_type_ = jsonObject[nim::kNIMPriorityUseCdnHost].toString().toInt() != 0;
			//融合存储
			if (jsonObject.contains(nim::kNIMFcsAuthType) && jsonObject[nim::kNIMFcsAuthType].isString())
				config.fcs_auth_type_ = jsonObject[nim::kNIMFcsAuthType].toString().toInt();
			if (jsonObject.contains(nim::kNIMFcsCustomEnable) && jsonObject[nim::kNIMFcsCustomEnable].isString())
				config.custom_enable_fcs_ = jsonObject[nim::kNIMFcsCustomEnable].toString().toInt() != 0;
			if (jsonObject.contains(nim::kNIMMockUA) && jsonObject[nim::kNIMMockUA].isString())
				config.mock_ua_ = jsonObject[nim::kNIMMockUA].toString().toStdString();
			if (jsonObject.contains(nim::kNIMMockRefer) && jsonObject[nim::kNIMMockRefer].isString())
				config.mock_refer_ = jsonObject[nim::kNIMMockRefer].toString().toStdString();
			if (jsonObject.contains(nim::kNIMLbsAddress) && jsonObject[nim::kNIMLbsAddress].isString())
				config.lbs_address_ = jsonObject[nim::kNIMLbsAddress].toString().toStdString();
			if (jsonObject.contains(nim::kNIMPrivateEnableHttps) && jsonObject[nim::kNIMPrivateEnableHttps].isString())
				config.use_https_ = jsonObject[nim::kNIMPrivateEnableHttps].toString().toInt() != 1;
			if (jsonObject.contains(nim::kNIMLbsBackupAddress) && jsonObject[nim::kNIMLbsBackupAddress].isArray())
			{
				QJsonArray arrayAdress = jsonObject[nim::kNIMLbsBackupAddress].toArray();
				for (const QJsonValue& address : arrayAdress) {
					config.lbs_backup_address_.push_back(address.toString().toStdString());
				}
			}
			if (jsonObject.contains(nim::kNIMDownloadAddressTemplate) && jsonObject[nim::kNIMDownloadAddressTemplate].isString())
				config.nos_download_address_ = jsonObject[nim::kNIMDownloadAddressTemplate].toString().toStdString();
			if (jsonObject.contains(nim::kNIMDefaultNosUploadHost) && jsonObject[nim::kNIMDefaultNosUploadHost].isString())
				config.default_nos_upload_host_ = jsonObject[nim::kNIMDefaultNosUploadHost].toString().toStdString();
			if (jsonObject.contains(nim::kNIMDefaultNosUploadAddress) && jsonObject[nim::kNIMDefaultNosUploadAddress].isString())
				config.default_nos_upload_address_ = jsonObject[nim::kNIMDefaultNosUploadAddress].toString().toStdString();
			if (jsonObject.contains(nim::kNIMNosLbsAddress) && jsonObject[nim::kNIMNosLbsAddress].isString())
				config.nos_lbs_address_ = jsonObject[nim::kNIMNosLbsAddress].toString().toStdString();
			if (jsonObject.contains(nim::kNIMDefaultLinkAddress) && jsonObject[nim::kNIMDefaultLinkAddress].isString())
				config.default_link_address_ = jsonObject[nim::kNIMDefaultLinkAddress].toString().toStdString();
		}
		HMODULE instance_nim_ = ::LoadLibraryA("nim.dll");
		if (instance_nim_ == NULL) {
			auto error = ::GetLastError();
			return ;
		}

		std::string app_data_path = (cWorkingDir() + u"nim"_q).toStdString();
		bool suc = nim::Client::Init(app_key.toStdString(), app_data_path, "", config);
		if (suc) {
			DEBUG_LOG(("Nim SDK初始化成功"));
		}
	}
	
	


}

} // namespace Platform
