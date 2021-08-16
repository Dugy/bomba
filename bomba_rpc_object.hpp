#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif

#include <csetjmp>

#if (_MSC_VER && !__INTEL_COMPILER) || __clang__
#define NO_DEFECT_REPORT_2118
#endif

#ifdef NO_DEFECT_REPORT_2118
#include <vector>
#endif

namespace Bomba {

// Suppress GCC warnings that I should not do this
#ifdef __GNUC__
#ifndef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsubobject-linkage"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-template-friend"
#endif
#endif

namespace Detail {

template <typename Returned>
struct IResponseReader {
	virtual Returned readResponse(IRpcResponder* responder, RequestToken token) const = 0;
};

	template <typename FirstArg, typename... Args>
	struct ArgsTupleProvider {
		using type = std::tuple<FirstArg, Args...>;
	};
	template <>
	struct ArgsTupleProvider<void> {
		using type = std::tuple<>;
	};

	template <typename Lambda, typename Returned, typename... Args>
	Returned getLambdaReturned(Returned (Lambda::*)(Args...) const) {
		return {};
	}

	template <typename Lambda, typename Returned, typename... Args>
	std::tuple<Args...> getLambdaArgs(Returned (Lambda::*)(Args...) const) {
		return {};
	}

	template <typename Lambda>
	constexpr auto getLambdaFunction(Lambda) {
		return &Lambda::operator();
	}

	struct Omniconverter {
		template <typename T>
		auto operator=(T assigned) const {
			return assigned;
		}
		template <typename T>
		operator T() const {
			return {};
		}
	};

	using ChildGettingFunction = IRemoteCallable* (*)(const IRemoteCallable* self, int offset, std::string_view name);
	using ChildNameGettingFunction = bool (*)(const IRemoteCallable* self, int offset, const IRemoteCallable* child,
			const Callback<void(std::string_view)>& reaction);

#ifdef NO_DEFECT_REPORT_2118
	struct ChildRpcEntry {
		ChildGettingFunction childGettingFunction = nullptr;
		ChildNameGettingFunction childNameGettingFunction = nullptr;
		std::string_view name;
		SerialisationFlags::Flags flags = SerialisationFlags::NONE;
		int offset = -1;
	};
#else

	template <typename Parent, int Index>
	struct SubclassAccess {
		friend int& childOffset(SubclassAccess<Parent, Index>);
		friend constexpr auto childName(SubclassAccess<Parent, Index>);
		friend constexpr SerialisationFlags::Flags flagsOf(SubclassAccess<Parent, Index>);
		friend constexpr ChildGettingFunction childGetter(SubclassAccess<Parent, Index>);
		friend constexpr ChildNameGettingFunction childNameGetter(SubclassAccess<Parent, Index>);
		friend constexpr auto boolIfDeclared(SubclassAccess<Parent, Index>);

		constexpr SubclassAccess() = default;
	};

	template <typename Parent, int Index, StringLiteral Name, SerialisationFlags::Flags Flags,
			ChildGettingFunction ChildGetter, ChildNameGettingFunction ChildNameGetter>
	struct SubclassSaver {
		friend int& childOffset(SubclassAccess<Parent, Index>) {
			static int saved = -1;
			return saved;
		}
		friend constexpr auto childName(SubclassAccess<Parent, Index>) {
			return Name;
		}
		friend constexpr SerialisationFlags::Flags flagsOf(SubclassAccess<Parent, Index>) {
			return Flags;
		}
		friend constexpr ChildGettingFunction childGetter(SubclassAccess<Parent, Index>) {
			return ChildGetter;
		}
		friend constexpr ChildNameGettingFunction childNameGetter(SubclassAccess<Parent, Index>) {
			return ChildNameGetter;
		}
		friend constexpr auto boolIfDeclared(SubclassAccess<Parent, Index>) {
			return true;
		}
		constexpr static int instantiated = 0;
	};

	template <typename Parent, int Index /* set to 0 */, StringLiteral Name, SerialisationFlags::Flags Flags,
			ChildGettingFunction ChildGetter, ChildNameGettingFunction ChildNameGetter, typename SFINAE = void>
	struct AddChildRpc {
		constexpr static int index = SubclassSaver<Parent, Index, Name, Flags, ChildGetter, ChildNameGetter>::instantiated;
	};

