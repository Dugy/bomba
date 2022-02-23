//usr/bin/g++ --std=c++20 -Wall $0 -g -lpthread -o ${o=`mktemp`} && exec $o $*
#include <iostream>
#include <vector>
#include "bomba_core.hpp"
#include "bomba_json.hpp"
#include "bomba_object.hpp"
#include "bomba_rpc_object.hpp"
#include "bomba_json_rpc.hpp"
#include "bomba_http.hpp"
#include "bomba_tcp_server.hpp"
#include "bomba_sync_client.hpp"
#include "bomba_json_wsp_description.hpp"
#include "bomba_dynamic_object.hpp"
#include "bomba_binary_protocol.hpp"
#include <string>
#include <map>
#include <memory>

using namespace Bomba;

using StringJSON = BasicJson<std::string, std::string>;
using JSON = BasicJson<std::string, GeneralisedBuffer&>;

constexpr static auto noFlags = JSON::Output::Flags::NONE;

struct DummyObject : public IDescribableSerialisable {
	int edges = 2;
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
		TypedSerialiser<std::decay_t<T>>::serialiseMember(format, member, noFlags);
		order++;
	}
	template <typename T>
	void readMember(IStructuredInput& format, std::optional<std::string_view> name,
					std::string expectedName, T& member, int index, int expectedIndex) {
		if (index != expectedIndex)
			return;
		if (!name.has_value() || *name != expectedName)
			throw ParseError("Expected " + expectedName);
		TypedSerialiser<T>::deserialiseMember(format, member, noFlags);
	}

	void serialiseInternal(IStructuredOutput& format, SerialisationFlags::Flags) const override {
		format.startWritingObject(noFlags, 9);
		order = 0;
		writeMember(format, "edges", edges);
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
		format.readObject(noFlags, [&] (std::optional<std::string_view> elementName, int index) {
			readMember(format, elementName, "edges", edges, index, 0);
			readMember(format, elementName, "isRed", isRed, index, 1);
			readMember(format, elementName,"name",  name, index, 2);
			readMember(format, elementName, "edgeSizes", edgeSizes, index, 3);
			readMember(format, elementName, "ticks", ticks, index, 4);
			readMember(format, elementName, "tags", tags, index, 5);
			readMember(format, elementName, "notes", notes, index, 6);
			readMember(format, elementName, "story", story, index, 7);
			return true;
		});
		return true;
	}
	void describe(IPropertyDescriptionFiller& filler) const override {
		auto describeType = [&] (auto& variable) {
			TypedSerialiser<std::decay_t<decltype(variable)>>::describeType(filler);
		};
		filler.addMember("edges", "so edgy", [&] { describeType(edges); });
		filler.addMember("isRed", "better not be commie", [&] { describeType(isRed); });
		filler.addMember("name", "gotta name it", [&] { describeType(name); });
		filler.addMember("edgeSizes", "big edgy", [&] { describeType(edgeSizes); });
		filler.addMember("ticks", "parasites or something", [&] { describeType(ticks); });
		filler.addMember("tags", "<head>", [&] { describeType(tags); });
		filler.addMember("notes", "written in a notepad", [&] { describeType(notes); });
		filler.addMember("story", "put fanfic here", [&] { describeType(story); });
	}
	std::string_view getTypeName() const override {
		return "DummyObject";
	}
	void listTypes(ISerialisableDescriptionFiller& filler) const override {
		filler.fillMembers(getTypeName(),
				[this] (IPropertyDescriptionFiller& filler) { describe(filler); });
	}
};

struct StandardObject : Serialisable<StandardObject> {
	int index = key<"index"> = 3;
	Optional<short int> subIndex = key<"sub_index"> = std::nullopt;
	bool deleted = key<"deleted"> = true;
	std::string contents = key<"contents"> = "Not much yet";
};

struct SuperStandardObject : Serialisable<SuperStandardObject> {
	StandardObject child = key<"child">;
};

struct OptionalTest : Serialisable<OptionalTest> {
	Optional<int> value = key<"value"> = 3;
};

struct DummyRpcClass : IRemoteCallable {
	std::string message;
	int time = 0;
	DummyRpcClass* backup = nullptr;

	struct : IRemoteCallable {
		bool call(IStructuredInput* arguments, IStructuredOutput& result, Callback<> introduceResult,
				Callback<void(std::string_view)>, std::optional<UserId>) const final override {
			if (arguments) {
				arguments->readObject(SerialisationFlags::OBJECT_LAYOUT_KNOWN, [&] (std::optional<std::string_view> name, int) {
					if (name) {
						arguments->skipObjectElement(noFlags);
						return true;
					}
					return false;
				});
			}
			introduceResult();
			result.writeString(noFlags, static_cast<DummyRpcClass*>(parent())->message);
			return true;
		}
		std::string operator()() const {
			IRpcResponder* responder = getResponder();
			auto token = responder->send(UserId{}, this, [] (IStructuredOutput& out, RequestToken) {
				out.startWritingObject(SerialisationFlags::OBJECT_LAYOUT_KNOWN, 0);
				out.endWritingObject(SerialisationFlags::OBJECT_LAYOUT_KNOWN);
			});

			std::string returned;
			responder->getResponse(token, [&] (IStructuredInput& response) {
				returned = response.readString(noFlags);
			});
			return returned;
		}
		void listTypes(ISerialisableDescriptionFiller&) const override {}
		void generateDescription(IRemoteCallableDescriptionFiller& filler) const override {
			filler.addMethod(parent()->childName(this).first, "Gets the stored message", [&] (IPropertyDescriptionFiller& filler) { },
					[&] (IPropertyDescriptionFiller& filler) {
				filler.addMember("stored", "the stored message", [&] { TypedSerialiser<std::string>::describeType(filler); });
			});
		}
		using IRemoteCallable::IRemoteCallable;
	} getMessage = this;
	struct : IRemoteCallable {
		bool call(IStructuredInput* arguments, IStructuredOutput& result, Callback<> introduceResult,
				Callback<void(std::string_view)> introduceError, std::optional<UserId>) const final override {
			if (!arguments) {
				methodNotFoundError("Expected params");
				return false;
			}
			bool found = false;
			arguments->readObject(SerialisationFlags::OBJECT_LAYOUT_KNOWN, [&] (std::optional<std::string_view> name, int) {
				if (!name) {
					static_cast<DummyRpcClass*>(parent())->time = arguments->readInt(SerialisationFlags::INT_32);
					found = true;
					return false;
				}
				if (*name == "new_time") {
					static_cast<DummyRpcClass*>(parent())->time =
							arguments->readInt(noFlags);
					found = true;
				}
				return !found;
			});
			if (!found) {
				parseError("Missing mandatory argument");
			}
			introduceResult();
			result.writeNull(noFlags);
			return found;
		}
		void operator()(int newTime) {
			IRpcResponder* responder = getResponder();
			auto token = responder->send(UserId{}, this, [newTime] (IStructuredOutput& out, RequestToken) {
				out.startWritingObject(SerialisationFlags::OBJECT_LAYOUT_KNOWN, 1);
				out.introduceObjectMember(SerialisationFlags::OBJECT_LAYOUT_KNOWN, "new_time", 0);
				out.writeInt(SerialisationFlags::OBJECT_LAYOUT_KNOWN, newTime);
				out.endWritingObject(SerialisationFlags::OBJECT_LAYOUT_KNOWN);
			});

			responder->getResponse(token, [&] (IStructuredInput& response) {
				response.readNull(SerialisationFlags::OBJECT_LAYOUT_KNOWN);
			});
		}
		void listTypes(ISerialisableDescriptionFiller&) const override {}
		void generateDescription(IRemoteCallableDescriptionFiller& filler) const override {
			filler.addMethod(parent()->childName(this).first, "Sets the time", [&] (IPropertyDescriptionFiller& filler) {
				filler.addMember("new_time", "the new time", [&] { TypedSerialiser<int>::describeType(filler); });
			}, [&] (IPropertyDescriptionFiller&) { });
		}
		using IRemoteCallable::IRemoteCallable;
	} setTime = this;

