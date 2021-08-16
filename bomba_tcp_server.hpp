#ifndef BOMBA_SYNC_SERVER_HPP
#define BOMBA_SYNC_SERVER_HPP

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif

#include <experimental/net>
#include <vector>
#include <chrono>

namespace Bomba {

namespace Net = std::experimental::net;
using NetStringView = std::experimental::fundamentals_v1::basic_string_view<char>;

template <typename Responder>
class TcpServer {
	Responder* _responder = nullptr;
	Net::io_context _context;
	Net::ip::tcp::endpoint _endpoint;
	Net::ip::tcp::acceptor _acceptor = {_context, _endpoint};
	std::chrono::nanoseconds _totalResponseTime = std::chrono::nanoseconds(0);
	int64_t _totalResponses = 0;

	struct Session {
		Net::ip::tcp::socket _socket;
		typename Responder::Session _responder;
		std::array<char, 2048> _responseArray = {};
		Net::const_buffer _responseBuffer = Net::buffer(_responseArray);
		std::vector<char> _leftovers;
		TcpServer* _parent = nullptr;
		int _index = {};

		Session(Net::ip::tcp::socket&& socket, Responder* responder, TcpServer* parent, int index)
			: _socket(std::move(socket)), _responder(responder->getSession()), _parent(parent), _index(index) { }

		ServerReaction readLeftovers() {
			auto [reaction, parsed] = _responder.respond(_leftovers, [this] (std::span<const char> output) {
				_socket.send(Net::buffer(output.data(), output.size()));
			});
			if (reaction == ServerReaction::DISCONNECT) {
				cancel();
				return reaction;
			}
			if (reaction == ServerReaction::OK) {
				if (int(_leftovers.size()) > parsed) {
					_leftovers = std::vector<char>(_leftovers.begin() + parsed, _leftovers.end());
				} else
					_leftovers.clear();
			}
			return reaction;
		}

		void readSome() {
			_socket.async_receive(_responseBuffer,
					[this] (std::error_code error, int length = 0) {
				if (error || length == 0 /* Means end of stream */) {
					return cancel();
				}

				auto startTime = std::chrono::steady_clock::now();
				_leftovers.insert(_leftovers.end(), _responseArray.begin(), _responseArray.begin() + length);
				ServerReaction reaction = ServerReaction::OK;
				while (!_leftovers.empty() && (reaction = readLeftovers()) == ServerReaction::OK) {}
				if (reaction == ServerReaction::DISCONNECT) {
					return cancel();
				}
				readSome();
				auto endTime = std::chrono::steady_clock::now();
				_parent->_totalResponseTime += std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
				_parent->_totalResponses++;
			});
		}

		void cancel() { // MUST RETURN AFTER CALLING cancel(), IT DESTROYS this
			_socket.close();
			_parent->destroySession(_index);
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
				_sessions.emplace_back(std::make_unique<Session>(std::move(socket), _responder, this, index));
				_sessions.back()->readSome();
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
	TcpServer(Responder* responder, int port, int threads)
			: _context(threads), _responder(responder), _endpoint(Net::ip::tcp::v4(), port) {
		startSession();
	}
	TcpServer(Responder* responder, int port)
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
	BackgroundTcpServer(Responder* responder, int port, int threads)
			: TcpServer<Responder>(responder, port, threads) {
		startWorker();
	}
	BackgroundTcpServer(Responder* responder, int port)
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
