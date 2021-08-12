# Bomba
C++20 library for convenient implementation of remote procedure calls and serialisation. Currently, it's a work in progress, not all intended components are implemented, there may be security issues or messy error handling.

For maximal convenience, it needs no code generation and no macros and is entirely header-only, yet its verbosity is at minimum.

It's written in bleeding edge C\++20. Works on GCC 11, Clang 12 or MSVC 19. Before C\++23 reflection (`reflexpr` expression) is available, a GCC-only trick relying on Defect Report 2118 is used. Clang and MSVC have to use a different implementation with slighly worse performance. It's embedded-friendly as it can be used without any dynamic allocation when compiled by GCC and it needs dynamic allocation only during initialisation if compiled with Clang or MSVC). Currently the only available implementation of the networking layer is based on `std::experimental::networking` and uses some dynamic allocation, so it's PC-only.

## Features
Bomba is not feature complete, but is already usable for some purposes.

### Protocols
Bomba implements several communication protocols for the purpose of communication in a standardised way supported by many other libraries. These are implemented in a way that avoids dynamic allocation, expecting the networking layer to make the necessary allocations (so that some buffers could be provided in a restrictive embedded environment).
* Http - Minimal implementation, supporting only GET and POST, but usable as a web server with some interactive content
* JSON-RPC - Not properly tested yet

Many other protocols should be possible to implement using the interfaces and concepts expected from protocols.

### Networking
Currently, the only implementation available uses `std::experimental::networking` version 1 for OS-independent networking without any dependencies. Because `std::experimental::networking` is not expected on heavily restrictive platforms, it uses also some dynamic allocation (specifically `std::vector` for resizable buffers and to allocate instances).

Currently, there are two networking related classes:
* `Bomba::TcpServer` (header `bomba_tcp_server.hpp`)
	* expects a parser that would parse messages and determine if they are entirely received
	* stores incomplete messages
	* can handle large numbers of messages per second per thread
	* uses only a few kilobytes of memory per session
* `Bomba::SyncNetworkClient` (header `bomba_sync_client.hpp`)
	* Sends a request and returns a ticket that can be used to read a received response (if it's not received yet, it blocks until it's received)

### Performance
I have not optimised the code much yet.

When testing the HTTP server component, the JMeter tool reported about 33000 responses to requests per second from a singlethreaded server (which is similar performance to Nginx), but it appeared to be more of a JMeter limitation. The server internally reported an average request processing time about 5800 nanoseconds, but this time could be greater because it did not track the overhead caused by the OS.

## Error handling
Common problems like incomplete requests in receive buffers are handled by returning enums for these kinds of calls, either alone or as part of `std::pair` or `std::tuple` with other values.

Less common problems like parse errors are handled by exceptions. The class representing the protocol (currently only HTTP) catches all exceptions and turns them into Error 500 responses. Other protocols are expected to handle their types of errors.

The networking client throws an exception if the call fails so that an incorrect response is not returned. Thus, an exception in the server is propagated to the client (the exact type of exception will not be retained, obviously)

It was experimentally determined that an exception adds 5 - 10 microseconds the processing of a request, which does not lead to a drastic slowdown. This choice is based on optimisation for successful calls and the atrocious verbosity of checking error codes after every function call.

Exceptions are always called by functions that throw them and these functions can be replaced if some macros are set. This will not cause stack unwinding and is not debugged or tested yet.

## Usage

### Serialisation
Define a serialisable class:
```C++
#include "bomba_object.hpp"
//...

struct Point : Bomba::Serialisable<Point> {
	float x = key<"x"> = 1;
	float y = key<"y"> = 1;
	bool visible = key<"visible"> = true;
};
```
This creates a class with two `float` members initialised to `1` by default and one `bool` member initialised to `false`. It provides the serialisation functionality through an `ISerialisable` interface.

Then it can be serialised and deserialised into string using this:
```C++
#include "bomba_json.hpp"
//...
// assuming there is an object point of class Point we want to serialise
std::string written = point.serialise<Bomba::BasicJson<>>();

// now, deserialising from a std::string instance named reading
point.deserialise<Bomba::BasicJson<>>(reading);
```

