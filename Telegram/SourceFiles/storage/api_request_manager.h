/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/flat_map.h"
#include "rpl/rpl.h"

#include <QtCore/QPointer>
#include <QtCore/QMap>
#include <QtNetwork/QNetworkReply>
#include <memory>

namespace Storage {

struct ApiRequestOptions {
	QString url;
	QByteArray method = "GET";
	QByteArray body;
	QVector<QPair<QByteArray, QByteArray>> headers;
	int timeoutMs = 15000;
	int retries = 0;
	bool logRequest = true;
};

struct ApiResponse {
	int id = 0;
	QString url;
	int status = 0;
	QNetworkReply::NetworkError error = QNetworkReply::NoError;
	QByteArray body;
};

class ApiRequestManager final : public base::has_weak_ptr {
public:
	ApiRequestManager();
	~ApiRequestManager();

	// Enqueue a request. Returns a producer that emits the response for this specific request.
	[[nodiscard]] rpl::producer<ApiResponse> enqueue(ApiRequestOptions options);
	void cancel(int id);

private:
	struct Enqueued {
		int id = 0;
		ApiRequestOptions options;
	};

	struct Sent {
		int id = 0;
		ApiRequestOptions options;
		QPointer<QNetworkReply> reply;
		int retriesLeft = 0;
		int redirectsLeft = 0;
		std::unique_ptr<class QTimer> timeout;
	};

	void enqueueInternal(Enqueued entry);
	void cancelInternal(int id);
	void checkSendNext();
	void send(const Enqueued &entry);
	Sent *findSent(int id, QNetworkReply *reply);
	void removeSent(int id);
	void onFinished(int id, QNetworkReply *reply);
	void retryOrFail(Sent &sent, QNetworkReply::NetworkError error);
	QNetworkRequest buildRequest(const ApiRequestOptions &options) const;
	QNetworkReply *performRequest(const ApiRequestOptions &options);
	void startTimeout(Sent &sent);
	void emitResponse(int id, ApiResponse &&response);

	QThread _thread;
	std::unique_ptr<class QNetworkAccessManager> _network;

	std::deque<Enqueued> _queue;
	base::flat_map<int, Sent> _sent;
	int _autoincrement = 0;

	// Map from request id to its response stream
	base::flat_map<int, std::shared_ptr<rpl::event_stream<ApiResponse>>> _responseStreams;
};

// Get the global ApiRequestManager instance (singleton)
[[nodiscard]] std::shared_ptr<ApiRequestManager> GetApiRequestManager();

} // namespace Storage

