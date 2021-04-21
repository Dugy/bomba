//usr/bin/g++ --std=c++20 -Wall $0 -o ${o=`mktemp`} && exec $o $*
#include <iostream>
#include "bomba_core.hpp"
#include "bomba_json.hpp"
#include <string>

using namespace Bomba;

using JSON = BasicJson<>;


struct DummyObject : public ISerialisable {
	void serialiseInternal(IStructuredOutput& format) const override {
		std::cout << "Serialising" << std::endl;
	}
	bool deserialiseInternal(IStructuredInput& format) override {
		std::cout << "Deserialising" << std::endl;
		return true;
	}
};

int main(int argc, char** argv) {

	int errors = 0;
	int tests = 0;

	auto doATest = [&] (auto is, auto shouldBe) {
		tests++;
		if (is != shouldBe) {
			errors++;
			std::cout << "Test failed: " << is << " instead of " << shouldBe << std::endl;
		}
	};

	std::string simpleJsonCode =	"{\n"
					"	\"thirteen\" : 13,\n"
					"	\"twoAndHalf\" : 2.5,\n"
					"	\"no\" : false,\n"
					"	\"array\" : [\n"
					"		null,\n"
					"		\"strink\"\n"
					"	]\n"
					"}";
	{
		std::cout << "Testing JSON write" << std::endl;
		std::string result;
		JSON::Output out(result);
		auto noFlags = JSON::Output::Flags::NONE;
		
		out.startWritingObject(noFlags);
		out.introduceObjectMember(noFlags, "thirteen", 0);
		out.writeValue(noFlags, int64_t(13));
		out.introduceObjectMember(noFlags, "twoAndHalf", 1);
		out.writeValue(noFlags, 2.5);
		out.introduceObjectMember(noFlags, "no", 2);
		out.writeValue(noFlags, false);
		out.introduceObjectMember(noFlags, "array", 3);
		out.startWritingArray(noFlags);
		out.introduceArrayElement(noFlags, 0);
		out.writeNull(noFlags);
		out.introduceArrayElement(noFlags, 1);
		out.writeValue(noFlags, std::string("strink"));
		out.endWritingArray(noFlags);
		out.endWritingObject(noFlags);
		
		doATest(result, simpleJsonCode);
	}
	
	{
		std::cout << "Testing JSON read" << std::endl;
		std::string result;
		JSON::Input in(simpleJsonCode);
		auto noFlags = JSON::Output::Flags::NONE;
		
		doATest(in.identifyType(), IStructuredInput::TYPE_OBJECT);
		in.startReadingObject(noFlags);
		doATest(in.good, true);
		
		std::cout << "Testing JSON read int" << std::endl;
		doATest(*in.nextObjectElement(noFlags), "thirteen");
		doATest(in.identifyType(), IStructuredInput::TYPE_INTEGER);
		doATest(in.readInt(noFlags), 13);
		doATest(in.good, true);
		
		std::cout << "Testing JSON read float" << std::endl;
		doATest(*in.nextObjectElement(noFlags), "twoAndHalf");
		doATest(in.identifyType(), IStructuredInput::TYPE_FLOAT);
		doATest(in.readFloat(noFlags), 2.5);
		doATest(in.good, true);
		
		std::cout << "Testing JSON read bool" << std::endl;
		doATest(*in.nextObjectElement(noFlags), "no");
		doATest(in.identifyType(), IStructuredInput::TYPE_BOOLEAN);
		doATest(in.readBool(noFlags), false);
		doATest(in.good, true);
		
		std::cout << "Testing JSON read array" << std::endl;
		doATest(*in.nextObjectElement(noFlags), "array");
		doATest(in.identifyType(), IStructuredInput::TYPE_ARRAY);
		in.startReadingArray(noFlags);
		doATest(in.good, true);
		
		std::cout << "Testing JSON read null" << std::endl;
		doATest(in.nextArrayElement(noFlags), true);
		doATest(in.identifyType(), IStructuredInput::TYPE_NULL);
		in.readNull(noFlags);
		doATest(in.good, true);
		
		std::cout << "Testing JSON read string" << std::endl;
		doATest(in.nextArrayElement(noFlags), true);
		doATest(in.identifyType(), IStructuredInput::TYPE_STRING);
		doATest(in.readString(noFlags), "strink");
		doATest(in.good, true);
		
		std::cout << "Testing JSON read end" << std::endl;
		doATest(in.nextArrayElement(noFlags), false);
		doATest(in.good, true);
		doATest(in.nextObjectElement(noFlags) == std::nullopt, true);
		doATest(in.good, true);
	}
	
	{
		std::cout << "Testing JSON seek" << std::endl;
		std::string result;
		JSON::Input in(simpleJsonCode);
		auto noFlags = JSON::Output::Flags::NONE;
		
		auto start = in.storePosition(noFlags);
		doATest(in.seekObjectElement(noFlags, "twoAndHalf"), true);
		doATest(in.good, true);
		doATest(in.identifyType(), IStructuredInput::TYPE_FLOAT);
		doATest(in.readFloat(noFlags), 2.5);
		doATest(in.good, true);
		
		in.restorePosition(noFlags, start);
		doATest(in.seekObjectElement(noFlags, "thirteen"), true);
		doATest(in.good, true);
	}


	std::cout << "Passed: " << (tests - errors) << " / " << tests << ", errors: " << errors << std::endl;

	DummyObject dummy;
	std::string source = "{ \"device\" : 3, \"port\" : 17 }";
	dummy.deserialise<JSON>(source);
	std::string result = dummy.serialise<JSON>();
	std::cout << result << std::endl;
	return 0;
}
