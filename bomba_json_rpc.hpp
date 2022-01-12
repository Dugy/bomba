#ifndef BOMBA_JSON_RPC
#define BOMBA_JSON_RPC

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif
#ifndef BOMBA_JSON
#include "bomba_json.hpp"
#endif
#ifndef BOMBA_HTTP
#include "bomba_http.hpp"
#endif

#include <array>
#include <string_view>
#include <charconv>
#include <sstream>
#include <cstring>
#include <cmath>

namespace Bomba {

enum class JsonRpcError {
	PARSE_ERROR = -32700,
	INVALID_REQUEST = -32600,
	METHOD_NOT_FOUND = -32601,
	INVALID_PARAMS = -32602,
	INTERNAL_ERROR = -32603,
};

template <BetterAssembledString LocalStringType = std::string>
class JsonRpcServerProtocol : public IHttpPostResponder {
	IRemoteCallable& _callable;
	using Json = BasicJson<LocalStringType, GeneralisedBuffer>;

	bool respondInternal(IStructuredInput& input, IStructuredOutput& output, Callback<> onResponseStarted) {
		constexpr auto noFlags = Json::Output::Flags::NONE;
		bool failed = false;
		bool responding = false;
		IStructuredInput::Location paramsPosition;
		bool idWritten = false;

		auto writeId = [&] {
			responding = true;
			onResponseStarted();
			output.startWritingObject(noFlags, 3);
			output.introduceObjectMember(noFlags, "jsonrpc", 0);
			output.writeString(noFlags, "2.0");
			output.introduceObjectMember(noFlags, "id", 1);

			auto identified = input.identifyType(noFlags);
			if (identified == IStructuredInput::TYPE_INTEGER)
				output.writeInt(noFlags, input.readInt(noFlags));
			else if (identified == IStructuredInput::TYPE_STRING)
				output.writeString(noFlags, input.readString(noFlags));
			else if (identified == IStructuredInput::TYPE_FLOAT)
				output.writeFloat(noFlags, input.readFloat(noFlags));
			else if (identified == IStructuredInput::TYPE_NULL) {
				input.readNull(noFlags);
				output.writeNull(noFlags);
			} else {
				input.skipObjectElement(noFlags);
				output.writeNull(noFlags);
			}
		};
		auto introduceError = [&] (std::string_view message, JsonRpcError errorType = JsonRpcError::INTERNAL_ERROR) {
			failed = true;
			if (!responding)
				return;

			output.introduceObjectMember(noFlags, "error", 2);
			auto errorWritten = output.writeObject(2);
			errorWritten.writeInt("code",  int(errorType));
			errorWritten.writeString("message", message);
		};

		try {
			const IRemoteCallable* method = nullptr;
			bool called = false;
			auto call = [&] () {
				auto introduceResult = [&] () {
					if (responding)
						output.introduceObjectMember(noFlags, "result", 2);
				};
				auto formerPosition = input.storePosition(noFlags);
				if (method) [[likely]] {
					called = true; // If it throws, it's still considered called
					if (paramsPosition) {
						input.restorePosition(noFlags, paramsPosition);
					}
					if (responding)
						method->call(paramsPosition ? &input : nullptr, output, introduceResult, introduceError);
					else {
						NullStructredOutput nullOutput;
						method->call(paramsPosition ? &input : nullptr, nullOutput, introduceResult, introduceError);
					}
				}
				return formerPosition;
			};

			// The logic that must be upheld:
			// * every loop advances by 1 entry (if position is modified, it must be restored)
			// * finding id starts the response, finding params stores their position and finding method causes its lookup
			// * it's called immediately when method, params and id are all known
			// * if something is missing, it's called after the loop
			input.readObject(noFlags, [&] (std::optional<std::string_view> nextName, int) {
				if (*nextName == "jsonrpc") {
					if (input.readString(noFlags) != "2.0") [[unlikely]] {
						introduceError("Unknown JSON-RPC version", JsonRpcError::INVALID_REQUEST);
						return false;
					}
				} else if (*nextName == "id") {
					if (!called) {
						writeId();
						if (method && paramsPosition) {
							input.restorePosition(noFlags, call());
						}
					} else
						input.skipObjectElement(noFlags);
				} else if (*nextName == "method") {
					if (!method) {
						auto path = input.readString(noFlags);
						method = PathWithSeparator<".", LocalStringType>::findCallable(path, &_callable);
						if (!method) [[unlikely]] {
							introduceError("Method not known", JsonRpcError::METHOD_NOT_FOUND);
						}
						if (responding && paramsPosition) {
							input.restorePosition(noFlags, call());
						}
					} else
						input.skipObjectElement(noFlags);
				} else if (*nextName == "params") {
					paramsPosition = input.storePosition(noFlags);
					if (method && responding) {
						call();
					} else {
						input.skipObjectElement(noFlags);
					}
				} else [[unlikely]] {
					introduceError("Unexpected member in request", JsonRpcError::INVALID_REQUEST);
					return false;
				}
				return !failed;
			});
			if (!failed && !called) {
				if (!method) [[unlikely]] {
					introduceError("Method name not found in request", JsonRpcError::METHOD_NOT_FOUND);
				}
				input.restorePosition(noFlags, call());
			}
		} catch (ParseError& e) {
			introduceError(e.what(), JsonRpcError::PARSE_ERROR);
		} catch (RemoteError& e) {
			introduceError(e.what(), JsonRpcError::INVALID_PARAMS);
		} catch (std::exception& e) {
			introduceError(e.what(), JsonRpcError::INTERNAL_ERROR);
		}

		if (responding) {
			output.endWritingObject(noFlags);
		}

		return !failed;
	}

public:
	JsonRpcServerProtocol(IRemoteCallable& callable) : _callable(callable) {
	}

