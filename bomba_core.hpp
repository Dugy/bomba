#ifndef BOMBA_CORE
#define BOMBA_CORE
#include <optional>
#include <array>
#include <string_view>
#include <span>
#include <tuple>
#include <cstring>

#ifndef BOMBA_ALTERNATIVE_ERROR_HANDLING
#include <stdexcept>
#endif

namespace Bomba {

#ifndef BOMBA_ALTERNATIVE_ERROR_HANDLING
struct ParseError : std::runtime_error {
	using std::runtime_error::runtime_error;
};
void parseError(std::string_view problem) {
	throw ParseError(std::string(problem));
}

struct MethodNotFoundError : std::runtime_error {
	using std::runtime_error::runtime_error;
};
void methodNotFoundError(std::string_view problem) {
	throw MethodNotFoundError(std::string(problem));
}

struct RemoteError : std::runtime_error {
	using std::runtime_error::runtime_error;
};
void remoteError(std::string_view problem) {
	throw RemoteError(std::string(problem));
}

void logicError(std::string_view problem) {
	throw std::logic_error(std::string(problem));
}
#endif

template <typename T = void()>
struct Callback {
	// This should never be instantiated
	static_assert(std::is_same_v<T, T>, "Invalid callback signature");
};

template <typename Functor, typename Returned, typename... Args>
concept FunctorForCallbackConstruction = requires(Functor functor, const Args&... args) {
	{ functor(args...) } -> std::same_as<Returned>;
};

template <typename Returned, typename... Args>
class Callback<Returned(Args...)> {
	void* closure = nullptr;
	Returned (*typeErased)(void*, const Args&...) = [] (void*, const Args&...) {};
public:
	Returned operator() (const Args&... args) const {
		return typeErased(closure, args...);
	}

	Callback() = default;
	template <FunctorForCallbackConstruction<Returned, Args...> T>
	Callback(const T& lambda)
			: closure(reinterpret_cast<void*>(const_cast<T*>(&lambda)))
			, typeErased([] (void* closure, const Args&... args) {
		return reinterpret_cast<T*>(closure)->operator()(args...);
	}) {}
};

template <typename Resource, typename Destruction>
auto makeRaiiContainer(Resource&& resource, Destruction&& destruction) {
	class RaiiContainer {
		Resource _resource;
		Destruction _destruction;
	public:
		RaiiContainer(Resource&& resource, Destruction&& destruction)
				: _resource(std::move(resource)), _destruction(std::move(destruction)) {}
		~RaiiContainer() {
			_destruction();
		}
		Resource* operator->() {
			return _resource;
		}
		Resource& operator*() {
			return *_resource;
		}
	};
	return RaiiContainer(std::move(resource), std::move(destruction));
}

struct Float16Placeholder {
	uint16_t data;
	Float16Placeholder& operator=(float value) {
		uint32_t asInt = reinterpret_cast<uint32_t&>(value);
		data = ((asInt >> 16) & 0x8000) | ((((asInt & 0x7f800000) - 0x38000000) >> 13 ) & 0x7c00) | ((asInt >> 13) & 0x03ff);
		return *this;
	}
	operator float() const {
		return ((data & 0x8000) << 16) | (((data & 0x7c00) + 0x1C000) << 13) | ((data & 0x03ff) << 13);
	}
};

namespace SerialisationFlags {
	// Flags for altering the behaviour of ISerialisedOutput and ISerialisedInput interfaces
	enum Flags {
		NONE = 0, // No flags, default value
		OBJECT_LAYOUT_KNOWN = 0x1, // Declares that the data is a known type and doesn't need names of elements
		MANDATORY = 0x2, // Fail instead of returning default values even if the format supports it (not implemented)
		OMIT_FALSE = 0x4, // Skip the entry if it's bool and it's false
		EMPTY_IS_NULL = 0x8, // Skip the entry if it's null

		// Declare the specific numeric type for formats where it matters (mutually exclusive)
		INT_8 = 0x100,
		UINT_8 = 0x110,
		INT_16 = 0x120,
		UINT_16 = 0x130,
		INT_32 = 0x140,
		UINT_32 = 0x150,
		INT_64 = 0x160,
		UINT_64 = 0x170,
		FLOAT_16 = 0x180,
		FLOAT_32 = 0x190,
		FLOAT_64 = 0x1a0,
		DETERMINED_NUMERIC_TYPE = 0x1f0, // Has all the bits used by numeric types at 1
	};

	template <typename T>
	Flags typeToFlags(T) {
		if constexpr(std::is_same_v<T, int8_t>) return INT_8;
		else if constexpr(std::is_same_v<T, uint8_t>) return UINT_8;
		else if constexpr(std::is_same_v<T, int16_t>) return INT_16;
		else if constexpr(std::is_same_v<T, uint16_t>) return UINT_16;
		else if constexpr(std::is_same_v<T, int32_t>) return INT_32;
		else if constexpr(std::is_same_v<T, uint32_t>) return UINT_32;
		else if constexpr(std::is_same_v<T, int64_t>) return INT_64;
		else if constexpr(std::is_same_v<T, uint64_t>) return UINT_64;
		else if constexpr(std::is_same_v<T, Float16Placeholder>) return FLOAT_16;
		else if constexpr(std::is_same_v<T, float>) return FLOAT_32;
		else if constexpr(std::is_same_v<T, double>) return FLOAT_64;
	}