	template <typename Parent, int Index, StringLiteral Name, SerialisationFlags::Flags Flags,
			ChildGettingFunction ChildGetter, ChildNameGettingFunction ChildNameGetter>
	struct AddChildRpc<Parent, Index, Name, Flags, ChildGetter, ChildNameGetter, std::enable_if_t<
			std::is_same_v<bool, decltype(boolIfDeclared(SubclassAccess<Parent, Index>()))>>> {
		constexpr static int index = AddChildRpc<Parent, Index + 1, Name, Flags, ChildGetter, ChildNameGetter>::index + 1;
	};

	template <typename Parent, int Index>
	constexpr int& getChildOffset() {
		return childOffset(SubclassAccess<Parent, Index>());
	}

	template <typename Parent, int Index = 0, typename SFIANE = void>
	struct ChildAccess {
		static void name(const IRemoteCallable* parent, const IRemoteCallable* child, const Callback<void(std::string_view)>& reaction) {
			logicError("Broken structure");
		}
		static IRemoteCallable* instance(const IRemoteCallable* parent, std::string_view name) {
			return nullptr;
		}
	};

	template <typename Parent, int Index>
	struct ChildAccess<Parent, Index, std::enable_if_t<
				std::is_same_v<bool, decltype(boolIfDeclared(SubclassAccess<Parent, Index>()))>>> {
		constexpr static SubclassAccess<Parent, Index> accessor = {};
		static void name(const IRemoteCallable* parent, const IRemoteCallable* child, const Callback<void(std::string_view)>& reaction) {
			int offset = childOffset(accessor);
			if (!childNameGetter(accessor)(parent, offset, child, reaction)) {
				ChildAccess<Parent, Index + 1>::name(parent, child, reaction);
			}
		}
		static IRemoteCallable* instance(const IRemoteCallable* parent, std::string_view name) {
			int offset = childOffset(accessor);
			if (name == childName(accessor))
				return childGetter(accessor)(parent, offset, name);
			return ChildAccess<Parent, Index + 1>::instance(parent, name);
		}
	};
#endif

	struct RpcArgumentInfo {
		const char* name = nullptr;
		SerialisationFlags::Flags flags = SerialisationFlags::NONE;
		void operator=(const RpcArgumentInfo& other) volatile {
			name = other.name;
			flags = other.flags;
		}
	};

	struct RpcSetupData {
		volatile RpcArgumentInfo* args = nullptr;
		int argsSize = 0;
		int argumentBeingSet = 0;
		int argumentBeingFilled = 0;
		std::jmp_buf jumpBuffer;
		inline static thread_local RpcSetupData* instance;

		void noticeArg(RpcArgumentInfo&& value) {
			argumentBeingFilled++;
			if (argumentBeingFilled > argsSize)
				std::longjmp(instance->jumpBuffer, true);

			for (int i = argumentBeingSet + 1; i < argsSize; i++) {
				if (!strcmp(value.name, args[i].name))
					return;
			}

			// Add if not seen yet, then exit
			args[argumentBeingSet] = value;
			std::longjmp(instance->jumpBuffer, true);
		}
	};

	template <typename Returned>
	class FutureBase {
	protected:
		const IResponseReader<Returned>* _reader = nullptr;
		IRpcResponder* _responder = nullptr;
		RequestToken _token = {};

		FutureBase(const FutureBase&) = delete;
		void operator=(const FutureBase&) = delete;
	public:
		FutureBase(const IResponseReader<Returned>* reader, IRpcResponder* responder, RequestToken token)
			: _reader(reader), _responder(responder), _token(token) {}
		FutureBase() = default;
	};
} // namespace Detail

// Placeholder class to be replaced by std::future once it's extended by Extensions for Concurrency
template <typename Returned>
class Future : Detail::FutureBase<Returned> {
	mutable std::optional<Returned> _value = std::nullopt;
	using Base = Detail::FutureBase<Returned>;
public:
	using Base::FutureBase;
	Future(Returned&& value) : _value(value) {}

	Returned get() const {
		if (!_value)
			_value = Base::_reader->readResponse(Base::_responder, Base::_token);
		return *_value;
	}

