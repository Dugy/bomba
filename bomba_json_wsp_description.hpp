#ifndef BOMBA_JSON_WSP_DESCRIPTION
#define BOMBA_JSON_WSP_DESCRIPTION

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif
#ifndef BOMBA_JSON
#include "bomba_json.hpp"
#endif

// Most information gathered from: https://zims-en.kiwix.campusafrica.gos.orange.com/wikipedia_en_all_nopic/A/JSON-WSP

namespace Bomba {

namespace Detail {
class JsonWspTypicalDescriptions : public IPropertyDescriptionFiller {
protected:
	IStructuredOutput& _output;
	static constexpr SerialisationFlags::Flags noFlags = SerialisationFlags::NONE;

public:
	JsonWspTypicalDescriptions(IStructuredOutput& output) : _output(output) {}

	void addInteger() override {
		_output.writeString(noFlags, "number");
	}

	void addFloat() override {
		_output.writeString(noFlags, "number");
	}

	void addBoolean() override {
		_output.writeString(noFlags, "boolean");
	}

	void addString() override {
		_output.writeString(noFlags, "string");
	}

	void addArray(Callback<void(IPropertyDescriptionFiller&)> filler) override {
		_output.startWritingArray(noFlags, 1);
		_output.introduceArrayElement(noFlags, 0);
		filler(*this);
		_output.endWritingArray(noFlags);
	}

	void addSubobject(std::optional<std::string_view> typeName,
			Callback<void(IPropertyDescriptionFiller&)>) override {
		_output.writeString(noFlags, typeName.value_or("object"));
	}
};

} // namespace Detail


class JsonWspMembersDescription : public Detail::JsonWspTypicalDescriptions {
	int _index = 0;

public:
	JsonWspMembersDescription(IStructuredOutput& output)
			: Detail::JsonWspTypicalDescriptions(output) {}

	void addMember(std::string_view name, std::string_view, Callback<> writer) override {
		_output.introduceObjectMember(noFlags, name, _index);
		writer();
		_index++;
	}

	void addOptional(Callback<void(IPropertyDescriptionFiller&)> filler) override {
		filler(*this);
	}
};

class JsonWspTypeDescription : public ISerialisableDescriptionFiller {
	IStructuredOutput& _output;
	int _index = 0;
	static constexpr SerialisationFlags::Flags noFlags = SerialisationFlags::NONE;

public:
	JsonWspTypeDescription(IStructuredOutput& output) : _output(output) {}

	void addMoreTypes(Callback<void(ISerialisableDescriptionFiller&)> otherFiller) override {
		otherFiller(*this);
	}

	void fillMembers(std::string_view name, Callback<void(IPropertyDescriptionFiller&)> filler) override {
		_output.introduceObjectMember(noFlags, name, _index);
		_output.startWritingObject(noFlags, IStructuredOutput::UNKNOWN_SIZE);
		JsonWspMembersDescription descriptor(_output);
		filler(descriptor);
		_output.endWritingObject(noFlags);
		_index++;
	}
};



static void writeJsonWspDocumentation(IStructuredOutput& output, std::string_view description, int objectIndex) {
	static constexpr SerialisationFlags::Flags noFlags = SerialisationFlags::NONE;
	output.introduceObjectMember(noFlags, "doc_lines", objectIndex);
	if (description.size() > 0) {
		output.startWritingArray(noFlags, 1);
		output.introduceArrayElement(noFlags, 0);
		output.writeString(noFlags, description);
	} else {
		output.startWritingArray(noFlags, 0);
	}
	output.endWritingArray(noFlags);
}

class JsonWspMethodDescription : public Detail::JsonWspTypicalDescriptions {
	int& _argCount;
	bool _optional = false;

public:
	JsonWspMethodDescription(const JsonWspMethodDescription&) = default;
	JsonWspMethodDescription(IStructuredOutput& output, int& argCount)
			: Detail::JsonWspTypicalDescriptions(output), _argCount(argCount){}

	void addMember(std::string_view name, std::string_view description, Callback<> writer) override {
		_argCount++; // The numbering printed starts at 1
		_output.introduceObjectMember(noFlags, name, _argCount - 1);
		_output.startWritingObject(noFlags, 4);
		_output.introduceObjectMember(noFlags, "def_order", 0);
		_output.writeInt(noFlags, _argCount);
		writeJsonWspDocumentation(_output, description, 1);
		_output.introduceObjectMember(noFlags, "type", 2);
		writer();
		_output.introduceObjectMember(noFlags, "optional", 3);
		_output.writeBool(noFlags, _optional);
		_output.endWritingObject(noFlags);
	}

