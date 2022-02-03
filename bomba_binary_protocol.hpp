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
	static std::array<char, sizeof(T)> prepareNumber(T value) {
		std::array<char, sizeof(T)> bytes;
		memcpy(bytes.data(), &value, bytes.size());
		if constexpr (std::endian::native == std::endian::big)
			std::reverse(bytes.begin(), bytes.end());
		else static_assert (std::endian::native == std::endian::little, "Weird endian, can't use LittleEndianNumberFormat");
		return bytes;
	}

	template <typename T>
	static void writeNumber(T value, SerialisationFlags::Flags flags, OutputStringType& output) {
		SerialisationFlags::typeWithFlags(flags, [&] (auto typedValue) {
			typedValue = value;
			std::array<char, sizeof(typedValue)> bytes = prepareNumber(typedValue);
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
	{ T::prepareNumber(int(0)) } -> std::same_as<std::array<char, sizeof(int)>>;
	T::writeNumber(1, flags, output);
	{ T::template readNumber<int64_t>(flags, std::span<const char>()) } -> std::same_as<std::tuple<bool, int64_t, int>>;
	{ T::template readNumber<double>(flags, std::span<const char>()) } -> std::same_as<std::tuple<bool, double, int>>;
};

template <AssembledString OutputStringType = std::string, typename SizeType = uint16_t,
				BinaryIntegerFormat<OutputStringType> NumWriter = LittleEndianNumberFormat<OutputStringType>, int MaxDepth = 3>
struct BinaryFormat {
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

		int position() {
			return _position;
		}
		
		
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

template <typename SizeType = uint16_t, std::derived_from<GeneralisedBuffer> ExpandingBufferType = ExpandingBuffer<>,
		  BinaryIntegerFormat<ExpandingBufferType> NumWriter = LittleEndianNumberFormat<ExpandingBufferType>, int MaxDepth = 3>
class BinaryProtocolServer {
	IRemoteCallable& callable;

public:
	BinaryProtocolServer(IRemoteCallable& callable) : callable(callable) {}

	class Session : public ITcpResponder {
		BinaryProtocolServer& parent;
		Session(BinaryProtocolServer& parent) : parent(parent) {}
		friend class BinaryProtocolServer;

	public:
		std::pair<ServerReaction, int64_t> respond(
				std::span<char> input, Callback<void(std::span<const char>)> writer) override {
			if (input.size() < sizeof(int) + sizeof(SizeType))
				return {ServerReaction::READ_ON, 0};

			ExpandingBufferType outputBuffer;
			typename BinaryFormat<decltype(outputBuffer), SizeType, NumWriter>::Input in(std::string_view(input.data(), input.size()));

			int messageId = in.readInt(SerialisationFlags::UINT_32);
			int inputSize = in.readInt(SerialisationFlags::typeToFlags(SizeType())); // We don't need the size
			if (input.size() < inputSize)
				return {ServerReaction::READ_ON, 0};

			const IRemoteCallable* target = &parent.callable;
			while (target->getChild(0)) {
				int request = in.readInt(SerialisationFlags::UINT_8);
				target = target->getChild(request);
				if (!target)
					return {ServerReaction::DISCONNECT, 0};
			}

			typename BinaryFormat<decltype(outputBuffer), SizeType, NumWriter>::Output out(outputBuffer);
			out.writeInt(SerialisationFlags::UINT_32, messageId);
			auto sizePosition = outputBuffer.size();
			out.writeInt(SerialisationFlags::typeToFlags(SizeType()), 0);

			target->call(&in, out, [&] {}, [&] (std::string_view problem) {
				out.writeString(SerialisationFlags::typeToFlags(SizeType()), problem);
			});
			SizeType outputSize = outputBuffer.size();
			memcpy(&std::span<char>(outputBuffer)[sizePosition], &outputSize, sizeof(outputSize));

			std::string_view response = outputBuffer;
			writer({response.data(), response.size()});

			return {ServerReaction::OK, in.position()};
		}
	};

	Session getSession() {
		return Session(*this);
	}
};

template <typename SizeType = uint16_t, std::derived_from<GeneralisedBuffer> ExpandingBufferType = ExpandingBuffer<>,
		  BinaryIntegerFormat<ExpandingBufferType> NumWriter = LittleEndianNumberFormat<ExpandingBufferType>, int MaxDepth = 3>
class BinaryProtocolClient : public IRpcResponder {
	ITcpClient& _tcpClient;
	uint32_t _sendOrder = 0;

	auto getResponseReader(RequestToken token, Callback<void(IStructuredInput&)> operation) {
		return [=] (std::span<char> data, bool identified)
						-> std::tuple<ServerReaction, RequestToken, int64_t> {
			typename BinaryFormat<ExpandingBufferType, SizeType, NumWriter, MaxDepth>::Input in(std::string_view(data.data(), data.size()));
			if (!identified) {
				if (data.size() < sizeof(int) + sizeof(SizeType))
					return {ServerReaction::READ_ON, RequestToken(), 0};
			}
			RequestToken receivedToken = { uint32_t(in.readInt(SerialisationFlags::UINT_32)) };
			SizeType size = in.readInt(SerialisationFlags::typeToFlags(SizeType()));
			if (data.size() < size)
				return {ServerReaction::READ_ON, receivedToken, 0};
			if (!identified && receivedToken != token)
				return {ServerReaction::WRONG_REPLY, receivedToken, size};

			operation(in);
			return {ServerReaction::OK, token, size};
		};
	}

public:
	BinaryProtocolClient(IRemoteCallable& callable, ITcpClient& tcpClient) : _tcpClient(tcpClient) {
		callable.setResponder(*this);
	}

	RequestToken send(UserId, const IRemoteCallable* method, Callback<void(IStructuredOutput&, RequestToken)> request) override {
		ExpandingBufferType outputBuffer;
		typename BinaryFormat<decltype(outputBuffer), SizeType, NumWriter>::Output out(outputBuffer);
		out.writeInt(SerialisationFlags::UINT_32, _sendOrder);
		RequestToken token{_sendOrder};
		_sendOrder++;
		auto sizePosition = outputBuffer.size();
		out.writeInt(SerialisationFlags::typeToFlags(SizeType()), 0);

		std::array<uint8_t, MaxDepth> path{};
		int depth = 0;
		const IRemoteCallable* parent = method->parent();
		for (const IRemoteCallable* it = method; parent; it = parent, parent = parent->parent(), depth++) {
			if (depth >= MaxDepth) [[unlikely]] throw std::logic_error("Max depth exceeded, set a greater depth as template argument");
			if (depth > 0)
				memmove(&path[1], &path[0], depth);
			path[0] = parent->childName(it).second;
		}
		for (int i = 0; i < depth; i++)
			out.writeInt(SerialisationFlags::UINT_8, path[i]);
		request(out, token);

		// FIXME: Use the NumWriter to deal with endianness, also in the copy above
		std::array<char, sizeof(SizeType)> writtenSize = NumWriter::prepareNumber(SizeType(outputBuffer.size()));
		std::span<char> writing = outputBuffer;
		memcpy(&writing[sizePosition], &writtenSize, sizeof(writtenSize));
		_tcpClient.writeRequest(outputBuffer);
		return token;
	}
	bool getResponse(RequestToken token, Callback<void(IStructuredInput&)> reader) override {
		_tcpClient.getResponse(token, getResponseReader(token, [&] (IStructuredInput& input) { reader(input); }));
		return true;
	}
	bool hasResponse(RequestToken token) override {
		bool hasIt = false;
		_tcpClient.tryToGetResponse(token, getResponseReader(token, [&] (IStructuredInput&) { hasIt = true; }));
		return hasIt;
	}
};


} // namespace Bomba
#endif // BOMBA_BINARY_PROTOCOL
