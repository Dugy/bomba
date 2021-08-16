#ifndef BOMBA_CORE
#define BOMBA_CORE
#include <optional>
#include <array>
#include <string_view>
#include <span>
#include <tuple>

#ifndef BOMBA_ALTERNATIVE_ERROR_HANDLING
#include <stdexcept>
#endif

namespace Bomba {

#ifndef BOMBA_ALTERNATIVE_ERROR_HANDLING
struct ParseError : std::runtime_error {
	using std::runtime_error::runtime_error;
};
void parseError(const char* problem) {
	throw ParseError(problem);
}

struct MethodNotFoundError : std::runtime_error {
	using std::runtime_error::runtime_error;
};
void methodNotFoundError(const char* problem) {
	throw MethodNotFoundError(problem);
}

struct RemoteError : std::runtime_error {
	using std::runtime_error::runtime_error;
};
void remoteError(const char* problem) {
	throw RemoteError(problem);
}

void logicError(const char* problem) {
	throw std::logic_error(problem);
}
#endif

template <typename T = void()>
struct Callback {
	// This should never be instantiated
	static_assert(std::is_same_v<T, T>, "Invalid callback signature");
};

template <typename Returned, typename... Args>
struct Callback<Returned(Args...)> {
	virtual Returned operator() (Args&&... args) const = 0;
};

namespace Detail {

template <typename Returned, typename Class, typename... Args>
auto makeCallbackHelper(Class&& instance, Returned (Class::*)(Args...) const) {
	struct CallbackImpl : Callback<Returned(Args...)> {
		Class instance;
		CallbackImpl(Class&& instance) : instance(std::forward<Class>(instance)) {}
		Returned operator() (Args&&... args) const final override {
			return instance(std::forward<Args>(args)...);
		}
	} retval = std::move(instance);
	return retval;
}

} // namespace Detail

template <typename T>
auto makeCallback(T&& func) {
	return Detail::makeCallbackHelper(std::forward<T>(func), &T::operator());
}

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

struct IStructuredOutput {
	using Flags = SerialisationFlags::Flags;

	virtual void writeInt(Flags flags, int64_t value) = 0;
	virtual void writeFloat(Flags flags, double value) = 0;
	virtual void writeString(Flags flags, std::string_view value) = 0;
	virtual void writeBool(Flags flags, bool value) = 0;
	virtual void writeNull(Flags flags) = 0;
	
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
		int loc;
	};
	virtual Location storePosition(Flags flags) = 0;
	virtual void restorePosition(Flags flags, Location location) = 0;
	
