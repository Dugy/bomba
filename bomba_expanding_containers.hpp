#ifndef BOMBA_EXPANDING_CONTAINERS
#define BOMBA_EXPANDING_CONTAINERS

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif

#include <vector>

namespace Bomba {

template <int StaticSize = 1024>
class ExpandingBuffer : public GeneralisedBuffer {
	std::array<char, StaticSize> _basic;
	std::vector<char> _extended;

	bool bufferFull() override {
		if (_extended.empty()) {
			_extended.resize(3 * StaticSize);
			memcpy(_extended.data(), _basic.data(), StaticSize);
			moveBuffer({&_extended[StaticSize], 2 * StaticSize});
		} else {
			size_t oldSize = _extended.size();
			_extended.resize(2 * oldSize);
			moveBuffer({&_extended[oldSize], oldSize});
		}
		return true;
	}

public:
	ExpandingBuffer() : GeneralisedBuffer({reinterpret_cast<char*>(&_basic), sizeof(_basic)}) {}

	operator std::span<char>() {
		if (_extended.empty()) {
			return {_basic.data(), _basic.size() - remainingSpace()};
		} else {
			return std::span<char>{_extended.begin(), _extended.size() - remainingSpace()};
		}
	}

	operator std::string_view() const {
		if (_extended.empty()) {
			return {_basic.data(), _basic.size() - remainingSpace()};
		} else {
			return {_extended.data(), _extended.size() - remainingSpace()};
		}
	}

	void clear() {
		_extended.clear();
		_extended.resize(0);
		moveBuffer({_basic.data(), _basic.size()});
	}
};

} // namespace Bomba

#endif // BOMBA_EXPANDING_CONTAINERS