	std::pair<std::string_view, int> childName(const IRemoteCallable* child) const final override {
		if (child == &getMessage)
			return {"get_message", 0};
		else if (child == &setTime)
			return {"set_time", 1};
		else if (child == backup && backup)
			return {"backup", 2};
		return {"", NO_SUCH_STRUCTURE};
	}
	const IRemoteCallable* getChild(std::string_view name) const final override {
		if (name == "get_message")
			return &getMessage;
		else if (name == "set_time")
			return &setTime;
		else if (name == "backup" && backup)
			return backup;
		std::cout << "Cant find " << name << std::endl;
		return nullptr;
	}
	const IRemoteCallable* getChild(int index) const final override {
		if (index == 0)
			return &getMessage;
		else if (index == 1)
			return &setTime;
		else if (index == 2 && backup)
			return backup;
		std::cout << "Cant find child number " << index << std::endl;
		return nullptr;
	}

	void setBackup(DummyRpcClass* newBackup) {
		backup = newBackup;
		setSelfAsParent(backup);
	}
	
	
	void listTypes(ISerialisableDescriptionFiller&) const override {}
	void generateDescription(IRemoteCallableDescriptionFiller& filler) const override {
		getMessage.generateDescription(filler);
		setTime.generateDescription(filler);
		if (backup)
			filler.addSubobject("backup", [this] (IRemoteCallableDescriptionFiller& filler) {
				backup->generateDescription(filler);
			});
	}
};

struct AdvancedRpcClass : RpcObject<AdvancedRpcClass> {
	std::string message;

	RpcMember<[] (AdvancedRpcClass* parent) {
		return parent->message;
	}> getMessage = child<"get_message">;

	RpcMember<[] (AdvancedRpcClass* parent, std::string newMessage = name("message")) {
		parent->message = newMessage;
	}> setMessage = child<"set_message">;

	RpcMember<[] (int first = name("first"), int second = name("second")) {
		return first + second;
	}> sum = child<"sum">;
};

struct ComplexRpcMessage : Serialisable<ComplexRpcMessage> {
	std::string message = key<"message">;
	std::string author = key<"author">;
};

struct ComplexRpcClass : RpcObject<ComplexRpcClass> {
	ComplexRpcMessage message;

	RpcMember<[] (ComplexRpcClass* parent) {
		return parent->message;
	}> getMessage = child<"get_message">;

	RpcMember<[] (ComplexRpcClass* parent, std::string newMessage = name("message"), std::string author = name("author")) {
		parent->message.message = newMessage;
		parent->message.author = author;
	}> setMessage = child<"set_message">;
};

struct FakeHttp {
	using StringType = std::string;

	std::string written;
	std::string toReturn;
	RequestToken tokenToGive;
	RequestToken tokenObtained;

	template <typename Lambda>
	RequestToken post(std::string_view contentType, const Lambda& call) {
		ExpandingBuffer<> buffer;
		call(buffer, tokenToGive);
		written.append(std::span<char>(buffer).data(), std::span<char>(buffer).size());
		return tokenToGive;
	}

	template <typename Lambda>
	void getResponse(RequestToken token, const Lambda& call) {
		tokenToGive.id++;
		tokenObtained = token;
		call(toReturn, true);
	}
};

struct FakeClient : ITcpClient {
	std::string request;
	std::string response;
	ServerReaction reaction;
	int64_t position;
	std::function<void()> expandResponse;
	void writeRequest(std::span<char> written) override {
		request = std::string_view(written.data(), written.size());
	}
	void getResponse(RequestToken, Callback<std::tuple<ServerReaction, RequestToken, int64_t>
					 (std::span<char> input, bool identified)> reader) override {
		if (expandResponse)
			expandResponse();
		auto [reactionObtained, token, positionObtained] = reader(std::span<char>(response.data(), response.size()), true);
		reaction = reactionObtained;
		position = positionObtained;
	}
};

template <typename Responder>
struct FakeServer {
	Responder& responder;
	std::pair<std::string, ServerReaction> respond(std::string request) {
		auto session = responder.getSession();
		std::string response;
		ServerReaction result = session.respond(std::span<char>(request.data(), request.size()),
						[&] (std::span<const char> output) {
				response = std::string_view(output.data(), output.size());
		}).first;
		return {response, result};
	}
};

struct ManualBinary {
	std::string str;
	template <typename T>
	void add(T added) {
		std::array<char, sizeof(T)> bytes;
		memcpy(bytes.data(), &added, sizeof(bytes));
		str.insert(str.end(), bytes.begin(), bytes.end());
	}
	template <typename SizeType = uint16_t>
	void addString(std::string_view appended) {
		add(SizeType(appended.size()));
		str.append(appended);
	}
};