### Remote Procedure Call
You can define an RPC function by declaring this:
```C++
Bomba::RpcLambda<[] (std::string newMessage = Bomba::name("message")) {
	std::cout << newMessage << std::endl;
}> coutPrinter;
```

This doesn't work on Clang because it doesn't support lambdas in unevaluated contexts yet. This implements the `IRemoteCallable` interface that can be used both as a client and as a server. If it's used as a client, it behaves like a functor that calls the server's method and returns the result the server has sent. If it's used as a server, it can be called from a client. It is possible to nest these structures:
```C++
#include "bomba_rpc_object.hpp"
//...

struct MessageStorage : RpcObject<MessageStorage> {
	std::string message;

	RpcMember<[] (MessageStorage* parent) {
		return parent->message;
	}> getMessage = child<"get_message">;

	RpcMember<[] (MessageStorage* parent, std::string newMessage = name("message")) {
		parent->message = newMessage;
	}> setMessage = child<"set_message">;

	RpcMember<[] (std::string newMessage = name("message")) {
		std::cout << newMessage << std::endl;
	}> setMessage = child<"cout_print">;
};
```

If an instance to a parent class is expected as the first argument, it acts like a member function. Otherwise, it is a static function. Both will appear in with the listed names in the parent's namespace in the RPC (e.g. if the parent is accessed as `storage` and the delimeter is a dot, then `setMessage` will be called as `storage.setMessage`).

For the purposes of customisation, you can implement `IRemoteCallable` yourself, but it can be impractical without `RpcLambda` (`RpcMember` inherits from it to allow connecting it to `RpcObject`).

### Servers
There are multiple ways to implement web servers, depending on the intended functionality

#### Simple interactive content
Here is an example how to make a server that serves a single HTML page and reacts to HTML Forms sent:
```C++
#include "bomba_tcp_server.hpp"
#include "bomba_http.hpp"
#include "bomba_rpc_object.hpp"
//...

constexpr std::string_view page =
R"~(<!DOCTYPE html><html>
<head>
	<title>Bomba Placeholder</title>
	<style>
		.general{
			text-align:center;
			font-family:Helvetica;
		}
	</style>
</head>
<body>
	<h1 class="general">Placeholder webpage served by Bomba</h1>
	<form method="post" action="/" class="general" >
		It's actually interactive - you can write into <code>std::cout</code>!<br>
		Text:&nbsp;<input type="text" name="sent"/><br>
		Repeats:&nbsp;<input type="number" name="repeats" value="1" min="0" max="5" /><br>
		<input type="checkbox" id="yell" name="yell" value="bla"/>
			<label for="yell">Yell</label><br>
		<input type="submit" value="Write" />
	</form>
</body></html>)~";
Bomba::SimpleGetResponder getResponder = { page };

Bomba::RpcLambda<[] (std::string newMessage = Bomba::name("sent"),
		int repeats = Bomba::name("repeats"), bool yell = Bomba::name("yell")) {
	for (int i = 0; i < repeats; i++) {
		if (yell)
			std::cout << "RECEIVED: " << newMessage << std::endl;
		else
			std::cout << "Received: " << newMessage << std::endl;
	}
}> method;
Bomba::HtmlPostResponder postResponder(&method);

Bomba::HttpServer http(&getResponder, &postResponder);
Bomba::TcpServer server(&http, 8080);
server.run();
```

#### POST-only server
This server can only respond to post request made by RPC calls, with no ability to serve a web page that could be used as a client.
```C++
Bomba::RpcLambda<[] (std::string newMessage = Bomba::name("sent"),
		int repeats = Bomba::name("repeats"), bool yell = Bomba::name("yell")) {
	for (int i = 0; i < repeats; i++) {
		if (yell)
			std::cout << "RECEIVED: " << newMessage << std::endl;
		else
			std::cout << "Received: " << newMessage << std::endl;
	}
}> method;
Bomba::HtmlPostResponder postResponder(&method);
Bomba::HttpServer http(nullptr, &postResponder);
Bomba::TcpServer server(&http, 8080);
server.run();
```

