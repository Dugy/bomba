#ifndef BOMBA_JSON
#define BOMBA_JSON
#include "bomba_core.hpp"
#include <array>
#include <string_view>
#include <charconv>
#include <sstream>
#include <cstring>
#include <cmath>

namespace Bomba {

template <AssembledString StringType = std::string>
struct BasicJson {
	class Input : public IStructuredInput {
		std::string_view _contents;
		int _position = 0;
		StringType _resultBuffer;
		
		void endOfInput() {
			good = false;
			parseError("Unexpected end of JSON input");
		}
		
		void fail(const char* problem) {
			parseError(problem);
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
		
		
		MemberType identifyType() final override {
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
			return result;
		}
		double readFloat(Flags flags) final override {
			eatWhitespace();
			int endPosition = _position;
			while (_contents.begin() + endPosition < _contents.end()
					&& ((_contents[endPosition] >= '0' && _contents[endPosition] <= '9')
						|| _contents[endPosition] == 'e' || _contents[endPosition] == 'E'
						|| _contents[endPosition] == '-' || _contents[endPosition] == '.')) {
				endPosition++;
			}
			std::stringstream reading(std::string(&_contents[_position], &_contents[endPosition]));
			reading.imbue(std::locale("C"));
			double result = 0;
			reading >> result;
			_position = endPosition;
			return result;
		}
		std::string_view readString(Flags flags) final override {
			eatWhitespace();
			char readingChar = getChar();
			if (readingChar != '"') [[unlikely]]
				fail("Expected JSON string");
			
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
			getChar();
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
			} else
				getChar();
			return std::nullopt;
		}
		bool seekObjectElement(Flags flags, std::string_view name) final override {
			eatWhitespace();
			char readingChar = getChar();
			if (readingChar != '{') [[unlikely]]
				fail("Excepted JSON object");
			int depth = 1;
			bool leftSide = true;
			while (depth > 0) {
				char readingChar = getChar();
				if (readingChar == '{')
					depth++;
				else if (readingChar == '}')
					depth--;
				else if (depth == 1) {
					if (readingChar == ':')
						leftSide = false;
					else if (readingChar == ',')
						leftSide = true;

					if (leftSide == true) {
						leftSide = false;
						auto property = nextObjectElement(flags);
						if (!property) [[unlikely]]
							fail("Expected JSON property name");
						else if (*property == name)
							return true;
					}
				}
				if (readingChar == '"') {
					do {
						readingChar = getChar();
					} while (readingChar != '"' && _contents[_position - 1] != '\\');
				}
			}
			getChar();
			return false;
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
		
		template <typename Num>
		void writeValueInternal(Num value) {
			constexpr int SIZE = 20;
			std::array<char, SIZE> bytes;
			memset(bytes.data(), 0, SIZE);
			std::to_chars(bytes.data(), bytes.data() + SIZE, value);
			_contents += &bytes[0];
		}
		void newLine() {
			_contents += '\n';
			for (int i = 0; i < _depth; i++)
				_contents += '\t';
		}
	public:
		Output(StringType& contents) : _contents(contents) {}
		
		void writeValue(Flags, int64_t value) final override {
			writeValueInternal(value);
		}
		void writeValue(Flags flags, double value) final override {
//			writeValueInternal(value); // GCC is missing std::to_chars for floating point numbers
			if (!std::isnan(value) && (!std::isinf(value))) [[likely]] {
				std::stringstream added;
				added.imbue(std::locale("C"));
				added << value;
				_contents += added.str().c_str();
			} else
				_contents += '0'; // NaN and inf not supported by Json
		}
		void writeValue(Flags flags, std::string_view value) final override {
			_contents += '"';
			for (const char it : value) {
				if (it == '\\' || it == '\n' || it == '"') [[unlikely]]
					_contents += '\\';
				_contents += it;
			}
			_contents += '"';
		}
		void writeValue(Flags flags, bool value) {
			if (value)
				_contents += "true";
			else
				_contents += "false";
		}
		void writeNull(Flags flags) {
			_contents += "null";
		}
		
		void startWritingArray(Flags flags) final override {
			_contents += '[';
			_depth++;
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
		
		void startWritingObject(Flags flags) final override {
			_contents += '{';
			_depth++;
		}
		void introduceObjectMember(Flags flags, std::string_view name, int index) final override {
			if (index > 0)
				_contents += ',';
			newLine();
			writeValue(flags, name);
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
