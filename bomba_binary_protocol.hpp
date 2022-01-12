#ifndef BOMBA_BINARY_PROTOCOL
#define BOMBA_BINARY_PROTOCOL

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif

#include <array>
#include <string_view>
#include <charconv>
#include <cstring>
#include <cmath>
#include <bit>

namespace Bomba {

template <AssembledString OutputStringType>
struct LittleEndianNumberFormat {
	template <typename T>
	static void writeNumber(T value, SerialisationFlags::Flags flags, OutputStringType& output) {
		SerialisationFlags::typeWithFlags(flags, [&] (auto typedValue) {
			typedValue = value;
			std::array<char, sizeof(typedValue)> bytes;
			memcpy(bytes.data(), &typedValue, bytes.size());
			if constexpr (std::endian::native == std::endian::big)
				std::reverse(bytes.begin(), bytes.end());
			else static_assert (std::endian::native == std::endian::little, "Weird endian, can't use LittleEndianNumberFormat");
			output += std::string_view(bytes.data(), bytes.size());
		});
	}

	template <typename T>
	static std::tuple<bool /*success*/, T /*value*/, int /*bytesRead*/> readNumber(SerialisationFlags::Flags flags, std::span<const char> data) {
		return SerialisationFlags::typeWithFlags(flags, [&] (auto typedValue) -> std::tuple<bool, T, int> {
			if (data.size() < sizeof(typedValue))
				return {false, 0 , 0};

			std::array<char, sizeof(decltype(typedValue))> bytes;
			memcpy(bytes.data(), data.data(), bytes.size());
			if constexpr (std::endian::native == std::endian::big)
				std::reverse(bytes.begin(), bytes.end());
			else static_assert (std::endian::native == std::endian::little, "Weird endian, can't use LittleEndianNumberFormat");
			memcpy(&typedValue, bytes.data(), sizeof(decltype(typedValue)));
			return {true, typedValue, sizeof(typedValue)};
		});
	}
};

template <typename T, typename OutputStringType>
concept BinaryIntegerFormat = requires(OutputStringType& output, SerialisationFlags::Flags flags) {
	T::writeNumber(1, flags, output);
	{ T::template readNumber<int64_t>(flags, std::span<const char>()) } -> std::same_as<std::tuple<bool, int64_t, int>>;
	{ T::template readNumber<double>(flags, std::span<const char>()) } -> std::same_as<std::tuple<bool, double, int>>;
};

template <AssembledString OutputStringType = std::string, typename SizeType = uint16_t,
				BinaryIntegerFormat<OutputStringType> NumWriter = LittleEndianNumberFormat<OutputStringType>, int MaxDepth = 3>
struct BinaryProtocol {
	class Input : public IStructuredInput {
		std::span<const char> _contents;
		int _position = 0;
		std::array<int, MaxDepth> _sizes;
		int _depth = -1;

		template <typename T>
		T readNumber(Flags flags) {
			auto [success, value, bytesRead] = NumWriter::template readNumber<T>(
							flags, std::span<const char>(_contents.begin() + _position, _contents.end()));
			if (!success) [[unlikely]] {
				parseError("Incomplete request");
				good = false;
				return 0;
			}
			_position += bytesRead;
			return value;
		}
		int64_t readSize(Flags flags) {
			if (flags & SerialisationFlags::DETERMINED_NUMERIC_TYPE)
				readInt(flags);
			return readInt(SerialisationFlags::Flags(flags | SerialisationFlags::typeToFlags(SizeType())));
		}
	public:
		Input(std::string_view contents) : _contents(contents.data(), contents.size()) {}
		
		
		MemberType identifyType(Flags) final override {
			return TYPE_INVALID; // Not supported
		}
		
