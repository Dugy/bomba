#ifndef BOMBA_CORE
#define BOMBA_CORE
#include <optional>
#include <array>
#include <string_view>

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
#endif

namespace SerialisationFlags {
	enum Flags {
		NONE = 0,
		NOT_CHILD = 0x1,
		MANDATORY = 0x2,
	};
};

struct IStructuredOutput {
	using Flags = SerialisationFlags::Flags;

	virtual void writeValue(Flags flags, int64_t value) = 0;
	virtual void writeValue(Flags flags, double value) = 0;
	virtual void writeValue(Flags flags, std::string_view value) = 0;
	virtual void writeValue(Flags flags, bool value) = 0;
	virtual void writeNull(Flags flags) = 0;
	
	virtual void startWritingArray(Flags flags, int size) = 0;
	virtual void introduceArrayElement(Flags flags, int index) = 0;
	virtual void endWritingArray(Flags flags) = 0;
	
	virtual void startWritingObject(Flags flags, int size) = 0;
	virtual void introduceObjectMember(Flags flags, std::string_view name, int index) = 0;
	virtual void endWritingObject(Flags flags) = 0;
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
	virtual bool seekObjectElement(Flags flags, std::string_view name) = 0;
	virtual void endReadingObject(Flags flags) = 0;
	
	struct Location {
		int loc;
	};
	virtual Location storePosition(Flags flags) = 0;
	virtual void restorePosition(Flags flags, Location location) = 0;
};

template <typename T>
concept AssembledString = std::is_same_v<T&, decltype(std::declval<T>() += 'a')>
		&& std::is_same_v<T&, decltype(std::declval<T>() += "a")>
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

// Matching types to the interface

template <std::integral Integer>
void serialiseMember(IStructuredOutput& out, Integer value, SerialisationFlags::Flags flags) {
	out.writeValue(flags, int64_t(value));
}

template <std::integral Integer>
void deserialiseMember(IStructuredInput& in, Integer& value, SerialisationFlags::Flags flags) {
	value = in.readInt(flags);
}

template <std::floating_point Float>
void serialiseMember(IStructuredOutput& out, Float value, SerialisationFlags::Flags flags) {
	out.writeValue(flags, double(value));
}

template <std::floating_point Float>
void deserialiseMember(IStructuredInput& in, Float& value, SerialisationFlags::Flags flags) {
	value = in.readFloat(flags);
}

void serialiseMember(IStructuredOutput& out, bool value, SerialisationFlags::Flags flags) {
	out.writeValue(flags, value);
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
	out.writeValue(flags, std::string_view(value));
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
#endif // BOMBA_CORE
