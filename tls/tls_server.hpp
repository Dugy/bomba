#ifndef BOMBA_TLS_SERVER_HPP
#define BOMBA_TLS_SERVER_HPP

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "../bomba_core.hpp"
#endif
#ifndef BOMBA_TCP_SERVER_HPP
#include "../bomba_tcp_server.hpp"
#endif

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <vector>
#include <chrono>

// THIS DOESN'T WORK, THE SSL LIBRARY RETURNS A VAGUE ERROR

#include <iostream>

namespace Bomba {

template <typename Responder>
class TlsServer {
	Responder& _responder;
	boost::asio::io_context _context;
	boost::asio::ssl::context _sslContext = boost::asio::ssl::context(boost::asio::ssl::context::tlsv12_server);
	boost::asio::ip::tcp::endpoint _endpoint;
	boost::asio::ip::tcp::acceptor _acceptor = {_context, _endpoint};
	std::chrono::nanoseconds _totalResponseTime = std::chrono::nanoseconds(0);
	int64_t _totalResponses = 0;

	struct Session : ITcpServerSession {
		using SocketType = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
		SocketType _socket;
		typename Responder::Session _responder;
		TcpServerBuffer _buffer;
		boost::asio::mutable_buffer _space = {};
		TlsServer& _parent = nullptr;
		int _index = {};

		Session(SocketType&& socket, Responder& responder, TlsServer& parent, int index)
				: _socket(std::move(socket)), _responder(responder.getSession()), _parent(parent), _index(index) {
			_socket.async_handshake(boost::asio::ssl::stream_base::server, [this] (const boost::system::error_code& error) {
				if (error) {
					std::cout << "Handshake error: " << error.message() << std::endl;
					_socket.lowest_layer().cancel();
					return destroy();
				}
				readSome();
			});
		}

		void readSome() {
			std::span<char> space = _buffer.space();
			_space = boost::asio::mutable_buffer(space.data(), space.size());
			_socket.async_read_some(_space, [this] (std::error_code error, int length = 0) {
				auto startTime = std::chrono::steady_clock::now();

				bool expectingMore = _buffer.receive(*this, error, length);

				if (expectingMore) {
					readSome();
				}
				auto endTime = std::chrono::steady_clock::now();
				_parent._totalResponseTime += std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);

				if (!expectingMore) {
					return cancel();
				}

			});
		}

		void cancel() { // MUST RETURN AFTER CALLING cancel(), IT DESTROYS this
			_socket.lowest_layer().cancel();
			_socket.async_shutdown([this] (const boost::system::error_code& error) {
				if (error) {
					std::cout << "Shutdown error: " << error.message() << std::endl;
				}
				return destroy();
			});
		}

		void destroy() {
			_parent.destroySession(_index);
		}

		std::pair<ServerReaction, int> feedToResponder(std::span<char> data) override {
			return _responder.respond(data, [this] (std::span<const char> output) {
				boost::system::error_code error;
				boost::asio::write(_socket, boost::asio::buffer(output.data(), output.size()), error);
				if (error) {
					std::cout << "Write error: " << error.message() << std::endl;
					return cancel();
				}
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
		_acceptor.async_accept(_context, [&] (std::error_code error, boost::asio::ip::tcp::socket socket) {
			if (error) {
				std::cout << "Error: " << error.message() << std::endl;
				return;
			}
			{
				std::lock_guard lock(_sessionsLock);
				int index = _sessions.size();
				typename Session::SocketType sslSocket(std::move(socket), _sslContext);
				_sessions.emplace_back(std::make_unique<Session>(std::move(sslSocket), _responder, *this, index));
			}
			startSession();
		});
	}

	void destroySession(int index) {
		std::lock_guard lock(_sessionsLock);
		if (index < std::ssize(_sessions) - 1) [[likely]] {
			_sessions.back()->_index = index;
			std::swap(_sessions[index], _sessions.back());
		}
		_sessions.pop_back();
	}

public:
	TlsServer(Responder& responder, int port, int threads)
			: _context(threads), _responder(responder), _endpoint(boost::asio::ip::tcp::v4(), port) {
		_sslContext.set_options(boost::asio::ssl::context::default_workarounds
					| boost::asio::ssl::context::no_sslv2 | boost::asio::ssl::context::single_dh_use);
		_sslContext.set_password_callback( [] { return "secretPassword"; });
		_sslContext.use_certificate_chain_file("server.crt");
		_sslContext.use_private_key_file("server.key", boost::asio::ssl::context::pem);
		_sslContext.use_tmp_dh_file("dh512.pem");

		startSession();
	}
	TlsServer(Responder& responder, int port)
			: _responder(responder), _endpoint(boost::asio::ip::tcp::v4(), port) {
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
class BackgroundTlsServer : private TlsServer<Responder> {
	std::thread _worker;
	void startWorker() {
		_worker = std::thread([this] {
			TlsServer<Responder>::run();
		});
	}
public:
	BackgroundTlsServer(Responder& responder, int port, int threads)
			: TlsServer<Responder>(responder, port, threads) {
		startWorker();
	}
	BackgroundTlsServer(Responder& responder, int port)
			: TlsServer<Responder>(responder, port) {
		startWorker();
	}
	~BackgroundTlsServer() {
		TlsServer<Responder>::stopRunning();
		_worker.join();
	}

	std::chrono::nanoseconds averageResponseTime() {
		return TlsServer<Responder>::averageResponseTime();
	}
};

} // namespace Bomba

#endif // BOMBA_TLS_SERVER_HPP
