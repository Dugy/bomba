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
	IStructuredOutput::ObjectFiller& _output;
	static constexpr SerialisationFlags::Flags noFlags = SerialisationFlags::NONE;

public:
	JsonWspTypicalDescriptions(IStructuredOutput::ObjectFiller& output) : _output(output) {}

	void addInteger() override {
		_output.underlyingOutput().writeString(noFlags, "number");
	}

	void addFloat() override {
		_output.underlyingOutput().writeString(noFlags, "number");
	}

	void addBoolean() override {
		_output.underlyingOutput().writeString(noFlags, "boolean");
	}

	void addString() override {
		_output.underlyingOutput().writeString(noFlags, "string");
	}

	void addArray(Callback<void(IPropertyDescriptionFiller&)> filler) override {
		_output.underlyingOutput().startWritingArray(noFlags, 1);
		_output.underlyingOutput().introduceArrayElement(noFlags, 0);
		filler(*this);
		_output.underlyingOutput().endWritingArray(noFlags);
	}

	void addSubobject(std::optional<std::string_view> typeName,
			Callback<void(IPropertyDescriptionFiller&)>) override {
		_output.underlyingOutput().writeString(noFlags, typeName.value_or("object"));
	}
};

} // namespace Detail


class JsonWspMembersDescription : public Detail::JsonWspTypicalDescriptions {
public:
	JsonWspMembersDescription(IStructuredOutput::ObjectFiller& output)
			: Detail::JsonWspTypicalDescriptions(output) {}

	void addMember(std::string_view name, std::string_view, Callback<> writer) override {
		_output.introduceMember(name);
		writer();
	}

	void addOptional(Callback<void(IPropertyDescriptionFiller&)> filler) override {
		filler(*this);
	}
};

class JsonWspTypeDescription : public ISerialisableDescriptionFiller {
	IStructuredOutput::ObjectFiller& _output;
	static constexpr SerialisationFlags::Flags noFlags = SerialisationFlags::NONE;

public:
	JsonWspTypeDescription(IStructuredOutput::ObjectFiller& output) : _output(output) {}

	void addMoreTypes(Callback<void(ISerialisableDescriptionFiller&)> otherFiller) override {
		otherFiller(*this);
	}

	void fillMembers(std::string_view name, Callback<void(IPropertyDescriptionFiller&)> filler) override {
		auto object = _output.writeObject(name);
		JsonWspMembersDescription descriptor(object);
		filler(descriptor);
	}
};



static void writeJsonWspDocumentation(IStructuredOutput::ObjectFiller& output, std::string_view description) {
	auto made = output.writeArray("doc_lines", description.size() ? 1 : 0);
	if (description.size() > 0) {
		made.writeString(description);
	}
}

class JsonWspMethodDescription : public Detail::JsonWspTypicalDescriptions {
	int& _argCount;
	bool _optional = false;

public:
	JsonWspMethodDescription(const JsonWspMethodDescription&) = default;
	JsonWspMethodDescription(IStructuredOutput::ObjectFiller& output, int& argCount)
			: Detail::JsonWspTypicalDescriptions(output), _argCount(argCount){}

	void addMember(std::string_view name, std::string_view description, Callback<> writer) override {
		_argCount++; // The numbering printed starts at 1
		auto result = _output.writeObject(name, 4);
		result.writeInt("def_order", _argCount);
		writeJsonWspDocumentation(result, description);
		result.introduceMember("type");
		writer();
		result.writeBool("optional", _optional);
	}

	void addOptional(Callback<void(IPropertyDescriptionFiller&)> filler) override {
		_optional = true;
		filler(*this);
	}
};

class JsonWspReturnDescription : public Detail::JsonWspTypicalDescriptions {
	int _returnValues = 0;
public:
	JsonWspReturnDescription(IStructuredOutput::ObjectFiller& output)
			: Detail::JsonWspTypicalDescriptions(output) {}

	void addMember(std::string_view, std::string_view description, Callback<> writer) override {
		if (_returnValues > 0)
			throw std::logic_error("Can't send multiple return values through JSON-WSP");
		auto made = _output.writeObject("ret_info");
		writeJsonWspDocumentation(made, description);
		made.introduceMember("type");
		writer();
		_returnValues++;
	}

	void addOptional(Callback<void(IPropertyDescriptionFiller&)> filler) override {
		filler(*this);
	}

	~JsonWspReturnDescription() {
		if (_returnValues == 0) {
			addMember("", "", [&] { _output.underlyingOutput().writeNull(noFlags); });
		}
	}
};

template <BetterAssembledString StringType>
class JsonWspDescription : public IRemoteCallableDescriptionFiller {
	IStructuredOutput::ObjectFiller& _output;
	StringType _path;
	static constexpr SerialisationFlags::Flags noFlags = SerialisationFlags::NONE;
public:
	JsonWspDescription(IStructuredOutput::ObjectFiller& output, std::string_view name = "", std::string_view prefix = "") : _output(output) {
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
		auto methodObject = _output.writeObject(methodName, 3);
		writeJsonWspDocumentation(methodObject, description);
		{
			auto paramsObject = methodObject.writeObject("params");
			int methodDescriptorArgCount = 0;
			JsonWspMethodDescription methodDescriptor(paramsObject, methodDescriptorArgCount);
			paramFiller(methodDescriptor);
		}
		JsonWspReturnDescription returnDescriptor(methodObject);
		returnFiller(returnDescriptor);
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
	{
		auto mainObject = output.writeObject(6);
		mainObject.writeString("type", "jsonwsp/description");
		mainObject.writeString("version", "1.0");
		mainObject.writeString("servicename", name);
		mainObject.writeString("url", url);
		{
			auto typesObject = mainObject.writeObject("types");
			JsonWspTypeDescription typeDescriptor(typesObject);
			callable.listTypes(typeDescriptor);
		}
		{
			auto typesObject = mainObject.writeObject("methods");
			JsonWspDescription<StringType> rpcDescriptor(typesObject);
			callable.generateDescription(rpcDescriptor);
		}
	}
	return rawOutput;
}

} // namespace Bomba

#endif // BOMBA_JSON_WSP_DESCRIPTION
