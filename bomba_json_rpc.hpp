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

template <BetterAssembledString StringType = std::string>
class JsonRpcServerProtocol {
	IRemoteCallable* _callable = nullptr;

	bool respondInternal(IStructuredInput& input, IStructuredOutput& output, Callback<> onResponseStarted) {
		constexpr auto noFlags = BasicJson<StringType>::Output::Flags::NONE;
		bool failed = false;
		bool responding = false;
		IStructuredInput::Location idSeekingStartpoint;

		input.startReadingObject(noFlags);

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
		auto checkIdExistence = [&] {
			if (!responding && idSeekingStartpoint.loc != IStructuredInput::Location::UNINITIALISED) {
				auto originalPosition = input.storePosition(noFlags);
				input.restorePosition(noFlags, idSeekingStartpoint);
				if (input.seekObjectElement(noFlags, "id", true)) {
					writeId();
				}
				input.restorePosition(noFlags, originalPosition);
			}
		};
		auto introduceError = [&] (std::string_view message, JsonRpcError errorType = JsonRpcError::INTERNAL_ERROR) {
			failed = true;
			checkIdExistence();
			if (!responding)
				return;

			output.introduceObjectMember(noFlags, "error", 2);
			output.startWritingObject(noFlags, 2);
			output.introduceObjectMember(noFlags, "code", 0);
			output.writeInt(noFlags, int(errorType));
			output.introduceObjectMember(noFlags, "mesage", 1);
			output.writeString(noFlags, message);
			output.endWritingObject(noFlags);
		};

		try {
			std::optional<std::string_view> nextName;
			const IRemoteCallable* method = nullptr;
			bool called = false;
			auto call = [&] (IStructuredInput* inputSelected) {
				auto introduceResult = [&] () {
					if (responding)
						output.introduceObjectMember(noFlags, "result", 2);
				};
				if (method) [[likely]] {
					called = true; // If it throws, it's still considered called
					checkIdExistence();
					if (responding)
						method->call(inputSelected, output, introduceResult, introduceError);
					else {
						NullStructredOutput nullOutput;
						method->call(inputSelected, nullOutput, introduceResult, introduceError);
					}
				}
			};
			auto readMethod = [&] () {
				auto path = input.readString(noFlags);
				method = PathWithSeparator<".", StringType>::findCallable(path, _callable);
				if (!method) {
					introduceError("Method not known", JsonRpcError::METHOD_NOT_FOUND);
				}
			};

			while ((nextName = input.nextObjectElement(noFlags)) && !failed) {
				if (*nextName == "jsonrpc") {
					idSeekingStartpoint = input.storePosition(noFlags);
					if (input.readString(noFlags) != "2.0") {
						introduceError("Unknown JSON-RPC version", JsonRpcError::INVALID_REQUEST);
						break;
					}
				} else if (*nextName == "id") {
					if (!called) {
						writeId();
					} else
						input.skipObjectElement(noFlags);
				} else if (*nextName == "method") {
					if (!method) {
						idSeekingStartpoint = input.storePosition(noFlags);
						readMethod();
					} else
						input.skipObjectElement(noFlags);
				} else if (*nextName == "params") {
					idSeekingStartpoint = input.storePosition(noFlags);
					if (!method) {
						auto paramsPosition = input.storePosition(noFlags);
						if (input.seekObjectElement(noFlags, "method", true)) {
							readMethod();
							input.restorePosition(noFlags, paramsPosition);
							call(&input);
						} else {
							input.restorePosition(noFlags, paramsPosition);
							introduceError("Method name not found in the request", JsonRpcError::INVALID_REQUEST);
						}
					} else {
						call(&input);
					}
				} else {
					introduceError("Unexpected member in request", JsonRpcError::INVALID_REQUEST);
					break;
				}
			}
			if (!failed && !called) {
				call(nullptr);
			}
			input.endReadingObject(noFlags);
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
	JsonRpcServerProtocol(IRemoteCallable* callable) : _callable(callable) {
	}

	template <HttpWriteStarter WriteStarter>
	bool post(std::string_view, std::string_view contentType, std::span<const char> request, const WriteStarter& writeStarter) {
		if (contentType != "application/json") [[unlikely]] {
			return false;
		}

		auto& response = writeStarter("application/json");
		constexpr auto noFlags = BasicJson<StringType>::Output::Flags::NONE;
				
		typename BasicJson<StringType>::Input input(std::string_view(request.data(), request.size()));
		typename BasicJson<StringType>::Output output(response);

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
			return true; // Http should not report an error here
		} else if (inputType == IStructuredInput::TYPE_OBJECT) {
			respondInternal(input, output, {});
			return true;
		}
		return false; // The request was total nonsense
	}
};

template <BetterAssembledString StringType = std::string, HttpGetResponder<StringType> GetResponder = const DummyGetResponder>
class JsonRpcServer {
	JsonRpcServerProtocol<StringType> _protocol;
	HttpServer<StringType, GetResponder, decltype(_protocol)> _http;
public:
	using Session = typename decltype(_http)::Session;
	JsonRpcServer(IRemoteCallable* callable, GetResponder* getResponder = nullptr)
			: _protocol(callable), _http(getResponder, &_protocol) {
	}
	Session getSession() {
		return _http.getSession();
	}
};

template <typename HttpType, BetterAssembledString StringType = std::string>
class JsonRpcClientProtocol : public IRpcResponder {
	constexpr static auto noFlags = BasicJson<StringType>::Output::Flags::NONE;
	HttpType* _upper = nullptr;
public:
	
