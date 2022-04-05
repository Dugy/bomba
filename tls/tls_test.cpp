#include <iostream>
#include "tls_sync_client.hpp"
#include "tls_server.hpp"

#include <iostream>
#include "../bomba_http.hpp"

int main(int, char**) {
	constexpr std::string_view target = "duckduckgo.com";
	Bomba::TlsSyncClient client(target);
	Bomba::HttpClient http(client, target);

	auto response = http.get();
	http.getResponse(response, [&] (std::span<char> response, bool) {
		std::cout << std::string_view(response.data(), response.size()) << std::endl;
		return true;
	});


//	Bomba::SimpleGetResponder getResponder;
//	getResponder.resource = page;
//	Bomba::HttpServer<> httpServer = {getResponder};
//	Bomba::TlsServer<decltype(httpServer)> server = {httpServer, 8901}; // Very unlikely this port will be used for something
//	server.run();
}