	bool seekObjectElement(Flags flags, std::string_view name, bool nameAlreadyRead) {
		if (nameAlreadyRead)
			skipObjectElement(flags);
		while (std::optional<std::string_view> nextName = nextObjectElement(flags)) {
			if (*nextName == name)
				return true;
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

template <typename T>
concept AssembledString = std::is_same_v<T&, decltype(std::declval<T>() += 'a')>
		&& std::is_same_v<T&, decltype(std::declval<T>() += "a")>
		&& std::is_convertible_v<T, std::string_view>
		&& std::is_void_v<decltype(std::declval<T>().clear())>;

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
			
	friend void serialiseMember(IStructuredOutput&, const ISerialisable&, SerialisationFlags::Flags);
	friend void deserialiseMember(IStructuredInput&, ISerialisable&, SerialisationFlags::Flags);
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
			const Callback<void(IStructuredOutput&, RequestToken)>& request) = 0;
	virtual bool getResponse(RequestToken token, const Callback<void(IStructuredInput&)>& reader) = 0;
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
	
	virtual bool call(IStructuredInput* arguments, IStructuredOutput& result, const Callback<>& introduceResult,
			const Callback<>& introduceError, std::optional<UserId> user = std::nullopt) const {
		return false;
	}
	virtual const IRemoteCallable* getChild(std::string_view name) const {
		return nullptr;
	}
	virtual std::string_view childName(const IRemoteCallable* child) const {
		logicError("No such structure");
		return "";
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
	virtual void getResponse(RequestToken token, const Callback<std::tuple<ServerReaction, RequestToken, int64_t>
			(std::span<char> input, bool identified)>& reader) = 0;
	virtual void tryToGetResponse(RequestToken token, const Callback<std::tuple<ServerReaction, RequestToken, int64_t>
			(std::span<char> input, bool identified)>& reader) {
		getResponse(token, reader);
	}
};

struct ITcpResponder {
	virtual std::pair<ServerReaction, int64_t> respond(
				std::span<char> input, const Callback<void(std::span<const char>)>& writer) = 0;
};

// Matching types to the interface

template <std::integral Integer>
void serialiseMember(IStructuredOutput& out, Integer value, SerialisationFlags::Flags flags) {
	out.writeInt(flags, value);
}

template <std::integral Integer>
void deserialiseMember(IStructuredInput& in, Integer& value, SerialisationFlags::Flags flags) {
	value = in.readInt(flags);
}

template <std::floating_point Float>
void serialiseMember(IStructuredOutput& out, Float value, SerialisationFlags::Flags flags) {
	out.writeFloat(flags, value);
}

template <std::floating_point Float>
void deserialiseMember(IStructuredInput& in, Float& value, SerialisationFlags::Flags flags) {
	value = in.readFloat(flags);
}

void serialiseMember(IStructuredOutput& out, bool value, SerialisationFlags::Flags flags) {
	out.writeBool(flags, value);
}

void deserialiseMember(IStructuredInput& in, bool& value, SerialisationFlags::Flags flags) {
	value = in.readBool(flags);
}

void serialiseMember(IStructuredOutput& out, const ISerialisable& value, SerialisationFlags::Flags flags) {
	value.serialiseInternal(out, flags);
}

void deserialiseMember(IStructuredInput& in, ISerialisable& value, SerialisationFlags::Flags flags) {
	value.deserialiseInternal(in, flags);
}

template <typename T>
concept MemberString = requires(T value, std::string_view view) {
	std::string_view(const_cast<const T&>(value));
	value = view;
};

template <MemberString StringType>
void serialiseMember(IStructuredOutput& out, const StringType& value, SerialisationFlags::Flags flags) {
	out.writeString(flags, value);
}

template <MemberString StringType>
void deserialiseMember(IStructuredInput& in, StringType& value, SerialisationFlags::Flags flags) {
	value = in.readString(flags);
}

template <typename T>
concept WithSerialiserFunctions = requires(T value, IStructuredInput& in, IStructuredOutput& out) {
	serialiseMember(out, value, SerialisationFlags::NONE);
	deserialiseMember(in, value, SerialisationFlags::NONE);
};

template <WithSerialiserFunctions T, size_t size>
void serialiseMember(IStructuredOutput& out, const std::array<T, size>& value, SerialisationFlags::Flags flags) {
	out.startWritingArray(flags, size);
	for (int i = 0; i < int(size); i++) {
		out.introduceArrayElement(flags, i);
		serialiseMember(out, value[i], flags);
	}
	out.endWritingArray(flags);
}

template <WithSerialiserFunctions T, size_t size>
void deserialiseMember(IStructuredInput& in, std::array<T, size>& value, SerialisationFlags::Flags flags) {
	in.startReadingArray(flags);
	for (int i = 0; i < int(size) && in.nextArrayElement(flags); i++) {
		deserialiseMember(in, value[i], flags);
	}
	in.endReadingArray(flags);
}

template <typename V>
concept SerialisableVector = !MemberString<V> && requires(V v) {
	{ v[0] } -> WithSerialiserFunctions;
	v.push_back(typename V::value_type());
	v.resize(0);
	{ v.size() } -> std::integral;
};

template <SerialisableVector Vector>
void serialiseMember(IStructuredOutput& out, const Vector& value, SerialisationFlags::Flags flags) {
	out.startWritingArray(flags, value.size());
	for (int i = 0; i < int(value.size()); i++) {
		out.introduceArrayElement(flags, i);
		serialiseMember(out, value[i], flags);
	}
	out.endWritingArray(flags);
}

template <SerialisableVector Vector>
void deserialiseMember(IStructuredInput& in, Vector& value, SerialisationFlags::Flags flags) {
	in.startReadingArray(flags);
	int index = 0;
	while (in.nextArrayElement(flags)) {
		if (int(value.size()) < index + 1)
			value.emplace_back(typename Vector::value_type());
		deserialiseMember(in, value[index], flags);
		index++;
	}
	if (int(value.size()) > index)
		value.resize(index);
	in.endReadingArray(flags);
}

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
void serialiseMember(IStructuredOutput& out, const Map& value, SerialisationFlags::Flags flags) {
	out.startWritingObject(flags, value.size());
	int index = 0;
	for (auto& it : value) {
		out.introduceObjectMember(flags, std::string_view(it.first), index);
		serialiseMember(out, it.second, flags);
		index++;
	}
	out.endWritingObject(flags);
}

template <SerialisableMap Map>
void deserialiseMember(IStructuredInput& in, Map& value, SerialisationFlags::Flags flags) {
	in.startReadingObject(flags);
	std::optional<std::string_view> elementName;
	if (value.empty()) {
		while ((elementName = in.nextObjectElement(flags))) { // Yes, there is an assignment
			deserialiseMember(in, value[typename Map::key_type(*elementName)], flags);
		}
	} else {
		// If there were some elements, update them and move them into a new map
		// Manipulating a set of names that were already used would be annoying
		Map result;
		while ((elementName = in.nextObjectElement(flags))) {
			auto found = value.find(typename Map::key_type(*elementName));
			if (found != value.end()) {
				deserialiseMember(in, found->second, flags);
				result[typename Map::key_type(*elementName)] = std::move(found->second);
			} else
				deserialiseMember(in, result[typename Map::key_type(*elementName)], flags);
		}
		std::swap(result, value);
	}
	in.endReadingObject(flags);
}

template <typename P>
concept SerialisableSmartPointer = requires(P p, std::decay_t<decltype(*p)>* raw) {
	P(raw);
	{ *p } -> WithSerialiserFunctions;
	p.reset();
	bool(p);
};

template <SerialisableSmartPointer Ptr>
void serialiseMember(IStructuredOutput& out, const Ptr& value, SerialisationFlags::Flags flags) {
	if (value)
		serialiseMember(out, *value, flags);
	else
		out.writeNull(flags);
}

template <SerialisableSmartPointer Ptr>
void deserialiseMember(IStructuredInput& in, Ptr& value, SerialisationFlags::Flags flags) {
	if (in.identifyType(flags) != IStructuredInput::TYPE_NULL) {
		if (!value)
			value = Ptr(new std::decay_t<decltype(*value)>());
		deserialiseMember(in, *value, flags);
	} else {
		if (value)
			value.reset();
	}
}


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