	template <typename Functor>
	auto typeWithFlags(Flags flags, const Functor& callback) {
		Flags cleaned = Flags(flags & DETERMINED_NUMERIC_TYPE);
		if (!cleaned)
			cleaned = INT_32;

		if (cleaned == INT_8) return callback(int8_t());
		else if (cleaned == UINT_8) return callback(uint8_t());
		else if (cleaned == INT_16) return callback(int16_t());
		else if (cleaned == UINT_16) return callback(uint16_t());
		else if (cleaned == INT_32) return callback(int32_t());
		else if (cleaned == UINT_32) return callback(uint32_t());
		else if (cleaned == INT_64) return callback(int64_t());
		else if (cleaned == UINT_64) return callback(uint64_t());
		else if (cleaned == FLOAT_16) return callback(Float16Placeholder());
		else if (cleaned == FLOAT_32) return callback(float());
		else if (cleaned == FLOAT_64) return callback(double());
		else throw std::logic_error("Weird numeric type");
	}
};

template <typename T>
struct TypedSerialiser {
	static_assert(!std::is_same_v<T, T>, "No specialisation for serialising this type");
};

struct IStructuredOutput {
	// Used by code that writes data or messages, implement it to create an output format
	using Flags = SerialisationFlags::Flags;

	// Should write an integer to the output
	virtual void writeInt(Flags flags, int64_t value) = 0;
	// Should write a floating-point number to the output
	virtual void writeFloat(Flags flags, double value) = 0;
	// Should write a string to the output
	virtual void writeString(Flags flags, std::string_view value) = 0;
	// Should write a boolean variable to the output
	virtual void writeBool(Flags flags, bool value) = 0;
	// Should write a null value to the output
	virtual void writeNull(Flags flags) = 0;
	
	// Array is written as startWritingArray, [introduceObjectElement, writeInt (or any other type)], endWritingArray
	constexpr static int UNKNOWN_SIZE = -1;
	// Called just before writing an array, if size is not known in advance, UNKNOWN_SIZE is used as size
	virtual void startWritingArray(Flags flags, int size) = 0;
	// Called before writing the value of any array element
	virtual void introduceArrayElement(Flags flags, int index) = 0;
	// Called after writing the last value in the array
	virtual void endWritingArray(Flags flags) = 0;
	
	// Object is written as startWritingObject, [introduceObjectMember, writeInt (or any other type], endWritingObject
	// Called just before writing an object, if number of elements is not known in advance, UNKNOWN_SIZE is used as size
	virtual void startWritingObject(Flags flags, int size) = 0;
	// Called before writing the value of any object element, name of element is needed (otherwise it's an array)
	virtual void introduceObjectMember(Flags flags, std::string_view name, int index) = 0;
	// Called after writing the last value in the object
	virtual void endWritingObject(Flags flags) = 0;

	// Should write an optional value, empty if it's absent and calling the callback to write the value if present
	virtual void writeOptional(Flags flags, bool present, Callback<> writeValue) = 0;

	virtual ~IStructuredOutput() = default;

	// Convenience classes and methods for using this interface in a less error prone way
	class ArrayFiller {
		int _index = 0;
		IStructuredOutput& _output;
		static constexpr SerialisationFlags::Flags NoFlags = SerialisationFlags::NONE;
		ArrayFiller(IStructuredOutput& output, int size) : _output(output) {
			output.startWritingArray(NoFlags, size);
		}
		friend struct IStructuredOutput;

	public:
		~ArrayFiller() {
			_output.endWritingArray(NoFlags);
		}

		void writeInt(int64_t value) { introduceMember().writeInt(NoFlags, value); }
		void writeFloat(double value) { introduceMember().writeFloat(NoFlags, value); }
		void writeString(std::string_view value) { introduceMember().writeString(NoFlags, value); }
		void writeBool(bool value) { introduceMember().writeBool(NoFlags, value); }
		void writeNull() { introduceMember().writeNull(NoFlags); }

		ArrayFiller writeArray(int size = UNKNOWN_SIZE) {
			return ArrayFiller(introduceMember(), size);
		}
		auto writeObject(int size = UNKNOWN_SIZE);
		IStructuredOutput& underlyingOutput() {
			return _output;
		}
		IStructuredOutput& introduceMember() {
			_output.introduceArrayElement(NoFlags, _index);
			_index++;
			return _output;
		}
	};
	ArrayFiller writeArray(int size = UNKNOWN_SIZE) {
		return ArrayFiller(*this, size);
	}


	class ObjectFiller {
		int _index = 0;
		IStructuredOutput& _output;
		static constexpr SerialisationFlags::Flags NoFlags = SerialisationFlags::NONE;
		ObjectFiller(IStructuredOutput& output, int size) : _output(output) {
			output.startWritingObject(NoFlags, size);
		}
		friend struct IStructuredOutput;

	public:
		~ObjectFiller() {
			_output.endWritingObject(NoFlags);
		}

		void writeInt(std::string_view name, int64_t value) { introduceMember(name).writeInt(NoFlags, value); }
		void writeFloat(std::string_view name, double value) { introduceMember(name).writeFloat(NoFlags, value); }
		void writeString(std::string_view name, std::string_view value) { introduceMember(name).writeString(NoFlags, value); }
		void writeBool(std::string_view name, bool value) { introduceMember(name).writeBool(NoFlags, value); }
		void writeNull(std::string_view name) { introduceMember(name).writeNull(NoFlags); }

		ArrayFiller writeArray(std::string_view name, int size = UNKNOWN_SIZE) {
			return ArrayFiller(introduceMember(name), size);
		}
		ObjectFiller writeObject(std::string_view name, int size = UNKNOWN_SIZE) {
			return ObjectFiller(introduceMember(name), size);
		}
		IStructuredOutput& underlyingOutput() {
			return _output;
		}
		IStructuredOutput& introduceMember(std::string_view name) {
			_output.introduceObjectMember(NoFlags, name, _index);
			_index++;
			return _output;
		}
	};
	ObjectFiller writeObject(int size = UNKNOWN_SIZE) {
		return ObjectFiller(*this, size);
	}
};

auto IStructuredOutput::ArrayFiller::writeObject(int size) {
	return ObjectFiller(introduceMember(), size);
}

struct NullStructredOutput : IStructuredOutput {
	void writeInt(Flags, int64_t) final override {}
	void writeFloat(Flags, double) final override {}
	void writeString(Flags, std::string_view) final override {}
	void writeBool(Flags, bool) final override {}
	void writeNull(Flags) final override {}

