#ifndef BOMBA_FILE_SERVER
#define BOMBA_FILE_SERVER

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif
#include <unordered_map>
#include <vector>
#include <string>
#include <filesystem>
#include <shared_mutex>
#include <fstream>
#include <iterator>

namespace Bomba {

class CachingFileServer {
	std::filesystem::path _root;

	struct CachedFile {
		std::string type;
		std::vector<char> contents;
	};
	std::unordered_map<std::string, std::string> _extensions;
	std::unordered_map<std::string, CachedFile> _cache;
	std::shared_mutex _mutex;


	void cacheFolder(const std::filesystem::path& path, std::string_view prefix) {
		for (const auto& file : std::filesystem::directory_iterator(path)) {
			std::string localPath = std::string(prefix) + '/' + file.path().filename().string();
			if (file.is_directory()) {
				cacheFolder(file.path(), localPath);
			} else {
				cacheFile(file.path(), localPath);
			}
		}
	}
	void cacheFile(const std::filesystem::path& path, std::string_view localPath) {
		if (localPath == "/index.html") [[unlikely]]
			cacheFile(path, "/");
		std::string extension = path.extension().string();
		for (char& c : extension)
			c = std::tolower(c);
		CachedFile entry;
		auto foundExtension = _extensions.find(extension);
		if (foundExtension != _extensions.end()) {
			entry.type = foundExtension->second;
		} else {
			entry.type = "application/" + extension.substr(1); // Improvise if unknown
		}

		std::ifstream file(path, std::ios::binary);
		file.unsetf(std::ios::skipws);
		file.seekg(0, std::ios::end);
		std::streampos fileSize = file.tellg();
		file.seekg(0, std::ios::beg);
		entry.contents.insert(entry.contents.begin(), std::istream_iterator<char>(file), std::istream_iterator<char>());

		_cache.insert(std::make_pair(localPath, std::move(entry)));
	}

public:
	CachingFileServer(const std::filesystem::path& path) : _root(path) {
		_extensions[".html"] = "text/html";
		_extensions[".js"] = "text/javascript";
		_extensions[".css"] = "text/css";
		_extensions[".json"] = "application/json";
		_extensions[".jpg"] = "image/jpeg";
		_extensions[".jpeg"] = "image/jpeg";
		_extensions[".png"] = "image/png";
		_extensions[".gif"] = "image/gif";
		_extensions[".ico"] = "image/vnd.microsoft.icon";
		_extensions[".svg"] = "image/svg+xml";
		_extensions[".bmp"] = "image/bmp";
		_extensions[".ttf"] = "font/ttf";
		_extensions[".gz"] = "application/gzip";
		_extensions[".xml"] = "application/xml";
		reload();
	}

	template <HttpWriteStarter GetterCallback>
	bool get(std::string_view path, GetterCallback outputProvider) {
		std::shared_lock lock(_mutex);
		auto found = _cache.find(std::string(path));
		if (found == _cache.end()) [[unlikely]]
				return false;
		auto& output = outputProvider(found->second.type);
		output += std::string_view(found->second.contents.data(), found->second.contents.size());
		return true;
	}
	void reload() {
		std::lock_guard lock(_mutex);
		_cache.clear();
		cacheFolder(_root, "");
	}
};


} // namespace Bomba

#endif // BOMBA_FILE_SERVER