int main(int argc, char** argv) {

	int errors = 0;
	int tests = 0;

	auto doATest = [&] (auto is, auto shouldBe) {
		tests++;
		if constexpr(std::is_floating_point_v<decltype(is)>) {
			if ((is > 0 && (is > shouldBe * 1.0001 || is < shouldBe * 0.9999)) ||
					(is < 0 && (is < shouldBe * 1.0001 || is > shouldBe * 0.9999))) {
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
	auto doATestIgnoringWhitespace = [&] (std::string is, std::string shouldBe) {
		tests++;
		auto isWhitespace = [] (char letter) {
			return letter == ' ' || letter == '\t' || letter == '\n' || letter == '\r';
		};
		for (int i = 0, j = 0; i < std::ssize(is) && j < std::ssize(shouldBe); i++, j++) {
			while (isWhitespace(is[i]) && i < std::ssize(is)) i++;
			while (isWhitespace(shouldBe[j]) && j < std::ssize(shouldBe)) j++;
			if ((i >= std::ssize(is)) != (j >= std::ssize(shouldBe)) || is[i] != shouldBe[j]) {
				errors++;
				std::cout << "Test failed: " << is << " instead of " << shouldBe << std::endl;
				break;
			}
		}
	};
	auto doATestBinary = [&] (auto is, auto shouldBe) {
		tests++;
		bool equal = (std::ssize(is), std::ssize(shouldBe));
		if (equal) {
			for (int i = 0; i < std::ssize(is); i++)
				if (is[i] != shouldBe[i]) {
					equal = false;
					break;
				}
		}
		if (!equal) {
			errors++;
			std::cout << "Test failed: " << std::endl;
			for (auto it : is) {
				std::cout << int(it) << ' ';
			}
			std::cout << std::endl << "instead of" << std::endl;
			for (auto it : shouldBe) {
				std::cout << int(it) << ' ';
			}
			std::cout << std::endl;
		}
	};

	std::string simpleJsonCode =
			"{\n"
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
		Bomba::ExpandingBuffer result;
		JSON::Output out(result);

		out.startWritingObject(noFlags, 4);
		out.introduceObjectMember(noFlags, "thirteen", 0);
		out.writeInt(noFlags, 13);
		out.introduceObjectMember(noFlags, "twoAndHalf", 1);
		out.writeFloat(noFlags, 2.5);
		out.introduceObjectMember(noFlags, "no", 2);
		out.writeBool(noFlags, false);
		out.introduceObjectMember(noFlags, "array", 3);
		out.startWritingArray(noFlags, 2);
		out.introduceArrayElement(noFlags, 0);
		out.writeNull(noFlags);
		out.introduceArrayElement(noFlags, 1);
		out.writeString(noFlags, "strink");
		out.endWritingArray(noFlags);
		out.endWritingObject(noFlags);

		doATest(std::string_view(result), simpleJsonCode);
	}

	{
		std::cout << "Testing JSON output facade" << std::endl;
		Bomba::ExpandingBuffer result;
		JSON::Output out(result);
		{
			auto obj = out.writeObject();
			obj.writeInt("thirteen", 13);
			obj.writeFloat("twoAndHalf", 2.5);
			obj.writeBool("no", false);
			{
				auto array = obj.writeArray("array");
				array.writeNull();
				array.writeString("strink");
			}
		}
		doATest(std::string_view(result), simpleJsonCode);
	}

	{
		std::cout << "Testing JSON read" << std::endl;
		std::string result;
		JSON::Input in(simpleJsonCode);

		doATest(in.identifyType(noFlags), IStructuredInput::TYPE_OBJECT);

		int realIndex = 0;
		in.readObject(noFlags, [&] (std::optional<std::string_view> name, int index) {
			doATest(realIndex, index);
			realIndex++;
			if (index == 0) {
				std::cout << "Testing JSON read int" << std::endl;
				doATest(*name, "thirteen");
				doATest(in.identifyType(noFlags), IStructuredInput::TYPE_INTEGER);
				doATest(in.readInt(noFlags), 13);
				doATest(in.good, true);
			} else if (index == 1) {
				std::cout << "Testing JSON read float" << std::endl;
				doATest(*name, "twoAndHalf");
				doATest(in.identifyType(noFlags), IStructuredInput::TYPE_FLOAT);
				doATest(in.readFloat(noFlags), 2.5);
				doATest(in.good, true);
			} else if (index == 2) {
				std::cout << "Testing JSON read bool" << std::endl;
				doATest(*name, "no");
				doATest(in.identifyType(noFlags), IStructuredInput::TYPE_BOOLEAN);
				doATest(in.readBool(noFlags), false);
				doATest(in.good, true);
			} else if (index == 3) {
				std::cout << "Testing JSON read array" << std::endl;
				doATest(*name, "array");
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
			}
			return true;
		});
		doATest(in.good, true);
	}

	std::string dummyObjectJson =
			"{\n"
			"	\"edges\" : 3,\n"
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
		std::cout << "Testing JSON skipping" << std::endl;
		std::string result;
		JSON::Input in(dummyObjectJson);
		doATest(in.good, true);

		auto start = in.storePosition(noFlags);
		in.readObject(noFlags, [&] (std::optional<std::string_view> name, int index) {
			if (index == 7) {
				doATest(*name, "story");
				doATest(in.identifyType(noFlags), IStructuredInput::TYPE_NULL);
			}
			in.skipObjectElement(noFlags);
			doATest(in.good, true);
			return true;
		});

		in.restorePosition(noFlags, start);
		in.readObject(noFlags, [&] (std::optional<std::string_view> name, int) {
			doATest(*name, "edges");
			return false;
		});
		doATest(in.good, true);
	}

	{
		std::cout << "Testing template facade" << std::endl;
		DummyObject tested;
		tested.edges = 3;
		tested.isRed = false;
		tested.name = "SuperTriangle";
		tested.edgeSizes = { 4, 5, 6 };
		tested.tags = {"not red", "triangle", "super"};
		tested.notes = {{ "none", "nil"}, {"nothing", "null"}};
		tested.story = nullptr;
		std::string written = tested.serialise<StringJSON>();
		doATest(written, dummyObjectJson);

		DummyObject tested2;
		tested2.deserialise<JSON>(dummyObjectJson);
		doATest(tested2.edges, 3);
		doATest(tested2.isRed, false);
		doATest(tested2.name, "SuperTriangle");
		doATest(tested2.edgeSizes.size(), 3ull);
		doATest(tested2.tags[0], "not red");
		doATest(tested2.notes["none"], "nil");
		doATest(tested2.story, nullptr);
	}

	const std::string standardObjectJson =
			"{\n"
			"	\"index\" : 8,\n"
			"	\"sub_index\" : 15,\n"
			"	\"deleted\" : true,\n"
			"	\"contents\" : \"Not much at this point\"\n"
			"}";

	{
		std::cout << "Testing Serialisable class write" << std::endl;
		StandardObject tested;
		tested.index = 8;
		tested.subIndex = makeOptional<short int>(15);
		tested.contents = "Not much at this point";
		std::string written = tested.serialise<StringJSON>();
		std::cout << "Object is at " << &tested << " and the optional at " << &tested.subIndex << std::endl;
		doATest(written, standardObjectJson);

		std::cout << "Testing Serialisable class read" << std::endl;
		StandardObject tested2;
		tested2.deserialise<JSON>(standardObjectJson);
		doATest(tested2.index, 8);
		doATest(bool(tested2.subIndex), true);
		doATest(*tested2.subIndex, 15);
		doATest(tested2.deleted, true);
		doATest(tested2.contents, "Not much at this point");
	}
	
	{
		std::cout << "Testing buffers" << std::endl;
		
		{
			Bomba::ExpandingBuffer<3> buffer;
			buffer += '1';
			buffer += "10";
			buffer += " trombones from heck";
			doATest(std::string_view(buffer), "110 trombones from heck");
		}
		
		{
			struct TestStreamingBuffer : StreamingBuffer<3> {
				std::string data;
				void flush() override {
					data += std::string_view(_basic.data(), _basic.size());
					data += '\n';
				}
			};
			TestStreamingBuffer buffer;
			buffer += "15";
			buffer += '3';
			buffer += " bananas out of nowhere";
			doATest(buffer.data, "153\n ba\nnan\nas \nout\n of\n no\nwhe\n");
		}
	}

	const std::string dummyRpcRequest1 =
			"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : 0,\n"
			"	\"method\" : \"get_message\",\n"
			"	\"params\" : {}\n"
			"}";

	const std::string dummyRpcRequest2 =
			"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : 1,\n"
			"	\"method\" : \"set_time\",\n"
			"	\"params\" : {\n"
			"		\"new_time\" : 1366\n"
			"	}\n"
			"}";

	const std::string dummyRpcRequest3 =
			"{\n"
			"	\"params\" : {\n"
			"		\"new_time\" : 1366\n"
			"	},\n"
			"	\"method\" : \"set_time\",\n"
			"	\"id\" : \"initial_time_set\","
			"	\"jsonrpc\" : \"2.0\"\n"
			"}";

	const std::string dummyRpcRequest4 =
			"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : 2,\n"
			"	\"method\" : \"backup.get_message\",\n"
			"	\"params\" : {}\n"
			"}";

	const std::string dummyRpcRequest5 =
			"[\n"
			"	{\n"
			"		\"jsonrpc\" : \"2.0\",\n"
			"		\"id\" : null,\n"
			"		\"method\" : \"backup.get_message\",\n"
			"		\"params\" : {}\n"
			"	},\n"
			"	{\n"
			"		\"jsonrpc\" : \"2.0\",\n"
			"		\"id\" : 3.5,\n"
			"		\"method\" : \"get_message\",\n"
			"		\"params\" : {}\n"
			"	}\n"
			"]";

	const std::string dummyRpcReply1 =
			"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : 0,\n"
			"	\"result\" : \"nevermind\"\n"
			"}";

	const std::string dummyRpcReply2 =
			"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : 1,\n"
			"	\"result\" : null\n"
			"}";

	const std::string dummyRpcReply3 =
			"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : \"initial_time_set\",\n"
			"	\"result\" : null\n"
			"}";

	const std::string dummyRpcReply4 =
			"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : 2,\n"
			"	\"result\" : \"actually...\"\n"
			"}";

	const std::string dummyRpcReply5 =
			"[\n"
			"	{\n"
			"		\"jsonrpc\" : \"2.0\",\n"
			"		\"id\" : null,\n"
			"		\"result\" : \"actually...\"\n"
			"	},\n"
			"	{\n"
			"		\"jsonrpc\" : \"2.0\",\n"
			"		\"id\" : 3.5,\n"
			"		\"result\" : \"nevermind\"\n"
			"	}\n"
			"]";
			
	struct DummyWriteStarter : Bomba::IWriteStarter {
		Bomba::ExpandingBuffer<1024> buffer;
		void writeUnknownSize(std::string_view, Callback<void(GeneralisedBuffer&)> filler) override {
			filler(buffer);
		}
		void writeKnownSize(std::string_view, int64_t, Callback<void(GeneralisedBuffer&)> filler) override {
			filler(buffer);
		}
	};
	

	{
		std::cout << "Testing JSON-RPC server" << std::endl;

		DummyRpcClass dummyRpcBackup;
		DummyRpcClass dummyRpc;
		dummyRpc.setBackup(&dummyRpcBackup);
		dummyRpc.message = "nevermind";
		dummyRpcBackup.message = "actually...";
		DummyWriteStarter writeStarter;
		auto viewToSpan = [] (std::string_view str) { return std::span<char>(const_cast<char*>(str.data()), str.size()); };
		JsonRpcServerProtocol protocol = dummyRpc;
		protocol.post("", "application/json", viewToSpan(dummyRpcRequest1), writeStarter);
		doATest(std::string_view(writeStarter.buffer), dummyRpcReply1);

		writeStarter.buffer.clear();
		protocol.post("", "application/json", viewToSpan(dummyRpcRequest2), writeStarter);
		doATest(std::string_view(writeStarter.buffer), dummyRpcReply2);
		doATest(dummyRpc.time, 1366);

		dummyRpc.time = 1200;
		writeStarter.buffer.clear();
		protocol.post("", "application/json", viewToSpan(dummyRpcRequest3), writeStarter);
		doATest(std::string_view(writeStarter.buffer), dummyRpcReply3);
		doATest(dummyRpc.time, 1366);

		writeStarter.buffer.clear();
		protocol.post("", "application/json", viewToSpan(dummyRpcRequest4), writeStarter);
		doATest(std::string_view(writeStarter.buffer), dummyRpcReply4);

		writeStarter.buffer.clear();
		protocol.post("", "application/json", viewToSpan(dummyRpcRequest5), writeStarter);
		doATest(std::string_view(writeStarter.buffer), dummyRpcReply5);
	}

	{
		std::cout << "Testing JSON-RPC client" << std::endl;

		DummyRpcClass dummyRpcBackup;
		DummyRpcClass dummyRpc;
		dummyRpc.setBackup(&dummyRpcBackup);
		FakeHttp http;
		JsonRpcClientProtocol client{dummyRpc, http};
		http.toReturn = dummyRpcReply1;
		doATest(dummyRpc.getMessage(), "nevermind");
		doATest(http.written, dummyRpcRequest1);

		http.written.clear();
		http.toReturn = dummyRpcReply2;
		dummyRpc.setTime(1366);
		doATest(http.written, dummyRpcRequest2);

		http.written.clear();
		http.toReturn = dummyRpcReply4;
		doATest(dummyRpc.backup->getMessage(), "actually...");
		doATest(http.written, dummyRpcRequest4);
	}

	const std::string advancedRpcRequest1 =
			"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : 0,\n"
			"	\"method\" : \"set_message\",\n"
			"	\"params\" : {\n"
			"		\"message\" : \"Flag\"\n"
			"	}\n"
						"}";
	const std::string advancedRpcRequest2 =
			"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : 1,\n"
			"	\"method\" : \"get_message\",\n"
			"	\"params\" : {}\n"
			"}";
	const std::string advancedRpcRequest3 =
			"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : 2,\n"
			"	\"method\" : \"sum\",\n"
			"	\"params\" : {\n"
			"		\"first\" : 2,\n"
			"		\"second\" : 3\n"
			"	}\n"
			"}";

	const std::string advancedRpcResponse1 =
			"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : 0,\n"
			"	\"result\" : null\n"
			"}";
	const std::string advancedRpcResponse2 =
			"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : 1,\n"
			"	\"result\" : \"Flag\"\n"
			"}";
	const std::string advancedRpcResponse3 =
			"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : 2,\n"
			"	\"result\" : 5\n"
			"}";

	{
		std::cout << "Testing RPC object" << std::endl;
		AdvancedRpcClass advancedRpc;
		DummyWriteStarter writeStarter;
		auto viewToSpan = [] (std::string_view str) { return std::span<char>(const_cast<char*>(str.data()), str.size()); };
		Bomba::JsonRpcServerProtocol protocol = advancedRpc;
		protocol.post("", "application/json", viewToSpan(advancedRpcRequest1), writeStarter);
		doATest(std::string_view(writeStarter.buffer), advancedRpcResponse1);
		writeStarter.buffer.clear();
		protocol.post("", "application/json", viewToSpan(advancedRpcRequest2), writeStarter);
		doATest(std::string_view(writeStarter.buffer), advancedRpcResponse2);
		writeStarter.buffer.clear();
		protocol.post("", "application/json", viewToSpan(advancedRpcRequest3), writeStarter);
		doATest(std::string_view(writeStarter.buffer), advancedRpcResponse3);

		FakeHttp http;
		Bomba::JsonRpcClientProtocol client{advancedRpc, http};
		http.toReturn = advancedRpcResponse1;
		advancedRpc.setMessage("Flag");
		doATest(http.written, advancedRpcRequest1);

		http.written.clear();
		http.toReturn = advancedRpcResponse2;
		auto future = advancedRpc.getMessage.async();
		doATest(future.is_ready(), true);
		doATest(future.get(), "Flag");
		doATest(http.written, advancedRpcRequest2);

		http.written.clear();
		http.toReturn = advancedRpcResponse3;
		doATest(advancedRpc.sum(2, 3), 5);
		doATest(http.written, advancedRpcRequest3);
	}

	const std::string expectedGet =
			"GET /conspiracy.html HTTP/1.1\r\n"
			"Host: faecesbook.con\r\n"
			"\r\n";

	const std::string replyToGet =
			"HTTP/1.1 200 OK\r\n"
			"Date: Sun, 11 Jul 2021 18:46:33 GMT\r\n" // Extra entries that must be ignored
			"Server: Sioux\r\n"
			"Content-Length: 9\r\n"
			"\r\n"
			"Blablabla";

	{
		std::cout << "Testing HTTP client" << std::endl;

		FakeClient client;
		Bomba::HttpClient http(client, "faecesbook.con");
		auto token = http.get("/conspiracy.html");
		doATest(client.request, expectedGet);

		std::string downloaded;
		client.expandResponse = [&, step = 1] () mutable {
			client.response = replyToGet.substr(0, std::min<int>(step, replyToGet.size()));
			step++;
		};
		http.getResponse(token, [&] (std::span<char> response, bool success) {
			doATest(success, true);
			downloaded = std::string_view(response.data(), response.size());
			return true;
		});
		doATest(downloaded, "Blablabla");
		doATest(int(client.reaction), int(ServerReaction::OK));
	}

	const std::string someHtml =
R"~(<!doctype html>
<html lang=en>
	<head><title>CONSPIRACY</title></head>
	<body>The bloodsucking shapeshifting reptilians are trying to steal the souls of our children! Awaken sheeple!</body>
</html>)~";

	const std::string sentHtml =
			"HTTP/1.1 200 OK\r\n"
			"Content-Length: 197\r\n"
			"Content-Type: text/html\r\n"
			"\r\n"
			"<!doctype html>\n"
			"<html lang=en>\n"
			"	<head><title>CONSPIRACY</title></head>\n"
			"	<body>The bloodsucking shapeshifting reptilians are trying to steal the souls of our children! Awaken sheeple!</body>\n"
			"</html>";
	{
		std::cout << "Testing HTTP server" << std::endl;
		Bomba::SimpleGetResponder getResponder;
		getResponder.resource = someHtml;
		Bomba::DummyPostResponder postResponder;
		Bomba::HttpServer http = {getResponder, postResponder};
		FakeServer server = {http};
		auto [response, reaction] = server.respond(expectedGet);
		doATest(int(reaction), int(ServerReaction::OK));
		doATestIgnoringWhitespace(response, sentHtml);
	}

	const std::string longerExpectedGet =
				"GET /?assigned=%26%2344608%3B%26%2351221%3B%26%2351008%3B+told+us HTTP/1.1\r\n"
				"Host: faecesbook.con\r\n"
				"\r\n";

	const std::string expectedPost =
				"POST / HTTP/1.1\r\n"
				"Content-Length: 58\r\n"
				"Host: faecesbook.con\r\n"
				"Content-Type: application/x-www-form-urlencoded\r\n"
				"\r\n"
				"assigned=G%f6tterd%e4merung%21+End+is+comi%26%23328%3bg%21";

	const std::string expectedPostResponse =
				"HTTP/1.1 204 No Content\r\n\r\n";

	static std::string rpcTestAssigned = "";
	using InlineMethod = Bomba::RpcStatelessLambda<[] (std::string newValue = Bomba::name("assigned")) {
		rpcTestAssigned = newValue;
	}>;

	{
		std::cout << "Testing better HTTP client" << std::endl;
		FakeClient client;
		Bomba::HttpClient<> http(client, "faecesbook.con");
		auto token = http.get("/?assigned=%26%2344608%3B%26%2351221%3B%26%2351008%3B+told+us");
		doATest(client.request, longerExpectedGet);
		client.response = sentHtml;

		std::string downloaded;
		http.getResponse(token, [&] (std::span<char> response, bool success) {
			doATest(success, true);
			downloaded = std::string_view(response.data(), response.size());
			return true;
		});
		doATest(someHtml, downloaded);

		rpcTestAssigned.clear();
		InlineMethod inlineMethod;
		inlineMethod.setResponder(http);
		client.response = expectedPostResponse;
		inlineMethod("Götterdämerung! End is comiňg!");
		doATestIgnoringWhitespace(client.request, expectedPost);
	}

	 {
		std::cout << "Testing better HTTP server" << std::endl;
		Bomba::SimpleGetResponder getResponder;
		getResponder.resource = someHtml;
		InlineMethod inlineMethod;
		Bomba::RpcGetResponder<std::string> betterGetResponder(getResponder, inlineMethod);
		Bomba::HtmlPostResponder postResponder(inlineMethod);
		Bomba::HttpServer http(betterGetResponder, postResponder);
		FakeServer server = {http};
		{
			auto [response, reaction] = server.respond(expectedGet);
			doATest(int(reaction), int(ServerReaction::OK));
			doATestIgnoringWhitespace(response, sentHtml);
		}
		{
			auto [response, reaction] = server.respond(longerExpectedGet);
			doATest(int(reaction), int(ServerReaction::OK));
			doATestIgnoringWhitespace(response, sentHtml);
			doATest("김정은 told us", rpcTestAssigned);
		}
		{
			auto [response, reaction] = server.respond(expectedPost);
			doATest(int(reaction), int(ServerReaction::OK));
			doATestIgnoringWhitespace(response, expectedPostResponse);
			doATest("Götterdämerung! End is comiňg!", rpcTestAssigned);
		}
	}

	auto makeHttpTestFixture = [&] {
		struct Fixture {
			Bomba::SimpleGetResponder getResponder;
			InlineMethod methodServer;
			Bomba::RpcGetResponder<std::string> betterGetResponder = {getResponder, methodServer};
			Bomba::HtmlPostResponder<> postResponder = {methodServer};
			Bomba::HttpServer<> httpServer = {betterGetResponder, postResponder};
			Bomba::BackgroundTcpServer<decltype(httpServer)> server = {httpServer, 8901}; // Very unlikely this port will be used for something

			InlineMethod methodClient;
			std::string targetAddress = "0.0.0.0";
			std::string targetPort = "8901";
			Bomba::SyncNetworkClient client = {targetAddress, targetPort};
			Bomba::HttpClient<> httpClient = {client, targetAddress};

			Fixture(const std::string& html) {
				getResponder.resource = html;
				methodClient.setResponder(httpClient);
			}

		};
		return Fixture(someHtml);
	};

	{
		std::cout << "Testing HTTP loop on localhost" << std::endl;
		auto fixture = makeHttpTestFixture();
		auto response = fixture.httpClient.get("/");
		fixture.httpClient.getResponse(response, [&] (std::span<char> response, bool) {
			doATest(someHtml, std::string_view(response.data(), response.size()));
			return true;
		});
		fixture.methodClient("We are so woke");
		doATest(rpcTestAssigned, "We are so woke");

		auto future = fixture.methodClient.async("We fear the needle");
		while (!future.is_ready()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		doATest(rpcTestAssigned, "We fear the needle");
	}

	{
		std::cout << "Testing HTTP out of order" << std::endl;
		auto fixture = makeHttpTestFixture();

		auto future1 = fixture.methodClient.async("We fear microchips");
		auto future2 = fixture.methodClient.async("We fear the politicians");
		auto future3 = fixture.methodClient.async("We fear the 5G");
		// Should not throw exceptions
		future3.get();
		future2.get();
		future1.get();
	}

	{
		std::cout << "Internally benchmarking HTTP server's GET...";
		auto fixture = makeHttpTestFixture();
		for (int i = 0; i < 2000; i++) {
			auto response = fixture.httpClient.get("/");
			fixture.httpClient.getResponse(response, [&] (std::span<char>, bool) {
				return true;
			});
		}
		std::cout << " average response time is " << fixture.server.averageResponseTime().count() << " ns per packet" << std::endl;
	}

	{
		std::cout << "Asynchronously benchmarking HTTP server's GET...";
		auto fixture = makeHttpTestFixture();
		std::atomic_int totalRequests = 0;
		auto workload = [&] () {
			std::array<Bomba::RequestToken, 50000> requests = {};
			Bomba::SyncNetworkClient client = {fixture.targetAddress, fixture.targetPort};
			Bomba::HttpClient<> httpClient = {client, fixture.targetAddress};
			
			for (Bomba::RequestToken& token : requests)
				token = httpClient.get("/");
			// We don't have enough threads to wait for each response individually
			for (Bomba::RequestToken& token : requests) {
				httpClient.getResponse(token, [&] (std::span<char>, bool) {
					totalRequests++;
					return true;
				});
			}
		};
		auto startTime = std::chrono::steady_clock::now();
		{
			std::vector<std::jthread> workers;
			for (int i = 0; i < 5; i++)
				workers.emplace_back(workload);
			// Destructors wait until they finish
		}
		auto endTime = std::chrono::steady_clock::now();
		int average = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count() / totalRequests;
		
		std::cout << " average response time is " << average << " ns per request, " <<
					fixture.server.averageResponseTime().count() << " ns reported internally" << std::endl;
	}

	{
		std::cout << "Internally benchmarking HTTP server's POST...";
		auto fixture = makeHttpTestFixture();
		for (int i = 0; i < 2000; i++) {
			fixture.methodClient("Protest against the clouds!");
		}
		std::cout << " average response time is " << fixture.server.averageResponseTime().count() << " ns per packet" << std::endl;
	}

	const std::string expectedJsonRpcRequest =
					"POST / HTTP/1.1\r\n"
					"Content-Length: 100\r\n"
					"Host: 0.0.0.0\r\n"
					"Content-Type: application/json\r\n\r\n"
					"{\n"
					"	\"jsonrpc\" : \"2.0\",\n"
					"	\"id\" : 1,\n"
					"	\"method\" : \"sum\",\n"
					"	\"params\" : {\n"
					"		\"first\" : 3,\n"
					"		\"second\" : 5\n"
					"	}\n"
					"}";
	const std::string expectedJsonRpcResponse =
					"HTTP/1.1 200 OK\r\n"
					"Content-Length: 48\r\n"
					"Content-Type: application/json\r\n\r\n"
					"{\n"
					"	\"jsonrpc\" : \"2.0\",\n"
					"	\"id\" : 1\n,"
					"	\"result\" : 8\n"
					"}";

	{
		std::cout << "Testing JSON-RPC client" << std::endl;
		AdvancedRpcClass method;

		FakeClient client;
		Bomba::HttpClient<> http(client, "faecesbook.con");
		Bomba::JsonRpcClient<> jsonRpc(method, client, "0.0.0.0");

		client.response = expectedJsonRpcResponse;
		doATest(method.sum(3, 5), 8);
		doATestIgnoringWhitespace(client.request, expectedJsonRpcRequest);
	}

	{
		std::cout << "Testing JSON-RPC server" << std::endl;
		AdvancedRpcClass method;
		Bomba::SimpleGetResponder getResponder;
		getResponder.resource = someHtml;
		Bomba::JsonRpcServer<std::string> jsonRpc(method, getResponder);
		FakeServer server = {jsonRpc};
		auto [response, reaction] = server.respond(expectedJsonRpcRequest);
		doATest(int(reaction), int(ServerReaction::OK));
		doATestIgnoringWhitespace(response, expectedJsonRpcResponse);
	}
	
	auto makeJsonRpcTestFixture = [&] {
		struct Fixture {
			AdvancedRpcClass methodServer;
			Bomba::JsonRpcServer<std::string> jsonRpcServer = {methodServer};
			Bomba::BackgroundTcpServer<decltype(jsonRpcServer)> server = {jsonRpcServer, 8901}; // Very unlikely this port will be used for something

			AdvancedRpcClass methodClient;
			Bomba::SyncNetworkClient client = {"0.0.0.0", "8901"};
			Bomba::JsonRpcClient<> jsonRpcClient = {methodClient, client, "0.0.0.0"};
		};
		return Fixture();
	};
	
	{
		std::cout << "Testing JSON-RPC loop on localhost" << std::endl;
		auto fixture = makeJsonRpcTestFixture();
		fixture.methodClient.setMessage("Believe a random guy on 4chan!");
		doATest(fixture.methodServer.message, "Believe a random guy on 4chan!");
		fixture.methodServer.message = "In 4chan we trust";
		doATest(fixture.methodClient.getMessage(), "In 4chan we trust");
	}

	{
		std::cout << "Internally benchmarking the JSON-RPC server...";
		auto fixture = makeJsonRpcTestFixture();
		for (int i = 0; i < 2000; i++) {
			fixture.methodClient.setMessage("Anonymous trolls are the most credible!");
		}
		std::cout << " average response time is " << fixture.server.averageResponseTime().count() << " ns" << std::endl;
	}
	
	const std::string expectedManualObjectDescription =
R"~({
	"edges" : "number",
	"isRed" : "boolean",
	"name" : "string",
	"edgeSizes" : [
		"number"
	],
	"ticks" : [
		[
			"number"
		]
	],
	"tags" : [
		"string"
	],
	"notes" : "object",
	"story" : "string"
})~";

	const std::string expectedAutomaticObjectDescription =