	void startWritingArray(Flags, int) final override {}
	void introduceArrayElement(Flags, int) final override {}
	void endWritingArray(Flags) final override {}

	void startWritingObject(Flags, int) final override {}
	void introduceObjectMember(Flags, std::string_view, int) final override {}
	void endWritingObject(Flags) final override {}

	void writeOptional(Flags, bool, Callback<>) final override {}
};

struct IStructuredInput {
	// Used by code that reads data or messages, implement it to create an input format
	using Flags = SerialisationFlags::Flags;
	
	enum MemberType {
		TYPE_INTEGER,
		TYPE_FLOAT,
		TYPE_STRING,
		TYPE_BOOLEAN,
		TYPE_NULL,
		TYPE_ARRAY,
		TYPE_OBJECT,
		TYPE_INVALID, // Other values are not guaranteed to be valid if the data is invalid
	};
	
	bool good = true;
	
	// Should return the type of the following element
	virtual MemberType identifyType(Flags flags) = 0;
	
	// Should parse an integer and return it (exact type may be supplied in flags)
	virtual int64_t readInt(Flags flags) = 0;
	// Should parse a floating point number and return it (exact type may be supplied in flags)
	virtual double readFloat(Flags flags) = 0;
	// Should parse a string and return a view of it (size type may be supplied in flags)
	virtual std::string_view readString(Flags flags) = 0;
	// Should parse a boolean and return it
	virtual bool readBool(Flags flags) = 0;
	// Should read a null (only to get the reading position behind it)
	virtual void readNull(Flags flags) = 0;
	
	// Should read an array, will be called as startReadingArray, [nextArrayElement, readInt (or something else)], endReadingArray
	// Should start reading an array
	virtual void startReadingArray(Flags flags) = 0;
	// Should read the introduction of the next array element, returns if it's the last element
	virtual bool nextArrayElement(Flags flags) = 0;
	// Should end reading an array
	virtual void endReadingArray(Flags flags) = 0;
	// TODO: Refactor to more functional style
	
	// Should read an object, calling the supplied callback on each element, with name (if available) and index
	// Should end on the last element and when the callback returns 0
	virtual void readObject(Flags flags, Callback<bool(std::optional<std::string_view> memberName, int index)> onEach) = 0;
	// Skips the next value
	virtual void skipObjectElement(Flags flags) = 0;

	// Should call the functor on the value if it's present and return if it was present
	virtual bool readOptional(Flags flags, Callback<> readValue) = 0;
	
	struct Location {
		constexpr static int UNINITIALISED = -1;
		int loc = UNINITIALISED;
		operator bool() {
			return (loc != UNINITIALISED);
		}
	};
	// Should return the current position to be returned there later
	virtual Location storePosition(Flags flags) = 0;
	// Should come back to a previously stored position
	virtual void restorePosition(Flags flags, Location location) = 0;
};

template<int Size>
struct StringLiteral {
	constexpr StringLiteral(const char (&str)[Size]) {
		std::copy_n(str, Size, value);
	}
	constexpr int size() const {
		return Size - 1;
	}
	constexpr const char* c_str() const {
		return value;
	}
	constexpr char& operator[](int index) {
		return value[index];
	}
	constexpr char operator[](int index) const {
		return value[index];
	}
	constexpr bool operator==(const char* other) const {
		return compare(other);
	}
	template <int Size2>
	constexpr bool operator==(const StringLiteral<Size2>& other) const {
		if constexpr(Size != Size2)
			return false;
		else
			return compare(other.c_str());
	}
	constexpr operator std::string_view() const {
		return std::string_view(c_str());
	}
	char value[Size];

private:
	template <int depth = 0>
	constexpr bool compare(const char* other) const {
		if constexpr(depth == Size)
			return true;
		else {
			if (value[depth] != *other)
				return false;
			return compare<depth + 1>(other + 1);
		}
	}
};

class GeneralisedBuffer {
	using BufferType = std::span<char>;
	BufferType _buffer;
	int _size = 0;

public:
	GeneralisedBuffer(const BufferType& buffer) : _buffer(buffer) {}

	GeneralisedBuffer& operator+=(char added) {
		*_buffer.begin() = added;
		_buffer = BufferType(_buffer.begin() + 1, _buffer.end());
		_size++;
		if (_buffer.size() == 0) {
			bufferFull();
		}
		return *this;
	}

	GeneralisedBuffer& operator+=(std::span<const char> added) {
		int position = 0;
		while (position < int(added.size())) {
			int toCopy = std::min<int>(added.size() - position, remainingSpace());
			memcpy(_buffer.data(), added.data() + position, toCopy);
			_buffer = BufferType(_buffer.begin() + toCopy, _buffer.end());
			_size += toCopy;
			if (_buffer.size() == 0) [[likely]] {
				if (!bufferFull()) return *this;
			}
			position += toCopy;
		}
		return *this;
	}

	GeneralisedBuffer& operator+=(std::string_view added) {
		return operator+=(std::span<const char>(added.begin(), added.end()));
	}

	GeneralisedBuffer& operator+=(const char* added) {
		return operator+=(std::span<const char>(added, strlen(added)));
	}

	int size() {
		return _size;
	}

protected:
	virtual bool bufferFull() = 0;
	void moveBuffer(const BufferType& newBuffer) {
		_buffer = newBuffer;
	}
	int remainingSpace() const {
		return _buffer.size();
	}
};

template <int StaticSize = 1024>
class StreamingBuffer : public GeneralisedBuffer {
protected:
	std::array<char, StaticSize> _basic;
	int _sizeAtLastFlush = 0;

	bool bufferFull() override {
		flush();
		_sizeAtLastFlush = size();
		moveBuffer({_basic.data(), StaticSize});
		return true;
	}