	RequestToken send(UserId user, const IRemoteCallable* method,
				Callback<void(IStructuredOutput&, RequestToken token)> request) final override {
		auto methodName = PathWithSeparator<".", StringType>::constructPath(method);
		return _upper->post("application/json", [methodName, &request]
					(typename HttpType::StringType& output, RequestToken token) {
			typename BasicJson<StringType>::Output writer = output;
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
		_upper->getResponse(token, [&reader, &retval, token] (std::span<char> message, bool success) {
			if (!success)
				return false;
			typename BasicJson<StringType>::Input input(std::string_view(message.data(), message.size()));
			input.startReadingObject(noFlags);
			std::optional<std::string_view> nextName;
			while ((nextName = input.nextObjectElement(noFlags))) {
				if (*nextName == "jsonrpc") {
					if (input.readString(noFlags) != "2.0") {
						parseError("Unknown JSON-RPC version");
						return false;
					}
				} else if (*nextName == "id") {
					if (input.readInt(noFlags) != token.id) {
						parseError("Out of order response");
					}
				} else if (*nextName == "error") {
					remoteError("Call failed"); // TODO: Actually rethrow
					return false;
				} else if (*nextName == "result") {
					reader(input);
				} else {
					parseError("Unknown toplevel element");
					return false;
				}
			}
			input.endReadingObject(noFlags);
			retval = input.good;
			return true;
		});
		return retval;
	}
	
	JsonRpcClientProtocol(IRemoteCallable* callable, HttpType* upper)
			: _upper(upper) {
		callable->setResponder(this);
	}
};

namespace Detail {
template <BetterAssembledString StringType>
struct HttpOwner {
	HttpClient<StringType> http;
};
} // namespace Detail


template <BetterAssembledString StringType = std::string>
class JsonRpcClient : private Detail::HttpOwner<StringType>, public JsonRpcClientProtocol<HttpClient<StringType>> {
	using HttpOwner = Detail::HttpOwner<StringType>;
public:
	JsonRpcClient(IRemoteCallable* callable, ITcpClient* client, std::string_view virtualHost)
			: Detail::HttpOwner<StringType>{{client, virtualHost}},
			  JsonRpcClientProtocol<HttpClient<StringType>>(callable, &(HttpOwner::http)) {}
};

} // namespace Bomba
#endif // BOMBA_JSON_RPC
