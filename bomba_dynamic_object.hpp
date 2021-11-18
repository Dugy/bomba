#ifndef BOMBA_DYNAMIC_OBJECT
#define BOMBA_DYNAMIC_OBJECT

#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif

#ifndef BOMBA_RPC_OBJECT // Needed to run in godbolt
#include "bomba_rpc_object.hpp"
#endif

#include <vector>

namespace Bomba {

template <typename T>
concept IsAFunctor = requires() {
	&std::decay_t<T>::operator();
};

class RpcLambdaHolder {
	union {
		IRemoteCallable* held = nullptr;
		std::array<char, sizeof(RpcStatelessLambda<[] {}>) + 3 * sizeof(void*)> local;
	} _content;
	const Detail::RpcLambdaInformationHolder* _info = nullptr; // Not nullptr if it's leaf
	bool _isLocal = {};
	bool _owning = {};

	void reset() {
		if (!_isLocal && _owning && _content.held) {
			delete _content.held;
			_content.held = 0;
		}
	}

	RpcLambdaHolder(const RpcLambdaHolder&) = delete;
	void operator=(const RpcLambdaHolder&) = delete;

	RpcLambdaHolder() = default;

public:
	template <IsAFunctor LambdaType>
	RpcLambdaHolder(LambdaType&& from)
		: _isLocal(sizeof(std::decay_t<RpcLambda<std::decay_t<LambdaType>>>) <= sizeof(_content.local) &&
				std::is_trivially_destructible_v<LambdaType>)
		, _owning(true)
	{
		//_descriptionFiller = RpcLambda<std::decay_t<LambdaType>>;
		if (_isLocal) {
			auto made = new (&_content.local) RpcLambda<std::decay_t<LambdaType>>(std::forward<LambdaType>(from));
			_info = &made->info;
		} else {
			auto made = new RpcLambda<std::decay_t<LambdaType>>(std::forward<LambdaType>(from));
			_info = &made->info;
			_content.held = made;
		}
	}

	template <std::derived_from<IRemoteCallable> T>
	RpcLambdaHolder(T&& instance)
		: _isLocal(false)
		, _owning(true)
	{
		_content.held = new T(std::move<T>(instance));
	}

	RpcLambdaHolder(RpcLambdaHolder&& other) {
		operator=(std::forward<RpcLambdaHolder>(other));
	}

	~RpcLambdaHolder() {
		reset();
	}

	RpcLambdaHolder& operator=(RpcLambdaHolder&& other) {
		reset();
		_isLocal = other._isLocal;
		_info = other._info;
		_owning = other._owning;
		if (_isLocal) {
			memcpy(&_content.local, &other._content.local, sizeof(_content.local));
		} else {
			_content.held = other._content.held;
			other._content.held = nullptr;
		}
		return *this;
	}

	IRemoteCallable* operator->() const {
		if (_isLocal) {
			return const_cast<IRemoteCallable*>(reinterpret_cast<const IRemoteCallable*>(&_content.local));
		} else {
			return _content.held;
		}
	}
	IRemoteCallable& operator*() const { return *operator->(); }

	bool isLocal() const {
		return _isLocal;
	}

	void listTypes(ISerialisableDescriptionFiller& filler) const {
		_info->subtypesAdder(filler);
	}

	void addToDescription(std::string_view name, IRemoteCallableDescriptionFiller& filler) const {
		_info->unnamedChildAdder(name, filler);
	}

	static RpcLambdaHolder nonOwning(IRemoteCallable& referree) {
		RpcLambdaHolder made;
		made._owning = false;
		made._isLocal = false;
		made._content.held = &referree;
		return made;
	}
};

template <typename Invalid>
class TypedRpcLambdaHolder {
	static_assert(std::is_same_v<Invalid, Invalid>, "Template arguments to TypedRpcLambdaHolder must be in the form Returned(Arg1, Arg2, AndSoOn)");
};

template <typename Returned, typename... Args>
class TypedRpcLambdaHolder<Returned(Args...)> : public RpcLambdaHolder {
	Returned (*call)(IRemoteCallable*, Args...) = nullptr;

public:
	template <FunctorForCallbackConstruction<Returned, Args...> LambdaType>
	TypedRpcLambdaHolder(LambdaType&& from)
		: RpcLambdaHolder(std::forward<LambdaType>(from))
	{
		call = [](IRemoteCallable* callable, Args... args) {
			return (*reinterpret_cast<RpcLambda<std::decay_t<LambdaType>>*>(callable))(args...);
		};
	}

	template <typename... Args2>
	Returned operator()(Args2... args) {
		return call(operator->(), std::forward<Args>(args)...);
	}
};


class DynamicRpcObject : public IRemoteCallable {
	std::vector<std::pair<std::string, RpcLambdaHolder>> _contents;

public:
	void add(std::string_view name, RpcLambdaHolder&& function) {
		_contents.emplace_back(std::pair<std::string, RpcLambdaHolder>(name, std::move(function)));
		setSelfAsParent(&*_contents.back().second);
	}

	bool call(IStructuredInput*, IStructuredOutput&, Callback<>,
			  Callback<void(std::string_view)>, std::optional<UserId>) const override {
		return false;
	}
	const IRemoteCallable* getChild(std::string_view name) const override {
		for (auto& [funcName, func] : _contents) {
			if (funcName == name)
				return func.operator->();
		}
		return nullptr;
	}
	std::string_view childName(const IRemoteCallable* child) const override {
		for (auto& [name, func] : _contents) {
			if (func.operator->() == child)
				return name;
		}
		return "";
	}

	void listTypes(ISerialisableDescriptionFiller& filler) const override {
		for (auto& [name, func] : _contents) {
			func.listTypes(filler);
		}
	}
	void generateDescription(IRemoteCallableDescriptionFiller& filler) const override {
		for (auto& [name, func] : _contents) {
			func.addToDescription(name, filler);
		}
	}

	DynamicRpcObject() = default;

	DynamicRpcObject(DynamicRpcObject&& from) : _contents(std::move(from._contents)) {
		for (auto& [name, functor] : _contents) {
			setSelfAsParent(&*functor);
		}
	}
};

} // namespace Bomba

#endif // BOMBA_DYNAMIC_OBJECT