	virtual void flush() = 0;

public:
	StreamingBuffer() : GeneralisedBuffer({reinterpret_cast<char*>(&_basic), sizeof(_basic)}) { }
};

template <int StaticSize>
struct NonOwningStreamingBuffer : StreamingBuffer<StaticSize> {
	Callback<void(std::span<const char>)> writer;
	void flush() override {
		if (this->size() > this->_sizeAtLastFlush) {
			writer({this->_basic.data(), size_t(this->size() - this->_sizeAtLastFlush)});
			this->_sizeAtLastFlush = this->size();
		}
	}
	NonOwningStreamingBuffer(Callback<void(std::span<const char>)> writer) : writer(writer) {}
	~NonOwningStreamingBuffer() {
		flush();
	}
};


template <int StaticSize = 1024>
class NonExpandingBuffer : public GeneralisedBuffer {
	std::array<char, StaticSize> _basic;

	bool bufferFull() override {
		return false;
	}

public:
	NonExpandingBuffer() : GeneralisedBuffer({reinterpret_cast<char*>(&_basic), sizeof(_basic)}) {}

	operator std::span<char>() {
		return std::span<char>(_basic.data(), _basic.size() - remainingSpace());
	}
	operator std::string_view() const {
		return {_basic.data(), _basic.size() - remainingSpace()};
	}

	void clear() {
		moveBuffer({_basic.data(), _basic.size()});
	}
};

template <typename V>
concept CharVectorType = requires(V v) {
{ v.data() } -> std::same_as<char*>;
{ v.size() } -> std::integral;
v.resize(0);
{ std::span<char>{ v.begin(), v.end() - 1 } };
};

template <int StaticSize = 1024, CharVectorType Vector = std::string>
class ExpandingBuffer : public GeneralisedBuffer {
	std::array<char, StaticSize> _basic;
	Vector _extended;

	bool bufferFull() override {
		if (_extended.empty()) {
			_extended.resize(3 * StaticSize);
			memcpy(_extended.data(), _basic.data(), StaticSize);
			moveBuffer({&_extended[StaticSize], 2 * StaticSize});
		} else {
			size_t oldSize = _extended.size();
			_extended.resize(2 * oldSize);
			moveBuffer({&_extended[oldSize], oldSize});
		}
		return true;
	}

public:
	ExpandingBuffer() : GeneralisedBuffer({reinterpret_cast<char*>(&_basic), sizeof(_basic)}) {}

	operator std::span<char>() {
		if (_extended.empty()) {
			return {_basic.begin(), _basic.end() - remainingSpace()};
		} else {
			return std::span<char>{_extended.begin(), _extended.end() - remainingSpace()};
		}
	}

	operator std::string_view() const {
		if (_extended.empty()) {
			return {_basic.begin(), _basic.end() - remainingSpace()};
		} else {
			return {_extended.begin(), _extended.end() - remainingSpace()};
		}
	}

	void clear() {
		_extended.resize(0);
		moveBuffer({_basic.data(), _basic.size()});
	}
};

template <typename T>
concept AssembledString = requires(T str) {
	str	+= 'a';
	str += std::string_view("a");
};

template <typename T>
concept BetterAssembledString = std::is_constructible_v<T>
		&& std::is_same_v<T&, decltype(std::declval<T>() += 'a')>
		&& std::is_same_v<T&, decltype(std::declval<T>() += "a")>
		&& std::is_same_v<T&, decltype(std::declval<T>() += std::string_view("a"))>
		&& std::is_same_v<T&, decltype(std::declval<T>() += std::declval<T>())>
		&& std::is_convertible_v<T, std::string_view>
		&& std::is_void_v<decltype(std::declval<T>().clear())>;

template <typename T>
concept DataFormat = std::is_base_of_v<IStructuredOutput, typename T::Output>
		&& std::is_base_of_v<IStructuredInput, typename T::Input>;

struct IPropertyDescriptionFiller {
	// Implementing this interface adds a new format for describing structures of objects

	// Should introduce a new member with the given name and description and call the functor when it's ready
	virtual void addMember(std::string_view name, std::string_view description, Callback<> writer) = 0;
	// Should add an integer type to the description
	virtual void addInteger() = 0;
	// Should add a floating point type to the description
	virtual void addFloat() = 0;
	// Should add a boolean type to the description
	virtual void addBoolean() = 0;
	// Should add a string type to the description
	virtual void addString() = 0;
	// Should add an optional type to the description and use the functor to fill it
	virtual void addOptional(Callback<void(IPropertyDescriptionFiller&)> filler) = 0;
	// Should add an array type to the description and use the functor to fill it
	virtual void addArray(Callback<void(IPropertyDescriptionFiller&)> filler) = 0;
	// Should add an already defined object type to the description with the given name and use the functor to fill it
	virtual void addSubobject(std::optional<std::string_view> typeName,
			Callback<void(IPropertyDescriptionFiller&)> filler) = 0;
};

struct ISerialisableDescriptionFiller {
	// Implementing this interface adds a new format for describing nested structures of objects

	// Should add another type, usually nested type, using the provided functor
	virtual void addMoreTypes(Callback<void(ISerialisableDescriptionFiller&)> otherFiller) = 0;
	// Should start describing the current type, with the given name and using the functor in argument
	virtual void fillMembers(std::string_view name, Callback<void(IPropertyDescriptionFiller&)> filler) = 0;
};
	
struct ISerialisable {
	// Represents a class that can be serialised and deserialised, usually implemented by Serialisable in bomba_object.hpp

	template <DataFormat F>
	std::string serialise() {
		std::string output;
		typename F::Output format(output);
		serialiseInternal(format);
		return output;
	}
	
	template <DataFormat F, typename FromType>
	void serialise(FromType& output) {
		typename F::Output format(output);
		serialiseInternal(format);
		return output;
	}
	
