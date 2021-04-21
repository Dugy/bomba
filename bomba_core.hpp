#ifndef BOMBA_CORE
#define BOMBA_CORE
#include <optional>

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
	
	virtual void startWritingArray(Flags flags) = 0;
	virtual void introduceArrayElement(Flags flags, int index) = 0;
	virtual void endWritingArray(Flags flags) = 0;
	
	virtual void startWritingObject(Flags flags) = 0;
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
	
	virtual MemberType identifyType() = 0;
	
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
	virtual void serialiseInternal(IStructuredOutput& format) const = 0;
	virtual bool deserialiseInternal(IStructuredInput& format) = 0;
};

} // namespace Bomba
#endif // BOMBA_CORE