R"~({
	"index" : "number",
	"sub_index" : "number",
	"deleted" : "boolean",
	"contents" : "string"
})~";

	const std::string expectedAutomaticSubtypesDescription =
R"~({
	"StandardObject" : {
		"index" : "number",
		"sub_index" : "number",
		"deleted" : "boolean",
		"contents" : "string"
	}
})~";

	auto printObjectDescription = [&] (const IDescribableSerialisable& printed) {
		Bomba::ExpandingBuffer written;
		JSON::Output jsonOutput(written);
		{
			auto description = jsonOutput.writeObject();
			JsonWspMembersDescription descriptionGenerator(description);
			printed.describe(descriptionGenerator);
		}
		return std::string(written);
	};
	
	{
		std::cout << "Testing object description through JSON-WSP" << std::endl;
		DummyObject manual;
		std::string written = printObjectDescription(manual);
		doATest(written, expectedManualObjectDescription);
		
		StandardObject automatic;
		written = printObjectDescription(automatic);
		doATest(written, expectedAutomaticObjectDescription);
		
		written = automatic.getTypeName();
		doATest(written, "StandardObject");
		
		SuperStandardObject super;
		Bomba::ExpandingBuffer written2;
		JSON::Output jsonOutput(written2);
		{
			auto outputObject = jsonOutput.writeObject();
			JsonWspTypeDescription description{outputObject};
			super.listTypes(description);
		}
		doATest(std::string_view(written2), expectedAutomaticSubtypesDescription);
	}
	
	const std::string expectedRpcDescription =