	template <DataFormat F, typename FromType>
	bool deserialise(const FromType& from) {
		typename F::Input format(from);
		return deserialiseInternal(format);
	}

protected:
	// Should write the structure's contents using the output format object
	virtual void serialiseInternal(IStructuredOutput& format,
			SerialisationFlags::Flags flags = SerialisationFlags::NONE) const = 0;
	// Should update the structure's contents using the input format object
	virtual bool deserialiseInternal(IStructuredInput& format,
			SerialisationFlags::Flags flags = SerialisationFlags::NONE) = 0;

	template <typename> friend struct TypedSerialiser;
};

struct IDescribableSerialisable : ISerialisable {
	virtual void describe([[maybe_unused]] IPropertyDescriptionFiller& filler) const {
		// Does nothing if not supported
	}
	virtual std::string_view getTypeName() const {
		return "Unnamed";
	}
	virtual void listTypes([[maybe_unused]] ISerialisableDescriptionFiller& filler) const {
		// Not supported if not overloaded, does nothing
	}
	template <typename> friend struct TypedSerialiser;
};

struct IMethodDescriptionFiller {
	// Implementing this allows adding parameters to autogenerated remote method description for a custom description format

	// Should add a parameter to the description
	virtual void addParameter(std::string_view name, std::string_view type, std::string_view description, bool mandatory) = 0;
};

struct IRemoteCallableDescriptionFiller {
	// Implementing this allows adding methods and internally used objects to autogenerated method descruption for a new format

	// Should add a new method with the given name and description, using the provided interface to describe arguments and return values
	virtual void addMethod(std::string_view name, std::string_view description,
			Callback<void(IPropertyDescriptionFiller&)> paramFiller, Callback<void(IPropertyDescriptionFiller&)> returnFiller) = 0;
	// Should add a new internally used object with the given name, using the interface provided by the functor
	virtual void addSubobject(std::string_view name, Callback<void(IRemoteCallableDescriptionFiller&)> nestedFiller) = 0;
};

struct IWriteStarter {
	// An implementation of this interface should be provided by a class that can deal with both messages whose size is known
	// before writing and whose is determined by the write. The purpose is to allow both sending responses whose size is determined
	// by serialisation and downloading large files that are usually streamed, while the message needs to start with size.

	// Should prepare a buffer whose size will grow or is large enough and call the callback on it
	virtual void writeUnknownSize(std::string_view resourceType, Callback<void(GeneralisedBuffer&)> filler) = 0;
	// Should prepare a buffer for a message whose size is known in advance and call the callback on it
	virtual void writeKnownSize(std::string_view resourceType, int64_t size, Callback<void(GeneralisedBuffer&)> filler) = 0;
};

class IRemoteCallable;
	
struct UserId {
	int id;
};

struct RequestToken {
	uint32_t id = 0; // We want it to overflow in a defined way
	bool operator==(RequestToken other) const {
		return id == other.id;
	}
	bool operator!=(RequestToken other) const {
		return id != other.id;
	}
};

struct IRpcResponder {
	// Implementing this interface creates clients for a message type

	// Should serialise a message from a given user from a given method, supplying the output format and identifier to the callback
	virtual RequestToken send(UserId user, const IRemoteCallable* method,
			Callback<void(IStructuredOutput&, RequestToken)> request) = 0;
	// Should look for a response with a given identifier, construct a parser for it and call the callback on the parser
	virtual bool getResponse(RequestToken token, Callback<void(IStructuredInput&)> reader) = 0;
	// Should decide if a response with the given identifier is already available and can be accessed without waiting
	virtual bool hasResponse(RequestToken) {
		return true; // If not sufficiently async, it's sort of always available and getting it causes it to wait
	}

	// TODO: Add a way to discard a response and make the polling code call it when destroyed, or forgotten responses will pile up
};

class IRemoteCallable {
	// Implementing the virtual methods in this class will create a custom callable object
	// Doing so is not very practical without something like RPC lambda in bomba_rpc_object.hpp

	IRemoteCallable* _parent = nullptr;
	IRpcResponder* _responder = nullptr;
protected:
	void setSelfAsParent(IRemoteCallable* reparented) {
		reparented->_parent = this;
		reparented->_responder = _responder;
	}
	void unsetParent(IRemoteCallable* reparented) {
		reparented->_parent = nullptr;
		reparented->_responder = nullptr;
	}
	IRpcResponder* getResponder() const {
		if (!_responder && _parent) [[unlikely]]
			const_cast<IRpcResponder*&>(_responder) = _parent->getResponder(); // Lazy loading
		if (!_responder) [[unlikely]]
			logicError("Calling a remote procedure while not being a client");
		return _responder;
	}

public:
	IRemoteCallable(IRemoteCallable* parent = nullptr, IRpcResponder* responder = nullptr)
			: _parent(parent), _responder(responder) {}

	IRemoteCallable* parent() const {
		return _parent;
	}
	
	void setResponder(IRpcResponder& responder) {
		_responder = &responder;
	}
	
	// Should call the function with a prepared input of arguments assumed to be object members (or null), prepared output,
	// a functor that starts writing the result, a functor that writes an error message and a user identifier (or nullopt)
	virtual bool call([[maybe_unused]] IStructuredInput* arguments, [[maybe_unused]] IStructuredOutput& result,
			[[maybe_unused]] Callback<> introduceResult, [[maybe_unused]] Callback<void(std::string_view)> introduceError,
			[[maybe_unused]] std::optional<UserId> user = std::nullopt) const {
		return false;
	}
	// Should return the pointer to a child callable with a certain name (recursive access is handled elsewhere)
	virtual const IRemoteCallable* getChild([[maybe_unused]] std::string_view name) const {
		return nullptr;
	}
	// Should return the pointer to a child callable with a certain index (recursive access is handled elsewhere)
	virtual const IRemoteCallable* getChild([[maybe_unused]] int index) const {
		return nullptr;
	}

