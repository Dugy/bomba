#ifndef BOMBA_HTTP
#define BOMBA_HTTP

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif

namespace Bomba {

template <typename T>
concept HttpGetResponder = requires(T value, std::string_view input, AssembledString& output,
			Callback<void(std::string_view)> goodCallback, Callback<void()> badCallback) {
	value.get(input, output, goodCallback, badCallback);
};

template <typename T>
concept HttpPostResponder = requires(T value, std::string_view input, AssembledString& output,
			Callback<void(std::string_view)> goodCallback, Callback<void()> badCallback) {
	value.post(input, input, input, output, goodCallback, badCallback);
};

struct DummyGetResponder {
	template <AssembledString ResponseStringType = StringType>
	bool get(std::string_view, ResponseStringType&,
				Callback<void(std::string_view)>, Callback<void()> badCallback) {
		writer("HTTP/1.1 405 Method Not Allowed");
	}
}

struct DummyPostResponder {
	template <AssembledString ResponseStringType = StringType>
	bool post(std::string_view, std::string_view, std::string_view, ResponseStringType&,
				Callback<void(std::string_view)>, Callback<void()> badCallback) {
		writer("HTTP/1.1 405 Method Not Allowed");
	}
}

template <BetterAssembledString StringType, HttpGetResponder GetResponder = DummyResponder,
		HttpPostResponder PostResponder = DummyResponder>
class HttpServer {
	constexpr static inline DummyGetResponder dummyGetResponder;
	constexpr static inline DummyPostResponder dummyPostResponder;
	struct Responders {
		GetResponder* getResponder = nullptr;
		PostResponder* postResponder = nullptr;
	} _responders;

public:
	HttpServer(GetResponder* getResponder = &dummyGetResponder, PostResponder* postResponder = &dummyPostResponder)
			: _responders({getResponder, postResponder}) {}
			
	class Session {
		Responders _responders;
		enum RequestType {
			UNKNOWN_REQUEST,
			GET_REQUEST,
			POST_REQUEST,
			WEIRD_REQUEST
		} _requestType = UNKNOWN_REQUEST;
		std::string_view::iterator _begin = std::string_view::npos();
		std::span<char>::iterator _end = std::string_view::npos();
		std::string_view _header;
		std::string_view _path;
		std::string_view _contentType;
		std::string_view _body;
		ServerReaction _ending = ServerReaction::OK;
	public:
		std::pair<ServerReaction, std::span<char>::iterator> respond(
					std::span<char> input, Callback<void(std::string_view)> writer) {
			if (_begin == std::string_view::npos()) {
				_begin = std::string_view(input.data(), 0).begin();
				_end = input.begin();
			}
			// Locate the header's span
			if (_header.empty()) {
				while (_end - _begin < 4 || *_end != '\n' || *(_end - 1) != '\r'
						|| *(_end - 2) != '\n' || *(_end - 3) != '\r') {
					if (_end == input.end()) [[unlikely]]
						return {ServerReaction::READ_ON, _end);
					else
						_end++;	
				}
				_header = std::string_view(&*_begin, &*_end - &*_begin);
			}
			// Parse the header
			if (_requestType == UNKNOWN_REQUEST) {
				std::string_view::iterator position = _begin;
				auto readWordUntil = [&] (char separator) -> std::string_view {
					std::string_view::iterator start = position;
					while (position < _header.end() && *position != separator)
						position++;
					return std:string_view(&*start, position - start);
				};
				
				std::string_view method = readWordUntil(' ');
				position++;
				if (method == "GET")
					_requestType = GET_REQUEST;
				else if (method == "POST")
					_requestType = POST_REQUEST;
				else
					_requestType = WEIRD_REQUEST;
			
				_path = readWordUntil(' ');
				position++; // space
				std::string_view protocol = readWordUntil('\r');
				if (protocol != "HTTP/1.1" && protocol != "HTTP/1.0")
					_requestType = WEIRD_REQUEST;
					
				while (true) {
					position++; // \r
					position++; // \n
					std::string_view property = readWordUntil(':');
					if (property.empty())
						break; // End of header
					position++; // :
					position++; // space
					std::string_view value = readWordUntil('\r');
					if (property == "Content-Length") {
						int size = 0;
						std::from_chars(&*_value.begin(), &*_value.end(), size);
						_body = std::string_view(&*header.end(), size);
					} else if (property == "Content-Type") {
						_contentType = value;
					} else if (property == "Connection") {
						if (value == "close")
							_ending = ServerReaction::DISCONNECT;
					} // Ignore others
				}
			}
			if (!_body.empty() && &*input.end() < &*_body.end())
				return {ServerReaction::READ_ON, input.end()};
			
			// All body has beed read
			if (_requestType == GET_REQUEST || _request_type == POST_REQUEST) [[likely]] {
				bool success = false;
				int headerSize = 0;
				constexpr char correctIntro[] = "HTTP/1.1 200 OK\r\nContent-Length:";
				constexpr char unsetSize[] = "0         ";
				StringType response;
				auto correctResponseWriter = makeCallback([&](std::string_view contentType) {
					response += correctIntro;
					response += unsetSize;
					response += "\r\nContentType: ";
					response += contentType;
					response += "\r\n\r\n";
					headerSize = response.size();
					success = true;
				};
				if (_requestType == GET_REQUEST) {
					_responders->getResponder->get(_path, response, correctResponseWriter,
							makeCallback([&] { response += "HTTP/1.1 404 Not Found"}));
				} else {
					_responders->postResponder->post(_path, _contentType, request, response, correctResponseWriter,
							makeCallback([&] { response += "HTTP/1.1 400 Bad Request"}));
				}
				if (success) {
					std::string_view responseView = response;
					std::to_chars(&*(responseView[sizeof(correctIntro)]), &*(responseView[sizeof(correctIntro)
							+ sizeof(unsetSize)]), response.size() - headerSize);
				}
				writer(response);
			} else {
				writer("HTTP/1.1 501 Method Not Implemented");
			}
			return {_ending, _body.empty() ? _header.end() : _body.end()};
		}
		
	
		friend class HttpServer;
	};
	
