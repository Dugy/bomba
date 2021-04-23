//usr/bin/g++ --std=c++20 -Wall $0 -g -o ${o=`mktemp`} && exec $o $*
#include <iostream>
#include "bomba_core.hpp"
#include "bomba_json.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>

using namespace Bomba;

using JSON = BasicJson<>;
	
constexpr static auto noFlags = JSON::Output::Flags::NONE;

struct DummyObject : public ISerialisable {
	int edges = 2;
	float area = 2.1;
	bool isRed = true;
	std::string name = "MiniTriangle";
	std::vector<int> edgeSizes = { 7, 9 };
	std::vector<std::vector<int>> ticks;
	std::array<std::string, 3> tags = {"red", "quasi triangle", "small"};
	std::map<std::string, std::string> notes = {{ "many", "[complaints]"}};
	std::shared_ptr<std::string> story = std::make_shared<std::string>("Blablabla...");
	
	mutable int order = 0;
	template <typename T>
	void writeMember(IStructuredOutput& format, std::string name, T& member) const {
		format.introduceObjectMember(noFlags, name, order);
		serialiseMember(format, member, noFlags);
		order++;
	}
	template <typename T>
	void readMember(IStructuredInput& format, std::string name, T& member) {
		auto got = format.nextObjectElement(noFlags);
		if (*got != name)
			throw ParseError("Expected " + name);
		deserialiseMember(format, member, noFlags);
	}
	
	void serialiseInternal(IStructuredOutput& format, SerialisationFlags::Flags) const override {
		format.startWritingObject(noFlags, 9);
		order = 0;
		writeMember(format, "edges", edges);
		writeMember(format, "area", area);
		writeMember(format, "isRed", isRed);
		writeMember(format, "name", name);
		writeMember(format, "edgeSizes", edgeSizes);
		writeMember(format, "ticks", ticks);
		writeMember(format, "tags", tags);
		writeMember(format, "notes", notes);
		writeMember(format, "story", story);
		format.endWritingObject(noFlags);
	}
	bool deserialiseInternal(IStructuredInput& format, SerialisationFlags::Flags) override {
		format.startReadingObject(noFlags);
		readMember(format, "edges", edges);
		readMember(format, "area", area);
		readMember(format, "isRed", isRed);
		readMember(format, "name", name);
		readMember(format, "edgeSizes", edgeSizes);
		readMember(format, "ticks", ticks);
		readMember(format, "tags", tags);
		readMember(format, "notes", notes);
		readMember(format, "story", story);
		format.endReadingObject(noFlags);
		return true;
	}
};