#### Serving a folder
This is an example how to provide the contents of a folder (named `public_html` in this example) as a website. The main page is expected to be called `index.html`.
```C++
#include "bomba_tcp_server.hpp"
#include "bomba_http.hpp"
#include "bomba_caching_file_server.hpp"
//...

Bomba::CachingFileServer cachingFileServer("public_html");
Bomba::HttpServer http(&cachingFileServer);
Bomba::TcpServer server(&http, 8080);
server.run();
```

#### Switching page after each request
This example shows how to make a server that responds to an RPC call through HTML GET (`http://0.0.0.0:8080/count_print.html?message=Hello`) and redirects to a page with the same name as the endpoint (which would be `public_html/cout_print.html`), which may contain something about the message being received.
```C++
#include "bomba_tcp_server.hpp"
#include "bomba_http.hpp"
#include "bomba_rpc_object.hpp"
#include "bomba_caching_file_server.hpp"
//...

struct Rpc : RpcObject<Rpc> {
	RpcMember<[] (std::string message = name("message")) {
		std::cout << "Received: " << message << std::endl;
	}> setMessage = child<"cout_print.html">;
};
Bomba::CachingFileServer cachingFileServer("public_html");
Bomba::RpcGetResponder<std::string, Bomba::CachingFileServer> getResponder(&cachingFileServer, &rpc);
Bomba::HttpServer http(&getResponder);
Bomba::TcpServer server(&http, 8080);
server.run();
```

The `RpcGetResponder` class is defined in `bomba_http.hpp` and can be replaced by a custom class that modifies the returned file accordingly to the return value of the request. However, doing this would result in creating some sort of PHP duplicate, which would not be a good practice. A better feature for doing such a thing is planned.

### Clients
Implementing a better client than `Bomba::SyncNetworkClient` might allow more functionality, but it should be good enough for convenient synchronous requests.

#### Downloading a resource through HTTP
This will download and print the index page of anything served at `0.0.0.0:8080`. This is not the intended usage
```C++
#include "bomba_sync_client.hpp"
#include "bomba_http.hpp"
//...

Bomba::SyncNetworkClient client("0.0.0.0", "8080");
Bomba::HttpClient<> http(&client, "0.0.0.0");
auto identifier = http.get("/");
// Can do other stuff here, like send more requests
http.getResponse(identifier, Bomba::makeCallback([](std::span<char> response) {
	std::cout << "Page is:" << std::endl;
	std::cout << std::string_view(response.data(), response.size()) << std::endl;
	return true;
}));
```

### Calling an RPC method through HTTP
This will call a HTTP method of a [server](#post-only-server).
```C++
#include "bomba_sync_client.hpp"
#include "bomba_http.hpp"
#include "bomba_rpc_object.hpp"
//..

Bomba::RpcLambda<[] (std::string newMessage = Bomba::name("sent"),
		int repeats = Bomba::name("repeats"),
		bool yell = Bomba::name("yell") | Bomba::SerialisationFlags::OMIT_FALSE) {
	throw std::runtime_error("Not this one!"); // This lambda will not be called in the client
}> method;

Bomba::SyncNetworkClient client("0.0.0.0", "8080");
Bomba::HttpClient<> http(&client, "0.0.0.0");
method.setResponder(&http);
method("A verÿ lông messäge.", 2, true);
```

### Custom string type
If you can't use `std::string` for some reasons (like restrictions regarding dynamic allocation), you can define your own string type (assuming it's called `String`) and serialise JSON as `Bomba::BasicJson<String>`. I recommend aliasing that type with `using`.

Your string type has to be convertible to `std::string_view`, needs to have a `clear()` method and needs to have the `+=` operator overloaded for `char` and `const char*`, similarly to `std::string`. Other traits are not needed.