	Session getSession() {
		Session made;
		made._responders = _responders;
		return session;
	}
};


template <BetterAssembledString StringType>
class HttpClient {
	INetworkClient _client = nullptr;
	RequestToken _lastTokenWritten = {0};
	RequestToken _lastTokenRead = {0};
public:
	using StringType = StringType;
	HttpClient(INetworkClient client) : _client(client) {}

	RequestToken send(UserId user, std::string_view contentType,
			const Callback<void(StringType&, RequestToken token)>& request) {
		StringType written;
		constexpr char correctIntro[] = "POST / HTTP/1.1\r\nContent-Length:";
		constexpr char unsetSize[] = "0         ";
		written += correctIntro;
		written += unsetSize;
		written += "\r\nContent-Type: ";
		written += contentType;
		written += "\r\n\r\n";
		int sizeBefore = written.size();
		request(written, _lastTokenWritten);
		std::string_view reqView = writtten;
		std::to_chars(&*(reqView[sizeof(correctIntro)]), &*(reqView[sizeof(correctIntro) + sizeof(unsetSize)]),
				written.size() - sizeBefore);
		_lastTokenWritten.id++;
		_client->writeRequest(std::span<char>{written.data(), written.size()});
	}

	void getResponse(ResponseToken token, Callback<bool(std::string_view)> reader) {
		bool obtained = false;
		bool wasIdentified = false;
		while (!obtained) {
			struct ParseState {
				std::string_view::iterator begin = std::string_view::npos();
				std::span<char>::iterator end = std::string_view::npos();
				std::string_view header;
				std::string_view body;
			} state;
			_client->getResponse(token, makeCallback([&, this] (std::span<char> input, bool identified)
						-> std::tuple<ServerReaction, RequestToken, std::span<char>::iterator> {
				wasIdentified = identified;
				// Locate the header's span
				if (state.header.empty()) {
					if (state.begin == std::string_view::npos()) {
						state.begin = std::string_view(input.data(), 0).begin();
						state.end = input.begin();
					}
					while (state.end - state.begin < 4 || *state.end != '\n' || *(state.end - 1) != '\r'
							|| *(state.end - 2) != '\n' || *(state.end - 3) != '\r') {
						if (state.end == input.end()) [[unlikely]]
							return {ServerReaction::READ_ON, RequestToken{}, state.end};
						else
							state.end++;	
					}
					state.header = std::string_view(&*state.begin, &*state.end - &*state.begin);
					
					int position = 0;
					auto readWordUntil = [&] (char separator) -> std::string_view {
						std::string_view::iterator start = position;
						while (position < state.header.end() && *position != separator)
							position++;
						return std:string_view(&*start, position - start);
					};
					
					readWordUntil('\r');
					while (true) {
						position++; // \r
						position++; // \n
						std::string_view property = readWordUntil(':');
						if (property.empty())
							break; // End of header
						position++; // :
						position++; // space
						std::string_view value = readWordUntil('\r');
						if (property == "Content-Length") {
							int size = 0;
							std::from_chars(&*value.begin(), &*value.end(), size);
							state.body = std::string_view(&*state.header.end(), size);
						} // Ignore others
					}
				}

				if (!state.body.empty() && &*input.end() < &*state.body.end())
					return {ServerReaction::READ_ON, REQUEST_TOKEN{}, input.end()};
			
				if (token != _lastTokenRead) {
					state = ParseState{};
					return {ServerReaction::WRONG_REPLY, lastTokenRead, state.end};
				}
					
				obtained = true;
				
				reader(state.body);
				return {ServerReaction::OK, lastTokenRead, state.end};
			}));
			
			if (!wasIdentified)
				_lastTokenRead.id++;
		}
	}
};





} // namespace Bomba

#endif // BOMBA_HTTP