int main(int argc, char** argv) {

	int errors = 0;
	int tests = 0;

	auto doATest = [&] (auto is, auto shouldBe) {
		tests++;
		if constexpr(std::is_floating_point_v<decltype(is)>) {
			if (is > shouldBe * 1.0001 || is < shouldBe * 0.9999) {
				errors++;
				std::cout << "Test failed: " << is << " instead of " << shouldBe << std::endl;
			}
		} else {
			if (is != shouldBe) {
				errors++;
				std::cout << "Test failed: " << is << " instead of " << shouldBe << std::endl;
			}
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
		
		out.startWritingObject(noFlags, 4);
		out.introduceObjectMember(noFlags, "thirteen", 0);
		out.writeValue(noFlags, int64_t(13));
		out.introduceObjectMember(noFlags, "twoAndHalf", 1);
		out.writeValue(noFlags, 2.5);
		out.introduceObjectMember(noFlags, "no", 2);
		out.writeValue(noFlags, false);
		out.introduceObjectMember(noFlags, "array", 3);
		out.startWritingArray(noFlags, 2);
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
		
		doATest(in.identifyType(noFlags), IStructuredInput::TYPE_OBJECT);
		in.startReadingObject(noFlags);
		doATest(in.good, true);
		
		std::cout << "Testing JSON read int" << std::endl;
		doATest(*in.nextObjectElement(noFlags), "thirteen");
		doATest(in.identifyType(noFlags), IStructuredInput::TYPE_INTEGER);
		doATest(in.readInt(noFlags), 13);
		doATest(in.good, true);
		
		std::cout << "Testing JSON read float" << std::endl;
		doATest(*in.nextObjectElement(noFlags), "twoAndHalf");
		doATest(in.identifyType(noFlags), IStructuredInput::TYPE_FLOAT);
		doATest(in.readFloat(noFlags), 2.5);
		doATest(in.good, true);
		
		std::cout << "Testing JSON read bool" << std::endl;
		doATest(*in.nextObjectElement(noFlags), "no");
		doATest(in.identifyType(noFlags), IStructuredInput::TYPE_BOOLEAN);
		doATest(in.readBool(noFlags), false);
		doATest(in.good, true);
		
		std::cout << "Testing JSON read array" << std::endl;
		doATest(*in.nextObjectElement(noFlags), "array");
		doATest(in.identifyType(noFlags), IStructuredInput::TYPE_ARRAY);
		in.startReadingArray(noFlags);
		doATest(in.good, true);
		
		std::cout << "Testing JSON read null" << std::endl;
		doATest(in.nextArrayElement(noFlags), true);
		doATest(in.identifyType(noFlags), IStructuredInput::TYPE_NULL);
		in.readNull(noFlags);
		doATest(in.good, true);
		
		std::cout << "Testing JSON read string" << std::endl;
		doATest(in.nextArrayElement(noFlags), true);
		doATest(in.identifyType(noFlags), IStructuredInput::TYPE_STRING);
		doATest(in.readString(noFlags), "strink");
		doATest(in.good, true);
		
		std::cout << "Testing JSON read end" << std::endl;
		doATest(in.nextArrayElement(noFlags), false);
		in.endReadingArray(noFlags);
		doATest(in.good, true);
		doATest(in.nextObjectElement(noFlags) == std::nullopt, true);
		in.endReadingObject(noFlags);
		doATest(in.good, true);
	}
	
	{
		std::cout << "Testing JSON seek" << std::endl;
		std::string result;
		JSON::Input in(simpleJsonCode);
		
		auto start = in.storePosition(noFlags);
		doATest(in.seekObjectElement(noFlags, "twoAndHalf"), true);
		doATest(in.good, true);
		doATest(in.identifyType(noFlags), IStructuredInput::TYPE_FLOAT);
		doATest(in.readFloat(noFlags), 2.5);
		doATest(in.good, true);
		
		in.restorePosition(noFlags, start);
		doATest(in.seekObjectElement(noFlags, "thirteen"), true);
		doATest(in.good, true);
	}
	
	std::string dummyObjectJson =	"{\n"
					"	\"edges\" : 3,\n"
					"	\"area\" : 3.8,\n"
					"	\"isRed\" : false,\n"
					"	\"name\" : \"SuperTriangle\",\n"
					"	\"edgeSizes\" : [\n"
					"		4,\n"
					"		5,\n"
					"		6\n"
					"	],\n"
					"	\"ticks\" : [],\n"
					"	\"tags\" : [\n"
					"		\"not red\",\n"
					"		\"triangle\",\n"
					"		\"super\"\n"
					"	],\n"
					"	\"notes\" : {\n"
					"		\"none\" : \"nil\",\n"
					"		\"nothing\" : \"null\"\n"
					"	},\n"
					"	\"story\" : null\n"
					"}";
	
	{
		std::cout << "Testing template facade" << std::endl;
		DummyObject tested;
		tested.edges = 3;
		tested.area = 3.8;
		tested.isRed = false;
		tested.name = "SuperTriangle";
		tested.edgeSizes = { 4, 5, 6 };
		tested.tags = {"not red", "triangle", "super"};
		tested.notes = {{ "none", "nil"}, {"nothing", "null"}};
		tested.story = nullptr;
		std::string written = tested.serialise<JSON>();
		doATest(written, dummyObjectJson);
		
		DummyObject tested2;
		tested2.deserialise<JSON>(dummyObjectJson);
		doATest(tested2.edges, 3);
		doATest(tested2.area, 3.8);
		doATest(tested2.isRed, false);
		doATest(tested2.name, "SuperTriangle");
		doATest(tested2.edgeSizes.size(), 3ull);
		doATest(tested2.tags[0], "not red");
		doATest(tested2.notes["none"], "nil");
		doATest(tested2.story, nullptr);
	}


	std::cout << "Passed: " << (tests - errors) << " / " << tests << ", errors: " << errors << std::endl;

	return 0;
}