R"~({
	"type" : "jsonwsp/description",
	"version" : "1.0",
	"servicename" : "Get Message and Set Time",
	"url" : "getmessageandsettime.com",
	"types" : {
	},
	"methods" : {
		"get_message" : {
			"doc_lines" : [
				"Gets the stored message"
			],
			"params" : {
			},
			"ret_info" : {
				"doc_lines" : [
					"the stored message"
				],
				"type" : "string"
			}
		},
		"set_time" : {
			"doc_lines" : [
				"Sets the time"
			],
			"params" : {
				"new_time" : {
					"def_order" : 1,
					"doc_lines" : [
						"the new time"
					],
					"type" : "number",
					"optional" : false
				}
			},
			"ret_info" : {
				"doc_lines" : [],
				"type" : null
			}
		},
		"backup.get_message" : {
			"doc_lines" : [
				"Gets the stored message"
			],
			"params" : {
			},
			"ret_info" : {
				"doc_lines" : [
					"the stored message"
				],
				"type" : "string"
			}
		},
		"backup.set_time" : {
			"doc_lines" : [
				"Sets the time"
			],
			"params" : {
				"new_time" : {
					"def_order" : 1,
					"doc_lines" : [
						"the new time"
					],
					"type" : "number",
					"optional" : false
				}
			},
			"ret_info" : {
				"doc_lines" : [],
				"type" : null
			}
		}
	}
})~";
	
	{
		std::cout << "Testing method description through JSON-WSP" << std::endl;
		DummyRpcClass rpc;
		DummyRpcClass backup;
		rpc.backup = &backup;
		std::string description = describeInJsonWsp<std::string>(rpc, "getmessageandsettime.com", "Get Message and Set Time");
		doATest(description, expectedRpcDescription);
	}
	
	const std::string expectedComplexRpcDescription =
