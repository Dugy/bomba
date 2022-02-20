#ifndef BOMBA_FILE_SERVER
#define BOMBA_FILE_SERVER

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif

#ifndef BOMBA_HTTP
#include "bomba_http.hpp"
#endif

#include <unordered_map>
#include <vector>
#include <string>
#include <filesystem>
#include <shared_mutex>
#include <fstream>
#include <iterator>
#include <functional>
#include <iostream>

namespace Bomba {

class FileServerBase : public IHttpGetResponder {
	std::vector<std::function<void()>> _modifiers;
protected:
	std::unordered_map<std::string, std::string> _extensions;
	std::filesystem::path _root;

	FileServerBase(const std::filesystem::path& path) : _root(path) {
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
	}

	std::string extensionDescription(std::string_view extension) {
		auto foundExtension = _extensions.find(std::string(extension));
		if (foundExtension != _extensions.end()) {
			return foundExtension->second;
		} else {
			return "application/" + std::string(extension.substr(1)); // Improvise if unknown
		}
	}

public:
	void addModifier(std::function<void()>&& modifier) {
		modifier();
		_modifiers.emplace_back(std::move(modifier));
	}
	void applyModifiers() {
		for (auto& modifier : _modifiers) {
			modifier();
		}
	}
	void clearModifiers() {
		_modifiers.clear();
	}
};

class DynamicFileServer : public FileServerBase {
public:
	using FileProviderType = std::function<void(Callback<void(std::span<const char> chunk)> writeChunk)>;
private:
	struct GeneratedFileEntry {
		FileProviderType provider;
		bool allKnownAtOnce = false;
	};
	std::unordered_map<std::string, GeneratedFileEntry> _generatedFiles;

public:
	DynamicFileServer(const std::filesystem::path& path) : FileServerBase(path) {}

	void addGeneratedFile(std::string_view name, bool allKnownAtOnce, FileProviderType&& provider) {
		_generatedFiles['/' + std::string(name)] = GeneratedFileEntry{provider, allKnownAtOnce};
	}

	bool get(std::string_view path, IWriteStarter& outputProvider) {
		auto foundGenerated = _generatedFiles.find(std::string(path));
		if (foundGenerated != _generatedFiles.end()) {
			std::string_view extension = path.substr(path.find_last_of('.'));
			if (foundGenerated->second.allKnownAtOnce) {
				foundGenerated->second.provider([&] (std::span<const char> chunk) {
					outputProvider.writeKnownSize(extensionDescription(extension), chunk.size(), [&] (GeneralisedBuffer& buffer) {
						buffer += chunk;
					});
				});
			} else {
				outputProvider.writeUnknownSize(extensionDescription(extension), [&] (GeneralisedBuffer& buffer) {
					foundGenerated->second.provider([&] (std::span<const char> chunk) {
						buffer += chunk;
					});
				});
			}
			return true;
		}

		std::string editedPath = std::string(path);
		if (editedPath.empty() || editedPath.back() == '/') {
			editedPath = "/index.html";
		} else {
			if (editedPath[0] == '.' || editedPath.find("../") != std::string_view::npos || editedPath.find("/.") != std::string_view::npos) {
				std::cout << "Forbidden path" << std::endl;
				return false; // Exclude paths out of the folder, hidden files or empty paths
			}
		}

		std::filesystem::path fullPath = _root;
		fullPath.append(editedPath.substr(1));

		if (!std::filesystem::exists(fullPath) || !std::filesystem::is_regular_file(fullPath)) {
			std::cout << "Can't send " << fullPath << std::endl;
			return false;
		}
		uintmax_t size = std::filesystem::file_size(fullPath);

		std::ifstream file(fullPath);
		outputProvider.writeKnownSize(extensionDescription(fullPath.extension().string()), size, [&] (GeneralisedBuffer& output) {
			uintmax_t position = 0;
			while (position < size) {
				std::array<char, 4096> buffer;
				int amount = std::min<int>(buffer.size(), size - position);
				file.read(buffer.data(), amount);
				output += std::span<const char>(buffer.data(), amount);
				position += amount;
			}
		});
		return true;
	}
};

class CachingFileServer : public FileServerBase {

	struct CachedFile {
		std::string type;
		std::vector<char> contents;

		CachedFile(const std::filesystem::path& path, CachingFileServer* parent) {
			std::string extension = path.extension().string();
			for (char& c : extension)
				c = std::tolower(c);
			type = parent->extensionDescription(extension);
		}
	};
	std::unordered_map<std::string, CachedFile> _cache;
	std::vector<std::string> _fileNames;
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
		applyModifiers();
	}

public:
	CachingFileServer(const std::filesystem::path& path) : FileServerBase(path) {
		reload();
	}

	bool get(std::string_view path, IWriteStarter& outputProvider) {
		std::shared_lock lock(_mutex);
		auto found = _cache.find(std::string(path));
		if (found == _cache.end()) [[unlikely]] {
			std::cout << "No such file " << path << std::endl;
			return false;
		}
		outputProvider.writeKnownSize(found->second.type, found->second.contents.size(), [&] (GeneralisedBuffer& output) {
			output += std::string_view(found->second.contents.data(), found->second.contents.size());
		});
		return true;
	}
	void reload() {
		std::lock_guard lock(_mutex);
		reloadInternal();
	}
	void reset() {
		std::lock_guard lock(_mutex);
		clearModifiers();
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
