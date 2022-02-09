# Bomba
C++20 library for convenient implementation of remote procedure calls and serialisation. It's not complete yet, but it's usable. There may be bugs, security issues or messy error handling.

To maximise convenience, it needs no code generation and no macros and is entirely header-only, yet its verbosity is at minimum. It is also usable without any additional libraries.

It's written in bleeding edge C\++20. Works on GCC 10, Clang 13 or MSVC 19. Before C\++23 reflection (`reflexpr` expression) is available, a GCC-only trick relying on Defect Report 2118 is used. Clang and MSVC have to use a different implementation with slightly worse performance. It's intended to be embedded-friendly as it can be used without any dynamic allocation when compiled by GCC and no dynamic allocation after initialisation if compiled with Clang or MSVC. Currently the only available implementation of the networking layer is based on `std::experimental::networking` and uses some dynamic allocation, so that part (about 130 lines) might need reimplementation if used outside PC architectures.

## Showcase
After setting up the components (examples are below), this is all the code needed to define an API call that allows remotely calling a lambda:
```C++
Bomba::RpcMember<[] (std::string message = Bomba::name("message"),
			bool important = Bomba::name("important")) {
	if (important)
		std::cout << "Notification: " << message << std::endl;
}> notifyMe = child<"notify_me">;
```

This code can be used both as server and as client, so that the client calls the server's lambda and ignores the contents of its own.

Assuming the path is correct and the feature is enabled, it can be called from a script (only a JavaScript implementation is available):
```JavaScript
#!/snap/bin/nodejs
const bomba = await import("./bomba.js");
const [api, types] = await bomba.loadApi("0.0.0.0:8080");
api.notify_me("If you see this, it's working.", true);
```

Connecting to it through the browser can automatically generate the code for calling `notify_me()`. To allow convenient access with minimum work, it can also generate a GUI like this:
![](screenshot.png)

## Features
Bomba is fully usable, but some additional features are planned.

