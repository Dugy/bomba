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
#include <functional>

namespace Bomba {

class CachingFileServer {
	std::filesystem::path _root;

	struct CachedFile {
		std::string type;
		std::vector<char> contents;

		CachedFile(const std::filesystem::path& path, CachingFileServer* parent) {
			std::string extension = path.extension().string();
			for (char& c : extension)
				c = std::tolower(c);
			auto foundExtension = parent->_extensions.find(extension);
			if (foundExtension != parent->_extensions.end()) {
				type = foundExtension->second;
			} else {
				type = "application/" + extension.substr(1); // Improvise if unknown
			}
		}
	};
	std::unordered_map<std::string, std::string> _extensions;
	std::unordered_map<std::string, CachedFile> _cache;
	std::vector<std::string> _fileNames;
	std::vector<std::function<void()>> _modifiers;
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
		if (localPath == "/index.html")
			cacheFile(path, "/");
		CachedFile entry(path, this);

		std::ifstream file(path, std::ios::binary);
		file.unsetf(std::ios::skipws);
		file.seekg(0, std::ios::end);
		std::streampos fileSize = file.tellg();
		file.seekg(0, std::ios::beg);
		entry.contents.insert(entry.contents.begin(), std::istream_iterator<char>(file), std::istream_iterator<char>());

		_fileNames.emplace_back(localPath);
		_cache.insert(std::make_pair(std::string_view(_fileNames.back()), std::move(entry)));
	}
	void reloadInternal() {
		_cache.clear();
		_fileNames.clear();
		cacheFolder(_root, "");
		for (auto& modifier : _modifiers) {
			modifier();
		}
	}
	void addModifier(std::function<void()>&& modifier) {
		modifier();
		_modifiers.emplace_back(std::move(modifier));
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

	bool get(std::string_view path, IWriteStarter& outputProvider) {
		std::shared_lock lock(_mutex);
		auto found = _cache.find(std::string(path));
		if (found == _cache.end()) [[unlikely]]
				return false;
		auto& output = outputProvider.writeKnownSize(found->second.type, found->second.contents.size());
		output += std::string_view(found->second.contents.data(), found->second.contents.size());
		return true;
	}
	void reload() {
		std::lock_guard lock(_mutex);
		reloadInternal();
	}
	void reset() {
		std::lock_guard lock(_mutex);
		_modifiers.clear();
		reloadInternal();
	}
	void addGeneratedFile(std::string_view name, std::vector<char>&& contents) {
		addModifier([this, name = '/' + std::string(name), contents = std::move(contents)] {
			CachedFile entry(name, this);
			entry.contents = contents;
			_cache.insert(std::make_pair(name, std::move(entry)));
		});
	}
	void addGeneratedFile(std::string_view name, const std::string& contents) {
		std::vector<char> contentsVec = {contents.begin(), contents.end()};
		addGeneratedFile(name, std::move(contentsVec));
	}
};


} // namespace Bomba

#endif // BOMBA_FILE_SERVER