	bool post(std::string_view, std::string_view contentType, std::span<char> request, IWriteStarter& writeStarter) override {
		if (contentType != "application/json") [[unlikely]] {
			return false;
		}

		bool result = false;
		writeStarter.writeUnknownSize("application/json", [&] (GeneralisedBuffer& response) {
			constexpr auto noFlags = Json::Output::Flags::NONE;

			typename Json::Input input(std::string_view(request.data(), request.size()));
			typename Json::Output output(response);

			auto inputType = input.identifyType(noFlags);
			if (inputType == IStructuredInput::TYPE_ARRAY) {
				// Handle an array of requests
				input.startReadingArray(noFlags);
				output.startWritingArray(noFlags, IStructuredOutput::UNKNOWN_SIZE);
				int resultArrayIndex = 0;
				while (input.nextArrayElement(noFlags)) {
					auto previousPosition = input.storePosition(noFlags);
					bool success = respondInternal(input, output, [&] () mutable {
						output.introduceArrayElement(noFlags, resultArrayIndex);
						resultArrayIndex++;
					});
					if (!success) {
						input.restorePosition(noFlags, previousPosition);
						input.skipObjectElement(noFlags);
					}
				}
				input.endReadingArray(noFlags);
				output.endWritingArray(noFlags);
				result = true; // Http should not report an error here
			} else if (inputType == IStructuredInput::TYPE_OBJECT) {
				respondInternal(input, output, {});
				result = true;
			}
		});
		return result; // The request was total nonsense
	}
};

template <BetterAssembledString LocalStringType = std::string,
		  std::derived_from<GeneralisedBuffer> ExpandingBufferType = ExpandingBuffer<>>
class JsonRpcServer {
	JsonRpcServerProtocol<LocalStringType> _protocol;
	HttpServer<ExpandingBufferType> _http;
	static inline DummyGetResponder dummyGetResponderInstance = {};
public:
	using Session = typename decltype(_http)::Session;
	JsonRpcServer(IRemoteCallable& callable, IHttpGetResponder& getResponder = dummyGetResponderInstance)
			: _protocol(callable), _http(getResponder, _protocol) {
	}
	Session getSession() {
		return _http.getSession();
	}
};

template <typename HttpType, BetterAssembledString LocalStringType = std::string>
class JsonRpcClientProtocol : public IRpcResponder {
	using Json = BasicJson<LocalStringType, GeneralisedBuffer>;
	constexpr static auto noFlags = Json::Output::Flags::NONE;
	HttpType& _upper = nullptr;
public:
	
