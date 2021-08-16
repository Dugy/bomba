#ifndef BOMBA_HTTP
#define BOMBA_HTTP

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif
#include <charconv>

// Good HTTP protocol description: https://www3.ntu.edu.sg/home/ehchua/programming/webprogramming/HTTP_Basics.html
// Good testing site: http://www.ptsv2.com/

namespace Bomba {

namespace Detail {
struct HttpParseState {
	int transition = 0;
	int bodySize = -1;

	void reset() {
		transition = 0;
		bodySize = -1;
	}

	template <typename FirstLineReader, typename HeaderReader>
	std::pair<ServerReaction, int64_t> parse(const FirstLineReader& firstLineReader,
				const HeaderReader& headerReader, std::span<char> input) {
		if (input.size() < 4) { // Not even space for the separator
			return {ServerReaction::READ_ON, 0};
		}
		while (input[transition] != '\n' || input[transition - 1] != '\r'
			   || input[transition - 2] != '\n' || input[transition - 3] != '\r') {
			if (transition >= int(input.size())) [[unlikely]]
					return {ServerReaction::READ_ON, input.size()};
			else
				 transition++;
		}
		transition++;

		int position = 0;
		auto readWordUntil = [&] (char separator) -> std::string_view {
			const int startPosition = position;
			std::span<char>::iterator start = std::span(input.begin() + position, 1).begin();
			while (position <  transition && input[position] != separator)
				position++;
			position++;
			return std::string_view(&*start, position - startPosition - 1);
		};

		std::string_view firstLine = readWordUntil('\r');
		if (!firstLineReader(input, firstLine.size())) {
			return {ServerReaction::DISCONNECT, input.size()};
		}

		while (position < transition) {
			position += 1; // The \n after \r
			std::string_view property = readWordUntil(':');
			if (property.empty())
				break; // End of header
			while (input[position] == ' ')
				position++;
			std::string_view value = readWordUntil('\r');
			if (property == "Content-Length") {
				std::from_chars(&*value.begin(), &*value.end(), bodySize);
			} else headerReader(property, input, value.data() - input.data(), value.size());
		}
		return { ServerReaction::OK, transition };
	}
};
} // namespace Detail

template <AssembledString StringType = std::string>
void demangleUtf8Url(std::string_view input, StringType& output) {
	for (int i = 0; i < input.size(); i++) {
		if (input[i] == '+') {
			output += ' ';
		} else if (input[i] == '%') {
			if (i + 3 > input.size()) [[unlikely]]
				return;
			unsigned char escaped = {};
			std::from_chars(&input[i + 1], &input[i + 3], escaped, 16);
			output += escaped;
			i += 2;
		} else [[likely]]
			output += input[i];
	}
}

template <AssembledString StringType = std::string>
void demangleHttpResponse(std::string_view input, StringType& output) {
	for (int i = 0; i < int(input.size()); i++) {
		if (input[i] == '+') {
			output += ' ';
		} else if (input[i] == '%') {
			auto utf8Encode = [&output] (int value) {
				if (value < 0x80)
					output += value;
				else {
					if (value > 0x800) {
						if (value >= 0x10000) {
							output += 0b11110000 + (value >> 18);
							output += 0b10000000 + ((value >> 12) & 0b00111111);
						} else {
							output += 0b11100000 + (value >> 12);
						}
						output += 0b10000000 + ((value >> 6) & 0b00111111);
					} else {
						output += 0b11000000 + (value >> 6);
					}
					output += 0b10000000 + (value & 0b00111111);
				}
			};
			auto urlDecode = [&input] (int index) {
				if (index + 3 > int(input.size())) [[unlikely]]
					return 0;
				int escaped = {};
				std::from_chars(&input[index + 1], &input[index + 3], escaped, 16);
				return escaped;
			};
			int decoded = urlDecode(i);
			if (decoded == '&' && i + 8 < int(input.size())) [[unlikely]] {
				if (urlDecode(i + 3) == '#' && input[i + 6] >= '0' && input[i + 6] <= '9') {
					int numberEnd = i + 7;
					while (numberEnd < int(input.size()) && input[numberEnd] >= '0' && input[numberEnd] <= '9')
						numberEnd++;
					if (numberEnd + 2 < int(input.size())) [[likely]] {
						if (input[numberEnd] == '%' && input[numberEnd + 1] == '3'
								&& (input[numberEnd + 2] == 'b' || input[numberEnd + 2] == 'B')) {
							std::from_chars(&input[i + 6], &input[numberEnd], decoded);
							i = numberEnd;
						}
					}
				}
			}
			utf8Encode(decoded);
			i += 2;
		} else [[likely]]
			output += input[i];
	}
}

template <AssembledString StringType = std::string>
void mangleHttpResponse(std::string_view input, StringType& output) {
	auto encodeOne = [&output] (unsigned char letter) {
		if ((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z')
				|| (letter >= '0' && letter <= '9')) [[likely]] {
			output += letter;
		} else if (std::string_view(".*-_^\\~'`|<>[]{}()").find(letter) != std::string_view::npos) {
			output += letter;
		} else {
			output += "%00";
			std::string_view outputView = output;
			std::to_chars(const_cast<char*>(outputView.data() + outputView.size() - 2),
						  const_cast<char*>(outputView.data() + outputView.size()), letter, 16);
		}
	};
	for (int i = 0; i < int(input.size()); i++) {
		unsigned char letter = *reinterpret_cast<const unsigned char*>(&input[i]);
		if (letter == ' ')
			output += '+';
		else if (letter <= 0x7f) [[likely]]
			encodeOne(letter);
		else {
			int length = 1;
			int value = 0;
			if ((letter & 0b11100000) == 0b11000000) {
				length = 2;
				value = letter & 0b00011111;
			} else if ((letter & 0b11110000) == 0b11100000) {
				length = 3;
				value = letter & 0b00001111;
			} else {
				length = 4;
				value = letter & 0b00000111;
			}
			for (int j = 1; j < length; j++) {
				if (i + j >= int(input.size())) [[unlikely]]
					return;
				value <<= 6;
				value += 0b00111111 & *reinterpret_cast<const unsigned char*>(&input[i + j]);
			}
			i += length - 1;
			if (value > 0xff) {
				encodeOne('&');
				encodeOne('#');
				constexpr int SIZE = 10;
				std::array<char, SIZE> bytes = {};
				std::to_chars(bytes.data(), bytes.data() + SIZE, value); // Digits don't need encoding
				output += bytes.data();
				encodeOne(';');
			} else {
				encodeOne(value);
			}
		}
	}
}

template <AssembledString StringType = std::string>
struct HtmlMessageEncoding {
	class Input : public IStructuredInput {
		std::string_view _contents;
		StringType _demangled;
		int _position = 0;
		bool _objectStarted = false; // No object recursion allowed

		int getEnd() const {
			int end = _position;
			while (end < int(_contents.size()) && _contents[end] != '&')
				end++;
			return end;
		}
		std::string_view demangleTillEndNoMove() {
			int end = getEnd();
			_demangled.clear();
			demangleHttpResponse(_contents.substr(_position, end - _position), _demangled);
			return _demangled;
		}
		std::string_view demangleTillEnd() {
			int end = getEnd();
			_demangled.clear();
			demangleHttpResponse(_contents.substr(_position, end - _position), _demangled);
			_position = end;
			return _demangled;
		}

		void fail(const char* problem) {
			parseError(problem);
			good = false;
		}

	public:
		Input(std::string_view contents) : _contents(contents) {}


		MemberType identifyType(Flags) final override {
			std::string_view demangled = demangleTillEndNoMove();
			if (demangled.size() == 0)
				return TYPE_STRING; // Empty string

			int demangledPos = 0;
			if (demangled[demangledPos] == '-')
				demangledPos++;
			bool exponentWasUsed = false;
			bool decimalWasUsed = false;
			bool canBeInt = true;
			for (; demangledPos < int(demangled.size()); demangledPos++) {
				if (demangled[demangledPos] == 'e' || demangled[demangledPos] == 'E') {
					if (exponentWasUsed) [[unlikely]]
						return TYPE_STRING;
					exponentWasUsed = true;
					if (demangledPos >= int(demangled.size())) [[unlikely]]
						return TYPE_STRING;
					if (demangled[demangledPos] == '-')
						demangledPos++;
					canBeInt = false;
				} else if (demangled[demangledPos] == '.') {
					if (decimalWasUsed || exponentWasUsed) [[unlikely]]
						return TYPE_STRING;
					decimalWasUsed = true;
					canBeInt = false;
				}
				if (demangled[_position] < '0' || demangled[_position] > '9')
					return TYPE_STRING;
			}
			return canBeInt ? TYPE_INTEGER : TYPE_FLOAT;
		}

		int64_t readInt(Flags) final override {
			int end = getEnd();
			int64_t result = 0;
			std::from_chars_result got = std::from_chars(&_contents[_position], &_contents[end], result);
			_position = end;
			if (got.ec != std::errc()) [[unlikely]]
				fail("Expected integer");
			return result;
		}
		double readFloat(Flags) final override {
			std::string_view demangled = demangleTillEnd();
			double result = 0;
			std::from_chars_result got = std::from_chars(demangled.data(), demangled.data() + demangled.size(), result);
			_position += got.ptr - &_contents[_position];
			if (got.ec != std::errc()) [[unlikely]]
				fail("Expected double");
			return result;
		}
		std::string_view readString(Flags) final override {
			return demangleTillEnd();
		}
		bool readBool(Flags) final override {
			_position = getEnd();
			return true;
		}
		void readNull(Flags) final override {
			// Assuming it will be empty
		}

		void startReadingArray(Flags) final override {
			fail("Type doesn't support array");

		}
		bool nextArrayElement(Flags) final override {
			fail("Type doesn't support array");
			return false;
		}
		void endReadingArray(Flags) final override {
			fail("Type doesn't support array");
		}

		void startReadingObject(Flags) final override {
			if (_objectStarted) [[unlikely]]
					fail("Type doesn't support object");
			_objectStarted = true;
		}
		std::optional<std::string_view> nextObjectElement(Flags) final override {
			if (_position >= int(_contents.size()))
				return std::nullopt;
			if (_contents[_position] == '&')
				_position++;
			int start = _position;
			while (_position < int(_contents.size()) && _contents[_position] != '=')
				_position++;
			_demangled.clear();
			demangleHttpResponse(_contents.substr(start, _position - start), _demangled);
			_position++;
			return _demangled;
		}
		void skipObjectElement(Flags) final override {
			if (_contents[_position] == '&')
				_position++;
			while (_position < int(_contents.size()) && _contents[_position] != '&')
				_position++;
		}
		void endReadingObject(Flags) final override {
			_objectStarted = false;
		}

		Location storePosition(Flags) final override {
			return Location{ _position };
		}
		void restorePosition(Flags, Location location) final override {
			_position = location.loc;
		}
	};

	class Output : public IStructuredOutput {
		StringType& _contents;
		static constexpr int OBJECT_NOT_WRITTEN = -1;
		int _objectIndex = OBJECT_NOT_WRITTEN;

		template <typename Num>
		void writeValue(Num value) {
			constexpr int SIZE = 20;
			std::array<char, SIZE> bytes = {};
			std::to_chars(bytes.data(), bytes.data() + SIZE, value);
			_contents += &bytes[0];
		}
	public:
		Output(StringType& contents) : _contents(contents) {}

		void writeInt(Flags, int64_t value) final override {
			writeValue(value);
		}
		void writeFloat(Flags, double value) final override {
			writeValue(value);
		}
		void writeString(Flags, std::string_view value) final override {
			mangleHttpResponse(value, _contents);
		}
		void writeBool(Flags, bool value) final override {
			if (value)
				_contents += "true";
			else
				logicError("Bool must be written with the OMIT_FALSE flag");
		}
		void writeNull(Flags flags) final override {
			if (!(flags & Flags::EMPTY_IS_NULL))
				logicError("Null must be written with the EMPTY_IS_NULL flag");
		}

		void startWritingArray(Flags, int size) final override {
			logicError("Format does not support arrays");
		}
		void introduceArrayElement(Flags, int index) final override {
			logicError("Format does not support arrays");
		}
		void endWritingArray(Flags) final override {
			logicError("Format does not support arrays");
		}

		void startWritingObject(Flags, int size) final override {
			if (_objectIndex != OBJECT_NOT_WRITTEN) [[unlikely]]
					logicError("Format does not support nested objects");
			_objectIndex = 0;
		}
		void introduceObjectMember(Flags, std::string_view name, int) final override {
			if (_objectIndex != 0)
				_contents += '&';
			_contents += name.data();
			_contents += '=';
			_objectIndex++;
		}
		void endWritingObject(Flags) final override {
			_objectIndex = OBJECT_NOT_WRITTEN;
		}
	};
};

template <typename T, typename AssembledStringType>
concept HttpGetResponder = requires(T value, std::string_view input,
		decltype ([] (std::string_view) -> AssembledStringType& { return *reinterpret_cast<AssembledStringType*>(0); } ) writer) {
	{ value.get(input, writer) } -> std::same_as<bool>;
};

template <typename T, typename AssembledStringType>
concept HttpPostResponder = requires(T value, std::string_view input, std::span<char> body,
		decltype([] (std::string_view) -> AssembledStringType& { return *reinterpret_cast<AssembledStringType*>(0); }) writer) {
	{ value.post(input, input, body, writer) } -> std::same_as<bool>;
};

struct DummyGetResponder {
	template <AssembledString ResponseStringType = std::string>
	static bool get(std::string_view, Callback<ResponseStringType&(std::string_view)>) {
		return false;
	}
};

template <typename T>
concept HttpWriteStarter = requires(T value, std::string_view streamType) {
	{ value(streamType) } -> AssembledString;
};

template <typename T, typename WriteStarter>
concept HttpFileGetterCallback = requires(T value, WriteStarter starter) {
	{ value(starter) } -> std::same_as<bool>;
};

struct SimpleGetResponder {
	std::string_view resource;
	std::string_view resourceType = "text/html";
	template <HttpWriteStarter WriteStarter>
	bool get(std::string_view, const WriteStarter& writeResponse) {
		auto& response = writeResponse(std::string_view(resourceType));
		response += resource;
		return true;
	}
};

// Compiler dies on this, maybe there's an error but I can't find it
//template <typename T, typename AssembledStringType>
//concept DownloadRpcDispatcher = requires(T value, const IRemoteCallable* method, IStructuredInput* args,
//		decltype ([] (std::string_view) -> AssembledStringType& {}) writer,
//		decltype ([] (typename T::template GetterCallbackArg<decltype (writer)>) { return true; }) outputProvider) {
//{ value.dispatch(method, args, writer, outputProvider) } -> std::same_as<bool>;
//};

template <AssembledString AssembledStringType = std::string,
		typename OutputFormat = typename HtmlMessageEncoding<AssembledStringType>::Output,
		StringLiteral name = "application/x-www-form-urlencoded">
struct DownloadIfFilePresent {
	template <HttpWriteStarter WriteStarter>
	using GetterCallbackArg = WriteStarter;
	template <HttpWriteStarter WriteStarter, HttpFileGetterCallback<WriteStarter> GetterCallback>
	static bool dispatch(const IRemoteCallable* method, IStructuredInput* args,
					WriteStarter outputProvider, GetterCallback writer) {
		bool fileFound = writer(outputProvider);
		if (method) {
			if (fileFound) {
				NullStructredOutput nullResponse;
				method->call(args, nullResponse, [] {}, [] {});
			} else {
				AssembledStringType& output = outputProvider(name);
				OutputFormat response(output);
				method->call(args, response, [] {}, [] {});
			}
			return true;
		} else return fileFound;
	}
};

template <AssembledString ResponseStringType, HttpGetResponder<ResponseStringType> PageProvider,
		/*DownloadRpcDispatcher<ResponseStringType>*/ typename DispatcherType = const DownloadIfFilePresent<ResponseStringType>>
class RpcGetResponder {
	PageProvider* _provider = nullptr;
	IRemoteCallable* _callable = nullptr;
	static constexpr DownloadIfFilePresent<ResponseStringType> defaultDispatcher = {};
	DispatcherType* _dispatcher = nullptr;

public:
	RpcGetResponder(PageProvider* provider, IRemoteCallable* callable, DispatcherType* dispatcher = &defaultDispatcher)
		: _provider(provider), _callable(callable), _dispatcher(dispatcher) {}

	template <typename HttpWriteStarter>
	bool get(std::string_view entirePath, const HttpWriteStarter& writeResponse) {
		auto transition = entirePath.find_first_of('?');
		std::string_view path;
		const IRemoteCallable* method = nullptr;
		std::optional<typename HtmlMessageEncoding<ResponseStringType>::Input> inputParsed = std::nullopt;
		if (transition != std::string_view::npos) {
			path = entirePath.substr(0, transition);
			if (_callable) {
				std::string_view editedPath = path.substr(1);
				method = PathWithSeparator<"/", ResponseStringType>::findCallable(editedPath, _callable);
			}
			inputParsed.emplace(entirePath.substr(transition + 1));
		} else {
			path = entirePath;
		}
		return _dispatcher->dispatch(method, method ? &*inputParsed : nullptr, writeResponse,
				[&] (typename DispatcherType::template GetterCallbackArg<HttpWriteStarter> outputProvider) -> bool {
			return _provider->get(path, outputProvider);
		});
	}
};

struct DummyPostResponder {
	template <HttpWriteStarter WriteStarter>
	static bool post(std::string_view path, std::string_view, std::span<char> request, WriteStarter) {
		return false;
	}
};

class HtmlPostResponder {
	IRemoteCallable* _callable = nullptr;
public:
	HtmlPostResponder(IRemoteCallable* callable) : _callable(callable) {}

	template <HttpWriteStarter WriteStarter>
	bool post(std::string_view path, std::string_view, std::span<char> request,
				WriteStarter) {
		std::string_view editedPath = path.substr(1);
		using ResponseStringType = std::remove_reference_t<decltype(std::declval<WriteStarter>()(std::string_view("")))>;
		const IRemoteCallable* method = PathWithSeparator<"/", ResponseStringType>::findCallable(editedPath, _callable);
		if (!method)
			return false;
		typename HtmlMessageEncoding<ResponseStringType>::Input input = {std::string_view(request.data(), request.size())};
		NullStructredOutput nullOutput;
		method->call(&input, nullOutput, [] {}, [] {});
		return true;
	}
};

template <BetterAssembledString StringType = std::string, HttpGetResponder<StringType> GetResponder = const DummyGetResponder,
		HttpPostResponder<StringType> PostResponder = const DummyPostResponder>
class HttpServer {
	struct Responders {
		GetResponder* getResponder = nullptr;
		PostResponder* postResponder = nullptr;
	} _responders;

public:
	HttpServer(GetResponder* getResponder = nullptr, PostResponder* postResponder = nullptr)
			: _responders({getResponder, postResponder}) {}
			
	class Session : ITcpResponder {
		Responders _responders;
		enum RequestType {
			UNINVESTIGATED_REQUEST,
			UNKNOWN_REQUEST,
			GET_REQUEST,
			POST_REQUEST,
			WEIRD_REQUEST
		} _requestType = UNINVESTIGATED_REQUEST;
		std::pair<int, int> _path;
		std::pair<int, int> _contentType;
		ServerReaction _ending = ServerReaction::OK;
		Detail::HttpParseState _state;
	public:
		std::pair<ServerReaction, int64_t> respond(
					std::span<char> input, Callback<void(std::span<const char>)> writer) override {
			auto firstLineReader = [&] (std::span<char> input, int size) {
				int separator1 = 0;
				while (input[separator1] != ' ' && separator1 < size)
					separator1++;
				std::string_view methodName = {input.data(), size_t(separator1)};
				if (methodName == "GET")
					_requestType = GET_REQUEST;
				else if (methodName == "POST")
					_requestType = POST_REQUEST;
				else
					_requestType = WEIRD_REQUEST;

				separator1++;
				int separator2 = separator1;
				while (input[separator2] != ' ' && separator2 < size)
					separator2++;
				_path = std::pair<int, int>(separator1, separator2 - separator1);
				separator2++;
				int separator3 = separator2;
				while (input[separator3] != '\r' && separator3 < size)
					separator3++;
				std::string_view protocol = {input.data() + separator2, size_t(separator3 - separator2)};
				if (protocol != "HTTP/1.1" && protocol != "HTTP/1.0")
					_requestType = WEIRD_REQUEST;
				return true;
			};
			auto headerReader = [&] (std::string_view property, std::span<char> input, int valueOffset, int valueSize) {
				if (property == "Content-Type") {
					_contentType = std::pair<int, int>(valueOffset, valueSize);
				} else if (property == "Connection") {
					std::string_view value = {input.data() + valueOffset, size_t(valueSize)};
					if (value == "close")
						_ending = ServerReaction::DISCONNECT;
				} // Ignore others
			};
			auto restore = [this] () {
				_state.reset();
				_requestType = UNINVESTIGATED_REQUEST;
			};

			// Locate the header's span
			if (_state.bodySize == -1) {
				auto [reaction, position] = _state.parse(firstLineReader, headerReader, input);
				if (reaction != ServerReaction::OK) {
					return {reaction, position};
				}
			}

			if (_requestType != POST_REQUEST)
				_state.bodySize = 0;
			int consuming = _state.transition + _state.bodySize;
			
			// All body has beed read
			if (_requestType == GET_REQUEST || _requestType == POST_REQUEST) [[likely]] {
				if (_requestType == POST_REQUEST) {
					if (_state.bodySize == -1 || int(input.size()) < _state.transition + _state.bodySize) {
						return {ServerReaction::READ_ON, input.size()};
					}
				}

				bool success = false;
				int headerSize = 0;
				constexpr char correctIntro[] = "HTTP/1.1 200 OK\r\nContent-Length:";
				constexpr char unsetSize[] = " 0         ";
				StringType response;
				bool startedResponse = false;
				auto correctResponseWriter = [&](std::string_view contentType) -> StringType& {
					startedResponse = true;
					response += correctIntro;
					response += unsetSize;
					response += "\r\nContentType: ";
					response += contentType;
					response += "\r\n\r\n";
					headerSize = response.size();
					return response;
				};
				std::string_view path{input.data() + _path.first, size_t(_path.second)};
				try {
					if (_requestType == GET_REQUEST) {
						success = _responders.getResponder->get(path, correctResponseWriter);
						if (!success) [[unlikely]] {
								response +=
										"HTTP/1.1 404 Not Found\r\n"
										"Content-Length: 73\r\n\r\n"
										"<!doctype html><html lang=en><title>Error 404: Resource not found</title>";
						}
					} else {
						std::span<char> body = {input.begin() + _state.transition, input.begin() + _state.transition + _state.bodySize};
						std::string_view contentType = {input.data() + _contentType.first, size_t(_contentType.second)};
						success = _responders.postResponder->post(path, contentType, body, correctResponseWriter);
						if (!success) [[unlikely]] {
							response +=
									"HTTP/1.1 400 Bad Request\r\n"
									"Content-Length: 66\r\n\r\n"
									"<!doctype html><html lang=en><title>Error 400: Bad request</title>";
						}
					}
				} catch (...) {
					response +=
							"HTTP/1.1 500 Internal Server Error\r\n"
							"Content-Length: 76\r\n\r\n"
							"<!doctype html><html lang=en><title>Error 500: Internal server error</title>";
				}
				if (success) {
					if (startedResponse) {
						std::string_view responseView = response;
						// For whatever reasons, std::string_view::operator[] chooses the const version
						std::to_chars(const_cast<char*>(&responseView[sizeof(correctIntro)]),
								const_cast<char*>(&responseView[sizeof(correctIntro) + sizeof(unsetSize)]),
								response.size() - headerSize);
					} else {
						response += "HTTP/1.1 204 No Content\r\n\r\n";
					}
				}
				writer(response);
				restore();
			} else {
				constexpr std::string_view errorMessage =
									"HTTP/1.1 501 Method Not Implemented\n\r"
									"Content-Length: 77\r\n\r\n"
									"<!doctype html><html lang=en><title>Error 501: Method not implemented</title>";
				writer(std::span<const char>(errorMessage.begin(), errorMessage.size()));
				restore();
			}
			return {_ending, consuming};
		}
		
	
		friend class HttpServer;
	};
	
	Session getSession() {
		Session made;
		made._responders = _responders;
		return made;
	}
};

template <BetterAssembledString StringT = std::string>
class HttpClient : public IRpcResponder {
	ITcpClient* _client = nullptr;
	RequestToken _lastTokenWritten = {0};
	RequestToken _lastTokenRead = {0};
	std::string_view _virtualHost;

	RequestToken send(UserId, const IRemoteCallable* method,
			Callback<void(IStructuredOutput&, RequestToken)> request) override {
		auto methodName = PathWithSeparator<"/", StringT>::constructPath(method);

		return send("application/x-www-form-urlencoded",
					[methodName, &request] (StringT& output, RequestToken token) {
			typename HtmlMessageEncoding<StringT>::Output writer = output;
			request(writer, token);
		});
	}
	bool getResponse(RequestToken token, Callback<void(IStructuredInput&)> reader) override {
		getResponse(token, [&] (std::span<char> data, bool success) {
			if (!success) {
				remoteError("Server did not respond with OK");
				return false;
			}
			typename HtmlMessageEncoding<StringT>::Input input = {std::string_view(data.data(), data.size())};
			reader(input);
			return true;
		});
		return true;
	}

	bool hasResponse(RequestToken token) override {
		bool has = false;
		getResponse(token, [&] (std::span<char> data, bool) {
			has = true;
			return true;
		});
		return has;
	}

public:
	using StringType = StringT;
	HttpClient(ITcpClient* client, std::string_view virtualHost) : _client(client), _virtualHost(virtualHost) {}

	RequestToken send(std::string_view contentType,
			Callback<void(StringType&, RequestToken token)> request) {
		StringType written;
		constexpr char correctIntro[] = "POST / HTTP/1.1\r\nContent-Length: ";
		constexpr char unsetSize[] = "0         ";
		written += correctIntro;
		written += unsetSize;
		written += "\r\nHost: ";
		written += _virtualHost;
		written += "\r\nContent-Type: ";
		written += contentType;
		written += "\r\n\r\n";
		int sizeBefore = written.size();
		request(written, RequestToken{_lastTokenWritten});
		std::string_view reqView = written;
		std::to_chars(const_cast<char*>(&reqView[sizeof(correctIntro) - 1]),
				const_cast<char*>(&reqView[sizeof(correctIntro) + sizeof(unsetSize)]),
				written.size() - sizeBefore);
		_lastTokenWritten.id++;
		_client->writeRequest(std::span<char>{written.data(), written.size()});
		return _lastTokenWritten;
	}

	RequestToken get(std::string_view resource = "/") {
		StringType written;
		constexpr StringLiteral intro = "GET ";
		constexpr StringLiteral mid = " HTTP/1.1\r\nHost: ";
		constexpr StringLiteral suffix = "\r\n\r\n";
		written += intro;
		written += resource;
		written += mid;
		written += _virtualHost;
		written += suffix;
		_lastTokenWritten.id++;
		_client->writeRequest(std::span<char>{written.data(), written.size()});
		return _lastTokenWritten;
	}

	void getResponse(RequestToken token, Callback<bool(std::span<char> message, bool success)> reader) {
		bool obtained = false;
		while (!obtained) {
			int resultCode = 0;
			auto checkServerResponse = [&] (std::span<char> header, int size) {
				int separator1 = 0;
				while (header[separator1] != ' ' && separator1 < size)
					separator1++;
				separator1++;
				int separator2 = separator1 + 1;
				while (header[separator2] != ' ' && separator2 < size)
					separator2++;
				std::from_chars(&header[separator1], &header[separator2], resultCode);
				return true;
			};
			Detail::HttpParseState state;
			_client->getResponse(token, [&, this] (std::span<char> input, bool identified)
						-> std::tuple<ServerReaction, RequestToken, int64_t> {
				// Locate the header's span
				if (state.bodySize == -1) {
					auto [reaction, position] = state.parse(checkServerResponse,
							[] (std::string_view, std::span<char>, int, int) {}, input);
					if (reaction != ServerReaction::OK) {
						return {reaction, RequestToken{}, position};
					}
				}

				if (state.bodySize > 0 && int(input.size()) < state.transition + state.bodySize) {
					return {ServerReaction::READ_ON, RequestToken{}, input.size()};
				}
			
				if (!identified && token != RequestToken{ _lastTokenRead.id + 1}) {
					int offset = state.transition + std::max(0, state.bodySize);
					state = Detail::HttpParseState{};
					_lastTokenRead.id++;
					return {ServerReaction::WRONG_REPLY, _lastTokenRead, offset};
				}					
				obtained = true;

				reader(std::span<char>(input.begin() + state.transition, input.begin() + state.transition + state.bodySize),
						(resultCode >= 200 && resultCode < 300));
				if (!identified)
					_lastTokenRead.id++;
				return {ServerReaction::OK, _lastTokenRead, state.transition + state.bodySize};
			});
		}
	}
};





} // namespace Bomba

#endif // BOMBA_HTTP
