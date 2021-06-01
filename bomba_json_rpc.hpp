#ifndef BOMBA_JSON_RPC
#define BOMBA_JSON_RPC

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif
#ifndef BOMBA_JSON
#include "bomba_json.hpp"
#endif

#include <array>
#include <string_view>
#include <charconv>
#include <sstream>
#include <cstring>
#include <cmath>

namespace Bomba {

template <BetterAssembledString StringType = std::string>
class JsonRpcServerProtocol {
	IRemoteCallable* _callable = nullptr;
	
	
public:
	JsonRpcServerProtocol(IRemoteCallable* callable) : _callable(callable) {
	}

	template <AssembledString ResponseStringType = StringType>
	bool post(std::string_view, std::string_view contentType, std::string_view request, ResponseStringType& _response,
				Callback<void(std::string_view)>& goodCallback, Callback<void()>& badCallback) {
		if (contentType != "application/json") {
			badCallback();
			return false;
		} else 
			goodCallback("application/json");
				
		typename BasicJson<StringType>::Input input(request);
		typename BasicJson<StringType>::Output output(_response);
		constexpr auto noFlags = BasicJson<StringType>::Output::Flags::NONE;
		output.startWritingObject(noFlags, 3);
		output.introduceObjectMember(noFlags, "jsonrpc", 0);
		output.writeString(noFlags, "2.0");
		input.startReadingObject(noFlags);
		
		std::optional<std::string_view> nextName;
		int index = 1;
		const IRemoteCallable* method = nullptr;
		bool called = false;
		auto introduceError = makeCallback([&] () {
			output.introduceObjectMember(noFlags, "error", index++);
		});
		auto call = [&] (typename BasicJson<StringType>::Input* input) {
			try {
				auto introduceResult = makeCallback([&] () {
					output.introduceObjectMember(noFlags, "result", index++);
				});
				if (method) [[likely]] {
					method->call(input, output, introduceResult, introduceError);
					called = true;
				} else {
					introduceError();
					output.writeString(noFlags, "Method not known");
				}
			} catch (std::exception& e) {
				introduceError();
				output.writeString(noFlags, e.what());
			}
		};
		auto readMethod = [&] () {
			auto path = input.readString(noFlags);
			method = PathWithSeparator<".", StringType>::findCallable(path, _callable);
		};
		
		while ((nextName = input.nextObjectElement(noFlags))) {
			if (*nextName == "jsonrpc") {
				if (input.readString(noFlags) != "2.0") {
					parseError("Unknown JSON-RPC version");
					return false;
				}
			} else if (*nextName == "id") {
				output.introduceObjectMember(noFlags, "id", index++);
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
					parseError("Bad id type");
					return false;
				}
			} else if (*nextName == "method") {
				if (!method)
					readMethod();
				else
					input.skipObjectElement(noFlags);
			} else if (*nextName == "params") {
				if (!method) {
					auto paramsPosition = input.storePosition(noFlags);
					if (input.seekObjectElement(noFlags, "method", true)) {
						readMethod();
						input.restorePosition(noFlags, paramsPosition);
						call(&input);
					} else {
						input.restorePosition(noFlags, paramsPosition);
						introduceError();
						output.writeString(noFlags, "Method name not found in the request");
					}
				} else {
					call(&input);
				}
			} else {
				parseError("Unexpected member in request");
				return false;
			}
		}
		if (!called) {
			call(nullptr);
		}

		input.endReadingObject(noFlags);
		output.endWritingObject(noFlags);
		return true;
	}
};

template <typename HttpType, BetterAssembledString StringType = std::string>
class JsonRpcClientProtocol : private IRpcResponder {
	constexpr static auto noFlags = BasicJson<StringType>::Output::Flags::NONE;
	HttpType* _upper = nullptr;
public:
	
	RequestToken send(UserId user, const IRemoteCallable* method,
				const Callback<void(IStructuredOutput&, RequestToken token)>& request) final override {
		auto methodName = PathWithSeparator<".", StringType>::constructPath(method);
		return _upper->post(UserId{}, [methodName, &request]
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
			request(writer, std::move(token));
			writer.endWritingObject(noFlags);
		});
	}
	bool getResponse(RequestToken token, const Callback<void(IStructuredInput&)>& reader) final override {
		bool retval = false;
		_upper->getResponse(token, [&reader, &retval, token] (std::string_view unparsed) {
			typename BasicJson<StringType>::Input input(unparsed);
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


} // namespace Bomba
#endif // BOMBA_JSON_RPC
