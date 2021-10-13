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
	int parsePosition = 0;
	int bodySize = -1;

	void reset() {
		parsePosition = 0;
		bodySize = -1;
	}

	std::pair<ServerReaction, int64_t> parse(std::span<char> input) {
		int position = parsePosition;

		if (position == 0) { // Header not read yet
			while (position < int(input.size())) {
				if (input[position] == '\r') {
					position++;
					if (position >= int(input.size()))
						break;
					if (input[position] != '\n')
						return {ServerReaction::DISCONNECT, 0};
					break;
				}
				position++;
			}

			if (position == int(input.size())) {
				return {ServerReaction::READ_ON, position - 1};
			}
			position++;

			if (!firstLineReader({input.data(), size_t(position)})) {
				return {ServerReaction::DISCONNECT, 0};
			}
			parsePosition = position;
		}

		if (position == int(input.size())) {
			return {ServerReaction::READ_ON, position - 1};
		}
		// Parse it line by line
		while (input[position] != '\r') {
			int attributeNameStart = 0;
			std::string_view attributeName;
			int attributeValueStart = 0;
			std::string_view attributeValue;

			// Skip initial whitespace
			while (position < int(input.size())) {
				if (input[position] != ' ' && input[position] != '\t') {
					attributeNameStart = position;
					break;
				}
				position++;
			}
			if (position >= int(input.size())) {
				return {ServerReaction::READ_ON, position - 1};
			}

			// Go through the attribute name
			while (position < int(input.size())) {
				if (input[position] == ' ' || input[position] == '\t' || input[position] == ':') {
					attributeName = std::string_view(input.data() + attributeNameStart, position - attributeNameStart);
					break;
				}
				if (input[position] <= 'Z' && input[position] >= 'A')
					input[position] += 'a' - 'A';
				position++;
			}
			if (position >= int(input.size())) {
				return {ServerReaction::READ_ON, position - 1};
			}

			// Find the value
			bool colonFound = false;
			while (position < int(input.size())) {
				if (input[position] == ':')
					colonFound = true;
				if (input[position] != ' ' && input[position] != '\t' && input[position] != ':') {
					attributeValueStart = position;
					break;
				}
				position++;
			}
			if (position >= int(input.size())) {
				return {ServerReaction::READ_ON, position - 1};
			}
			if (colonFound == false) {
				return {ServerReaction::DISCONNECT, 0};
			}

			// Find the end of the attribute
			while (position < int(input.size())) {
				if (input[position] == '\r') {
					attributeValue = std::string_view(input.data() + attributeValueStart, position - attributeValueStart);
					break;
				}
				position++;
			}
			if (position >= int(input.size())) {
				return {ServerReaction::READ_ON, position - 1};
			}

			// Use the data
			if (attributeName == "content-length") {
				std::from_chars(&*attributeValue.begin(), &*attributeValue.end(), bodySize);
			} else
				headerReader(attributeName, attributeValue, std::pair<int, int>(attributeValueStart, attributeValue.size()));

			// Finish the line
			position += 2;
			parsePosition = position;
			if (position >= int(input.size())) {
				return {ServerReaction::READ_ON, position - 1};
			}
		}

		if (parsePosition + 1 >= int(input.size())) {
			return {ServerReaction::READ_ON, position - 1};
		}
		if (input[parsePosition + 1] != '\n')
			return {ServerReaction::DISCONNECT, 0};
		parsePosition += 2; // Skip the last \r\n, get to content

		return { ServerReaction::OK, parsePosition };
	}

	virtual bool firstLineReader(std::string_view firstLine) = 0;
	virtual void headerReader(std::string_view property, std::string_view value, std::pair<int, int> location) = 0;
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

template <typename T>
concept HttpWriteStarter = requires(const T& value, std::string_view streamType) {
	{ value(streamType) } -> AssembledString;
};

template <typename T, typename WriteStarter>
concept HttpFileGetterCallback = requires(T value, WriteStarter starter) {
	{ value(starter) } -> std::same_as<bool>;
};

