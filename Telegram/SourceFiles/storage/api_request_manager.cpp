/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

#include "storage/api_request_manager.h"

#include "base/logging.h"
#include "crl/crl_on_main.h"

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtCore/QThread>
#include <QtCore/QTimer>

namespace Storage {
namespace {

constexpr auto kMaxHttpConcurrent = 15;
constexpr auto kDefaultTimeoutMs = 15000;
constexpr auto kMaxRedirects = 5;

std::weak_ptr<ApiRequestManager> GlobalApiRequestManager;

} // namespace

ApiRequestManager::ApiRequestManager()
: _network(std::make_unique<QNetworkAccessManager>()) {
	_network->moveToThread(&_thread);
	QObject::connect(&_thread, &QThread::finished, [=] {
		for (auto &[id, sent] : base::take(_sent)) {
			if (sent.timeout) {
				sent.timeout->stop();
			}
			if (sent.reply) {
				sent.reply->abort();
				sent.reply->deleteLater();
			}
		}
		_queue.clear();
		_network = nullptr;
	});
	_thread.start();
}

ApiRequestManager::~ApiRequestManager() {
	for (auto &[id, sent] : base::take(_sent)) {
		if (sent.timeout) {
			sent.timeout->stop();
		}
		if (sent.reply) {
			sent.reply->abort();
			sent.reply->deleteLater();
		}
	}
	_queue.clear();
	_thread.quit();
	_thread.wait();
}

rpl::producer<ApiResponse> ApiRequestManager::enqueue(ApiRequestOptions options) {
	if (options.timeoutMs <= 0) {
		options.timeoutMs = kDefaultTimeoutMs;
	}
	const auto id = ++_autoincrement;
	
	// Create a dedicated response stream for this request
	auto stream = std::make_shared<rpl::event_stream<ApiResponse>>();
	_responseStreams.emplace(id, stream);
	
	// Return a producer that filters responses for this specific request id
	auto producer = stream->events();
	
	Enqueued entry{ id, std::move(options) };
	const auto weak = base::make_weak(this);
	QMetaObject::invokeMethod(_network.get(), [weak, entry = std::move(entry)]() mutable {
		if (const auto strong = weak.get()) {
			strong->enqueueInternal(std::move(entry));
		}
	}, Qt::QueuedConnection);
	
	return producer;
}

void ApiRequestManager::cancel(int id) {
	const auto weak = base::make_weak(this);
	QMetaObject::invokeMethod(_network.get(), [weak, id] {
		if (const auto strong = weak.get()) {
			strong->cancelInternal(id);
		}
	}, Qt::QueuedConnection);
}


void ApiRequestManager::enqueueInternal(Enqueued entry) {
	const auto exists = std::find_if(
		_queue.begin(),
		_queue.end(),
		[id = entry.id](const Enqueued &e) { return e.id == id; });
	if (exists == _queue.end()) {
		_queue.push_back(std::move(entry));
	}
	checkSendNext();
}

void ApiRequestManager::cancelInternal(int id) {
	_queue.erase(
		std::remove_if(
			_queue.begin(),
			_queue.end(),
			[id](const Enqueued &e) { return e.id == id; }),
		_queue.end());
	removeSent(id);
	// Clean up response stream
	_responseStreams.erase(id);
}

void ApiRequestManager::checkSendNext() {
	while (_sent.size() < kMaxHttpConcurrent && !_queue.empty()) {
		const auto entry = _queue.front();
		_queue.pop_front();
		send(entry);
	}
}

void ApiRequestManager::send(const Enqueued &entry) {
	const auto requestId = entry.id;
	const auto reply = performRequest(entry.options);
	auto sent = Sent{
		.id = requestId,
		.options = entry.options,
		.reply = reply,
		.retriesLeft = entry.options.retries,
		.redirectsLeft = kMaxRedirects,
	};
	startTimeout(sent);
	QObject::connect(
		reply,
		&QNetworkReply::finished,
		this,
		[this, requestId, reply] {
			onFinished(requestId, reply);
		});
	_sent.emplace(requestId, std::move(sent));
	if (entry.options.logRequest) {
		LOG(("HTTP Request: %1 %2")
			.arg(QString::fromLatin1(entry.options.method))
			.arg(entry.options.url));
	}
}

ApiRequestManager::Sent *ApiRequestManager::findSent(
	int id,
	QNetworkReply *reply) {
	const auto i = _sent.find(id);
	return (i != _sent.end() && i->second.reply == reply)
		? &i->second
		: nullptr;
}

void ApiRequestManager::removeSent(int id) {
	const auto i = _sent.find(id);
	if (i != _sent.end()) {
		if (i->second.timeout) {
			i->second.timeout->stop();
		}
		if (i->second.reply) {
			i->second.reply->deleteLater();
		}
		_sent.erase(i);
		checkSendNext();
	}
}

void ApiRequestManager::onFinished(int id, QNetworkReply *reply) {
	const auto sent = findSent(id, reply);
	if (!sent) {
		return;
	}
	if (sent->timeout) {
		sent->timeout->stop();
	}
	const auto error = reply->error();
	if (error != QNetworkReply::NoError) {
		retryOrFail(*sent, error);
		return;
	}

	// Handle redirects
	const auto redirTarget = reply->attribute(
		QNetworkRequest::RedirectionTargetAttribute);
	if (redirTarget.isValid() && sent->redirectsLeft > 0) {
		auto url = redirTarget.toUrl();
		if (url.isRelative()) {
			url = reply->url().resolved(url);
		}
		if (url.isValid()) {
			sent->redirectsLeft--;
			if (sent->options.logRequest) {
				LOG(("HTTP Redirect: %1 -> %2 (left %3)")
					.arg(sent->options.url)
					.arg(url.toString())
					.arg(sent->redirectsLeft));
			}
			sent->options.url = url.toString();

			// Restart request with new URL
			if (sent->timeout) {
				sent->timeout->stop();
			}
			if (sent->reply) {
				sent->reply->deleteLater();
			}
			sent->reply = performRequest(sent->options);
			startTimeout(*sent);
			QObject::connect(
				sent->reply,
				&QNetworkReply::finished,
				this,
				[this, id](QNetworkReply *r) { onFinished(id, r); });
			return;
		}
	}
	if (redirTarget.isValid() && sent->redirectsLeft <= 0) {
		retryOrFail(*sent, QNetworkReply::TooManyRedirectsError);
		return;
	}
	const auto statusCode = reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute);
	const auto status = statusCode.isValid() ? statusCode.toInt() : 0;
	auto body = reply->readAll();
	ApiResponse response{
		.id = sent->id,
		.url = sent->options.url,
		.status = status,
		.error = error,
		.body = std::move(body),
	};
	if (sent->options.logRequest) {
		LOG(("HTTP Response: %1 %2 status %3 bytes %4")
			.arg(QString::fromLatin1(sent->options.method))
			.arg(sent->options.url)
			.arg(status)
			.arg(response.body.size()));
	}
	removeSent(id);
	emitResponse(id, std::move(response));
}

