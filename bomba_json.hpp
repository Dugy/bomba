#ifndef BOMBA_JSON
#define BOMBA_JSON

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif

#include <array>
#include <string_view>
#include <charconv>
#include <cstring>
#include <cmath>

namespace Bomba {

template <AssembledString StringType = std::string>
struct BasicJson {
	class Input : public IStructuredInput {
		std::string_view _contents;
		StringType _resultBuffer;
		int _position = 0;
		
		void endOfInput() {
			good = false;
			parseError("Unexpected end of JSON input");
		}
		
		void fail(const char* problem) {
			remoteError(problem);
			good = false;
		}
		
		bool isPastEnd(int position) {
			if (_contents.begin() + position < _contents.end()) [[likely]]
				return false;
				
			if (good) [[unlikely]]
				endOfInput();
			return true;
		}
		
		char getChar() {
			if (!isPastEnd(_position)) [[likely]] {
				char got =  _contents[_position];
				_position++;
				return got;
			}
			return '\0';
		}
		
		template <bool mandatory = true>
		char peekChar() {
			if (_contents.begin() + _position < _contents.end()) [[likely]]
				return _contents[_position];
			if constexpr(mandatory)
				endOfInput();
			return '\0';
		}
		
		void eatWhitespace() {
			char nextChar = peekChar<false>();
			while (nextChar == ' ' || nextChar == '\t' || nextChar == '\n' || nextChar == '\r'
						|| nextChar == ',' || nextChar == ':') {
				_position++;
				nextChar = peekChar<false>();		
			}
		}
		
	public:
		Input(std::string_view contents) : _contents(contents) {}
		
		
		MemberType identifyType(Flags flags) final override {
			eatWhitespace();
			if (_contents.begin() + _position >= _contents.end()) [[unlikely]]
				return TYPE_INVALID;
			
			char nextChar = peekChar();
			if (nextChar == '"')
				return TYPE_STRING;
			if (nextChar == '-' || (nextChar >= '0' && nextChar <= '9')) {
				for (int i = _position; _contents.begin() + i < _contents.end(); i++) {
					if (_contents[i] == '.' || _contents[i] == 'e' || _contents[i] == 'E')
						return TYPE_FLOAT;
					if (_contents[i] < '0' || _contents[i] > '9')
						return TYPE_INTEGER;
				}
				return TYPE_INTEGER;
			}
			if (nextChar == 'n')
				return TYPE_NULL;
			if (nextChar == 't' || nextChar == 'f')
				return TYPE_BOOLEAN;
			if (nextChar == '[')
				return TYPE_ARRAY;
			if (nextChar == '{')
				return TYPE_OBJECT;

			return TYPE_INVALID;
		}
		
		int64_t readInt(Flags flags) final override {
			eatWhitespace();
			int64_t result = 0;
			std::from_chars_result got = std::from_chars(&_contents[_position], &*_contents.end(), result);
			_position += got.ptr - &_contents[_position];
			if (got.ec != std::errc()) [[unlikely]]
				fail("Expected JSON integer");
			// Deal with floats sent instead of integers
			char next = peekChar();
			while ((next >= '0' && next <= '9') || next == '.' || next == 'e' || next == 'E' || next == '-') {
				_position++;
				next = peekChar();
			}
			return result;
		}
		double readFloat(Flags flags) final override {
			eatWhitespace();
			double result = 0;
			std::from_chars_result got = std::from_chars(&_contents[_position], &*_contents.end(), result);
			_position += got.ptr - &_contents[_position];
			if (got.ec != std::errc()) [[unlikely]]
				fail("Expected JSON double");
			return result;
		}
		std::string_view readString(Flags flags) final override {
			eatWhitespace();
			char readingChar = getChar();
			if (readingChar != '"') [[unlikely]] {
				std::cout << "Found " << readingChar << " instead of \"" << std::endl;
				fail("Expected JSON string");
			}
			
			_resultBuffer.clear();
			readingChar = getChar();
			while (readingChar != '"') {
				if (readingChar == '\\') [[unlikely]] {
					readingChar = getChar();
					if (readingChar == '\\')
						_resultBuffer += '\\';
					else if (readingChar == '"')
						_resultBuffer += '"';
					else if (readingChar == 'n')
						_resultBuffer += '\n';
				} else _resultBuffer += readingChar;
				readingChar = getChar();
			}
			return _resultBuffer;
		}
		bool readBool(Flags flags) final override {
			eatWhitespace();
			char readingChar = getChar();
			if (readingChar == 't') {
				for (auto& it : {'r', 'u', 'e'}) {
					readingChar = getChar();
					if (it != readingChar)
						fail("Expected JSON bool");
				}
				return true;
			} else if (readingChar == 'f') {
				for (auto& it : {'a', 'l', 's', 'e'}) {
					readingChar = getChar();
					if (it != readingChar)
						fail("Expected JSON bool");
				}
				return false;
			} else [[unlikely]] fail("Expected JSON bool");
			return false;
		}
		void readNull(Flags flags) final override {
			eatWhitespace();
			for (auto& it : {'n', 'u', 'l', 'l'}) {
				char readingChar = getChar();
				if (it != readingChar)
					fail("Expected JSON null");
			}
		}
		
