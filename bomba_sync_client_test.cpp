#include <iostream>
#include "bomba_sync_client.hpp"
#include "bomba_http.hpp"
#include "bomba_rpc_object.hpp"
#include "bomba_json_rpc.hpp"

struct AdvancedRpcClass : Bomba::RpcObject<AdvancedRpcClass> {
	std::string message;

	Bomba::RpcMember<[] (AdvancedRpcClass* parent) {
		return parent->message;
	}> getMessage = child<"get_message">;

	Bomba::RpcMember<[] (AdvancedRpcClass* parent, std::string newMessage = Bomba::name("message")) {
		parent->message = newMessage;
	}> setMessage = child<"set_message">;

	Bomba::RpcMember<[] (int first = Bomba::name("first"), int second = Bomba::name("second")) {
		return first + second;
	}> sum = child<"sum">;
};

int main(int argc, char** argv) {
	AdvancedRpcClass method;

	Bomba::SyncNetworkClient client("0.0.0.0", "8080");
	Bomba::JsonRpcClient<> jsonRpc(&method, &client, "0.0.0.0");
	method.setResponder(&jsonRpc); // TODO: Why am I setting this twice?

	std::cout << method.getMessage() << std::endl;
	std::string newMessage = "Dread";
	std::getline(std::cin, newMessage);
	method.setMessage(newMessage);
}