	RequestToken send(UserId user, const IRemoteCallable* method,
				Callback<void(IStructuredOutput&, RequestToken token)> request) final override {
		auto methodName = PathWithSeparator<".", LocalStringType>::constructPath(method);
		return _upper.post("application/json", [methodName, &request]
					(GeneralisedBuffer& output, RequestToken token) {
			typename Json::Output writer(output);
			writer.startWritingObject(noFlags, 3);
			writer.introduceObjectMember(noFlags, "jsonrpc", 0);
			writer.writeString(noFlags, "2.0");
			writer.introduceObjectMember(noFlags, "id", 1);
			writer.writeInt(noFlags, token.id);
			writer.introduceObjectMember(noFlags, "method", 2);
			writer.writeString(noFlags, methodName);
			writer.introduceObjectMember(noFlags, "params", 3);
			request(writer, token);
			writer.endWritingObject(noFlags);
		});
	}
	bool getResponse(RequestToken token, Callback<void(IStructuredInput&)> reader) final override {
		bool retval = false;
		_upper.getResponse(token, [&reader, &retval, token] (std::span<char> message, bool success) {
			if (!success)
				return false;
			typename Json::Input input(std::string_view(message.data(), message.size()));
			bool okay = true;
			auto fail = [&okay] (std::string_view problem) {
				parseError(problem);
				okay = false;
			};
			input.readObject(noFlags, [&] (std::optional<std::string_view> nextName, int) {
				if (*nextName == "jsonrpc") {
					if (input.readString(noFlags) != "2.0") {
						fail("Unknown JSON-RPC version");
						return false;
					}
				} else if (*nextName == "id") {
					if (input.readInt(noFlags) != token.id) {
						fail("Out of order response");
					}
				} else if (*nextName == "error") {
					input.readObject(noFlags, [&] (std::optional<std::string_view> nextErrorElement, int) {
						if (*nextErrorElement == "message") {
							std::string_view message = input.readString(noFlags);
							remoteError(message);
							okay = false;
							return false;
						} else input.skipObjectElement(noFlags);
						return true;
					});
					remoteError("Call failed");
					okay = false;
					return false;
				} else if (*nextName == "result") {
					reader(input);
				} else {
					fail("Unknown toplevel element");
					return false;
				}
				return true;
			});
			retval = input.good;
			return okay;
		});
		return retval;
	}
	
	JsonRpcClientProtocol(IRemoteCallable& callable, HttpType& upper)
			: _upper(upper) {
		callable.setResponder(this);
	}
};

namespace Detail {
template <BetterAssembledString StringType, std::derived_from<GeneralisedBuffer> ExpandingBufferType>
struct HttpOwner {
	HttpClient<StringType, ExpandingBufferType> http;
};
} // namespace Detail


template <BetterAssembledString StringType = std::string, std::derived_from<GeneralisedBuffer> ExpandingBufferType = ExpandingBuffer<>>
class JsonRpcClient : private Detail::HttpOwner<StringType, ExpandingBufferType>, public JsonRpcClientProtocol<HttpClient<StringType>> {
	using HttpOwner = Detail::HttpOwner<StringType, ExpandingBufferType>;
public:
	JsonRpcClient(IRemoteCallable& callable, ITcpClient& client, std::string_view virtualHost)
			: Detail::HttpOwner<StringType, ExpandingBufferType>{{client, virtualHost}},
			  JsonRpcClientProtocol<HttpClient<StringType>>(callable, HttpOwner::http) {}

	bool hasResponse(RequestToken token) override {
		return static_cast<IRpcResponder&>(HttpOwner::http).hasResponse(token);
	}
};

} // namespace Bomba
#endif // BOMBA_JSON_RPC