	void addOptional(Callback<void(IPropertyDescriptionFiller&)> filler) override {
		_optional = true;
		filler(*this);
	}
};

class JsonWspReturnDescription : public Detail::JsonWspTypicalDescriptions {
	int _returnValues = 0;
public:
	JsonWspReturnDescription(IStructuredOutput& output)
			: Detail::JsonWspTypicalDescriptions(output) {}

	void addMember(std::string_view, std::string_view description, Callback<> writer) override {
		if (_returnValues > 0)
			throw std::logic_error("Can't send multiple return values through JSON-WSP");
		_output.startWritingObject(noFlags, 2);
		writeJsonWspDocumentation(_output, description, 0);
		_output.introduceObjectMember(noFlags, "type", 1);
		writer();
		_output.endWritingObject(noFlags);
		_returnValues++;
	}

	void addOptional(Callback<void(IPropertyDescriptionFiller&)> filler) override {
		filler(*this);
	}

	~JsonWspReturnDescription() {
		if (_returnValues == 0) {
			addMember("", "", [&] { _output.writeNull(noFlags); });
		}
	}
};

template <BetterAssembledString StringType>
class JsonWspDescription : public IRemoteCallableDescriptionFiller {
	IStructuredOutput& _output;
	int _functionCount = 0;
	StringType _path;
	static constexpr SerialisationFlags::Flags noFlags = SerialisationFlags::NONE;
public:
	JsonWspDescription(IStructuredOutput& output, std::string_view name = "", std::string_view prefix = "") : _output(output) {
		if (!prefix.empty()) {
			_path += prefix;
			_path += '.';
		}
		if (!name.empty()) {
			_path += name;
			_path += '.';
		}
	}

	void addMethod(std::string_view name, std::string_view description, Callback<void(IPropertyDescriptionFiller&)> paramFiller,
				Callback<void(IPropertyDescriptionFiller&)> returnFiller) override {
		StringType methodName;
		methodName += _path;
		methodName += name;
		_output.introduceObjectMember(noFlags, methodName, _functionCount);
		_output.startWritingObject(noFlags, 3);
		writeJsonWspDocumentation(_output, description, 0);
		_output.introduceObjectMember(noFlags, "params", 1);
		_output.startWritingObject(noFlags, IStructuredOutput::UNKNOWN_SIZE);
		int methodDescriptorArgCount = 0;
		JsonWspMethodDescription methodDescriptor(_output, methodDescriptorArgCount);
		paramFiller(methodDescriptor);
		_output.endWritingObject(noFlags);
		_output.introduceObjectMember(noFlags, "ret_info", 2);
		{
			JsonWspReturnDescription returnDescriptor(_output);
			returnFiller(returnDescriptor);
		}
		_output.endWritingObject(noFlags);
		_functionCount++;
	}
	void addSubobject(std::string_view name, Callback<void(IRemoteCallableDescriptionFiller&)> nestedFiller) override {
		JsonWspDescription<StringType> subobjectFiller(_output, name, _path);
		nestedFiller(subobjectFiller);
	}
};

template <BetterAssembledString StringType = std::string, BetterAssembledString AuxiliaryStrings = StringType>
StringType describeInJsonWsp(IRemoteCallable& callable, std::string_view url, std::string_view name) {
	StringType rawOutput;
	typename BasicJson<AuxiliaryStrings, StringType>::Output outputInstance(rawOutput);
	IStructuredOutput& output = outputInstance;
	constexpr SerialisationFlags::Flags noFlags = SerialisationFlags::NONE;
	output.startWritingObject(noFlags, 6);
	int index = 0;
	output.introduceObjectMember(noFlags, "type", index++);
	output.writeString(noFlags, "jsonwsp/description");
	output.introduceObjectMember(noFlags, "version", index++);
	output.writeString(noFlags, "1.0");
	output.introduceObjectMember(noFlags, "servicename", index++);
	output.writeString(noFlags, name);
	output.introduceObjectMember(noFlags, "url", index++);
	output.writeString(noFlags, url);
	output.introduceObjectMember(noFlags, "types", index++);
	output.startWritingObject(noFlags, IStructuredOutput::UNKNOWN_SIZE);
	{
		JsonWspTypeDescription typeDescriptor(output);
		callable.listTypes(typeDescriptor);
	}
	output.endWritingObject(noFlags);
	output.introduceObjectMember(noFlags, "methods", index++);
	output.startWritingObject(noFlags, IStructuredOutput::UNKNOWN_SIZE);
	{
		JsonWspDescription<StringType> rpcDescriptor(output);
		callable.generateDescription(rpcDescriptor);
	}
	output.endWritingObject(noFlags);
	output.endWritingObject(noFlags);
	return rawOutput;
}

} // namespace Bomba

#endif // BOMBA_JSON_WSP_DESCRIPTION