	bool is_ready() const {
		if (_value)
			return true;
		return Base::_responder->hasResponse(Base::_token);
	}
};

template <>
class Future<void> : Detail::FutureBase<void> {
	using Base = Detail::FutureBase<void>;
	mutable bool _done = false;
public:
	using Base::FutureBase;
	Future() = default;

	void get() const {
		if (_done)
			return;
		Base::_reader->readResponse(Base::_responder, Base::_token);
		_done = true;
	}

	bool is_ready() const {
		if (_done)
			return true;
		return (_done = Base::_responder->hasResponse(Base::_token));
	}
};

namespace Detail {
	template <auto Lambda, SerialisationFlags::Flags Flags, typename LambdaType, typename Returned, typename FirstArg, typename... Args>
	class RpcLambdaParent : public IRemoteCallable, public IResponseReader<Returned> {
	protected:
		using ArgsTuple = typename ArgsTupleProvider<FirstArg, Args...>::type;
		constexpr static int argsSize = std::tuple_size<ArgsTuple>::value;

		static constexpr bool usesParent() {
			if constexpr(std::is_pointer_v<FirstArg>) {
				if constexpr(std::is_base_of_v<IRemoteCallable, std::remove_pointer_t<FirstArg>>) {
					return true;
				}
			}
			return false;
		}
		constexpr static inline int argumentInfoSize = usesParent() ? argsSize - 1 : argsSize;

		template <typename Tuple, size_t... indexes>
		static void callWithAPartOfArgs(std::index_sequence<indexes...>, const Tuple& args) {
			if constexpr(std::tuple_size<Tuple>::value + 1 < argumentInfoSize) {
				callWithAPartOfArgs(std::make_index_sequence<std::tuple_size<Tuple>::value + 1>(),
						std::tuple_cat(args, std::tuple<Detail::Omniconverter>{}));
			}
			auto setupData =  Detail::RpcSetupData::instance;
			setupData->argumentBeingSet = std::tuple_size<Tuple>::value;
			setupData->argumentBeingFilled = setupData->argumentBeingSet;
			if (!setjmp(setupData->jumpBuffer)) {
				if constexpr(usesParent()) {
					Lambda(nullptr, std::get<indexes>(args)...); // Problems here imply some arguments were not named
				} else {
					Lambda(std::get<indexes>(args)...);
				}
				// The last default-valued argument will cause a long jump, return to the condition,
				// evaluate as false and continue to the following line
			}
		}

		static Detail::RpcArgumentInfo* makeArgumentInfo() {
			Detail::RpcArgumentInfo* result = [] {
				if constexpr(usesParent()) {
					static std::array<Detail::RpcArgumentInfo, argsSize - 1> values = {};
					return values.data();
				} else {
					static std::array<Detail::RpcArgumentInfo, argsSize> values = {};
					return values.data();
				}
			}();
			Detail::RpcSetupData setup;
			setup.args = result;
			setup.argsSize = argumentInfoSize;
			Detail::RpcSetupData::instance = &setup;
			if constexpr(argumentInfoSize > 0) {
				callWithAPartOfArgs(std::make_index_sequence<0>(), std::tuple<>{});
			}
			Detail::RpcSetupData::instance = nullptr;
			return result;
		}
		inline static Detail::RpcArgumentInfo* argumentInfo = makeArgumentInfo();

		template <int ArgIndex = 0>
		static void setArg(std::string_view name, ArgsTuple& args, IStructuredInput& in, SerialisationFlags::Flags flags) {
			constexpr int Index = usesParent() ? ArgIndex - 1 : ArgIndex;
			if constexpr(ArgIndex >= argsSize) {
				return;
			} else {
				if (name == argumentInfo[Index].name)
					deserialiseMember(in, std::get<ArgIndex>(args), SerialisationFlags::Flags(flags | argumentInfo[Index].flags));
				else
					setArg<ArgIndex + 1>(name, args, in, SerialisationFlags::Flags(flags | argumentInfo[Index].flags));
			}
		}

		template <size_t... indexes>
		static void callInternal(std::index_sequence<indexes...>, Returned* retval, ArgsTuple& args) {
			if constexpr(std::is_same_v<Returned, void>) {
				Lambda(std::get<indexes>(args)...);
			} else {
				*retval = Lambda(std::get<indexes>(args)...);
			}
		}