	// Returns the name and index of a child with a certain address (to allow a callable to identify itself)
	constexpr static int NO_SUCH_STRUCTURE = -1;
	virtual std::pair<std::string_view, int> childName([[maybe_unused]] const IRemoteCallable* child) const {
		logicError("No such structure");
		return {"", NO_SUCH_STRUCTURE};
	}
	
	// Uses the supplied interface to describe all composed types used by this function
	virtual void listTypes([[maybe_unused]] ISerialisableDescriptionFiller& filler) const {
		// Not supported if not overloaded, does nothing
	}
	// Use the supplied interface to describe the function, its arguments and its return type
	virtual void generateDescription([[maybe_unused]] IRemoteCallableDescriptionFiller& filler) const {
		// Not supported if not overloaded, does nothing
	}
	virtual ~IRemoteCallable() = default;
};

template <StringLiteral Separator, BetterAssembledString StringType = std::string>
struct PathWithSeparator {
	static const IRemoteCallable* findCallable(std::string_view path, const IRemoteCallable* root) {
		auto start = path.begin();
		const IRemoteCallable* current = root;
		while (start < path.end()) {
			auto end = start;
			while (end != path.end()) {
				for (int i = 0; (end + i) != path.end() && *(end + i) == Separator[i]; i++) {
					if (i + 1 == Separator.size()) {
						end += i;
						goto foundSeparator;
					}
				}
				end++;
			}
			foundSeparator:;
			
			current = current->getChild(std::string_view(start, end));
			if (!current)
				return nullptr;
			end++;
			start = end;
		}
		return current;
	}
	
	static StringType constructPath(const IRemoteCallable* callable) {
		if (!callable->parent())
			return StringType();

		StringType path;
		prependPath(callable->parent(), path);
		path += callable->parent()->childName(callable).first;
		return path;
	}
	
private:
	static void prependPath(const IRemoteCallable* callable, StringType& pathSoFar) {
		if (!callable->parent())
			return;
			
		prependPath(callable->parent(), pathSoFar);
		pathSoFar += callable->parent()->childName(callable).first;
		pathSoFar += Separator.c_str();
	}
};

enum class ServerReaction {
	OK,
	READ_ON,
	WRONG_REPLY,
	DISCONNECT,
};

struct ITcpClient {
	// Interface for networking clients that communicate using streams, should be able to store identified responses
	// that were not accessed yet

	// Should write the buffer in argument into the stream
	virtual void writeRequest(std::span<char> written) = 0;

	// Should look for a response with the identifier in the stream or an already identified response, using the supplied functor
	// that parses the stream, taking chunks of data and a flag whether it's already identified, returning information whether it's
	// the right message, or it's a wrong one, or incomplete, or if the stream is corrupted, plus the parsed message's identifier
	// (if identified) and the size of the message it read (if it could read a message). The functor shall parse zero or one message.
	virtual void getResponse(RequestToken token, Callback<std::tuple<ServerReaction, RequestToken, int64_t>
			(std::span<char> input, bool identified)> reader) = 0;

	// Should look for a response with the identifier in the stream, similarly to getResponse(), but without processing
	// the actual response. Intended for periodically polling if a response has arrived. Must be fast if called token.
	virtual void tryToGetResponse(RequestToken token, Callback<std::tuple<ServerReaction, RequestToken, int64_t>
			(std::span<char> input, bool identified)> reader) {
		getResponse(token, reader);
	}
};

struct ITcpResponder {
	// Interface for classes that can respond to streams of requests.

	// Should take a chunk of data and a functor that sends chunks of data and return whether the data is okay or corrupted
	// and the number of bytes read (the functor can be called as many times as needed)
	virtual std::pair<ServerReaction, int64_t> respond(
				std::span<char> input, Callback<void(std::span<const char>)> writer) = 0;
};

// Matching types to the interface

template <std::integral Integer>
struct TypedSerialiser<Integer> {
	static void serialiseMember(IStructuredOutput& out, Integer value, SerialisationFlags::Flags flags) {
		if (!(flags & SerialisationFlags::DETERMINED_NUMERIC_TYPE))
			flags = decltype(flags)(flags | SerialisationFlags::typeToFlags(value));
		out.writeInt(flags, value);
	}

	static void deserialiseMember(IStructuredInput& in, Integer& value, SerialisationFlags::Flags flags) {
		if (!(flags & SerialisationFlags::DETERMINED_NUMERIC_TYPE))
			flags = decltype(flags)(flags | SerialisationFlags::typeToFlags(value));
		value = in.readInt(flags);
	}

	static void describeType(IPropertyDescriptionFiller& filler)  {
		filler.addInteger();
	}
	static void listTypes(ISerialisableDescriptionFiller&) {};
};

template <std::floating_point Float>
struct TypedSerialiser<Float> {
	static void serialiseMember(IStructuredOutput& out, Float value, SerialisationFlags::Flags flags) {
		if (!(flags & SerialisationFlags::DETERMINED_NUMERIC_TYPE))
			flags = decltype(flags)(flags | SerialisationFlags::typeToFlags(value));
		out.writeFloat(flags, value);
	}

	static void deserialiseMember(IStructuredInput& in, Float& value, SerialisationFlags::Flags flags) {
		if (!(flags & SerialisationFlags::DETERMINED_NUMERIC_TYPE))
			flags = decltype(flags)(flags | SerialisationFlags::typeToFlags(value));
		value = in.readFloat(flags);
	}

	static void describeType(IPropertyDescriptionFiller& filler)  {
		filler.addFloat();
	}
	static void listTypes(ISerialisableDescriptionFiller&) {};
};

template <>
struct TypedSerialiser<bool> {
	static void serialiseMember(IStructuredOutput& out, bool value, SerialisationFlags::Flags flags) {
		out.writeBool(flags, value);
	}

	static void deserialiseMember(IStructuredInput& in, bool& value, SerialisationFlags::Flags flags) {
		value = in.readBool(flags);
	}

