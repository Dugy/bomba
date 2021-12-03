#ifndef BOMBA_SYNC_SERVER_HPP
#define BOMBA_SYNC_SERVER_HPP

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif

#include <experimental/net>
#include <vector>
#include <chrono>

#include <iostream>

namespace Bomba {

namespace Net = std::experimental::net;
using NetStringView = std::experimental::fundamentals_v1::basic_string_view<char>;

struct ITcpServerSession {
	virtual std::pair<ServerReaction, int> feedToResponder(std::span<char> data) = 0;
	virtual void notifyMessageWasParsed() = 0;
};

struct TcpReceiver {
	// Returns if the communication is expected to continue
	virtual bool receive(ITcpServerSession& session, std::error_code error, int length) = 0;
	virtual std::span<char> space() = 0;
};

class TcpServerBuffer : TcpReceiver {
	constexpr static int ResponseMaxSize = 2048;
	constexpr static int ResponseBufferSize = ResponseMaxSize * 1.5;
	std::array<char, ResponseBufferSize> _responseArray = {};
	std::span<char> _responseBuffer = std::span<char>(_responseArray.data(), ResponseMaxSize);
	int _keptStart = 0;
	int _keptEnd = 0;
	std::vector<char> _longLeftovers;

public:
	TcpServerBuffer() = default;
	virtual ~TcpServerBuffer() = default;

private:
	ServerReaction readBuffer(ITcpServerSession& session) {
		std::span<char> input;
		if (_longLeftovers.empty()) [[likely]] {
			input = std::span<char>(_responseArray.begin() + _keptStart, _keptEnd - _keptStart);
		} else {
			input = std::span<char>(_longLeftovers.begin() + _keptStart, _keptEnd - _keptStart);
		}

		auto [reaction, parsed] = session.feedToResponder(input);

		if (reaction == ServerReaction::DISCONNECT) {
			return reaction;
		}
		if (reaction == ServerReaction::OK) {
			_keptStart += parsed;
		}
		return reaction;
	}

public:
	virtual bool receive(ITcpServerSession& session, std::error_code error, int length) {
		if (error || length == 0 /* Means end of stream */) {
			return false;
		}

		_keptEnd += length;
		if (!_longLeftovers.empty()) [[unlikely]] {
			// Message too large for the buffer, it had to be written into the vector
			_longLeftovers.resize(_keptEnd); // Shrink to contain only the data that was actually read
		}

		ServerReaction reaction = ServerReaction::OK;
		int previousReadStart = _keptStart;
		while ((reaction = readBuffer(session)) == ServerReaction::OK) {
			session.notifyMessageWasParsed();
			if (_keptStart == _keptEnd)
				break;
		}
		if (reaction == ServerReaction::DISCONNECT) {
			return false;
		}

		if (!_longLeftovers.empty() && previousReadStart != _keptStart) [[unlikely]] {
			// Something was read successfully
			int left = _longLeftovers.size() - _keptStart;
			if (left > int(_responseArray.size())) {
				_longLeftovers.erase(_longLeftovers.begin(), _longLeftovers.begin() + _keptStart);
				_keptEnd = left;
				_keptStart = 0;
			} else {
				memcpy(_responseArray.data(), _longLeftovers.data() + _keptStart, left);
				_keptEnd = left;
				_keptStart = 0;
				_longLeftovers.clear();
				_longLeftovers.shrink_to_fit();
			}
		}

		if (!_longLeftovers.empty()) [[unlikely]] {
			// Long message, make space at the end of the vector
			_keptEnd = _longLeftovers.size();
			_longLeftovers.resize(_keptEnd + ResponseBufferSize);
			_responseBuffer = std::span<char>(_longLeftovers.data() + _keptEnd, _longLeftovers.size() - _keptEnd);
		} else {
			if (_keptStart == 0 && _keptEnd == ResponseBufferSize) [[unlikely]] {
				// No more space in the buffer, move it to the vector
				_longLeftovers.resize(2 * ResponseBufferSize);
				memcpy(_longLeftovers.data(), _responseArray.data(), _responseArray.size());
				_responseBuffer = std::span<char>(_longLeftovers.data() + _keptEnd, _longLeftovers.size() - _keptEnd);
			} else {
				// Normal buffer usage
				if (_keptStart == _keptEnd) {
					// We can start from the beginning
					_keptStart = 0;
					_keptEnd = 0;
					_responseBuffer = std::span<char>(_responseArray.data(), ResponseMaxSize);
				} else if (_keptStart > ResponseBufferSize - _keptEnd) {
					// More space at the start
					int copySize = _keptEnd - _keptStart;
					memmove(_responseArray.data(), _responseArray.data() + _keptStart, copySize);
					int bufferSize = std::min(_keptStart, ResponseMaxSize);
					_responseBuffer = std::span<char>(_responseArray.data() + copySize, bufferSize);
					_keptStart = 0;
					_keptEnd = copySize;
				} else {
					// More space at the end
					int bufferSize = std::min(ResponseBufferSize - _keptEnd, ResponseMaxSize);
					_responseBuffer = std::span<char>(_responseArray.data() + _keptEnd, bufferSize);
				}
			}
		}

		return true;
	}

