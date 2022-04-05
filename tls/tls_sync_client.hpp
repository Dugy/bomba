#ifndef BOMBA_SYNC_CLIENT
#define BOMBA_SYNC_CLIENT

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "../bomba_core.hpp"
#endif

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <vector>
#include <string>
#include <unordered_map>

namespace Bomba {

class TlsSyncClient : public ITcpClient {
	boost::asio::io_context _ioContext;
	boost::asio::ip::tcp::resolver _resolver = boost::asio::ip::tcp::resolver(_ioContext);
	boost::asio::ssl::context _sslContext = boost::asio::ssl::context(boost::asio::ssl::context::tls);
	using SocketType = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
	SocketType _socket = SocketType(_ioContext, _sslContext);
	boost::asio::ip::basic_endpoint<boost::asio::ip::tcp> _server;
	std::unordered_map<RequestToken, std::vector<char>> _responses;
	std::vector<char> _leftovers;
	
	void connect() {
		_socket.next_layer().connect(_server);
		_socket.handshake(boost::asio::ssl::stream_base::client);
	}

	void ensureConnected() {
		if (!_socket.next_layer().is_open())
			connect();
	}

	template <bool doWait, typename Reader>
	void searchRequests(RequestToken tokenSought, Reader&& reader) {
		// See if some response was already received
		auto found = _responses.find(tokenSought);
		if (found != _responses.end()) {
			auto [reaction, token, position] = reader(found->second, true);
			if (reaction == ServerReaction::OK || reaction == ServerReaction::DISCONNECT)
				return;
			if (reaction == ServerReaction::READ_ON)
				logicError("First it was WRONG_REPLY, now it is READ_ON?");
		}

		// Wait for a reply if none was received before
		std::array<char, 2048> responseBuffer;
		bool wasReceived = false;
		while (!wasReceived) {
			std::error_code error;
			if constexpr(!doWait) {
				if (_socket.next_layer().available() == 0)
					break;
			}
			auto received = _socket.read_some(boost::asio::buffer(responseBuffer));
			if (error) {
				remoteError(error.message().c_str());
				break;
			}
			_leftovers.insert(_leftovers.end(), responseBuffer.begin(), responseBuffer.begin() + received);
			while (!_leftovers.empty()) {
				auto [reaction, tokenReceived, position] = reader(_leftovers, false);
				if (reaction == ServerReaction::OK)
					wasReceived = true;
				else if (reaction == ServerReaction::DISCONNECT || reaction == ServerReaction::READ_ON) {
					return;
				} else if (reaction == ServerReaction::WRONG_REPLY) {
					_responses.insert(std::make_pair(tokenReceived, std::vector<char>(_leftovers.begin(), _leftovers.begin() + position)));
				} // else read on
				_leftovers = std::vector<char>(_leftovers.begin() + position, _leftovers.end());
			}
		}
	}

public:
	TlsSyncClient(std::string_view server, int port = 443)
		: _server(*_resolver.resolve(server, std::to_string(port)).begin()) {
	}
	
	void writeRequest(std::span<char> written) override {
		if (!_socket.next_layer().is_open())
			connect();
		while (written.size() > 0) {
			auto inSocket = _socket.write_some(boost::asio::buffer(written.data(), written.size()));
			written = std::span<char>(written.begin() + inSocket, written.end());
		}
	}
	
	void getResponse(RequestToken token, Callback<std::tuple<ServerReaction, RequestToken, int64_t>
					(std::span<char> input, bool identified)> reader) override {
		ensureConnected();
		searchRequests<true>(token, reader);
	}

	void tryToGetResponse(RequestToken token, Callback<std::tuple<ServerReaction, RequestToken, int64_t>
					(std::span<char> input, bool identified)> reader) override {
		ensureConnected();
		searchRequests<false>(token, reader);
	}
};

} // namespace Bomba
#endif // BOMBA_SYNC_CLIENT
