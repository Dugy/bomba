//usr/bin/g++ --std=c++20 -Wall $0 -g -o ${o=`mktemp`} && exec $o $*
#include <iostream>
#include <string>
#include "bomba_tcp_server.hpp"
#include "bomba_http.hpp"
#include "bomba_object.hpp"
#include "bomba_rpc_object.hpp"
#include "bomba_json_rpc.hpp"
#include "bomba_download_server.hpp"
#include "bomba_json_wsp_description.hpp"

struct RpcMessage : Bomba::Serialisable<RpcMessage> {
	std::string message = key<"message">;
	std::string author = key<"author">;
	bool nsfw = key<"nsfw">;
};

struct AdvancedRpcClass : Bomba::RpcObject<AdvancedRpcClass> {
	std::string message;

	Bomba::RpcMember<[] (AdvancedRpcClass* parent) {
		return parent->message;
	}> getMessage = child<"get_message">;

	Bomba::RpcMember<[] (AdvancedRpcClass* parent, RpcMessage newMessage = Bomba::name("message")) {
		parent->message = newMessage.message;
	}> setMessage = child<"set_message">;

	Bomba::RpcMember<[] (int first = Bomba::name("first"), int second = Bomba::name("second")) {
		return first + second;
	}> sum = child<"sum">;
	
	Bomba::RpcMember<[] (std::string message = Bomba::name("message"), bool important = Bomba::name("important")) {
		if (important)
			std::cout << "Notification: " << message << std::endl;
	}> notifyMe = child<"notify_me">;
};

int main(int argc, char** argv) {

//	AdvancedRpcClass method;
//	std::string description = describeInJsonWsp<std::string>(method, "bomba_experiment.com", "Bomba test");

//	std::unique_ptr<Bomba::FileServerBase> fileServer = [&] () -> std::unique_ptr<Bomba::FileServerBase> {
//		if (true || argc > 1 && std::string_view(argv[1]) == std::string_view("dynamic")) {
//			auto made = std::make_unique<Bomba::DynamicFileServer>("../public_html");
//			made->addGeneratedFile("api_description.json", true, [=] (Bomba::Callback<void(std::span<const char>)> writer) {
//				writer(std::span<const char>(description.begin(), description.end()));
//			});
//			return made;
//		} else {
//			auto made = std::make_unique<Bomba::CachingFileServer>("../public_html");
//			made->addGeneratedFile("api_description.json", description);
//			return made;
//		}
//	}();
//	Bomba::JsonRpcServer jsonRpc(method, *fileServer);
//	Bomba::TcpServer server(jsonRpc, 8080);
	Bomba::CachingFileServer cachingFileServer("public_html");
	Bomba::DummyPostResponder postResponder;
	Bomba::HttpServer http(cachingFileServer, postResponder);
	Bomba::TcpServer server(http, 8080);
	server.run();
}