R"~({
	"type" : "jsonwsp/description",
	"version" : "1.0",
	"servicename" : "Store Messages with Author Names",
	"url" : "readytofacespamfromcults.com",
	"types" : {
		"ComplexRpcMessage" : {
			"message" : "string",
			"author" : "string"
		}
	},
	"methods" : {
		"get_message" : {
			"doc_lines" : [],
			"params" : {
			},
			"ret_info" : {
				"doc_lines" : [],
				"type" : "ComplexRpcMessage"
			}
		},
		"set_message" : {
			"doc_lines" : [],
			"params" : {
				"message" : {
					"def_order" : 1,
					"doc_lines" : [],
					"type" : "string",
					"optional" : true
				},
				"author" : {
					"def_order" : 2,
					"doc_lines" : [],
					"type" : "string",
					"optional" : true
				}
			},
			"ret_info" : {
				"doc_lines" : [],
				"type" : null
			}
		}
	}
})~";
	
	{
		std::cout << "Testing complete description through JSON-WSP" << std::endl;
		ComplexRpcClass rpc;
		std::string description = describeInJsonWsp<std::string>(rpc, "readytofacespamfromcults.com", "Store Messages with Author Names");
		doATest(description, expectedComplexRpcDescription);
	}

	{
		std::cout << "Testing dynamic functions" << std::endl;
		int incrementee = 0;
		auto incrementer = [&incrementee] () {
			incrementee++;
		};
		RpcLambdaHolder holder(incrementer);
		doATest(holder.isLocal(), true);

		std::string_view emptyJson = "{}";
		JSON::Input input(emptyJson);
		Bomba::ExpandingBuffer outputBuffer;
		JSON::Output output(outputBuffer);
		holder->call(&input, output, [] {}, [] (std::string_view) {});
		doATest(incrementee, 1);

		RpcLambdaHolder movedHolder = std::move(holder);
		JSON::Input input2(emptyJson);
		movedHolder->call(&input2, output, [] {}, [] (std::string_view) {});
		doATest(incrementee, 2);

		int incrementee2 = 0;
		int incrementee3 = 0;
		RpcLambdaHolder holder2([&] { incrementee++; incrementee2++; incrementee3++; });
		JSON::Input input3(emptyJson);
		holder2->call(&input3, output, [] {}, [] (std::string_view) {});
		doATest(incrementee2, 1);

		RpcLambdaHolder movedHolder2 = std::move(holder2);
		JSON::Input input4(emptyJson);
		movedHolder2->call(&input4, output, [] {}, [] (std::string_view) {});
		doATest(incrementee3, 2);
	}

	constexpr std::string_view alternateAdvancedRequest2Response =
R"~({
	"jsonrpc" : "2.0",
	"id" : 1,
	"result" : {
		"message" : "Reptilians!!!",
		"author" : "Who knows"
	}
})~";

	constexpr std::string_view alternateAdvancedRequest2Response2 =
