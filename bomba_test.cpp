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
#include <string>
#include <vector>
#include <map>
#include <memory>

using namespace Bomba;

using JSON = BasicJson<>;
	
constexpr static auto noFlags = JSON::Output::Flags::NONE;

struct DummyObject : public ISerialisable {
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
};

struct StandardObject : Serialisable<StandardObject> {
	int index = key<"index"> = 3;
	short int subIndex = key<"sub_index"> = 7;
	bool deleted = key<"deleted"> = true;
	std::string contents = key<"contents"> = "Not much yet";
};

struct DummyRpcClass : IRemoteCallable {
	std::string message;
	int time = 0;
	DummyRpcClass* backup = nullptr;
	
	struct : IRemoteCallable {
		bool call(IStructuredInput* arguments, IStructuredOutput& result, const Callback<>& introduceResult,
				const Callback<>&, std::optional<UserId>) const final override {
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
			auto token = responder->send(UserId{}, this, makeCallback(
						[] (IStructuredOutput& out, RequestToken) {
				out.startWritingObject(noFlags, 0);
				out.endWritingObject(noFlags);
			}));
			
			std::string returned;
			responder->getResponse(token, makeCallback([&] (IStructuredInput& response) {
				returned = response.readString(noFlags);
			}));
			return returned;
		}
		using IRemoteCallable::IRemoteCallable;
	} getMessage = this;
	struct : IRemoteCallable {
		bool call(IStructuredInput* arguments, IStructuredOutput& result, const Callback<>& introduceResult,
				const Callback<>& introduceError, std::optional<UserId>) const final override {
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
			auto token = responder->send(UserId{}, this, makeCallback(
						[newTime] (IStructuredOutput& out, RequestToken) {
				out.startWritingObject(noFlags, 1);
				out.introduceObjectMember(noFlags, "new_time", 0);
				out.writeInt(noFlags, newTime);
				out.endWritingObject(noFlags);
			}));
			
			responder->getResponse(token, makeCallback([&] (IStructuredInput& response) {
				response.readNull(noFlags);
			}));
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

struct FakeHttp {
	using StringType = std::string;
	
	std::string written;
	std::string toReturn;
	RequestToken tokenToGive;
	RequestToken tokenObtained;
	
	template <typename Lambda>
	RequestToken post(UserId, const Lambda& call) {
		call(written, tokenToGive);
		return tokenToGive;
	}
	
	template <typename Lambda>
	void getResponse(RequestToken token, const Lambda& call) {
		tokenToGive.id++;
		tokenObtained = token;
		call(toReturn);
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
	void getResponse(RequestToken, const Callback<std::tuple<ServerReaction, RequestToken, int64_t>
					 (std::span<char> input, bool identified)>& reader) override {
		if (expandResponse)
			expandResponse();
		auto [reactionObtained, token, positionObtained] = reader(std::span<char>(response.begin(), response.size()), true);
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
						makeCallback([&] (std::span<const char> output) {
				response = std::string_view(output.data(), output.size());
		})).first;
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
		std::string result;
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
		std::string written = tested.serialise<JSON>();
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
		std::string written = tested.serialise<JSON>();
		doATest(written, standardObjectJson);
		
		std::cout << "Testing Serialisable class read" << std::endl;
		StandardObject tested2;
		tested2.deserialise<JSON>(standardObjectJson);
		doATest(tested2.index, 8);
		doATest(tested2.subIndex, 15);
		doATest(tested2.deleted, true);
		doATest(tested2.contents, "Not much at this point");
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
			"	\"result\" : null,\n"
			"	\"id\" : \"initial_time_set\"\n"
			"}";
						
	const std::string dummyRpcReply4 =
		 	"{\n"
			"	\"jsonrpc\" : \"2.0\",\n"
			"	\"id\" : 2,\n"
			"	\"result\" : \"actually...\"\n"
			"}";
	
	{
		std::cout << "Testing RPC server" << std::endl;
		
		DummyRpcClass dummyRpcBackup;
		DummyRpcClass dummyRpc;
		dummyRpc.setBackup(&dummyRpcBackup);
		dummyRpc.message = "nevermind";
		dummyRpcBackup.message = "actually...";
		std::string result;
		auto goodCallback = makeCallback([] (std::string_view) {});
		auto badCallback = makeCallback([] {});
		JsonRpcServerProtocol protocol = &dummyRpc;
		protocol.post("", "application/json", dummyRpcRequest1, result, goodCallback, badCallback);
		doATest(result, dummyRpcReply1);
		
		result.clear();
		protocol.post("", "application/json", dummyRpcRequest2, result, goodCallback, badCallback);
		doATest(result, dummyRpcReply2);
		doATest(dummyRpc.time, 1366);
		
		dummyRpc.time = 1200;
		result.clear();
		protocol.post("", "application/json", dummyRpcRequest3, result, goodCallback, badCallback);
		doATest(result, dummyRpcReply3);
		doATest(dummyRpc.time, 1366);
		
		result.clear();
		protocol.post("", "application/json", dummyRpcRequest4, result, goodCallback, badCallback);
		doATest(result, dummyRpcReply4);
	}
	
	{
		std::cout << "Testing RPC client" << std::endl;
		
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
		std::string result;
		auto goodCallback = Bomba::makeCallback([] (std::string_view) {});
		auto badCallback = Bomba::makeCallback([] {});
		Bomba::JsonRpcServerProtocol protocol = &advancedRpc;
		protocol.post("", "application/json", advancedRpcRequest1, result, goodCallback, badCallback);
		doATest(result, advancedRpcResponse1);
		result.clear();
		protocol.post("", "application/json", advancedRpcRequest2, result, goodCallback, badCallback);
		doATest(result, advancedRpcResponse2);
		result.clear();
		protocol.post("", "application/json", advancedRpcRequest3, result, goodCallback, badCallback);
		doATest(result, advancedRpcResponse3);

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
		http.getResponse(token, Bomba::makeCallback( [&] (std::span<char> response, bool success) {
			doATest(success, true);
			downloaded = std::string_view(response.data(), response.size());
			return true;
		}));
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
			"ContentType: text/html\r\n"
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
		http.getResponse(token, Bomba::makeCallback( [&] (std::span<char> response, bool success) {
			doATest(success, true);
			downloaded = std::string_view(response.data(), response.size());
			return true;
		}));
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
			Bomba::HtmlPostResponder postResponder = {&methodServer};
			Bomba::HttpServer<std::string, decltype(betterGetResponder), decltype(postResponder)> httpServer = {&betterGetResponder, &postResponder};
			Bomba::BackgroundTcpServer<decltype(httpServer)> server = {&httpServer, 8901}; // Very unlikely this port will be used for something
			
			InlineMethod methodClient;
			Bomba::SyncNetworkClient client = {"0.0.0.0", "8901"};
			Bomba::HttpClient<> httpClient = {&client, "0.0.0.0"};
			
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
		fixture.httpClient.getResponse(response, Bomba::makeCallback([&](std::span<char> response, bool) {
			doATest(someHtml, std::string_view(response.data(), response.size()));
			return true;
		}));
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
			fixture.httpClient.getResponse(response, Bomba::makeCallback([&](std::span<char>, bool) {
				return true;
			}));
		}
		std::cout << " average response time is " << fixture.server.averageResponseTime().count() << " ns" << std::endl;
	}
	
	{
		std::cout << "Internally benchmarking HTTP server's POST...";
		auto fixture = makeHttpTestFixture();
		for (int i = 0; i < 2000; i++) {
			fixture.methodClient("Protest against the clouds!");
		}
		std::cout << " average response time is " << fixture.server.averageResponseTime().count() << " ns" << std::endl;
	}

	std::cout << "Passed: " << (tests - errors) << " / " << tests << ", errors: " << errors << std::endl;

	return 0;
}
