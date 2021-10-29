//usr/bin/g++ --std=c++20 -Wall $0 -g -lpthread -o ${o=`mktemp`} && exec $o $*
#include <iostream>
#include "bomba_core.hpp"
#include "bomba_json.hpp"
#include "bomba_object.hpp"
#include "bomba_rpc_object.hpp"
#include "bomba_json_rpc.hpp"
#include "bomba_http.hpp"
#include "bomba_tcp_server.hpp"
#include "bomba_sync_client.hpp"
#include "bomba_json_wsp_description.hpp"
#include "bomba_expanding_containers.hpp"
#include <string>
#include <vector>
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
	void readMember(IStructuredInput& format, std::string name, T& member) {
		auto got = format.nextObjectElement(noFlags);
		if (*got != name)
			throw ParseError("Expected " + name);
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
		format.startReadingObject(noFlags);
		readMember(format, "edges", edges);
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
	short int subIndex = key<"sub_index"> = 7;
	bool deleted = key<"deleted"> = true;
	std::string contents = key<"contents"> = "Not much yet";
};

struct SuperStandardObject : Serialisable<SuperStandardObject> {
	StandardObject child = key<"child">;
};

struct DummyRpcClass : IRemoteCallable {
	std::string message;
	int time = 0;
	DummyRpcClass* backup = nullptr;

	struct : IRemoteCallable {
		bool call(IStructuredInput* arguments, IStructuredOutput& result, Callback<> introduceResult,
				Callback<void(std::string_view)>, std::optional<UserId>) const final override {
			if (arguments) {
				arguments->startReadingObject(noFlags);
				arguments->endReadingObject(noFlags);
			}
			introduceResult();
			result.writeString(noFlags, static_cast<DummyRpcClass*>(parent())->message);
			return true;
		}
		std::string operator()() const {
			IRpcResponder* responder = getResponder();
			auto token = responder->send(UserId{}, this, [] (IStructuredOutput& out, RequestToken) {
				out.startWritingObject(noFlags, 0);
				out.endWritingObject(noFlags);
			});

			std::string returned;
			responder->getResponse(token, [&] (IStructuredInput& response) {
				returned = response.readString(noFlags);
			});
			return returned;
		}
		void listTypes(ISerialisableDescriptionFiller&) const override {}
		void generateDescription(IRemoteCallableDescriptionFiller& filler) const override {
			filler.addMethod(parent()->childName(this), "Gets the stored message", [&] (IPropertyDescriptionFiller& filler) { },
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
			arguments->startReadingObject(noFlags);
			std::optional<std::string_view> name;
			bool found = false;
			while ((name = arguments->nextObjectElement(noFlags))) {
				if (*name == "new_time") {
					static_cast<DummyRpcClass*>(parent())->time =
							arguments->readInt(noFlags);
					found = true;
				}
			}
			arguments->endReadingObject(noFlags);
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
				out.startWritingObject(noFlags, 1);
				out.introduceObjectMember(noFlags, "new_time", 0);
				out.writeInt(noFlags, newTime);
				out.endWritingObject(noFlags);
			});

