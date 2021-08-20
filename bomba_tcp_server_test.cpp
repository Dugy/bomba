//usr/bin/g++ --std=c++20 -Wall $0 -g -o ${o=`mktemp`} && exec $o $*
#include <iostream>
#include <string>
#include "bomba_tcp_server.hpp"
#include "bomba_http.hpp"
#include "bomba_rpc_object.hpp"
#include "bomba_json_rpc.hpp"
#include "bomba_caching_file_server.hpp"

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

	Bomba::CachingFileServer cachingFileServer("../public_html");
	Bomba::JsonRpcServer<std::string, Bomba::CachingFileServer> jsonRpc(&method, &cachingFileServer);
	Bomba::TcpServer server(&jsonRpc, 8080);
	server.run();
}
