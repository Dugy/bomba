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

namespace SerialisationFlags {
	enum Flags {
		NONE = 0,
		NOT_CHILD = 0x1,
		MANDATORY = 0x2,
		OMIT_FALSE = 0x4,
		EMPTY_IS_NULL = 0x8,
	};
};

template <typename T>
struct TypedSerialiser {
	static_assert(!std::is_same_v<T, T>, "No specialisation for serialising this type");
};

struct IStructuredOutput {
	using Flags = SerialisationFlags::Flags;

	virtual void writeInt(Flags flags, int64_t value) = 0;
	virtual void writeFloat(Flags flags, double value) = 0;
	virtual void writeString(Flags flags, std::string_view value) = 0;
	virtual void writeBool(Flags flags, bool value) = 0;
	virtual void writeNull(Flags flags) = 0;
	
	constexpr static int UNKNOWN_SIZE = -1;
	virtual void startWritingArray(Flags flags, int size) = 0;
	virtual void introduceArrayElement(Flags flags, int index) = 0;
	virtual void endWritingArray(Flags flags) = 0;
	
	virtual void startWritingObject(Flags flags, int size) = 0;
	virtual void introduceObjectMember(Flags flags, std::string_view name, int index) = 0;
	virtual void endWritingObject(Flags flags) = 0;
};

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
};

struct IStructuredInput {
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
	
	virtual MemberType identifyType(Flags flags) = 0;
	
	virtual int64_t readInt(Flags flags) = 0;
	virtual double readFloat(Flags flags) = 0;
	virtual std::string_view readString(Flags flags) = 0;
	virtual bool readBool(Flags flags) = 0;
	virtual void readNull(Flags flags) = 0;
	
	virtual void startReadingArray(Flags flags) = 0;
	virtual bool nextArrayElement(Flags flags) = 0;
	virtual void endReadingArray(Flags flags) = 0;
	
	virtual void startReadingObject(Flags flags) = 0;
	virtual std::optional<std::string_view> nextObjectElement(Flags flags) = 0;
	virtual void skipObjectElement(Flags flags) = 0;
	virtual void endReadingObject(Flags flags) = 0;
	
	struct Location {
		constexpr static int UNINITIALISED = -1;
		int loc = UNINITIALISED;
	};
	virtual Location storePosition(Flags flags) = 0;
	virtual void restorePosition(Flags flags, Location location) = 0;
	
	bool seekObjectElement(Flags flags, std::string_view name, bool nameAlreadyRead) {
		if (nameAlreadyRead)
			skipObjectElement(flags);
		while (std::optional<std::string_view> nextName = nextObjectElement(flags)) {
			if (*nextName == name)
				return true;
			skipObjectElement(flags);
		}
		return false;
	}
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

	GeneralisedBuffer& operator+=(std::string_view added) {
		int position = 0;
		while (position < int(added.size())) {
			int toCopy = std::min<int>(added.size() - position, remainingSpace());
			memcpy(_buffer.data(), added.data() + position, toCopy);
			_buffer = BufferType(_buffer.begin() + toCopy, _buffer.end());
			_size += toCopy;
			if (_buffer.size() == 0) {
				if (!bufferFull()) return *this;
			}
			position += toCopy;
		}
		return *this;
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
	virtual void addMember(std::string_view name, std::string_view description, Callback<> writer) = 0;
	virtual void addInteger() = 0;
	virtual void addFloat() = 0;
	virtual void addBoolean() = 0;
	virtual void addString() = 0;
	virtual void addOptional(Callback<void(IPropertyDescriptionFiller&)> filler) = 0;
	virtual void addArray(Callback<void(IPropertyDescriptionFiller&)> filler) = 0;
	virtual void addSubobject(std::optional<std::string_view> typeName,
			Callback<void(IPropertyDescriptionFiller&)> filler) = 0;
};

struct ISerialisableDescriptionFiller {
	virtual void addMoreTypes(Callback<void(ISerialisableDescriptionFiller&)> otherFiller) = 0;
	virtual void fillMembers(std::string_view name, Callback<void(IPropertyDescriptionFiller&)> filler) = 0;
};
	
struct ISerialisable {
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
	virtual void serialiseInternal(IStructuredOutput& format,
			SerialisationFlags::Flags flags = SerialisationFlags::NONE) const = 0;
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
	virtual void addParameter(std::string_view name, std::string_view type, std::string_view description, bool mandatory) = 0;
};

struct IRemoteCallableDescriptionFiller {
	virtual void addMethod(std::string_view name, std::string_view description,
			Callback<void(IPropertyDescriptionFiller&)> paramFiller, Callback<void(IPropertyDescriptionFiller&)> returnFiller) = 0;
	virtual void addSubobject(std::string_view name, Callback<void(IRemoteCallableDescriptionFiller&)> nestedFiller) = 0;
};

struct IWriteStarter {
	virtual void writeUnknownSize(std::string_view resourceType, Callback<void(GeneralisedBuffer&)> filler) = 0;
	virtual void writeKnownSize(std::string_view resourceType, int64_t size, Callback<void(GeneralisedBuffer&)> filler) = 0;
};

class IRemoteCallable;
	
struct UserId {
	int id;
};

struct RequestToken {
	int id = 0;
	bool operator==(RequestToken other) const {
		return id == other.id;
	}
	bool operator!=(RequestToken other) const {
		return id != other.id;
	}
};

struct IRpcResponder {
	virtual RequestToken send(UserId user, const IRemoteCallable* method,
			Callback<void(IStructuredOutput&, RequestToken)> request) = 0;
	virtual bool getResponse(RequestToken token, Callback<void(IStructuredInput&)> reader) = 0;
	virtual bool hasResponse(RequestToken) {
		return true; // If not sufficiently async, it's sort of always available and getting it causes it to wait
	}
};

class IRemoteCallable {
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
	
