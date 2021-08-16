#ifndef BOMBA_SYNC_CLIENT
#define BOMBA_SYNC_CLIENT

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif

#include <experimental/net>
#include <vector>
#include <string>
#include <unordered_map>

namespace Bomba {

namespace Net = std::experimental::net;
using NetStringView = std::experimental::fundamentals_v1::basic_string_view<char>;

class SyncNetworkClient : public ITcpClient {
	Net::io_context _ioContext;
	Net::ip::tcp::socket _socket = Net::ip::tcp::socket{_ioContext};
	Net::ip::tcp::resolver _resolver = Net::ip::tcp::resolver{_ioContext};
	Net::ip::basic_endpoint<Net::ip::tcp> _server;
	std::unordered_map<RequestToken, std::vector<char>> _responses;
	std::vector<char> _leftovers;
	
	void connect() {
		_socket.connect(_server);
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
		while (true) {
			std::error_code error;
			if constexpr(!doWait) {
				if (_socket.available() == 0)
					break;
			}
			auto received = _socket.read_some(Net::buffer(responseBuffer), error);
			if (error) {
				remoteError(error.message().c_str());
				break;
			}
			_leftovers.insert(_leftovers.end(), responseBuffer.begin(), responseBuffer.begin() + received);
			while (!_leftovers.empty()) {
				auto [reaction, tokenReceived, position] = reader(_leftovers, false);
				if (reaction == ServerReaction::OK || reaction == ServerReaction::DISCONNECT)
					return;
				else if (reaction == ServerReaction::WRONG_REPLY) {
					_responses.insert(std::make_pair(tokenReceived, std::vector<char>(_leftovers.begin(), _leftovers.begin() + position)));
					if (!_leftovers.empty())
						_leftovers = std::vector<char>(_leftovers.begin() + position, _leftovers.end());
				} // else read on
			}
		}
	}

public:
	SyncNetworkClient(std::string server, std::string protocol)
			: _server(*_resolver.resolve(server, protocol).begin()) {
	}
	
	void writeRequest(std::span<char> written) override {
		if (!_socket.is_open())
			connect();
		while (written.size() > 0) {
			auto inSocket = _socket.write_some(Net::buffer(NetStringView(written.data(), written.size())));
			written = std::span<char>(written.begin() + inSocket, written.end());
		}
	}
	
	void getResponse(RequestToken token, const Callback<std::tuple<ServerReaction, RequestToken, int64_t>
					(std::span<char> input, bool identified)>& reader) override {
		if (!_socket.is_open())
			connect();
		searchRequests<true>(token, reader);
	}

	void tryToGetResponse(RequestToken token, const Callback<std::tuple<ServerReaction, RequestToken, int64_t>
					(std::span<char> input, bool identified)>& reader) override {
		searchRequests<false>(token, reader);
	}
};

} // namespace Bomba
#endif // BOMBA_SYNC_CLIENT