### Protocols
Bomba implements several communication protocols for the purpose of communication in a standardised way supported by many other libraries. These are implemented in a way that avoids dynamic allocation, but can be added easily (except some parts that can't be used on special platforms anyway).
* HTTP - Minimal implementation, supporting only GET and POST, but usable as a web server with some interactive content
* JSON-RPC - Built on top of HTTP POST
* Binary - short header and binary-encoded data (not any standard format, but close enough to be easily modifiable to one)

Many other protocols should be possible to implement using the interfaces and concepts expected from protocols. They may be added in the future.

### Networking
Currently, the only implementation available uses `std::experimental::networking` version 1 for OS-independent networking without any dependencies. Because `std::experimental::networking` is not expected on heavily restrictive platforms, this part uses also some dynamic allocation (specifically `std::vector` for expandable buffers and to allocate instances).

Currently, there are two networking related classes:
* `Bomba::TcpServer` (header `bomba_tcp_server.hpp`)
	* expects a parser that would parse messages and determine if they are entirely received
	* stores incomplete messages
	* can handle large numbers of messages per second per thread
	* uses only a few kilobytes of memory per session
* `Bomba::SyncNetworkClient` (header `bomba_sync_client.hpp`)
	* Sends a request and returns a ticket that can be used to read a received response (if it's not received yet, it blocks until it's received)
	* It's possible to check if the response was already received, eliminating the need to block entirely

### Performance
As a side effect of restricting dynamic allocation for embedded-friendliness, the library has very good performance. JMeter reports about 60,000 HTTP requests per second singlethreaded on a laptop CPU with turbo boost disabled (which is about twice the performance of Nginx), but this is mainly a limit of the networking interface. Internally measured time to parse and respond to a request is lower. Under particularly favourable circumstances, the throughput can reach 1,000,000 packets per second per second singlethreaded. Calling a function via JSON-RPC adds about 1 microsecond to the processing time.

## Error handling
Common problems like incomplete requests in receive buffers are handled by returning enums for these kinds of calls, either alone or as part of `std::pair` or `std::tuple` with other values.

Less common problems like JSON parse errors are handled by exceptions. The class representing the toplevel protocol catches all exceptions and turns them into Error 500 responses. Other protocols are expected to handle their types of errors.

JSON-RPC handles all `std::exception` instances, converting them to JSON-RPC errors. The exact error code shown might not be completely accurate because the specification is not very clear about it and I chose to interpret it in a way that's the most convenient the implement.

The networking client throws an exception if the call fails so that an incorrect response is not returned. Thus, an exception in the server is propagated to the client (the exact type of exception will not be retained, obviously).

It was experimentally determined that an exception adds 5 - 10 microseconds the processing of a request, which is comparable to the time needed to process a request. I chose this option to optimise for successful calls and because of the atrocious verbosity of checking error codes after every function call.

If there is a very strong reason not to use exceptions, it's intended to be avoidable. All internal exceptions have their own functions that throw them so that functions could be replaced if some macros are set. This will not cause stack unwinding and is not debugged or tested yet.

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

The first template argument sets the data format. `BasicJson<>` makes it JSON. `BinaryProtocol<>` makes it binary, similar to reinterpret casting to pragmapacked structures, but with proper handling of dynamically sized structures like strings (header `bomba_binary_protocol.hpp`). The format is better described in section where [it's used for remote procedure calls](#binary-rpc-server).

To use different internal type than `std::string` for unescaping strings, set it as a second template argument. More on this is [here](#custom-string-type). To append the result of `serialise()` to an existing string, use its overload that accepts a reference to the output as argument. The output type is set by the second template argument, which defaults to the type of the first argument.

### Remote Procedure Call
You can define an RPC function by declaring this:
```C++
#include "bomba_rpc_object.hpp"
//...

Bomba::RpcLambda<[] (std::string newMessage = Bomba::name("message")) {
	std::cout << newMessage << std::endl;
}> coutPrinter;

//...
coutPrinter("Hello warm world!");
```

Depending on the runtime configuration, this will either call the lambda or send a request to the server, have it run the lambda there, send a response back and return the value output by the server.

This will trigger errors in compilers and linters that don't support lambdas in unevaluated expressions (from C++20).

This implements the `IRemoteCallable` interface that can be used both as a client and as a server. If it's used as a client, it behaves like a functor that calls the server's method and returns the result the server has sent. If it's used as a server, it can be called from a client. It is possible to nest these structures:
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
	}> printMessage = child<"cout_print">;
};
```

If an instance to a parent class is expected as the first argument, it acts like a member function. Otherwise, it is a static function. Both will appear in with the listed names in the parent's namespace in the RPC (e.g. if the parent is accessed as `storage` and the delimeter is a dot, then `setMessage` will be called as `storage.setMessage`).

It needs a workaround on Clang because it considers the parent class to be only forward declared at that point.

For the purposes of customisation, you can implement `IRemoteCallable` yourself, but it can be impractical without `RpcLambda` (`RpcMember` inherits from it to allow connecting it to `RpcObject`).

These can be called as follows:
```C++
messageStorage.setMessage("Don't forget the keycard");
messageStorage.printMessage("Do you have the keycard with you?");
std::string obtained = messageStorage.getMessage();
```

If the client supports it, these can be non-blocking. It's possible to obtain a `Bomba::Future` object templated to the return type that allows checking if the return value is already available with its `is_ready()` method and obtaining the values (blocking if not ready) with its `get()` method. This should be changed to `std::future` once its expanded version becomes part of the standard.
```C++
auto future1 = messageStorage.printMessage.async("Lazy to wait until it's printed");
auto future2 = messageStorage.getMessage.async();
std::string message = future2.get();
future1.get();
```


#### Objects composed at runtime
If the structure is more complex, it may be inconvenient to declare as one huge class. Because of this, there is an `RpcLambdaHolder` class that wraps around implementations of the `IRemoteCallable` interface. This allows using lambdas that have closures, composing the remote interface from program components and many other conveniences, at the cost of minor overhead.

The code needed to wrap a class:
```C++
RpcLambdaHolder sendMessage = [&] (std::string message = name("message"), std::string author = name("author")) {
	messages.push_back(message, author);
};
```

It takes moves the lambda and takes its ownership. If the lambda is larger than three words, the copy will be dynamically allocated (this is supposed to be done only on startup, so it should be fine for devices with little memory). The object can be accessed using `operator->()`.

The code to create an object with multiple methods like this:
```C++
DynamicRpcObject object;
object.add("send_message", std::move(sendMessage));
object.add("read_message", std::move(readMessage));
```
This will dynamically allocate. `DynamicRpcObjects` implements the `IRemoteCallable` interface. It can be stored in the `RpcLambdaHolder` class as well.

In order to call the function from your program, the type must be known. It is enabled by the `TypedRpcLambdaHolder` class that is used similarly to `std::function`, but inherits from `RpcLambdaHolder` and has its functionality:
```C++
TypedRpcLambdaHolder<void(std::string)> sendMessage = [&] (std::string message = name("message")) {
	messages.push(message);
};
```


There is a static helper function for creating a non-owning `RpcLambdaHolder` to allow putting typed functions into a `DynamicRpcObject` without losing the ability to call them:
```C++
RpcLambdaHolder borrowed = RpcLambdaHolder::nonOwning(*sendMessage);
```

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

Bomba::RpcStatelessLambda<[] (std::string newMessage = Bomba::name("sent"),
		int repeats = Bomba::name("repeats"), bool yell = Bomba::name("yell")) {
	for (int i = 0; i < repeats; i++) {
		if (yell)
			std::cout << "RECEIVED: " << newMessage << std::endl;
		else
			std::cout << "Received: " << newMessage << std::endl;
	}
}> method;
Bomba::HtmlPostResponder postResponder(method);

Bomba::HttpServer http(getResponder, postResponder);
Bomba::TcpServer server(http, 8080);
server.run();
```

#### POST-only server
This server can only respond to post request made by RPC calls, with no ability to serve a web page that could be used as a client.
```C++
Bomba::RpcStatelessLambda<[] (std::string newMessage = Bomba::name("sent"),
		int repeats = Bomba::name("repeats"), bool yell = Bomba::name("yell")) {
	for (int i = 0; i < repeats; i++) {
		if (yell)
			std::cout << "RECEIVED: " << newMessage << std::endl;
		else
			std::cout << "Received: " << newMessage << std::endl;
	}
}> method;
Bomba::HtmlPostResponder postResponder(method);
Bomba::DummyGetResponder getResponder;
Bomba::HttpServer http(getResponder, postResponder);
Bomba::TcpServer server(http, 8080);
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
Bomba::HttpServer http(cachingFileServer);
Bomba::TcpServer server(http, 8080);
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
Bomba::RpcGetResponder<std::string> getResponder(cachingFileServer, rpc);
Bomba::HttpServer http(getResponder);
Bomba::TcpServer server(http, 8080);
server.run();
```

The `RpcGetResponder` class is defined in `bomba_http.hpp` and can be replaced by a custom class that modifies the returned file accordingly to the return value of the request. However, doing this would result in creating some sort of PHP duplicate, which would not be a good practice. The page should use RPC requests to fill the content.

#### A basic JSON-RPC server
This example shows how to make a JSON-RPC server that has two methods. It doesn't do anything beyond that.
```C++
#include <string>
#include "bomba_tcp_server.hpp"
#include "bomba_rpc_object.hpp"
#include "bomba_json_rpc.hpp"

struct MessageKeeper : Bomba::RpcObject<MessageKeeper> {
	std::string message;

	Bomba::RpcMember<[] (AdvancedRpcClass* parent) {
		return parent->message;
	}> getMessage = child<"get_message">;

	Bomba::RpcMember<[] (AdvancedRpcClass* parent, std::string newMessage = Bomba::name("message")) {
		parent->message = newMessage;
	}> setMessage = child<"set_message">;
};
//...

MessageKeeper method;
Bomba::JsonRpcServer jsonRpcServer = {method};
Bomba::BackgroundTcpServer<decltype(jsonRpcServer)> server = {jsonRpcServer, 8080};
server.run();
```

If the response is larger than 1 kiB, it will dynamically allocate. See [here](#changing-buffer-size) how to change this behaviour.

#### A JSON-RPC server that also responds to GET requests
The `JsonRpcServer` class also accepts all the `getResponder` classes from earlier examples:
```C++
#include <string>
#include "bomba_tcp_server.hpp"
#include "bomba_rpc_object.hpp"
#include "bomba_json_rpc.hpp"

struct Summer : Bomba::RpcObject<Summer> {
	Bomba::RpcMember<[] (int first = Bomba::name("first"), int second = Bomba::name("second")) {
		return first + second;
	}> sum = child<"sum">;
};
//...

Summer method;
Bomba::CachingFileServer cachingFileServer("public_html");
Bomba::JsonRpcServer<std::string, Bomba::CachingFileServer> jsonRpcServer = {}method, cachingFileServer};
Bomba::BackgroundTcpServer<decltype(jsonRpcServer)> server = {jsonRpcServer, 8080};
server.run();
```

This again will dynamically allocate if the response is larger than 1 kiB, [here](#changing-buffer-size)'s how to change it. This does not apply to downloaded files from `CachingFileServer`, their size is known when writing the response header and there is no need to keep the entire response in memory.

#### A JSON-RPC server that can provide its documentation and web content
The JSON-RPC protocol does not specify a format for describing the API, so a similar protocol's documentation can be generated to describe the API in good detail.
```C++
#include <string>
#include "bomba_tcp_server.hpp"
#include "bomba_http.hpp"
#include "bomba_rpc_object.hpp"
#include "bomba_json_rpc.hpp"
#include "bomba_caching_file_server.hpp"
#include "bomba_json_wsp_description.hpp"

struct RpcClass : Bomba::RpcObject<RpcClass> {
	std::string message;

	Bomba::RpcMember<[] (AdvancedRpcClass* parent) {
		return parent->message;
	}> getMessage = child<"get_message">;

	Bomba::RpcMember<[] (AdvancedRpcClass* parent, std::string newMessage = Bomba::name("message")) {
		parent->message = newMessage;
	}> setMessage = child<"set_message">;
};

int main(int argc, char** argv) {
	RpcClass method;
	std::string description = describeInJsonWsp<std::string>(method, "keeping-message.com", "Bomba test");

	Bomba::CachingFileServer cachingFileServer("public_html");
	cachingFileServer.addGeneratedFile("api_description.json", description);
	Bomba::JsonRpcServer<std::string> jsonRpc(method, cachingFileServer);
	Bomba::TcpServer server(jsonRpc, 8080);
	server.run();
}
```

#### Binary RPC server
This server responds to binary-encoded requests. The argument names are not used.
```C++
Bomba::RpcStatelessLambda<[] (std::string newMessage = Bomba::name("sent"),
		int repeats = Bomba::name("repeats"), bool yell = Bomba::name("yell")) {
	for (int i = 0; i < repeats; i++) {
		if (yell)
			std::cout << "RECEIVED: " << newMessage << std::endl;
		else
			std::cout << "Received: " << newMessage << std::endl;
	}
}> method;
BinaryProtocolServer<> binaryServer = {method};
Bomba::TcpServer server(http, 8080);
server.run();
```
The performance benefit of using a binary format appears to be insignificant compared to the overhead of reading from TCP.

The binary format is relatively simple, somewhat similar to reinterpret casting a struct defined in a pragma pack:
* A function call starts with identifier, a 32 bit unsigned integer, size, a 16 bit integer (can be overriten with a template argument), then a sequence of 8 bit unsigned integers telling the indexes of the objects on the path to the target and a sequence of arguments the function takes
* Numeric types are the same as in the serialised object or function call, but normalised to little endian (can be overriden with a template argument)
* String is dynamically sized and prefixed by its size, written as a 16 bit unsigned integer (same type as message size)
* Array is prefixed with size (same type as string), then contains the given number of classes
* Key-value map is prefixed with size (same type as string) and contains pairs of string keys and values
* Objects (corresponding to C++ classes) are sequences of values, without keys
* Optional types and pointers are not supported (yet)

### Clients
Implementing a better client than `Bomba::SyncNetworkClient` might allow more functionality, but it should be good enough for many use cases.

#### Downloading a resource through HTTP
This will download and print the index page of anything served at `0.0.0.0:8080`. This is not the intended usage
```C++
#include "bomba_sync_client.hpp"
#include "bomba_http.hpp"
//...

Bomba::SyncNetworkClient client("0.0.0.0", "8080");
Bomba::HttpClient http(&client, "0.0.0.0");
auto identifier = http.get("/");
// Can do other stuff here, like send more requests
http.getResponse(identifier, [](std::span<char> response) {
	std::cout << "Page is:" << std::endl;
	std::cout << std::string_view(response.data(), response.size()) << std::endl;
	return true;
});
```

#### Calling an RPC method through HTTP
This will call a HTTP method of a [server](#post-only-server).
```C++
#include "bomba_sync_client.hpp"
#include "bomba_http.hpp"
#include "bomba_rpc_object.hpp"
//..

Bomba::RpcStatelessLambda<[] (std::string newMessage = Bomba::name("sent"),
		int repeats = Bomba::name("repeats"),
		bool yell = Bomba::name("yell") | Bomba::SerialisationFlags::OMIT_FALSE) {
	throw std::runtime_error("Not this one!"); // This lambda will not be called in the client
}> method;

Bomba::SyncNetworkClient client("0.0.0.0", "8080");
Bomba::HttpClient http(client, "0.0.0.0");
method.setResponder(&http);
method("A verÿ lông messäge.", 2, true);
```

It's possible to use `SyncNetworkClient` for non-blocking calls through the `async()` method. It works by checking what is in the receive buffer, it's not really asynchronous.
```C++
auto future = method.async("Hearken ye", 1, false);
// later
if (future.is_ready())
	future.get();
```

Note: This again will dynamically allocate if the response is larger than 1 kiB, [here](#changing-buffer-size)'s how to change it.

#### JSON-RPC client
This is the client counterpart for the [JSON-RPC server example](#a-basic-json-rpc-server):
```C++
#include <string>
#include "bomba_sync_client.hpp"
#include "bomba_rpc_object.hpp"
#include "bomba_json_rpc.hpp"

struct MessageKeeper : Bomba::RpcObject<MessageKeeper> {
	std::string message;

	Bomba::RpcMember<[] (AdvancedRpcClass* parent) {
		return parent->message;
	}> getMessage = child<"get_message">;

	Bomba::RpcMember<[] (AdvancedRpcClass* parent, std::string newMessage = Bomba::name("message")) {
		parent->message = newMessage;
	}> setMessage = child<"set_message">;
};
//...

MessageKeeper remote;
Bomba::SyncNetworkClient client("0.0.0.0", "8080");
Bomba::JsonRpcClient<> jsonRpc(remote, client, "0.0.0.0");

std::cout << remote.getMessage() << std::endl;
std::string newMessage;
std::getline(std::cin, newMessage);
remote.setMessage(newMessage);
```

#### Binary RPC client
This is the client counterpart for the [Binary RPC server example](#binary-rpc-server):
```C++
Bomba::RpcStatelessLambda<[] (std::string newMessage = Bomba::name("sent"),
		int repeats = Bomba::name("repeats"), bool yell = Bomba::name("yell")) {
	for (int i = 0; i < repeats; i++) {
		if (yell)
			std::cout << "RECEIVED: " << newMessage << std::endl;
		else
			std::cout << "Received: " << newMessage << std::endl;
	}
}> method;
Bomba::SyncNetworkClient client("0.0.0.0", "8080");
BinaryProtocolClient<> binaryClient = {method, client};

std::getline(std::cin, newMessage);
remote(newMessage, 1, true);
```

#### JavaScript client
This part assumes the server code [can provide its generated documentation](#a-json-rpc-server-that-can-provide-its-documentation-and-web-content) and needs the `bomba.js` file provided in the `public_html` folder.

```JavaScript
let bombaGenerator = await import("./bomba.js");
[bomba, bomba.types, bomba.serviceName] = await bombaGenerator.loadApi();
bomba.set_message("Some stuff");
console.log(bomba.get_message());
```

The functions provided by the server will be accessed through the `bomba` namespace (or however else you call it). The code will work in browsers out of the box. In Node.js, the `node-fetch` package is needed to access the `fetch` function that it's missing, the `--experimental-repl-await` flag to get access to `await` in the CLI and a correct path to the file. This can be used to give your program a scripting interface.

The other two arguments returned by the `loadApi()` function are a table of classes used in the API and the name of the service.

#### Web-based GUI
It's possible to generate a GUI to quickly give your program a convenient remote interface. It simply reflects the functions' signatures, nothing more advanced is implemented (not even CSS at the moment).

```JavaScript
let bombaGenerator = await import("./bomba.js");
[bomba, bomba.types, bomba.serviceName] = await bombaGenerator.loadApi();
document.getElementById("body").appendChild(bomba.gui());
```
To obtain a single function's GUI, you can call `bomba.set_message.gui()`. I plan to add more customisation to this.

A GUI that works out of the box is provided by the `index.html` file in the `public_html` folder, so you can have a quick and dirty GUI without editing anything but C++.

The GUI can be accessed by connecting to the program's port via browser.

### Changing some other behaviour
Although the library uses a lot of dependency injection allowing to replace components by different ones, it needs to create some types, so they are supplied as template arguments.

#### Custom string type
If you can't use `std::string` for some reasons (like restrictions regarding dynamic allocation), you can define your own string type (assuming it's called `String`) and serialise JSON as `Bomba::BasicJson<String>`. I recommend aliasing that type with `using`.

Your string type has to be convertible to `std::string_view`, needs to have a `clear()` method and needs to have the `+=` operator overloaded for `char` and `const char*`, similarly to `std::string`. Other traits are not needed.

#### Changing buffer size
RPC responses have a 1 kiB buffer by default, if a larger one is needed, it will be dynamically allocated. There is always a template argument that allows changing it.

Typically, you will want:
* `ExpandingBuffer<2048>` - 2 kiB size (uses `std::string` as buffer if larger)
* `ExpandingBuffer<2048, std::vector<char>>` - 2 kiB size, uses `std::vector` as underlying storage (you might want to put something with a custom allocator there)
* `NonExpandingBuffer<2048>` - 2 kiB size, larger responses will be truncated (default size is 1024)

To make `HttpServer` use it, declare it as:
```C++
Bomba::HttpServer<Bomba::NonExpandingBuffer<2048>> jsonRpcServer = {&method};
```
The `JsonRpcServer` class will create its own `HttpServer` with the buffer type determined by the value of its second template argument (i.e. `JsonRpcServer<std::string, NonExpandingBuffer<2048>>`).

This does not affect cases where a resource with already known size is downloaded, because it doesn't need to keep the header in memory.

### Custom components
Bomba is designed with modularity in mind and almost any layer can be replaced by a different component. Parts that can be replaced by reimplementing an interface differently:
* Data format
* Message format
* Network protocol

#### Custom object encoding
A format should be a `struct` with two subobjects, `Input` and `Output`, implementing interfaces `IStructruredInput` and `IStructuredOutput` respectively. It's intended that some adjustments to the format could be supplied to it as template arguments to the outer struct.

It's clear that not all formats support everything defined by the interface, but an interface supporting only numbers and strings is good enough for serialising numbers and strings.

These interfaces are defined in `bomba_core.hpp` with comments explaining how to implement them. Their methods take flags as arguments (declared in `bomba_core.hpp`), which specify some details that are needed only by some implementations.

These interfaces are quite low level and are not meant to be used outside of higher level abstractions. The `Serialisable` class hides them completely and also adds appropriate flags.

The format is abstracted roughly in the style of variables of dynamically typed variables:
* Floating point number
* Integer
* Boolean
* String
* Null
* Array of variables (can be different types, but it would be impractical to use)
* String-indexed table of variables (can map to a C++ object or an map-like type)
* Optional (currently not implemented well, but works for JSON)

#### Custom message format
To implement a server, it's necessary to create a class that has a public method called `getSession()` that returns an object implementing the `ITcpResponder` interface. This interface must have a `respond()` method that parses incoming data, uses a callback to send responses back and return whether the communication is okay and how many bytes it read will not need again. This interface is defined and better explained in `bomba_core.hpp`.

To implement a client, it's necessary to implement the `IRpcResponder` interface defined in `bomba_core.hpp`.

#### Custom network protocol
The part of the server code that responds to input is defined by the `ITcpResponder` interface. A custom implementation can use it differently. For UDP, it's better to change it together with the message format. It's defined in `bomba_core.hpp`.

A client is somewhat harder to implement because responses might not arrive in the order they are requested. It needs to implement the `ITcpClient` interface, which requires an ability to identify which response is the required one and let the calling code keep the others. It's defined in `bomba_core.hpp`.