		void startReadingArray(Flags flags) final override {
			eatWhitespace();
			char readingChar = getChar();
			if (readingChar != '[')
				fail("Expected JSON array");
			
		}
		bool nextArrayElement(Flags flags) final override {
			eatWhitespace();
			if (peekChar() != ']')
				return true;
			return false;
		}
		void endReadingArray(Flags flags) final override {
			eatWhitespace();
			getChar();
		}
		
		void startReadingObject(Flags flags) final override {
			eatWhitespace();
			char readingChar = getChar();
			if (readingChar != '{')
				fail("Expected JSON object");
		}
		std::optional<std::string_view> nextObjectElement(Flags flags) final override {
			eatWhitespace();
			if (peekChar() != '}') {
				return readString(flags);
			}
			return std::nullopt;
		}
		void skipObjectElement(Flags flags) final override {
			eatWhitespace();
			int depth = 0;
			do {
				char readingChar = peekChar();
				if (readingChar == '{' || readingChar == '[') {
					depth++;
					getChar();
				} else if (readingChar == '}' || readingChar == ']') {
					depth--;
					getChar();
					if (depth <= 0)
						break;
				} else if (depth == 0 && (readingChar == ' ' || readingChar == '\t'
						|| readingChar == '\r' || readingChar == '\n' || readingChar == ',')) {
					break; // eatWhitespace() was called before
				} else if (readingChar == '"') {
					getChar();
					do {
						readingChar = getChar();
					} while (readingChar != '"' && _contents[_position - 1] != '\\');
				} else { // Numbers, bool, null...
					getChar();
				}
			} while (_contents.begin() + _position < _contents.end());
		}
		void endReadingObject(Flags flags) final override {
			eatWhitespace();
			getChar();
		}
		

		Location storePosition(Flags flags) final override {
			return Location{ _position };
		}
		void restorePosition(Flags flags, Location location) final override {
			_position = location.loc;
		}
	};

	class Output : public IStructuredOutput {
		StringType& _contents;
		int _depth = 0;
		bool _skipNewline = false;
		
		template <typename Num>
		void writeValue(Num value) {
			constexpr int SIZE = 20;
			std::array<char, SIZE> bytes = {};
			std::to_chars(bytes.data(), bytes.data() + SIZE, value);
			_contents += &bytes[0];
		}
		void newLine() {
			if (_skipNewline) [[unlikely]] {
				_skipNewline = false;
				return;
			}
			
			_contents += '\n';
			for (int i = 0; i < _depth; i++)
				_contents += '\t';
		}
	public:
		Output(StringType& contents) : _contents(contents) {}
		
		void writeInt(Flags, int64_t value) final override {
			writeValue(value);
		}
		void writeFloat(Flags flags, double value) final override {
			writeValue(value);
		}
		void writeString(Flags flags, std::string_view value) final override {
			_contents += '"';
			for (const char it : value) {
				if (it == '\\' || it == '\n' || it == '"') [[unlikely]]
					_contents += '\\';
				_contents += it;
			}
			_contents += '"';
		}
		void writeBool(Flags flags, bool value) final override {
			if (value)
				_contents += "true";
			else
				_contents += "false";
		}
		void writeNull(Flags flags) final override {
			_contents += "null";
		}
		
		void startWritingArray(Flags flags, int size) final override {
			_contents += '[';
			_depth++;
			if (size == 0)
				_skipNewline = true;
		}
		void introduceArrayElement(Flags flags, int index) final override {
			if (index > 0)
				_contents += ',';
			newLine();
		}
		void endWritingArray(Flags flags) final override {
			_depth--;
			newLine();
			_contents += ']';
		}
		
		void startWritingObject(Flags flags, int size) final override {
			_contents += '{';
			_depth++;
			if (size == 0)
				_skipNewline = true;
		}
		void introduceObjectMember(Flags flags, std::string_view name, int index) final override {
			if (index > 0)
				_contents += ',';
			newLine();
			writeString(flags, name);
			_contents += " : ";
		}
		void endWritingObject(Flags flags) final override {
			_depth--;
			newLine();
			_contents += '}';
		}
	};
};



} // namespace Bomba
#endif // BOMBA_JSON