			responder->getResponse(token, [&] (IStructuredInput& response) {
				response.readNull(noFlags);
			});
		}
		void listTypes(ISerialisableDescriptionFiller&) const override {}
		void generateDescription(IRemoteCallableDescriptionFiller& filler) const override {
			filler.addMethod(parent()->childName(this), "Sets the time", [&] (IPropertyDescriptionFiller& filler) {
				filler.addMember("new_time", "the new time", [&] { TypedSerialiser<int>::describeType(filler); });
			}, [&] (IPropertyDescriptionFiller& filler) { });
		}
		using IRemoteCallable::IRemoteCallable;
	} setTime = this;

	std::string_view childName(const IRemoteCallable* child) const final override {
		if (child == &getMessage)
			return "get_message";
		else if (child == &setTime)
			return "set_time";
		else if (child == backup && backup)
			return "backup";
		return "";
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
	Responder* responder;
	std::pair<std::string, ServerReaction> respond(std::string request) {
		auto session = responder->getSession();
		std::string response;
		ServerReaction result = session.respond(std::span<char>(request.data(), request.size()),
						[&] (std::span<const char> output) {
				response = std::string_view(output.data(), output.size());
		}).first;
		return {response, result};
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
	auto doATestIgnoringWhitespace = [&] (std::string is, std::string shouldBe) {
		tests++;
		auto isWhitespace = [] (char letter) {
			return letter == ' ' || letter == '\t' || letter == '\n' || letter == '\r';
		};
		for (int i = 0, j = 0; i < int(is.size()) && j < int(shouldBe.size()); i++, j++) {
			while (isWhitespace(is[i]) && i < int(is.size())) i++;
			while (isWhitespace(shouldBe[j]) && j < int(shouldBe.size())) j++;
			if ((i >= int(is.size())) != (j >= int(shouldBe.size())) || is[i] != shouldBe[j]) {
				errors++;
				std::cout << "Test failed: " << is << " instead of " << shouldBe << std::endl;
				break;
			}
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
		in.startReadingObject(noFlags);

		auto start = in.storePosition(noFlags);
		for (int i = 0; i < 7; i++) {
			in.nextObjectElement(noFlags);
			in.skipObjectElement(noFlags);
		}
		doATest(*in.nextObjectElement(noFlags), "story");
		doATest(in.identifyType(noFlags), IStructuredInput::TYPE_NULL);
		doATest(in.good, true);

		in.restorePosition(noFlags, start);
		doATest(*in.nextObjectElement(noFlags), "edges");
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
		tested.subIndex = 15;
		tested.contents = "Not much at this point";
		std::string written = tested.serialise<StringJSON>();
		doATest(written, standardObjectJson);

		std::cout << "Testing Serialisable class read" << std::endl;
		StandardObject tested2;
		tested2.deserialise<JSON>(standardObjectJson);
		doATest(tested2.index, 8);
		doATest(tested2.subIndex, 15);
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
		GeneralisedBuffer& writeUnknownSize(std::string_view resourceType) override {
			return buffer;
		}
		GeneralisedBuffer& writeKnownSize(std::string_view resourceType, int64_t size) override {
			return buffer;
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
		auto viewToSpan = [] (std::string_view str) { return std::span<const char>(str.data(), str.size()); };
		JsonRpcServerProtocol protocol = &dummyRpc;
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
		JsonRpcClientProtocol client{&dummyRpc, &http};
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
		auto viewToSpan = [] (std::string_view str) { return std::span<const char>(str.data(), str.size()); };
		Bomba::JsonRpcServerProtocol protocol = &advancedRpc;
		protocol.post("", "application/json", viewToSpan(advancedRpcRequest1), writeStarter);
		doATest(std::string_view(writeStarter.buffer), advancedRpcResponse1);
		writeStarter.buffer.clear();
		protocol.post("", "application/json", viewToSpan(advancedRpcRequest2), writeStarter);
		doATest(std::string_view(writeStarter.buffer), advancedRpcResponse2);
		writeStarter.buffer.clear();
		protocol.post("", "application/json", viewToSpan(advancedRpcRequest3), writeStarter);
		doATest(std::string_view(writeStarter.buffer), advancedRpcResponse3);

		FakeHttp http;
		Bomba::JsonRpcClientProtocol client{&advancedRpc, &http};
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
		Bomba::HttpClient<> http(&client, "faecesbook.con");
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
		Bomba::HttpServer<std::string, Bomba::SimpleGetResponder, Bomba::DummyPostResponder> http = {&getResponder, &postResponder};
		FakeServer server = {&http};
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
	using InlineMethod = Bomba::RpcLambda<[] (std::string newValue = Bomba::name("assigned")) {
		rpcTestAssigned = newValue;
	}>;

	{
		std::cout << "Testing better HTTP client" << std::endl;
		FakeClient client;
		Bomba::HttpClient<> http(&client, "faecesbook.con");
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
		inlineMethod.setResponder(&http);
		client.response = expectedPostResponse;
		inlineMethod("Götterdämerung! End is comiňg!");
		doATestIgnoringWhitespace(client.request, expectedPost);
	}

	 {
		std::cout << "Testing better HTTP server" << std::endl;
		Bomba::SimpleGetResponder getResponder;
		getResponder.resource = someHtml;
		InlineMethod inlineMethod;
		Bomba::RpcGetResponder<std::string, Bomba::SimpleGetResponder> betterGetResponder(&getResponder, &inlineMethod);
		Bomba::HtmlPostResponder postResponder(&inlineMethod);
		Bomba::HttpServer http(&betterGetResponder, &postResponder);
		FakeServer server = {&http};
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
			Bomba::RpcGetResponder<std::string, Bomba::SimpleGetResponder> betterGetResponder = {&getResponder, &methodServer};
			Bomba::HtmlPostResponder<> postResponder = {&methodServer};
			Bomba::HttpServer<std::string, decltype(betterGetResponder), decltype(postResponder)> httpServer = {&betterGetResponder, &postResponder};
			Bomba::BackgroundTcpServer<decltype(httpServer)> server = {&httpServer, 8901}; // Very unlikely this port will be used for something

			InlineMethod methodClient;
			std::string targetAddress = "0.0.0.0";
			std::string targetPort = "8901";
			Bomba::SyncNetworkClient client = {targetAddress, targetPort};
			Bomba::HttpClient<> httpClient = {&client, targetAddress};		

			Fixture(const std::string& html) {
				getResponder.resource = html;
				methodClient.setResponder(&httpClient);
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
			std::array<Bomba::RequestToken, 20000> requests = {};
			Bomba::SyncNetworkClient client = {fixture.targetAddress, fixture.targetPort};
			Bomba::HttpClient<> httpClient = {&client, fixture.targetAddress};
			
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
		
		std::cout << " average response time is " << average << " ns per request" << std::endl;
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
		Bomba::HttpClient<> http(&client, "faecesbook.con");
		Bomba::JsonRpcClient<> jsonRpc(&method, &client, "0.0.0.0");
		method.setResponder(&jsonRpc);

		client.response = expectedJsonRpcResponse;
		doATest(method.sum(3, 5), 8);
		doATestIgnoringWhitespace(client.request, expectedJsonRpcRequest);
	}

	{
		std::cout << "Testing JSON-RPC server" << std::endl;
		AdvancedRpcClass method;
		Bomba::SimpleGetResponder getResponder;
		getResponder.resource = someHtml;
		Bomba::JsonRpcServer<std::string, decltype(getResponder)> jsonRpc(&method, &getResponder);
		FakeServer server = {&jsonRpc};
		auto [response, reaction] = server.respond(expectedJsonRpcRequest);
		doATest(int(reaction), int(ServerReaction::OK));
		doATestIgnoringWhitespace(response, expectedJsonRpcResponse);
	}
	
	auto makeJsonRpcTestFixture = [&] {
		struct Fixture {
			AdvancedRpcClass methodServer;
			Bomba::JsonRpcServer<std::string> jsonRpcServer = {&methodServer};
			Bomba::BackgroundTcpServer<decltype(jsonRpcServer)> server = {&jsonRpcServer, 8901}; // Very unlikely this port will be used for something

			AdvancedRpcClass methodClient;
			Bomba::SyncNetworkClient client = {"0.0.0.0", "8901"};
			Bomba::JsonRpcClient<> jsonRpcClient = {&methodClient, &client, "0.0.0.0"};
			Fixture() {
				methodClient.setResponder(&jsonRpcClient);
			}

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
		jsonOutput.startWritingObject(SerialisationFlags::NONE, IStructuredOutput::UNKNOWN_SIZE);
		JsonWspMembersDescription descriptionGenerator(jsonOutput);
		printed.describe(descriptionGenerator);
		jsonOutput.endWritingObject(SerialisationFlags::NONE);
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
		jsonOutput.startWritingObject(SerialisationFlags::NONE, IStructuredOutput::UNKNOWN_SIZE);
		JsonWspTypeDescription description{jsonOutput};
		super.listTypes(description);
		jsonOutput.endWritingObject(SerialisationFlags::NONE);
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
		}
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

	std::cout << "Passed: " << (tests - errors) << " / " << tests << ", errors: " << errors << std::endl;

	return 0;
}
