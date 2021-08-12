#include <iostream>
#include "bomba_sync_client.hpp"
#include "bomba_http.hpp"
#include "bomba_rpc_object.hpp"

int main(int argc, char** argv) {
	Bomba::RpcLambda<[] (std::string newMessage = Bomba::name("sent"),
			int repeats = Bomba::name("repeats"),
			bool yell = Bomba::name("yell") | Bomba::SerialisationFlags::OMIT_FALSE) {
		throw std::runtime_error("Not this one!"); // This lambda will not be called by the client
	}> method;

	Bomba::SyncNetworkClient client("0.0.0.0", "8080");
	Bomba::HttpClient<> http(&client, "0.0.0.0");
	auto identifier = http.get("/");
	http.getResponse(identifier, Bomba::makeCallback([](std::span<char> response) {
		std::cout << "Page is:" << std::endl;
		std::cout << std::string_view(response.data(), response.size()) << std::endl;
		return true;
	}));

	method.setResponder(&http);
	method("A verÿ lông messäge.", 2, true);
}
