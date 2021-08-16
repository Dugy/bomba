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
	method.setResponder(&http);

	std::string line;
	while (std::getline(std::cin, line)) {
		method(line,  1, false);
	}
}