struct DummyGetResponder {
	template <HttpWriteStarter WriteStarter>
	static bool get(std::string_view, const WriteStarter&) {
		return false;
	}
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
				method->call(args, nullResponse, {}, {});
			} else {
				AssembledStringType& output = outputProvider(name);
				OutputFormat response(output);
				method->call(args, response, {}, {});
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
	bool post(std::string_view path, std::string_view, std::span<char> request, const WriteStarter&) {
		std::string_view editedPath = path.substr(1);
		using ResponseStringType = std::remove_reference_t<decltype(std::declval<WriteStarter>()(std::string_view("")))>;
		const IRemoteCallable* method = PathWithSeparator<"/", ResponseStringType>::findCallable(editedPath, _callable);
		if (!method)
			return false;
		typename HtmlMessageEncoding<ResponseStringType>::Input input = {std::string_view(request.data(), request.size())};
		NullStructredOutput nullOutput;
		method->call(&input, nullOutput, {}, {});
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

		struct ParseState : Detail::HttpParseState {
			enum RequestType {
				UNINVESTIGATED_REQUEST,
				UNKNOWN_REQUEST,
				GET_REQUEST,
				POST_REQUEST,
				WEIRD_REQUEST
			} requestType = UNINVESTIGATED_REQUEST;
			std::pair<int, int> path;
			std::pair<int, int> contentType;
			ServerReaction ending = ServerReaction::OK;

			virtual bool firstLineReader(std::string_view firstLine) override {
				int separator1 = 0;
				while (firstLine[separator1] != ' ' && separator1 < int(firstLine.size()))
					separator1++;
				std::string_view methodName = firstLine.substr(0, separator1);
				if (methodName == "GET")
					requestType = GET_REQUEST;
				else if (methodName == "POST")
					requestType = POST_REQUEST;
				else
					requestType = WEIRD_REQUEST;

				separator1++;
				int separator2 = separator1;
				while (firstLine[separator2] != ' ' && separator2 < int(firstLine.size()))
					separator2++;
				path = std::pair<int, int>(separator1, separator2 - separator1);
				separator2++;
				int separator3 = separator2;
				while (firstLine[separator3] != '\r' && separator3 < int(firstLine.size()))
					separator3++;
				std::string_view protocol = firstLine.substr(separator2, separator3 - separator2);
				if (protocol != "HTTP/1.1" && protocol != "HTTP/1.0")
					requestType = WEIRD_REQUEST;
				return true;
			}
			virtual void headerReader(std::string_view name, std::string_view value, std::pair<int, int> location) override {
				if (name == "content-type") {
					contentType = location;
				} else if (name == "connection") {
					if (value == "close")
						ending = ServerReaction::DISCONNECT;
				} // Ignore others
			}
		};

		ParseState _state;
	public:
		std::pair<ServerReaction, int64_t> respond(
					std::span<char> input, Callback<void(std::span<const char>)> writer) override {
			auto restore = [this] () {
				_state.reset();
				_state.requestType = ParseState::UNINVESTIGATED_REQUEST;
			};

			// Locate the header's span
			if (_state.bodySize == -1) {
				auto [reaction, position] = _state.parse(input);
				if (reaction != ServerReaction::OK) {
					restore();
					return {reaction, position};
				}
			}

			if (_state.requestType != ParseState::POST_REQUEST)
				_state.bodySize = 0;
			int consuming = _state.parsePosition + _state.bodySize;
			
			// All body has beed read
			if (_state.requestType == ParseState::GET_REQUEST || _state.requestType == ParseState::POST_REQUEST) [[likely]] {
				if (_state.requestType == ParseState::POST_REQUEST) {
					if (_state.bodySize == -1 || int(input.size()) < _state.parsePosition + _state.bodySize) {
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
					response += "\r\nContent-Type: ";
					response += contentType;
					response += "\r\n\r\n";
					headerSize = response.size();
					return response;
				};
				std::string_view path{input.data() + _state.path.first, size_t(_state.path.second)};
				try {
					if (_state.requestType == ParseState::GET_REQUEST) {
						success = _responders.getResponder->get(path, correctResponseWriter);
						if (!success) [[unlikely]] {
							constexpr std::string_view errorMessage =
									"HTTP/1.1 404 Not Found\r\n"
									"Content-Length: 73\r\n\r\n"
									"<!doctype html><html lang=en><title>Error 404: Resource not found</title>";
							writer(std::span<const char>(errorMessage.begin(), errorMessage.size()));
						}
					} else {
						std::span<char> body = {input.begin() + _state.parsePosition, input.begin() + _state.parsePosition + _state.bodySize};
						std::string_view contentType = {input.data() + _state.contentType.first, size_t(_state.contentType.second)};
						success = _responders.postResponder->post(path, contentType, body, correctResponseWriter);
						if (!success) [[unlikely]] {
							constexpr std::string_view errorMessage =
									"HTTP/1.1 400 Bad Request\r\n"
									"Content-Length: 66\r\n\r\n"
									"<!doctype html><html lang=en><title>Error 400: Bad request</title>";
							writer(std::span<const char>(errorMessage.begin(), errorMessage.size()));
						}
					}
				} catch (...) {
					constexpr std::string_view errorMessage =
							"HTTP/1.1 500 Internal Server Error\r\n"
							"Content-Length: 76\r\n\r\n"
							"<!doctype html><html lang=en><title>Error 500: Internal server error</title>";
					writer(std::span<const char>(errorMessage.begin(), errorMessage.size()));
				}
				if (success) {
					if (startedResponse) {
						std::string_view responseView = response;
						// For whatever reasons, std::string_view::operator[] chooses the const version
						std::to_chars(const_cast<char*>(&responseView[sizeof(correctIntro)]),
								const_cast<char*>(&responseView[sizeof(correctIntro) + sizeof(unsetSize)]),
								response.size() - headerSize);
						writer(response);
						restore();
					} else {
						constexpr std::string_view errorMessage = "HTTP/1.1 204 No Content\r\n\r\n";
						writer(std::span<const char>(errorMessage.begin(), errorMessage.size()));
						restore();
					}
				}
			} else {
				constexpr std::string_view errorMessage =
									"HTTP/1.1 501 Method Not Implemented\n\r"
									"Content-Length: 77\r\n\r\n"
									"<!doctype html><html lang=en><title>Error 501: Method not implemented</title>";
				writer(std::span<const char>(errorMessage.begin(), errorMessage.size()));
				restore();
			}
			return {_state.ending, consuming};
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

		return post("application/x-www-form-urlencoded",
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

	struct ParseState : Detail::HttpParseState {
		int resultCode = 0;

		virtual bool firstLineReader(std::string_view firstLine) override {
			int separator1 = 0;
			while (firstLine[separator1] != ' ' && separator1 < firstLine.size())
				separator1++;
			separator1++;
			int separator2 = separator1 + 1;
			while (firstLine[separator2] != ' ' && separator2 < firstLine.size())
				separator2++;
			std::from_chars(&firstLine[separator1], &firstLine[separator2], resultCode);
			return true;
		}
		virtual void headerReader(std::string_view, std::string_view, std::pair<int, int>) override { }
	};

public:
	using StringType = StringT;
	HttpClient(ITcpClient* client, std::string_view virtualHost) : _client(client), _virtualHost(virtualHost) {}

	RequestToken post(std::string_view contentType,
			Callback<void(StringType&, RequestToken token)> request) {
		_lastTokenWritten.id++;
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
			ParseState state;
			_client->getResponse(token, [&, this] (std::span<char> input, bool identified)
						-> std::tuple<ServerReaction, RequestToken, int64_t> {
				// Locate the header's span
				if (state.bodySize == -1) {
					auto [reaction, position] = state.parse(input);
					if (reaction != ServerReaction::OK) {
						return {reaction, RequestToken{}, position};
					}
				}

				if (state.bodySize > 0 && int(input.size()) < state.parsePosition + state.bodySize) {
					return {ServerReaction::READ_ON, RequestToken{}, input.size()};
				}
			
				if (!identified && token != RequestToken{ _lastTokenRead.id + 1}) {
					int offset = state.parsePosition + std::max(0, state.bodySize);
					state = ParseState{};
					_lastTokenRead.id++;
					return {ServerReaction::WRONG_REPLY, _lastTokenRead, offset};
				}					
				obtained = true;

				reader(std::span<char>(input.begin() + state.parsePosition, input.begin() + state.parsePosition + state.bodySize),
						(state.resultCode >= 200 && state.resultCode < 300));
				if (!identified)
					_lastTokenRead.id++;
				return {ServerReaction::OK, _lastTokenRead, state.parsePosition + state.bodySize};
			});
		}
	}
};

} // namespace Bomba

#endif // BOMBA_HTTP