	static void describeType(IPropertyDescriptionFiller& filler)  {
		filler.addBoolean();
	}
	static void listTypes(ISerialisableDescriptionFiller&) {};
};

template <std::derived_from<ISerialisable> Serialisable>
struct TypedSerialiser<Serialisable> {
	static void serialiseMember(IStructuredOutput& out, const Serialisable& value, SerialisationFlags::Flags flags) {
		static_cast<const ISerialisable&>(value).serialiseInternal(out, flags);
	}

	static void deserialiseMember(IStructuredInput& in, Serialisable& value, SerialisationFlags::Flags flags) {
		static_cast<ISerialisable&>(value).deserialiseInternal(in, flags);
	}

	static void describeType(IPropertyDescriptionFiller& filler)  {
		Serialisable value;
		filler.addSubobject(value.getTypeName(), [&value] (IPropertyDescriptionFiller& subFiller) {
			value.describe(subFiller);
		});
	}
	static void listTypes(ISerialisableDescriptionFiller& filler) {
		Serialisable instance;
		instance.listTypes(filler);
		filler.fillMembers(instance.getTypeName(), [&instance] (IPropertyDescriptionFiller& propertyFiller) {
			instance.describe(propertyFiller);
		});
	};
};

template <typename T>
concept MemberString = requires(T value, std::string_view view) {
	std::string_view(const_cast<const T&>(value));
	value = view;
};

template <MemberString StringType>
struct TypedSerialiser<StringType> {
	static void serialiseMember(IStructuredOutput& out, const StringType& value, SerialisationFlags::Flags flags) {
		out.writeString(flags, value);
	}

	static void deserialiseMember(IStructuredInput& in, StringType& value, SerialisationFlags::Flags flags) {
		value = in.readString(flags);
	}

	static void describeType(IPropertyDescriptionFiller& filler)  {
		filler.addString();
	}
	static void listTypes(ISerialisableDescriptionFiller&) {};
};

template <typename T>
concept WithSerialiserFunctions = requires(T value, IStructuredInput& in, IStructuredOutput& out) {
	TypedSerialiser<std::decay_t<T>>::serialiseMember(out, value, SerialisationFlags::NONE);
	TypedSerialiser<std::decay_t<T>>::deserialiseMember(in, value, SerialisationFlags::NONE);
};

template <WithSerialiserFunctions T, size_t size>
struct TypedSerialiser<std::array<T, size>> {
	static_assert (WithSerialiserFunctions<T>);
	static void serialiseMember(IStructuredOutput& out, const std::array<T, size>& value, SerialisationFlags::Flags flags) {
		out.startWritingArray(flags, size);
		for (int i = 0; i < int(size); i++) {
			out.introduceArrayElement(flags, i);
			TypedSerialiser<T>::serialiseMember(out, value[i], flags);
		}
		out.endWritingArray(flags);
	}

	static void deserialiseMember(IStructuredInput& in, std::array<T, size>& value, SerialisationFlags::Flags flags) {
		in.startReadingArray(flags);
		for (int i = 0; i < int(size) && in.nextArrayElement(flags); i++) {
			TypedSerialiser<T>::deserialiseMember(in, value[i], flags);
		}
		in.endReadingArray(flags);
	}

	static void describeType(IPropertyDescriptionFiller& filler)  {
		static_assert(size > 0, "Can't describe arrays with 0 elements");
		filler.addArray([&] (IPropertyDescriptionFiller& subFiller) {
			T value;
			TypedSerialiser<T>::describeType(subFiller);
		});
	}

	static void listTypes(ISerialisableDescriptionFiller& filler) {
		TypedSerialiser<T>::listTypes(filler);
	};
};

template <typename V>
concept SerialisableVector = !MemberString<V> && requires(V v) {
	{ v[0] } -> WithSerialiserFunctions;
	v.push_back(typename V::value_type());
	v.resize(0);
	{ v.size() } -> std::integral;
};

template <SerialisableVector Vector>
struct TypedSerialiser<Vector> {
	using ValueType = std::decay_t<decltype(std::declval<Vector>()[0])>;
	static void serialiseMember(IStructuredOutput& out, const Vector& value, SerialisationFlags::Flags flags) {
		out.startWritingArray(flags, value.size());
		for (int i = 0; i < int(value.size()); i++) {
			out.introduceArrayElement(flags, i);
			TypedSerialiser<ValueType>::serialiseMember(out, value[i], flags);
		}
		out.endWritingArray(flags);
	}

	static void deserialiseMember(IStructuredInput& in, Vector& value, SerialisationFlags::Flags flags) {
		in.startReadingArray(flags);
		int index = 0;
		while (in.nextArrayElement(flags)) {
			if (std::ssize(value) < index + 1)
				value.emplace_back(typename Vector::value_type());
			TypedSerialiser<ValueType>::deserialiseMember(in, value[index], flags);
			index++;
		}
		if (std::ssize(value) > index)
			value.resize(index);
		in.endReadingArray(flags);
	}

	static void describeType(IPropertyDescriptionFiller& filler)  {
		filler.addArray([] (IPropertyDescriptionFiller& subFiller) {
			TypedSerialiser<ValueType>::describeType(subFiller);
		});
	}

	static void listTypes(ISerialisableDescriptionFiller& filler) {
		TypedSerialiser<ValueType>::listTypes(filler);
	};
};

template <typename M>
concept SerialisableMap = requires(M map, M secondMap, typename M::key_type key) {
	{ map.size() } -> std::integral;
	std::string_view(map.begin()->first);
	{ map.begin()->second } -> WithSerialiserFunctions;
	{ map.find(key)->second } -> WithSerialiserFunctions;
	{ map[key] } -> WithSerialiserFunctions;
	std::swap(map, secondMap);
	bool(map.empty());
};

template <SerialisableMap Map>
struct TypedSerialiser<Map> {
	using ValueType = std::decay_t<decltype(std::declval<Map>().begin()->second)>;
	static void serialiseMember(IStructuredOutput& out, const Map& value, SerialisationFlags::Flags flags) {
		out.startWritingObject(flags, value.size());
		int index = 0;
		for (auto& it : value) {
			out.introduceObjectMember(flags, std::string_view(it.first), index);
			TypedSerialiser<ValueType>::serialiseMember(out, it.second, flags);
			index++;
		}
		out.endWritingObject(flags);
	}