R"~({
	"jsonrpc" : "2.0",
	"id" : 2,
	"result" : {
		"message" : "Reptilians!!!",
		"author" : "Who knows"
	}
})~";

	{
		std::cout << "Testing dynamic object" << std::endl;

		DynamicRpcObject object;
		ComplexRpcMessage closure;
		closure.author = "Who knows";
		closure.message = "Reptilians!!!";
		RpcLambdaHolder getMessage = [&closure] () {
			return closure;
		};
		RpcLambdaHolder setMessage = [&closure] (const std::string& message = name("message"), const std::string& author = name("author")) {
			closure.message = message;
			closure.author = author;
		};
		object.add("get_message", std::move(getMessage));
		object.add("set_message", std::move(setMessage));
		std::string description = describeInJsonWsp<std::string>(object, "readytofacespamfromcults.com", "Store Messages with Author Names");
		doATest(description, expectedComplexRpcDescription);

		DummyWriteStarter writeStarter;
		auto viewToSpan = [] (std::string_view str) { return std::span<char>(const_cast<char*>(str.data()), str.size()); };
		Bomba::JsonRpcServerProtocol protocol = object;
		protocol.post("", "application/json", viewToSpan(advancedRpcRequest2), writeStarter);
		doATest(std::string_view(writeStarter.buffer), alternateAdvancedRequest2Response);

		TypedRpcLambdaHolder<ComplexRpcMessage()> getMessage2 = [&closure] () {
			return closure;
		};
		RpcLambdaHolder setMessage2 = [&closure] (const std::string& message = name("message"), const std::string& author = name("author")) {
			closure.message = message;
			closure.author = author;
		};
		DynamicRpcObject backupObject;
		backupObject.add("get_message", RpcLambdaHolder::nonOwning(*getMessage2));
		backupObject.add("set_message", RpcLambdaHolder::nonOwning(*setMessage2));
		backupObject.add("backup", RpcLambdaHolder::nonOwning(object));

		DummyWriteStarter writeStarter2;
		Bomba::JsonRpcServerProtocol protocol2 = backupObject;
		protocol2.post("", "application/json", viewToSpan(dummyRpcRequest4), writeStarter2);
		doATest(std::string_view(writeStarter2.buffer), alternateAdvancedRequest2Response2);
	}

	{
		std::cout << "Testing binary format write" << std::endl;
		ManualBinary expected;
		std::string output;
		BinaryFormat<>::Output out(output);

		out.writeBool(noFlags, true);
		expected.add(true);
		out.writeInt(SerialisationFlags::UINT_16, 33333);
		expected.add(uint16_t(33333));
		out.writeInt(SerialisationFlags::INT_64, -1);
		expected.add(int64_t(-1));
		out.writeFloat(SerialisationFlags::FLOAT_32, 1.53);
		expected.add(float(1.53));
		std::string testString = "There is a meaning behind these numbers";
		out.writeString(noFlags, testString);
		expected.addString(testString);
		doATestBinary(output, expected.str);
	}

	{
		std::cout << "Testing binary format write structured" << std::endl;
		ManualBinary expected;
		std::string output;

		BinaryFormat<>::Output out(output);
		{
			auto writer = out.writeArray(2);
			expected.add(int16_t(2));
			writer.writeBool(true);
			expected.add(true);
			writer.writeBool(false);
			expected.add(false);
		}

		{
			auto writer = out.writeObject(2);
			expected.add(int16_t(2));
			writer.writeInt("a", 1);
			expected.addString("a");
			expected.add(1);

			writer.writeInt("b", 2);
			expected.addString("b");
			expected.add(2);
		}

		out.startWritingObject(SerialisationFlags::OBJECT_LAYOUT_KNOWN, 2);
		out.introduceObjectMember(SerialisationFlags::OBJECT_LAYOUT_KNOWN, "a", 0);
		out.writeInt(noFlags, 1);
		expected.add(1);
		out.introduceObjectMember(SerialisationFlags::OBJECT_LAYOUT_KNOWN, "b", 1);
		out.writeInt(noFlags, 2);
		expected.add(2);
		out.endWritingObject(SerialisationFlags::OBJECT_LAYOUT_KNOWN);

		doATestBinary(output, expected.str);
	}

	{
		std::cout << "Testing binary format write high level" << std::endl;
		ManualBinary expected;

		StandardObject obj;
		expected.add(3);
		expected.add(false);
		expected.add(true);
		expected.addString(obj.contents);

		std::string output = obj.serialise<BinaryFormat<>>();
		doATestBinary(output, expected.str);
	}

	{
		std::cout << "Testing binary format read" << std::endl;
		ManualBinary reading;
		reading.add(false);
		reading.add(int8_t(-9));
		reading.add(int32_t(-444));
		reading.add(int64_t(4325));
		reading.add(float(3.5));
		constexpr std::string_view testString = "It confirms my biases!";
		reading.addString(testString);

		BinaryFormat<>::Input in(reading.str);
		doATest(in.readBool(noFlags), false);
		doATest(in.readInt(SerialisationFlags::INT_8), -9);
		doATest(in.readInt(SerialisationFlags::INT_32), -444);
		doATest(in.readInt(SerialisationFlags::INT_64), 4325);
		doATest(in.readFloat(SerialisationFlags::FLOAT_32), 3.5);
		doATest(in.readString(noFlags), testString);
		doATest(in.good, true);
	}

	{
		std::cout << "Testing binary format read structured" << std::endl;
		ManualBinary reading;
		reading.add(uint16_t(3));
		reading.add(int8_t(-7));
		reading.add(int8_t(-3));
		reading.add(int8_t(9));
		reading.add(uint16_t(2));
		reading.addString("+");
		reading.add(int64_t(82));
		reading.addString("-");
		reading.add(float(-23.27));
		constexpr std::string_view testString = "ABCDEFSorosGates5G";
		reading.addString(testString);
		reading.add(int32_t(182));

		BinaryFormat<>::Input in(reading.str);
		in.startReadingArray(noFlags);
		doATest(in.readInt(SerialisationFlags::INT_8), -7);
		doATest(in.nextArrayElement(noFlags), true);
		doATest(in.readInt(SerialisationFlags::INT_8), -3);
		doATest(in.nextArrayElement(noFlags), true);
		doATest(in.readInt(SerialisationFlags::INT_8), 9);
		doATest(in.nextArrayElement(noFlags), false);
		in.endReadingArray(noFlags);

		in.readObject(noFlags, [&] (std::optional<std::string_view> name, int index) {
			if (index == 0) {
				doATest(name.has_value(), true);
				doATest(*name, "+");
				doATest(in.readInt(SerialisationFlags::INT_64), 82);
			} else if (index == 1) {
				doATest(name.has_value(), true);
				doATest(*name, "-");
				doATest(in.readFloat(SerialisationFlags::FLOAT_32), -23.27);
			} else {
				std::cout << "Error, wasn't supposed to iterate more than twice" << std::endl;
				errors++;
				return false;
			}
			return true;
		});

		in.readObject(SerialisationFlags::OBJECT_LAYOUT_KNOWN, [&] (std::optional<std::string_view> name, int index) {
			if (index == 0) {
				doATest(name.has_value(), false);
				doATest(in.readString(noFlags), testString);
			} else if (index == 1) {
				doATest(name.has_value(), false);
				doATest(in.readInt(SerialisationFlags::INT_32), 182);
			} else
				return false;
			return true;
		});
		doATest(in.good, true);
	}

	{
		std::cout << "Testing binary format read high level" << std::endl;
		ManualBinary reading;

		StandardObject obj;
		reading.add(9);
		reading.add(true);
		reading.add(static_cast<short int>(12));
		reading.add(false);
		reading.addString(obj.contents);
		obj.contents = "This is so wrong";

		obj.deserialise<BinaryFormat<>>(reading.str);
		doATest(obj.index, 9);
		doATest(bool(obj.subIndex), true);
		doATest(*obj.subIndex, 12);
		doATest(obj.deleted, false);
		doATest(obj.contents, StandardObject().contents);
	}

	ManualBinary binaryRequest1;
	constexpr int binaryRequestSize1 = 4 + 2 + 1 + sizeof(int);
	binaryRequest1.add(uint32_t(0)); // First message
	binaryRequest1.add(uint16_t(binaryRequestSize1)); // Size
	binaryRequest1.add(uint8_t(1)); // Request to method 1
	binaryRequest1.add(int(100)); // Argument value is 100

	ManualBinary binaryResponse1;
	binaryResponse1.add(uint32_t(0)); // First message
	binaryResponse1.add(uint16_t(4 + 2)); // Size

	ManualBinary binaryRequest2;
	int binaryRequestSize2 = 4 + 2 + 1;
	binaryRequest2.add(uint32_t(1)); // Second message
	binaryRequest2.add(uint16_t(binaryRequestSize2)); // Size
	binaryRequest2.add(uint8_t(0)); // Request to method 0

	ManualBinary binaryResponse2;
	constexpr std::string_view textInBinary = "Cens";
	binaryResponse2.add(uint32_t(1)); // Second message
	binaryResponse2.add(uint16_t(4 + 2 + 2 + textInBinary.size())); // Size
	binaryResponse2.addString(textInBinary);

	{
		std::cout << "Testing binary RPC server" << std::endl;
		DummyRpcClass api;
		BinaryProtocolServer<> server(api);
		api.message = textInBinary;

		{

			auto session = server.getSession();
			bool called = false;
			auto callResult = session.respond(std::span<char>(binaryRequest1.str.begin(), binaryRequest1.str.end()), [&] (std::span<const char> response) {
				doATestBinary(response, binaryResponse1.str);
				called = true;
			});
			doATest(called, true);
			doATest(int(callResult.first), int(ServerReaction::OK));
			doATest(callResult.second, binaryRequestSize1);
		}

		{

			auto session = server.getSession();
			bool called = false;
			auto callResult = session.respond(std::span<char>(binaryRequest2.str.begin(), binaryRequest2.str.end()), [&] (std::span<const char> response) {
				doATestBinary(response, binaryResponse2.str);
				called = true;
			});
			doATest(called, true);
			doATest(int(callResult.first), int(ServerReaction::OK));
			doATest(callResult.second, binaryRequestSize2);
		}
	}

	{
		std::cout << "Testing binary RPC client" << std::endl;
		DummyRpcClass api;
		FakeClient fakeTcpClient;
		BinaryProtocolClient<> client(api, fakeTcpClient);

		{
			fakeTcpClient.response = binaryResponse1.str;
			api.setTime(100);
			doATestBinary(fakeTcpClient.request, binaryRequest1.str);
		}

		{
			fakeTcpClient.response = binaryResponse2.str;
			doATestBinary(api.getMessage(), textInBinary);
			doATestBinary(fakeTcpClient.request, binaryRequest2.str);
		}

	}

	{
		std::cout << "Testing binary RPC high level" << std::endl;
		AdvancedRpcClass clientApi;
		FakeClient fakeTcpClient;
		BinaryProtocolClient<> client(clientApi, fakeTcpClient);
		std::string message = "Sssh, listen...";

		ManualBinary expectedResponse;
		constexpr int expectedResponseSize = 4 + 2;
		expectedResponse.add(uint32_t(0)); // First message
		expectedResponse.add(uint16_t(expectedResponseSize)); // Size
		fakeTcpClient.response = expectedResponse.str;

		clientApi.setMessage(message);

		AdvancedRpcClass serverApi;
		BinaryProtocolServer<> server(serverApi);
		auto serverSession = server.getSession();
		std::string serverResponse;

		auto callResult = serverSession.respond(std::span<char>(fakeTcpClient.request.begin(), fakeTcpClient.request.end()),
					[&] (std::span<const char> response) {
			serverResponse.append(response.data(), response.size());
		});

		doATestBinary(expectedResponse.str, serverResponse);
		doATest(serverApi.message, message);
	}

	static std::string statelessLambdaString = "";
	Bomba::RpcStatelessLambda<[] (std::string newString = Bomba::name("update")) {
		std::swap(statelessLambdaString, newString);
		return newString;
	}> standaloneMethod;

	{
		std::cout << "Testing single-method binary RPC" << std::endl;
		decltype(standaloneMethod) clientApi;
		FakeClient fakeTcpClient;
		BinaryProtocolClient<> client(clientApi, fakeTcpClient);

		ManualBinary expectedResponse;
		constexpr int expectedResponseSize = 4 + 2 + 2;
		expectedResponse.add(uint32_t(0)); // First message
		expectedResponse.add(uint16_t(expectedResponseSize)); // Size
		expectedResponse.add(uint16_t(0)); // Empty string
		fakeTcpClient.response = expectedResponse.str;

		std::string announcement = "Birds are robots";
		clientApi(announcement);

		decltype(standaloneMethod) serverApi;
		BinaryProtocolServer<> server(serverApi);
		auto serverSession = server.getSession();
		std::string serverResponse;

		auto callResult = serverSession.respond(std::span<char>(fakeTcpClient.request.begin(), fakeTcpClient.request.end()),
					[&] (std::span<const char> response) {
			serverResponse.append(response.data(), response.size());
		});

		doATestBinary(expectedResponse.str, serverResponse);
		doATest(announcement, statelessLambdaString);
	}

	auto makeBinaryTestFixture = [&] {
		struct Fixture {
			AdvancedRpcClass serverApi;
			BinaryProtocolServer<> binaryServer = {serverApi};
			Bomba::BackgroundTcpServer<decltype(binaryServer)> server = {binaryServer, 8901}; // Very unlikely this port will be used for something

			AdvancedRpcClass clientApi;
			std::string targetAddress = "0.0.0.0";
			std::string targetPort = "8901";
			Bomba::SyncNetworkClient client = {targetAddress, targetPort};
			BinaryProtocolClient<> binaryClient = {clientApi, client};
		};
		return Fixture();
	};

	{
		std::cout << "Testing binary RPC loop on localhost" << std::endl;
		auto fixture = makeBinaryTestFixture();
		fixture.clientApi.setMessage("Take the red pill");
		doATest(fixture.serverApi.message, "Take the red pill");

		Future<std::string> future = fixture.clientApi.getMessage();
		while (!future.is_ready()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		doATest(future.get(), "Take the red pill");
	}

	{
		std::cout << "Testing binary RPC out of order" << std::endl;
		auto fixture = makeBinaryTestFixture();

		fixture.serverApi.message = "Don't be a blue pill.";
		Future<std::string> future1 = fixture.clientApi.getMessage();
		fixture.serverApi.message = "Hobbies are for normies.";
		Future<std::string> future2 = fixture.clientApi.getMessage();
		fixture.serverApi.message = "Obsess with fictional problems.";
		Future<std::string> future3 = fixture.clientApi.getMessage();
		// Should not throw exceptions nor get stuck
		doATest(future3.get(), "Obsess with fictional problems.");
		doATest(future2.get(), "Hobbies are for normies.");
		doATest(future1.get(), "Don't be a blue pill.");
	}

	{
		std::cout << "Internally benchmarking binary RPC server...";
		auto fixture = makeBinaryTestFixture();
		for (int i = 0; i < 2000; i++) {
			fixture.clientApi.sum(12, 35);
		}
		std::cout << " average response time is " << fixture.server.averageResponseTime().count() << " ns per packet" << std::endl;
	}

	{
		std::cout << "Asynchronously benchmarking binary RPC server...";
		auto fixture = makeBinaryTestFixture();
		std::atomic_int totalRequests = 0;
		auto workload = [&] () {
			std::array<Future<int>, 50000> requests = {};
			Bomba::SyncNetworkClient client = {fixture.targetAddress, fixture.targetPort};
			AdvancedRpcClass clientApi;
			BinaryProtocolClient<> binaryClient = {clientApi, client};

			for (Future<int>& future : requests)
				future = clientApi.sum.async(15, 25);
			// We don't have enough threads to wait for each response individually
			for (Future<int>& future : requests) {
				future.get();
				totalRequests++;
			}
		};
		auto startTime = std::chrono::steady_clock::now();
		{
			std::vector<std::jthread> workers;
			for (int i = 0; i < 5; i++)
				workers.emplace_back(workload);
			// Destructors wait until they finish
		}
		auto endTime = std::chrono::steady_clock::now();
		int average = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count() / totalRequests;

		std::cout << " average response time is " << average << " ns per request, " <<
					fixture.server.averageResponseTime().count() << " ns reported internally" << std::endl;
	}

	std::cout << "Passed: " << (tests - errors) << " / " << tests << ", errors: " << errors << std::endl;

	return 0;
}