		bool call(IStructuredInput* arguments, IStructuredOutput& result, const Callback<>& introduceResult,
				const Callback<>&, std::optional<UserId>) const final override {
			ArgsTuple input;
			if constexpr(usesParent()) {
				std::get<0>(input) = static_cast<FirstArg>(parent());
			}

			if (arguments) {
				arguments->startReadingObject(Flags);
				std::optional<std::string_view> nextName;
				while ((nextName = arguments->nextObjectElement(Flags))) {
					if constexpr(usesParent()) {
						setArg<1>(*nextName, input, *arguments, Flags);
					} else {
						setArg<0>(*nextName, input, *arguments, Flags);
					}
				}
				arguments->endReadingObject(Flags);
			}

			if constexpr(std::is_same_v<Returned, void>) {
				callInternal(std::make_index_sequence<argsSize>(), nullptr, input);
				introduceResult();
				result.writeNull(Flags);
			} else {
				Returned returned;
				callInternal(std::make_index_sequence<argsSize>(), &returned, input);
				introduceResult();
				serialiseMember(result, returned, Flags);
			}
			return true;
		}

		template <typename First, typename... Others>
		void remoteCallHelper(IStructuredOutput& out, First first, Others... args) const {
			constexpr int index = argumentInfoSize - sizeof...(Others) - 1;
			SerialisationFlags::Flags flags = SerialisationFlags::Flags(Flags | argumentInfo[index].flags);
			auto write = [&] () {
				out.introduceObjectMember(flags, argumentInfo[index].name, index);
				serialiseMember(out, first, flags);
			};
			if constexpr(std::is_same_v<bool, First>) {
				if (first || !(flags & SerialisationFlags::OMIT_FALSE))
					write();
			} else {
				write();
			}
			remoteCallHelper(out, args...);
		}

		void remoteCallHelper(IStructuredOutput&) const {}

		Returned readResponse(IRpcResponder* responder, RequestToken token) const override {
			if constexpr(std::is_void_v<Returned>) {
				responder->getResponse(token, makeCallback([&](IStructuredInput& in) {
					in.readNull(Flags);
				}));
			} else {
				Returned result;
				responder->getResponse(token, makeCallback([&](IStructuredInput& in) {
					deserialiseMember(in, result, Flags);
				}));
				return result;
			}
		}

		template <typename... AnyArgs>
		RequestToken sendRequest(IRpcResponder* responder, AnyArgs&&... args) const {
			return responder->send({}, this, makeCallback(
					[&](IStructuredOutput& out, RequestToken) {
				out.startWritingObject(Flags, argumentInfoSize);
				if constexpr(!std::is_void_v<FirstArg>) {
					remoteCallHelper(out, std::forward<AnyArgs>(args)...);
				}
				out.endWritingObject(Flags);
			}));
		}

		template <typename... AnyArgs>
		Future<Returned> immediateCallToFuture(AnyArgs&&... args) const {
			if constexpr(std::is_void_v<Returned>) {
				Lambda(std::forward<AnyArgs>(args)...);
				return {};
			} else {
				return Future<Returned>(Lambda(std::forward<AnyArgs>(args)...));
			}
		}

	public:
		Returned operator()(Args... args) const requires(usesParent()) {
			IRpcResponder* responder = getResponder();
			if (responder) {
				return readResponse(responder, sendRequest(responder, std::forward<Args>(args)...));
			} else {
				return Lambda(static_cast<FirstArg>(parent()), std::forward<Args>(args)...);
			}
		}
		Returned operator()(FirstArg first, Args... args) const
				requires(!usesParent() && !std::is_same_v<void, FirstArg>) {
			IRpcResponder* responder = getResponder();
			if (responder) {
				return readResponse(responder, sendRequest(responder, std::forward<FirstArg>(first), std::forward<Args>(args)...));
			} else {
				return Lambda(std::forward<FirstArg>(first), std::forward<Args>(args)...);
			}
		}
		Returned operator()() const requires(std::is_same_v<void, FirstArg>) {
			IRpcResponder* responder = getResponder();
			if (responder) {
				return readResponse(responder, sendRequest(responder));
			} else {
				return Lambda();
			}
		}