	void setResponder(IRpcResponder* responder) {
		_responder = responder;
	}
	
	virtual bool call([[maybe_unused]] IStructuredInput* arguments, [[maybe_unused]] IStructuredOutput& result,
			[[maybe_unused]] Callback<> introduceResult, [[maybe_unused]] Callback<void(std::string_view)> introduceError,
			[[maybe_unused]] std::optional<UserId> user = std::nullopt) const {
		return false;
	}
	virtual const IRemoteCallable* getChild([[maybe_unused]] std::string_view name) const {
		return nullptr;
	}
	virtual std::string_view childName([[maybe_unused]] const IRemoteCallable* child) const {
		logicError("No such structure");
		return "";
	}
	
	virtual void listTypes([[maybe_unused]] ISerialisableDescriptionFiller& filler) const {
		// Not supported if not overloaded, does nothing
	}
	virtual void generateDescription([[maybe_unused]] IRemoteCallableDescriptionFiller& filler) const {
		// Not supported if not overloaded, does nothing
	}
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
		path += callable->parent()->childName(callable);
		return path;
	}
	
private:
	static void prependPath(const IRemoteCallable* callable, StringType& pathSoFar) {
		if (!callable->parent())
			return;
			
		prependPath(callable->parent(), pathSoFar);
		pathSoFar += callable->parent()->childName(callable);
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
	virtual void writeRequest(std::span<char> written) = 0;
	virtual void getResponse(RequestToken token, Callback<std::tuple<ServerReaction, RequestToken, int64_t>
			(std::span<char> input, bool identified)> reader) = 0;
	virtual void tryToGetResponse(RequestToken token, Callback<std::tuple<ServerReaction, RequestToken, int64_t>
			(std::span<char> input, bool identified)> reader) {
		getResponse(token, reader);
	}
};

struct ITcpResponder {
	virtual std::pair<ServerReaction, int64_t> respond(
				std::span<char> input, Callback<void(std::span<const char>)> writer) = 0;
};

// Matching types to the interface

template <std::integral Integer>
struct TypedSerialiser<Integer> {
	static void serialiseMember(IStructuredOutput& out, Integer value, SerialisationFlags::Flags flags) {
		out.writeInt(flags, value);
	}

	static void deserialiseMember(IStructuredInput& in, Integer& value, SerialisationFlags::Flags flags) {
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
		out.writeFloat(flags, value);
	}

	static void deserialiseMember(IStructuredInput& in, Float& value, SerialisationFlags::Flags flags) {
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
			if (int(value.size()) < index + 1)
				value.emplace_back(typename Vector::value_type());
			TypedSerialiser<ValueType>::deserialiseMember(in, value[index], flags);
			index++;
		}
		if (int(value.size()) > index)
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
		in.startReadingObject(flags);
		std::optional<std::string_view> elementName;
		if (value.empty()) {
			while ((elementName = in.nextObjectElement(flags))) { // Yes, there is an assignment
				TypedSerialiser<ValueType>::deserialiseMember(in, value[typename Map::key_type(*elementName)], flags);
			}
		} else {
			// If there were some elements, update them and move them into a new map
			// Manipulating a set of names that were already used would be annoying
			Map result;
			while ((elementName = in.nextObjectElement(flags))) {
				auto found = value.find(typename Map::key_type(*elementName));
				if (found != value.end()) {
					TypedSerialiser<ValueType>::deserialiseMember(in, found->second, flags);
					result[typename Map::key_type(*elementName)] = std::move(found->second);
				} else
					TypedSerialiser<ValueType>::deserialiseMember(in, result[typename Map::key_type(*elementName)], flags);
			}
			std::swap(result, value);
		}
		in.endReadingObject(flags);
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

template <typename P>
concept SerialisableSmartPointerBase = requires(P p, std::decay_t<decltype(*p)>* raw) {
	P(raw);
};

template <typename P>
concept SerialisableOptionalBase = requires(P p, std::decay_t<decltype(*p)> instance) {
	P(instance);
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
		if (value)
			TypedSerialiser<ValueType>::serialiseMember(out, *value, flags);
		else
			out.writeNull(flags);
	}

	static void deserialiseMember(IStructuredInput& in, Ptr& value, SerialisationFlags::Flags flags) {
		if (in.identifyType(flags) != IStructuredInput::TYPE_NULL) {
			if (!value)
				value = Ptr(new std::decay_t<decltype(*value)>());
			TypedSerialiser<ValueType>::deserialiseMember(in, *value, flags);
		} else {
			if (value)
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