	std::span<char> space() {
		return _responseBuffer;
	}
};

template <typename Responder>
class TcpServer {
	Responder& _responder;
	Net::io_context _context;
	Net::ip::tcp::endpoint _endpoint;
	Net::ip::tcp::acceptor _acceptor = {_context, _endpoint};
	std::chrono::nanoseconds _totalResponseTime = std::chrono::nanoseconds(0);
	int64_t _totalResponses = 0;

	struct Session : ITcpServerSession {
		Net::ip::tcp::socket _socket;
		typename Responder::Session _responder;
		TcpServerBuffer _buffer;
		Net::const_buffer _space = {};
		TcpServer& _parent = nullptr;
		int _index = {};

		Session(Net::ip::tcp::socket&& socket, Responder& responder, TcpServer& parent, int index)
				: _socket(std::move(socket)), _responder(responder.getSession()), _parent(parent), _index(index) {
			readSome();
		}

		void readSome() {
			std::span<char> space = _buffer.space();
			_space = Net::buffer(space.data(), space.size());
			_socket.async_receive(_space, [this] (std::error_code error, int length = 0) {
				auto startTime = std::chrono::steady_clock::now();

				bool expectíngMore = _buffer.receive(*this, error, length);

				if (expectíngMore) {
					readSome();
				}
				auto endTime = std::chrono::steady_clock::now();
				_parent._totalResponseTime += std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);

				if (!expectíngMore) {
					cancel();
					return;
				}

			});
		}

		void cancel() { // MUST RETURN AFTER CALLING cancel(), IT DESTROYS this
			_socket.close();
			_parent.destroySession(_index);
		}

		std::pair<ServerReaction, int> feedToResponder(std::span<char> data) override {
			return _responder.respond(data, [this] (std::span<const char> output) {
				_socket.send(Net::buffer(output.data(), output.size()));
			});
		}

		void notifyMessageWasParsed() override {
			_parent._totalResponses++;
		}

		~Session() {
		}
	};

	std::vector<std::unique_ptr<Session>> _sessions;
	std::mutex _sessionsLock;

	void startSession() {
		_acceptor.async_accept(_context, [&] (std::error_code error, Net::ip::tcp::socket socket) {
			if (error) {
//				std::cout << "Error: " << error.message() << std::endl;
				return;
			}
			{
				std::lock_guard lock(_sessionsLock);
				int index = _sessions.size();
				_sessions.emplace_back(std::make_unique<Session>(std::move(socket), _responder, *this, index));
			}
			startSession();
		});
	}

	void destroySession(int index) {
		std::lock_guard lock(_sessionsLock);
		if (index < int(_sessions.size()) - 1) [[likely]] {
			_sessions.back()->_index = index;
			std::swap(_sessions[index], _sessions.back());
		}
		_sessions.pop_back();
	}

public:
	TcpServer(Responder& responder, int port, int threads)
			: _context(threads), _responder(responder), _endpoint(Net::ip::tcp::v4(), port) {
		startSession();
	}
	TcpServer(Responder& responder, int port)
			: _responder(responder), _endpoint(Net::ip::tcp::v4(), port) {
		startSession();
	}

	void run() {
		_context.run();
	}

	void stopRunning() {
		_context.stop();
	}

	void runARound() {
		_context.poll();
	}

	std::chrono::nanoseconds averageResponseTime() {
		return _totalResponseTime / _totalResponses;
	}
};

template <typename Responder>
class BackgroundTcpServer : private TcpServer<Responder> {
	std::thread _worker;
	void startWorker() {
		_worker = std::thread([this] {
			TcpServer<Responder>::run();
		});
	}
public:
	BackgroundTcpServer(Responder& responder, int port, int threads)
			: TcpServer<Responder>(responder, port, threads) {
		startWorker();
	}
	BackgroundTcpServer(Responder& responder, int port)
			: TcpServer<Responder>(responder, port) {
		startWorker();
	}
	~BackgroundTcpServer() {
		TcpServer<Responder>::stopRunning();
		_worker.join();
	}

	std::chrono::nanoseconds averageResponseTime() {
		return TcpServer<Responder>::averageResponseTime();
	}
};

} // namespace Bomba

#endif // BOMBA_SYNC_SERVER_HPP