		Future<Returned> async(Args... args) const requires(usesParent()) {
			IRpcResponder* responder = getResponder();
			if (responder) {
				RequestToken token = sendRequest(responder, std::forward<Args>(args)...);
				return Future<Returned>(this, responder, token);
			} else {
				return immediateCallToFuture(static_cast<FirstArg>(parent()), std::forward<Args>(args)...);
			}
		}
		Future<Returned> async(FirstArg first, Args... args) const
				requires(!usesParent() && !std::is_same_v<void, FirstArg>) {
			IRpcResponder* responder = getResponder();
			if (responder) {
				RequestToken token = sendRequest(responder, std::forward<FirstArg>(first), std::forward<Args>(args)...);
				return Future<Returned>(this, responder, token);
			} else {
				return immediateCallToFuture(std::forward<FirstArg>(first), std::forward<Args>(args)...);
			}
		}
		Future<Returned> async() const requires(std::is_same_v<void, FirstArg>) {
			IRpcResponder* responder = getResponder();
			if (responder) {
				RequestToken token = sendRequest(responder);
				return Future<Returned>(this, responder, token);
			} else {
				return immediateCallToFuture();
			}
		}
	};

	template <auto Lambda, SerialisationFlags::Flags Flags, typename Placeholder>
	struct RpcLambdaRedirector : public IRemoteCallable {
		static_assert(!std::is_same_v<Lambda, Lambda>, "Failed to match the template overload");
	};


	template <auto Lambda, SerialisationFlags::Flags Flags, typename LambdaType, typename Returned, typename FirstArg, typename... Args>
	struct RpcLambdaRedirector<Lambda, Flags, Returned (LambdaType::*)(FirstArg, Args...) const>
			: public RpcLambdaParent<Lambda, Flags, decltype(Lambda), Returned, FirstArg, Args...> {
		using Parent = RpcLambdaParent<Lambda, Flags, decltype(Lambda), Returned, FirstArg, Args...>;
	};


	template <auto Lambda, SerialisationFlags::Flags Flags, typename LambdaType, typename Returned>
	struct RpcLambdaRedirector<Lambda, Flags, Returned (LambdaType::*)() const>
			: public RpcLambdaParent<Lambda, Flags, decltype(Lambda), Returned, void> {
		using Parent = RpcLambdaParent<Lambda, Flags, decltype(Lambda), Returned, void>;
	};

	template <typename Parent, StringLiteral Name>
	struct ChildObjectInitialisator { };

	class NamedFlagSetter : Omniconverter {
		const char* name = nullptr;
		SerialisationFlags::Flags flags = SerialisationFlags::NONE;
		NamedFlagSetter(const char* name, SerialisationFlags::Flags flags) : name(name), flags(flags) {}
	public:
		NamedFlagSetter(const char* name) : name(name) {}

		NamedFlagSetter operator|(SerialisationFlags::Flags newFlag) {
			return NamedFlagSetter{name, SerialisationFlags::Flags(flags | newFlag)};
		}
		template <typename T>
		operator T() const {
			auto instance = Detail::RpcSetupData::instance;
			instance->noticeArg(Detail::RpcArgumentInfo{name, flags});
			return {};
		}
	};
} // namespace Detail

Detail::NamedFlagSetter name(const char* value) {
	return Detail::NamedFlagSetter(value);
}

template <auto Func, SerialisationFlags::Flags Flags = SerialisationFlags::NONE>
class RpcLambda : public Detail::RpcLambdaRedirector<Func, Flags, decltype(&decltype(Func)::operator())> {
	using Parent = typename Detail::RpcLambdaRedirector<Func, Flags, decltype(&decltype(Func)::operator())>::Parent;
};

namespace Detail {
	class RpcObjectCommon : public IRemoteCallable {
	protected:

		RpcObjectCommon(RpcObjectCommon*& setup) {
			setup = this;
		}
	public:
		template <auto Func, SerialisationFlags::Flags Flags>
		void addChild(RpcLambda<Func, Flags>* added) {
			setSelfAsParent(added);
		}
	};
} // namespace Detail

template <typename Derived>
class RpcObject : public Detail::RpcObjectCommon {
	inline static thread_local RpcObjectCommon* setupInstance = nullptr;

#ifdef NO_DEFECT_REPORT_2118
	inline static std::vector<Detail::ChildRpcEntry>* preparation = nullptr;
	static std::vector<Detail::ChildRpcEntry> fillChildEntries() {
		std::vector<Detail::ChildRpcEntry> result;
		preparation = &result;
		{
			Derived instance;
		}
		preparation = nullptr;
		return result;
	}
	inline static std::vector<Detail::ChildRpcEntry> childEntries = fillChildEntries();
#endif

protected:
	std::string_view childName(const IRemoteCallable* child) const final override {
#ifdef NO_DEFECT_REPORT_2118
		for (auto& it : childEntries) {
			std::string_view nameObtained;
			if (it.childNameGettingFunction(this, it.offset, child, makeCallback([&] (std::string_view name) {
				nameObtained = name;
			}))) {
				return nameObtained;
			}
		}
		logicError("Broken structure");
		return "";
#else
		std::string_view result;
		Detail::ChildAccess<Derived>::name(this, child, makeCallback([&](std::string_view name) {
			result = name;
		}));
		return result;
#endif
	}
	const IRemoteCallable* getChild(std::string_view name) const final override {
#ifdef NO_DEFECT_REPORT_2118
		for (auto& it : childEntries) {
			if (it.name == name)
				return it.childGettingFunction(this, it.offset, name);
		}
		return nullptr;
#else
		return Detail::ChildAccess<Derived>::instance(this, name);
#endif
	}

	template <StringLiteral Name>
	inline static constexpr Detail::ChildObjectInitialisator<Derived, Name> child = {};

public:
	RpcObject() : Detail::RpcObjectCommon(setupInstance) {
	}

	template <auto, SerialisationFlags::Flags> friend struct RpcMember;
};

template <auto Func, SerialisationFlags::Flags Flags = SerialisationFlags::NONE>
struct RpcMember : public RpcLambda<Func, Flags> {
	template <typename Parent, StringLiteral Name>
	RpcMember(Detail::ChildObjectInitialisator<Parent, Name> initialisator) {
#ifdef NO_DEFECT_REPORT_2118
		static int offset = -1;
		if (Parent::preparation) {
			Detail::ChildRpcEntry entry;
			entry.name = Name;
			entry.childGettingFunction = childGetter;
			entry.childNameGettingFunction = childNameGetter<Name>;
			entry.offset = reinterpret_cast<intptr_t>(this) - reinterpret_cast<intptr_t>(Parent::setupInstance);
			Parent::preparation->push_back(entry);
			offset = entry.offset;
		}
#else
		constexpr int parentIndex = Detail::AddChildRpc<Parent, 0, Name, Flags, childGetter, childNameGetter<Name>>::index;
		int& offset = Detail::getChildOffset<Parent, parentIndex>();
		if (offset == -1) [[unlikely]] {
			offset = reinterpret_cast<intptr_t>(this) - reinterpret_cast<intptr_t>(Parent::setupInstance);
		}
#endif
		auto parent = reinterpret_cast<Detail::RpcObjectCommon*>(reinterpret_cast<intptr_t>(this) - offset);
		parent->addChild(this);
	}
private:

	constexpr static Detail::ChildGettingFunction childGetter = [] (const IRemoteCallable* parent, int offset, std::string_view name) {
		return reinterpret_cast<IRemoteCallable*>(reinterpret_cast<intptr_t>(parent) + offset);
	};
	template <StringLiteral Name>
	constexpr static Detail::ChildNameGettingFunction childNameGetter = [] (const IRemoteCallable* parent, int offset, const IRemoteCallable* child,
			const Callback<void(std::string_view)>& actionIfReal) {
		if (reinterpret_cast<intptr_t>(parent) + offset == reinterpret_cast<intptr_t>(child)) {
			actionIfReal(Name);
			return true;
		}
		return false;
	};
};

#ifdef __GNUC__
#ifndef __clang__
#pragma GCC diagnostic pop
#endif // __GNUC__
#endif // not __clang__

} // namespace Bomba

#undef NO_DEFECT_REPORT_2118