void ApiRequestManager::retryOrFail(
	Sent &sent,
	QNetworkReply::NetworkError error) {
	const auto statusCode = sent.reply
		? sent.reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
		: QVariant();
	const auto status = statusCode.isValid() ? statusCode.toInt() : 0;
	const auto message = sent.reply ? sent.reply->errorString() : QString();
	if (sent.retriesLeft > 0) {
		if (sent.options.logRequest) {
			LOG(("HTTP Retry: %1 %2 status %3 error %4 retriesLeft %5")
				.arg(QString::fromLatin1(sent.options.method))
				.arg(sent.options.url)
				.arg(status)
				.arg(message)
				.arg(sent.retriesLeft));
		}
		auto options = sent.options;
		options.retries = sent.retriesLeft - 1;
		const auto id = sent.id;
		removeSent(id);
		enqueueInternal(Enqueued{ id, std::move(options) });
		return;
	}
	if (sent.options.logRequest) {
		LOG(("HTTP Fail: %1 %2 status %3 error %4")
			.arg(QString::fromLatin1(sent.options.method))
			.arg(sent.options.url)
			.arg(status)
			.arg(message));
	}
	ApiResponse response{
		.id = sent.id,
		.url = sent.options.url,
		.status = status,
		.error = error,
	};
	removeSent(sent.id);
	emitResponse(sent.id, std::move(response));
}

QNetworkRequest ApiRequestManager::buildRequest(
	const ApiRequestOptions &options) const {
	QNetworkRequest request(options.url);
	request.setAttribute(
		QNetworkRequest::FollowRedirectsAttribute,
		true);
	for (const auto &header : options.headers) {
		request.setRawHeader(header.first, header.second);
	}
	return request;
}

QNetworkReply *ApiRequestManager::performRequest(
	const ApiRequestOptions &options) {
	const auto request = buildRequest(options);
	return _network->sendCustomRequest(
		request,
		options.method,
		options.body);
}

void ApiRequestManager::startTimeout(Sent &sent) {
	const auto timeoutMs = std::max(1, sent.options.timeoutMs);
	auto timer = std::make_unique<QTimer>();
	timer->setSingleShot(true);
	timer->setInterval(timeoutMs);
	const auto weak = base::make_weak(this);
	const auto requestId = sent.id;
	QObject::connect(timer.get(), &QTimer::timeout, [weak, requestId] {
		if (const auto strong = weak.get()) {
			const auto i = strong->_sent.find(requestId);
			if (i != strong->_sent.end()) {
				strong->retryOrFail(
					i->second,
					QNetworkReply::TimeoutError);
			}
		}
	});
	timer->moveToThread(&_thread);
	timer->start();
	sent.timeout = std::move(timer);
}

void ApiRequestManager::emitResponse(int id, ApiResponse &&response) {
	crl::on_main(this, [=, resp = std::move(response)]() mutable {
		// Emit to the specific request's stream
		const auto i = _responseStreams.find(id);
		if (i != _responseStreams.end()) {
			// Create a copy for the stream since we need to move it
			auto respCopy = resp;
			i->second->fire(std::move(respCopy));
			// Clean up the stream after emitting (one-time response)
			_responseStreams.erase(i);
		}
	});
}

std::shared_ptr<ApiRequestManager> GetApiRequestManager() {
	auto result = GlobalApiRequestManager.lock();
	if (!result) {
		GlobalApiRequestManager = result = std::make_shared<ApiRequestManager>();
	}
	return result;
}

} // namespace Storage

