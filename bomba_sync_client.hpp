#ifndef BOMBA_SYNC_CLIENT
#define BOMBA_SYNC_CLIENT

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif

#include <experimental/net>
#include <vector>
#include <string>

namespace Bomba {

namespace Net = std::experimental::net;
using NetStringView = std::experimental::fundamentals_v1::basic_string_view<char>;

class SyncNetworkClient : public ITcpClient {
	bool _okay = true;
	Net::io_context _ioContext;
	Net::ip::tcp::socket _socket = Net::ip::tcp::socket{_ioContext};
	Net::ip::tcp::resolver _resolver = Net::ip::tcp::resolver{_ioContext};
	Net::ip::basic_endpoint<Net::ip::tcp> _server;
	std::vector<std::vector<char>> _responses;
	
	void connect() {
		_socket.connect(_server);
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

		// See if some response was already received
		for (auto& response : _responses) {
			auto [reaction, token, position] = reader(response, true);
			if (reaction == ServerReaction::OK || reaction == ServerReaction::DISCONNECT)
				return;
			if (reaction == ServerReaction::READ_ON)
				logicError("First it was WRONG_REPLY, now it is READ_ON?");
		}
		
		// Wait for a reply if none was received before
		std::array<char, 2048> responseBuffer;
		std::vector<char> leftovers;
		while (true) {
			std::error_code error;
			auto received = _socket.receive(Net::buffer(responseBuffer), error);
			if (error) {
				remoteError(error.message().c_str());
				break;
			}
			leftovers.insert(leftovers.end(), responseBuffer.begin(), responseBuffer.begin() + received);
			auto [reaction, token, position] = reader(leftovers, true);
			if (reaction == ServerReaction::OK || reaction == ServerReaction::DISCONNECT)
				return;
			else if (reaction == ServerReaction::WRONG_REPLY) {
				_responses.emplace_back(std::vector<char>(leftovers.begin(), leftovers.begin() + position));
				if (!leftovers.empty())
					leftovers = std::vector<char>(leftovers.begin() + position, leftovers.end());
			} // else read on
		}
	}
};

} // namespace Bomba
#endif // BOMBA_SYNC_CLIENT
