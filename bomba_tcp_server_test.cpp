//usr/bin/g++ --std=c++20 -Wall $0 -g -o ${o=`mktemp`} && exec $o $*
#include <iostream>
#include <string>
#include "bomba_tcp_server.hpp"
#include "bomba_http.hpp"
#include "bomba_rpc_object.hpp"
#include "bomba_caching_file_server.hpp"

int main(int argc, char** argv) {
	Bomba::RpcLambda<[] (std::string newMessage = Bomba::name("sent"),
			int repeats = Bomba::name("repeats"), bool yell = Bomba::name("yell")) {
		for (int i = 0; i < repeats; i++) {
			if (yell)
				std::cout << "RECEIVED: " << newMessage << std::endl;
			else
				std::cout << "Received: " << newMessage << std::endl;
		}
	}> method;
	Bomba::CachingFileServer cachingFileServer("../public_html");
	Bomba::RpcGetResponder<std::string, Bomba::CachingFileServer> getResponder(&cachingFileServer, &method);
	Bomba::HtmlPostResponder postResponder(&method);

	Bomba::HttpServer http(&getResponder, &postResponder);
	Bomba::TcpServer server(&http, 8080);
	server.run();
}