		int64_t readInt(Flags flags) final override {
			return readNumber<int64_t>(flags);
		}
		double readFloat(Flags flags) final override {
			return readNumber<double>(flags);
		}
		std::string_view readString(Flags flags) final override {
			auto length = readSize(flags);
			if (_position + length > _contents.size()) [[unlikely]] {
				parseError("Incomplete request");
				good = false;
				return "";
			}
			std::string_view result{_contents.begin() + _position, _contents.begin() + _position + length};
			_position += length;
			return result;
		}
		bool readBool(Flags) final override {
			if (_position + sizeof(bool) > _contents.size()) [[unlikely]] {
				parseError("Incomplete request");
				good = false;
				return false;
			}

			bool result = _contents[_position];
			_position++;
			return result;
		}
		void readNull(Flags) final override {
		}
		
		void startReadingArray(Flags flags) final override {
			_depth++;
			if (_depth == MaxDepth)
				throw std::logic_error("Maximal depth of nested arrays exceeded, increase it in the template arguments!");
			_sizes[_depth] = readSize(flags);
		}
		bool nextArrayElement(Flags) final override {
			_sizes[_depth]--;
			return (_sizes[_depth] > 0);
		}
		void endReadingArray(Flags) final override {
			_sizes[_depth] = -1;
			_depth--;
		}

		void readObject(Flags flags, Callback<bool(std::optional<std::string_view> memberName, int index)> onEach) override {
			if (flags & SerialisationFlags::OBJECT_LAYOUT_KNOWN) {
				int index = 0;
				while (onEach(std::nullopt, index)) {
					index++;
				}
			} else {
				int64_t size = readSize(flags);
				for (int index = 0; index < size; index++) {
					std::string_view name = readString(flags);
					if (!onEach(name, index))
						break;
				}
			}
		}
		void skipObjectElement(Flags) final override {
			throw std::logic_error("Can't skip elements in a binary protocol");
		}
		

		Location storePosition(Flags) final override {
			return Location{ _position };
		}
		void restorePosition(Flags, Location location) final override {
			_position = location.loc;
		}
	};

	class Output : public IStructuredOutput {
		OutputStringType& _contents;
		
		void writeSize(SerialisationFlags::Flags flags, int64_t size) {
			if (flags & SerialisationFlags::DETERMINED_NUMERIC_TYPE)
				writeInt(flags, size);
			return writeInt(SerialisationFlags::Flags(flags | SerialisationFlags::typeToFlags(SizeType())), size);
		}
	public:
		Output(OutputStringType& contents) : _contents(contents) {}
		
		void writeInt(Flags flags, int64_t value) final override {
			NumWriter::writeNumber(value, flags, _contents);
		}
		void writeFloat(Flags flags, double value) final override {
			NumWriter::writeNumber(value, flags, _contents);
		}
		void writeString(Flags flags, std::string_view value) final override {
			writeSize(flags, value.size());
			_contents += value;
		}
		void writeBool(Flags flags, bool value) final override {
			NumWriter::template writeNumber<int8_t>(value, SerialisationFlags::Flags(flags | SerialisationFlags::INT_8), _contents);
		}
		void writeNull(Flags) final override {
		}
		
		void startWritingArray(Flags flags, int size) final override {
			if (size == IStructuredOutput::UNKNOWN_SIZE)
				throw std::logic_error("Size must be known with Bomba::BinaryProtocol");
			writeSize(flags, size);
		}
		void introduceArrayElement(Flags, int) final override {
		}
		void endWritingArray(Flags) final override {
		}
		
		void startWritingObject(Flags flags, int size) final override {
			if (!(flags & SerialisationFlags::OBJECT_LAYOUT_KNOWN)) {
				if (size == IStructuredOutput::UNKNOWN_SIZE)
					throw std::logic_error("Size must be known with Bomba::BinaryProtocol");
				writeSize(flags, size);
			}
		}
		void introduceObjectMember(Flags flags, std::string_view name, int) final override {
			if (!(flags & SerialisationFlags::OBJECT_LAYOUT_KNOWN)) {
				writeString(flags, name);
			}
		}
		void endWritingObject(Flags) final override {
		}
	};
};



} // namespace Bomba
#endif // BOMBA_BINARY_PROTOCOL