	static void deserialiseMember(IStructuredInput& in, Map& value, SerialisationFlags::Flags flags) {
		if (value.empty()) {
			in.readObject(flags, [&] (std::optional<std::string_view> elementName, int) { // Yes, there is an assignment
				TypedSerialiser<ValueType>::deserialiseMember(in, value[typename Map::key_type(*elementName)], flags);
				return true;
			});
		} else {
			// If there were some elements, update them and move them into a new map
			// Manipulating a set of names that were already used would be annoying
			Map result;
			in.readObject(flags, [&] (std::optional<std::string_view> elementName, int) {
				auto found = value.find(typename Map::key_type(*elementName));
				if (found != value.end()) {
					TypedSerialiser<ValueType>::deserialiseMember(in, found->second, flags);
					result[typename Map::key_type(*elementName)] = std::move(found->second);
				} else
					TypedSerialiser<ValueType>::deserialiseMember(in, result[typename Map::key_type(*elementName)], flags);
				return true;
			});
			std::swap(result, value);
		}
	}

	static void describeType(IPropertyDescriptionFiller& filler)  {
		filler.addSubobject(std::nullopt, [] (IPropertyDescriptionFiller& subFiller) {
			std::remove_reference_t<decltype(std::declval<Map>().begin()->second)> value;
			TypedSerialiser<ValueType>::describeType(subFiller);
		});
	}

	static void listTypes(ISerialisableDescriptionFiller& filler) {
		TypedSerialiser<ValueType>::listTypes(filler);
	};
};

template <typename T>
struct Optional : public std::optional<T>  {
	Optional() : std::optional<T>() { }
	Optional(std::nullopt_t) : std::optional<T>(std::nullopt) { }
	Optional(const Optional<T>& from) = default;
	Optional(Optional<T>&& from) = default;
	Optional<T>& operator=(const T& value) { std::optional<T>::operator=(value); return *this; }
	Optional<T>& operator=(T&& value) { std::optional<T>::operator=(value); return *this; }
	Optional<T>& operator=(const std::optional<T>& value) { std::optional<T>::operator=(value); return *this; }
	Optional<T>& operator=(std::optional<T>&& value) { std::optional<T>::operator=(value); return *this; }
	Optional<T>& operator=(const Optional<T>& value) { std::optional<T>::operator=(value); return *this; }
	Optional<T>& operator=(Optional<T>&& value) { std::optional<T>::operator=(value); return *this; }

	template <typename T2, typename... Args> friend Optional<T2> makeOptional(Args&&... args);
};

template <typename T2, typename... Args>
Optional<T2> makeOptional(Args&&... args) {
	Optional<T2> made;
	made = T2(args...);
	return std::move(made);
}

template <typename P>
concept SerialisableSmartPointerBase = requires(P p, std::decay_t<decltype(*p)>* raw) {
	P(raw);
};

template <typename P>
concept SerialisableOptionalBase = requires(P p, std::decay_t<decltype(*p)> instance) {
	p = instance;
};

template <typename P>
concept SerialisableSmartPointerLike = requires(P p) {
	{ *p } -> WithSerialiserFunctions;
	p.reset();
	bool(p);
};

template <typename P>
concept SerialisableSmartPointer = SerialisableSmartPointerBase<P> && SerialisableSmartPointerLike<P>;

template <typename P>
concept SerialisableOptional = SerialisableOptionalBase<P> && SerialisableSmartPointerLike<P>;

template <typename P>
concept SerialisableOptionalOrSmartPointer =
		(SerialisableSmartPointerBase<P> || SerialisableOptionalBase<P>) && SerialisableSmartPointerLike<P>;

template <SerialisableOptionalOrSmartPointer Ptr>
struct TypedSerialiser<Ptr> {
	using ValueType = std::decay_t<decltype(*std::declval<Ptr>())>;
	static void serialiseMember(IStructuredOutput& out, const Ptr& value, SerialisationFlags::Flags flags) {
		out.writeOptional(flags, bool(value), [&] { TypedSerialiser<ValueType>::serialiseMember(out, *value, flags); });
	}

	static void deserialiseMember(IStructuredInput& in, Ptr& value, SerialisationFlags::Flags flags) {
		bool present = in.readOptional(flags, [&] {
			if (!value) {
				if constexpr(SerialisableSmartPointerBase<Ptr>) {
					value = Ptr(new std::decay_t<decltype(*value)>());
				} else if constexpr(SerialisableOptionalBase<Ptr>) {
					value = std::decay_t<decltype(*value)>();
				} else static_assert(std::is_same_v<Ptr, Ptr>, "This is like an optional, but neither smart pointer nor optional");
			}
			TypedSerialiser<ValueType>::deserialiseMember(in, *value, flags);
		});
		if (!present) {
			value.reset();
		}
	}

	static void describeType(IPropertyDescriptionFiller& filler)  {
		filler.addOptional([] (IPropertyDescriptionFiller& subFiller) {
			TypedSerialiser<ValueType>::describeType(subFiller);
		});
	}

	static void listTypes(ISerialisableDescriptionFiller& filler) {
		TypedSerialiser<ValueType>::listTypes(filler);
	};
};

} // namespace Bomba

namespace std{
template <>
struct hash<Bomba::RequestToken> {
	std::size_t operator()(Bomba::RequestToken token) const {
		return token.id;
	}
};
} // namespace std
#endif // BOMBA_CORE
