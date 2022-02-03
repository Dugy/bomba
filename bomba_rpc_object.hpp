#ifndef BOMBA_RPC_OBJECT
#define BOMBA_RPC_OBJECT

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

	using ChildGettingFunction = IRemoteCallable* (*)(const IRemoteCallable* self, int offset);
	using ChildNameGettingFunction = bool (*)(const IRemoteCallable* self, int offset, const IRemoteCallable* child,
			Callback<void(std::string_view)> reaction);
	using ArgumentAddingFunction = void (*)(IPropertyDescriptionFiller& filler, std::string_view name);
	using SubtypesAddingFunction = void (*)(ISerialisableDescriptionFiller& filler);
	using ChildDescribingFunction = void (*)(IRemoteCallableDescriptionFiller& filler);

#ifdef NO_DEFECT_REPORT_2118
	struct ChildRpcEntry {
		ChildGettingFunction childGettingFunction = nullptr;
		ChildNameGettingFunction childNameGettingFunction = nullptr;
		SubtypesAddingFunction subtypesAddingFunction = nullptr;
		ChildDescribingFunction childDescribingFunction = nullptr;
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
		friend constexpr SubtypesAddingFunction subtypesAdder(SubclassAccess<Parent, Index>);
		friend constexpr ChildDescribingFunction childDescriptor(SubclassAccess<Parent, Index>);
		friend constexpr auto boolIfDeclared(SubclassAccess<Parent, Index>);

		constexpr SubclassAccess() = default;
	};

	template <typename Parent, int Index, StringLiteral Name, SerialisationFlags::Flags Flags,
			ChildGettingFunction ChildGetter, ChildNameGettingFunction ChildNameGetter,
			SubtypesAddingFunction SubtypesAdder, ChildDescribingFunction ChildDescriptor>
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
		friend constexpr SubtypesAddingFunction subtypesAdder(SubclassAccess<Parent, Index>) {
			return SubtypesAdder;
		}
		friend constexpr ChildDescribingFunction childDescriptor(SubclassAccess<Parent, Index>) {
			return ChildDescriptor;
		}
		friend constexpr auto boolIfDeclared(SubclassAccess<Parent, Index>) {
			return true;
		}
		constexpr static int instantiated = 0;
	};

	template <typename Parent, int Index /* set to 0 */, StringLiteral Name, SerialisationFlags::Flags Flags,
			ChildGettingFunction ChildGetter, ChildNameGettingFunction ChildNameGetter,
			SubtypesAddingFunction SubtypesAdder, ChildDescribingFunction childAdder, typename SFINAE = void>
	struct AddChildRpc {
		constexpr static int index = SubclassSaver<Parent, Index, Name, Flags, ChildGetter,
				ChildNameGetter, SubtypesAdder, childAdder>::instantiated;
	};

	template <typename Parent, int Index, StringLiteral Name, SerialisationFlags::Flags Flags,
			ChildGettingFunction ChildGetter, ChildNameGettingFunction ChildNameGetter,
			SubtypesAddingFunction SubtypesAdder, ChildDescribingFunction childAdder>
	struct AddChildRpc<Parent, Index, Name, Flags, ChildGetter, ChildNameGetter, SubtypesAdder, childAdder,
			std::enable_if_t<std::is_same_v<bool, decltype(boolIfDeclared(SubclassAccess<Parent, Index>()))>>> {
		constexpr static int index =
				AddChildRpc<Parent, Index + 1, Name, Flags, ChildGetter, ChildNameGetter, SubtypesAdder, childAdder>::index + 1;
	};

	template <typename Parent, int Index>
	constexpr int& getChildOffset() {
		return childOffset(SubclassAccess<Parent, Index>());
	}

	template <typename Parent, int Index = 0, typename SFIANE = void>
	struct ChildAccess {
		static std::pair<std::string_view, int> name(const IRemoteCallable* parent, const IRemoteCallable* child) {
			logicError("Broken structure");
			return {"", IRemoteCallable::NO_SUCH_STRUCTURE};
		}
		static IRemoteCallable* instance(const IRemoteCallable* parent, std::string_view name) {
			return nullptr;
		}
		static IRemoteCallable* instance(const IRemoteCallable* parent, int index) {
			return nullptr;
		}
		static void listTypes(ISerialisableDescriptionFiller&) {}
		static void describeChildren(IRemoteCallableDescriptionFiller&) {}
	};

	template <typename Parent, int Index>
	struct ChildAccess<Parent, Index, std::enable_if_t<
				std::is_same_v<bool, decltype(boolIfDeclared(SubclassAccess<Parent, Index>()))>>> {
		constexpr static SubclassAccess<Parent, Index> accessor = {};
		static std::pair<std::string_view, int> name(const IRemoteCallable* parent, const IRemoteCallable* child) {
			int offset = childOffset(accessor);
			std::string_view name;
			if (!childNameGetter(accessor)(parent, offset, child, [&] (std::string_view knownName) { name = knownName; })) {
				return ChildAccess<Parent, Index + 1>::name(parent, child);
			}
			return { name, Index };
		}
		static IRemoteCallable* instance(const IRemoteCallable* parent, std::string_view name) {
			int offset = childOffset(accessor);
			if (name == childName(accessor))
				return childGetter(accessor)(parent, offset);
			return ChildAccess<Parent, Index + 1>::instance(parent, name);
		}
		static IRemoteCallable* instance(const IRemoteCallable* parent, int index) {
			if (index == Index)
				return childGetter(accessor)(parent, childOffset(accessor));
			return ChildAccess<Parent, Index + 1>::instance(parent, index);
		}
		static void listTypes(ISerialisableDescriptionFiller& filler) {
			subtypesAdder(accessor)(filler);
			return ChildAccess<Parent, Index + 1>::listTypes(filler);
		}
		static void describeChildren(IRemoteCallableDescriptionFiller& filler) {
			childDescriptor(accessor)(filler);
			return ChildAccess<Parent, Index + 1>::describeChildren(filler);
		}
	};
#endif

	struct RpcArgumentInfo {
		const char* name = nullptr; // std::string_view can't be volatile
		SerialisationFlags::Flags flags = SerialisationFlags::NONE;
		ArgumentAddingFunction argumentAdder = nullptr;
		SubtypesAddingFunction subtypesAdder = nullptr;
		void operator=(const RpcArgumentInfo& other) volatile {
			name = other.name;
			flags = other.flags;
			argumentAdder = other.argumentAdder;
			subtypesAdder = other.subtypesAdder;
		}
	};

	struct RpcSetupData {
		volatile RpcArgumentInfo* args = nullptr;
		volatile int argsSize = 0;
		volatile int argumentBeingSet = 0;
		volatile int argumentBeingFilled = 0;
		std::jmp_buf jumpBuffer = {};
		inline static thread_local RpcSetupData* volatile instance;

		void noticeArg(RpcArgumentInfo&& value) {
			argumentBeingFilled = argumentBeingFilled + 1;
			if (argumentBeingFilled > argsSize)
				std::longjmp(instance->jumpBuffer, true);

			for (int i = argumentBeingSet + 1; i < argsSize; i++) {
				for (int j = 0; true; j++) {
					if (value.name[j] != args[i].name[j])
						break;
					else if (value.name[j] == '\0' && args[i].name[j] == '\0')
						return;
				}
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
		FutureBase(FutureBase&& other) : _reader(other._reader), _responder(other._responder), _token(other._token) {
			other._reader = nullptr;
			other._responder = nullptr;
		}
		FutureBase& operator=(FutureBase&&) {}
		FutureBase() = default;
	};
} // namespace Detail

// Placeholder class to be replaced by std::future once it's extended by Extensions for Concurrency
template <typename Returned>
class Future : Detail::FutureBase<Returned> {
	mutable std::optional<Returned> _value = std::nullopt;
	using Base = Detail::FutureBase<Returned>;

	void cleanup() {
		if (Base::_responder && !_value) {
			get();
			Base::_reader = nullptr;
			_value = std::nullopt;
		}
	}
public:
	using Base::FutureBase;
	Future(Returned&& value) : _value(value) {}
	Future& operator=(Future&& other) {
		cleanup();
		std::swap(Base::_reader, other._reader);
		std::swap(Base::_responder, other._responder);
		std::swap(Base::_token, other._token);
		std::swap(_value, other._value);
		return *this;
	}

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

	~Future() {
		cleanup();
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

struct RpcLambdaInformationHolder {
	void (*unnamedChildAdder)(std::string_view name, IRemoteCallableDescriptionFiller& filler) = nullptr;
	SubtypesAddingFunction subtypesAdder = nullptr;

	constexpr RpcLambdaInformationHolder(decltype(unnamedChildAdder) unnamedChildAdder, SubtypesAddingFunction subtypesAdder)
		: unnamedChildAdder(unnamedChildAdder)
		, subtypesAdder(subtypesAdder)
	{}
};

	template <typename LambdaType, SerialisationFlags::Flags Flags, typename Returned, typename FirstArg, typename... Args>
	class RpcLambdaParent : public IRemoteCallable, public IResponseReader<Returned> {
	protected:
		using ArgsTuple = typename ArgsTupleProvider<std::decay_t<FirstArg>, std::decay_t<Args>...>::type;
		constexpr static int argsSize = std::tuple_size<ArgsTuple>::value;

		[[no_unique_address]] LambdaType lambda;

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
		void callWithAPartOfArgs(std::index_sequence<indexes...>, const Tuple& args) const {
			if constexpr(std::tuple_size<Tuple>::value + 1 < argumentInfoSize) {
				callWithAPartOfArgs(std::make_index_sequence<std::tuple_size<Tuple>::value + 1>(),
						std::tuple_cat(args, std::tuple<Detail::Omniconverter>{}));
			}
			auto setupData = Detail::RpcSetupData::instance;
			setupData->argumentBeingSet = std::tuple_size<Tuple>::value;
			setupData->argumentBeingFilled = setupData->argumentBeingSet;
			if (!setjmp(setupData->jumpBuffer)) {
				if constexpr(usesParent()) {
					static_assert(std::is_invocable_v<decltype(lambda), std::nullptr_t, decltype(std::get<indexes>(args))...>,
							"RPC lambda seems to be missing descriptors set as default arguments");
					lambda(nullptr, std::get<indexes>(args)...); // Problems here imply some arguments were not named
				} else {
					static_assert(std::is_invocable_v<decltype(lambda), decltype(std::get<indexes>(args))...>,
							"RPC lambda seems to be missing descriptors set as default arguments");
					lambda(std::get<indexes>(args)...);
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
				// This will NOT call the actual lambda this time
				reinterpret_cast<RpcLambdaParent*>(1)->callWithAPartOfArgs(std::make_index_sequence<0>(), std::tuple<>{});
			}
			Detail::RpcSetupData::instance = nullptr;
			return result;
		}
		inline static Detail::RpcArgumentInfo* argumentInfo = makeArgumentInfo();

		template <int ArgIndex = 0>
		static void setArg(std::optional<std::string_view> name, ArgsTuple& args,
						IStructuredInput& in, int index, SerialisationFlags::Flags flags) {
			constexpr int Index = usesParent() ? ArgIndex - 1 : ArgIndex;
			if constexpr(ArgIndex >= argsSize) {
				return;
			} else {
				if ((name.has_value() && *name == argumentInfo[Index].name) || (!name.has_value() && Index == index))
					TypedSerialiser<std::decay_t<decltype(std::get<ArgIndex>(args))>>::deserialiseMember(
							in, std::get<ArgIndex>(args), SerialisationFlags::Flags(
										flags | argumentInfo[Index].flags | SerialisationFlags::OBJECT_LAYOUT_KNOWN));
				else
					setArg<ArgIndex + 1>(name, args, in, index, SerialisationFlags::Flags(flags));
			}
		}

		template <size_t... indexes>
		void callInternal(std::index_sequence<indexes...>, Returned* retval, ArgsTuple& args) const {
			if constexpr(std::is_same_v<Returned, void>) {
				lambda(std::get<indexes>(args)...);
			} else {
				*retval = lambda(std::get<indexes>(args)...);
			}
		}
		bool call(IStructuredInput* arguments, IStructuredOutput& result, Callback<> introduceResult,
				Callback<void(std::string_view)>, std::optional<UserId>) const final override {
			ArgsTuple input;
			if constexpr(usesParent()) {
				std::get<0>(input) = static_cast<FirstArg>(parent());
			}

			if (arguments) {
				arguments->readObject(SerialisationFlags::Flags(Flags | SerialisationFlags::OBJECT_LAYOUT_KNOWN),
									  [&] (std::optional<std::string_view> nextName, int index) {
					if constexpr(usesParent()) {
						setArg<1>(nextName, input, *arguments, index, Flags);
					} else {
						setArg<0>(nextName, input, *arguments, index, Flags);
					}
					return (nextName.has_value() || index < argsSize);
				});
			}

			if constexpr(std::is_same_v<Returned, void>) {
				callInternal(std::make_index_sequence<argsSize>(), nullptr, input);
				introduceResult();
				result.writeNull(Flags);
			} else {
				Returned returned;
				callInternal(std::make_index_sequence<argsSize>(), &returned, input);
				introduceResult();
				TypedSerialiser<Returned>::serialiseMember(result, returned, Flags);
			}
			return true;
		}

		template <typename First, typename... Others>
		void remoteCallHelper(IStructuredOutput& out, First first, Others... args) const {
			constexpr int index = argumentInfoSize - sizeof...(Others) - 1;
			SerialisationFlags::Flags flags = SerialisationFlags::Flags(Flags | argumentInfo[index].flags);
			auto write = [&] () {
				out.introduceObjectMember(SerialisationFlags::Flags(Flags | SerialisationFlags::OBJECT_LAYOUT_KNOWN), argumentInfo[index].name, index);
				TypedSerialiser<First>::serialiseMember(out, first, flags);
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
				responder->getResponse(token, [&](IStructuredInput& in) {
					in.readNull(Flags);
				});
			} else {
				Returned result;
				responder->getResponse(token, [&](IStructuredInput& in) {
					TypedSerialiser<Returned>::deserialiseMember(in, result, Flags);
				});
				return result;
			}
		}

		template <typename... AnyArgs>
		RequestToken sendRequest(IRpcResponder* responder, AnyArgs&&... args) const {
			return responder->send({}, this, [&] (IStructuredOutput& out, RequestToken) {
				out.startWritingObject(SerialisationFlags::Flags(Flags | SerialisationFlags::OBJECT_LAYOUT_KNOWN), argumentInfoSize);
				if constexpr(!std::is_void_v<FirstArg>) {
					remoteCallHelper(out, std::forward<AnyArgs>(args)...);
				}
				out.endWritingObject(SerialisationFlags::Flags(Flags | SerialisationFlags::OBJECT_LAYOUT_KNOWN));
			});
		}

		template <typename... AnyArgs>
		Future<Returned> immediateCallToFuture(AnyArgs&&... args) const {
			if constexpr(std::is_void_v<Returned>) {
				lambda(std::forward<AnyArgs>(args)...);
				return {};
			} else {
				return Future<Returned>(lambda(std::forward<AnyArgs>(args)...));
			}
		}

	public:
		RpcLambdaParent(const LambdaType& lambda) : lambda(lambda) {}

		constexpr static RpcLambdaInformationHolder info = RpcLambdaInformationHolder(
					[] (std::string_view name, IRemoteCallableDescriptionFiller& filler) {
			filler.addMethod(name, "", [] (IPropertyDescriptionFiller& paramFiller) {
				for (int i = 0; i < argumentInfoSize; i++) {
					argumentInfo[i].argumentAdder(paramFiller, argumentInfo[i].name);
				}
			}, [] (IPropertyDescriptionFiller& returnFiller) {
				if constexpr(!std::is_void_v<Returned>) {
					returnFiller.addMember("result", "", [&] {
						TypedSerialiser<Returned>::describeType(returnFiller);
					});
				}
			});
		}, [] (ISerialisableDescriptionFiller& filler) {
			for (int i = 0; i < argumentInfoSize; i++) {
				argumentInfo[i].subtypesAdder(filler);
			}
			if constexpr(!std::is_void_v<Returned>) {
				TypedSerialiser<Returned>::listTypes(filler);
			}
		});

		template <StringLiteral Name>
		constexpr static Detail::ChildDescribingFunction childAdder = [] (IRemoteCallableDescriptionFiller& filler) {
			info.unnamedChildAdder(Name, filler);
		};

	};

	template <SerialisationFlags::Flags Flags, typename Placeholder>
	struct RpcLambdaRedirector : public IRemoteCallable {
		static_assert(!std::is_same_v<Placeholder, Placeholder>, "Failed to match the template overload");
	};


	template <typename LambdaType, SerialisationFlags::Flags Flags, typename Returned, typename FirstArg, typename... Args>
	struct RpcLambdaRedirector<Flags, Returned (LambdaType::*)(FirstArg, Args...) const>
			: public RpcLambdaParent<LambdaType, Flags, Returned, FirstArg, Args...> {
		using Parent = RpcLambdaParent<LambdaType, Flags, Returned, FirstArg, Args...>;
		using Parent::Parent;

		Returned operator()(Args... args) const requires(Parent::usesParent()) {
			IRpcResponder* responder = Parent::getResponder();
			if (responder) {
				return Parent::readResponse(responder, Parent::sendRequest(responder, std::forward<Args>(args)...));
			} else {
				return Parent::lambda(static_cast<FirstArg>(Parent::parent()), std::forward<Args>(args)...);
			}
		}
		Returned operator()(FirstArg first, Args... args) const requires(!Parent::usesParent()) {
			IRpcResponder* responder = Parent::getResponder();
			if (responder) {
				return Parent::readResponse(responder, Parent::sendRequest(responder, std::forward<FirstArg>(first), std::forward<Args>(args)...));
			} else {
				return Parent::lambda(std::forward<FirstArg>(first), std::forward<Args>(args)...);
			}
		}

		Future<Returned> async(Args... args) const requires(Parent::usesParent()) {
			IRpcResponder* responder = Parent::getResponder();
			if (responder) {
				RequestToken token = Parent::sendRequest(responder, std::forward<Args>(args)...);
				return Future<Returned>(this, responder, token);
			} else {
				return Parent::immediateCallToFuture(static_cast<FirstArg>(Parent::parent()), std::forward<Args>(args)...);
			}
		}
		Future<Returned> async(FirstArg first, Args... args) const requires(!Parent::usesParent()) {
			IRpcResponder* responder = Parent::getResponder();
			if (responder) {
				RequestToken token = Parent::sendRequest(responder, std::forward<FirstArg>(first), std::forward<Args>(args)...);
				return Future<Returned>(this, responder, token);
			} else {
				return Parent::immediateCallToFuture(std::forward<FirstArg>(first), std::forward<Args>(args)...);
			}
		}
	};


	template <typename LambdaType, SerialisationFlags::Flags Flags, typename Returned>
	struct RpcLambdaRedirector<Flags, Returned (LambdaType::*)() const>
			: public RpcLambdaParent<LambdaType, Flags, Returned, void> {
		using Parent = RpcLambdaParent<LambdaType, Flags, Returned, void>;
		using Parent::Parent;

		Returned operator()() const {
			IRpcResponder* responder = Parent::getResponder();
			if (responder) {
				return Parent::readResponse(responder, Parent::sendRequest(responder));
			} else {
				return Parent::lambda();
			}
		}

		Future<Returned> async() const {
			IRpcResponder* responder = Parent::getResponder();
			if (responder) {
				RequestToken token = Parent::sendRequest(responder);
				return Future<Returned>(this, responder, token);
			} else {
				return Parent::immediateCallToFuture();
			}
		}

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
			constexpr ArgumentAddingFunction argAdder = [] (IPropertyDescriptionFiller& filler, std::string_view name) {
				filler.addMember(name, "", [&] { TypedSerialiser<std::optional<T>>::describeType(filler); });
			};
			constexpr SubtypesAddingFunction subtypeAdder = [] (ISerialisableDescriptionFiller& filler) {
				TypedSerialiser<T>::listTypes(filler);
			};
			instance->noticeArg(Detail::RpcArgumentInfo{name, flags, argAdder, subtypeAdder});
			return {};
		}
	};
} // namespace Detail

Detail::NamedFlagSetter name(const char* value) {
	return Detail::NamedFlagSetter(value);
}

template <typename LambdaType, SerialisationFlags::Flags Flags = SerialisationFlags::NONE>
struct RpcLambda : public Detail::RpcLambdaRedirector<Flags, decltype(&LambdaType::operator())> {
	using Parent = typename Detail::RpcLambdaRedirector<Flags, decltype(&LambdaType::operator())>;
	using Parent::Parent;
};

template <auto Func, SerialisationFlags::Flags Flags = SerialisationFlags::NONE>
struct RpcStatelessLambda : public Detail::RpcLambdaRedirector<Flags, decltype(&decltype(Func)::operator())> {
	using Parent = typename Detail::RpcLambdaRedirector<Flags, decltype(&decltype(Func)::operator())>;
	RpcStatelessLambda() : Parent(Func) {}
};


namespace Detail {
	class RpcObjectCommon : public IRemoteCallable {
	protected:

		RpcObjectCommon(RpcObjectCommon*& setup) {
			setup = this;
		}
	public:
		template <auto Func, SerialisationFlags::Flags Flags>
		void addChild(RpcStatelessLambda<Func, Flags>* added) {
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
	std::pair<std::string_view, int> childName(const IRemoteCallable* child) const final override {
#ifdef NO_DEFECT_REPORT_2118
		int index = 0;
		for (auto& it : childEntries) {
			std::string_view nameObtained;
			if (it.childNameGettingFunction(this, it.offset, child, [&] (std::string_view name) {
				nameObtained = name;
			})) {
				return {nameObtained, index};
			}
			index++;
		}
		logicError("Broken structure");
		return {"", NO_SUCH_STRUCTURE};
#else
		return Detail::ChildAccess<Derived>::name(this, child);
#endif
	}
	const IRemoteCallable* getChild(std::string_view name) const final override {
#ifdef NO_DEFECT_REPORT_2118
		for (auto& it : childEntries) {
			if (it.name == name)
				return it.childGettingFunction(this, it.offset);
		}
		return nullptr;
#else
		return Detail::ChildAccess<Derived>::instance(this, name);
#endif
	}
	const IRemoteCallable* getChild(int index) const final override {
#ifdef NO_DEFECT_REPORT_2118
		if (index < std::ssize(childEntries)) {
			return childEntries[index].childGettingFunction(this, childEntries[index].offset);
		}
		return nullptr;
#else
		return Detail::ChildAccess<Derived>::instance(this, index);
#endif
	}

	void listTypes(ISerialisableDescriptionFiller& filler) const final override {
#ifdef NO_DEFECT_REPORT_2118
		for (auto& it : childEntries) {
			it.subtypesAddingFunction(filler);
		}
#else
		return Detail::ChildAccess<Derived>::listTypes(filler);
#endif
	}

	void generateDescription(IRemoteCallableDescriptionFiller& filler) const final override {
#ifdef NO_DEFECT_REPORT_2118
		for (auto& it : childEntries) {
			it.childDescribingFunction(filler);
		}
#else
		return Detail::ChildAccess<Derived>::describeChildren(filler);
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
struct RpcMember : public RpcStatelessLambda<Func, Flags> {
	using ParentClass = typename RpcStatelessLambda<Func, Flags>::Parent;
	template <typename ParentObject, StringLiteral Name>
	RpcMember(Detail::ChildObjectInitialisator<ParentObject, Name> initialisator) {
#ifdef NO_DEFECT_REPORT_2118
		static int offset = -1;
		if (ParentObject::preparation) {
			Detail::ChildRpcEntry entry;
			entry.name = Name;
			entry.childGettingFunction = childGetter;
			entry.childNameGettingFunction = childNameGetter<Name>;
			entry.subtypesAddingFunction = ParentClass::info.subtypesAdder;
			entry.childDescribingFunction = ParentClass::template childAdder<Name>;
			entry.offset = reinterpret_cast<intptr_t>(this) - reinterpret_cast<intptr_t>(ParentObject::setupInstance);
			ParentObject::preparation->push_back(entry);
			offset = entry.offset;
		}
#else
		constexpr int parentIndex = Detail::AddChildRpc<ParentObject, 0, Name, Flags, childGetter, childNameGetter<Name>,
				ParentClass::info.subtypesAdder, ParentClass::template childAdder<Name> >::index;
		int& offset = Detail::getChildOffset<ParentObject, parentIndex>();
		if (offset == -1) [[unlikely]] {
			offset = reinterpret_cast<intptr_t>(this) - reinterpret_cast<intptr_t>(ParentObject::setupInstance);
		}
#endif
		auto parent = reinterpret_cast<Detail::RpcObjectCommon*>(reinterpret_cast<intptr_t>(this) - offset);
		parent->addChild(this);
	}
private:

	constexpr static Detail::ChildGettingFunction childGetter = [] (const IRemoteCallable* parent, int offset) {
		return reinterpret_cast<IRemoteCallable*>(reinterpret_cast<intptr_t>(parent) + offset);
	};

	template <StringLiteral Name>
	constexpr static Detail::ChildNameGettingFunction childNameGetter = [] (const IRemoteCallable* parent, int offset,
			const IRemoteCallable* child, Callback<void(std::string_view)> actionIfReal) {
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

#endif //BOMBA_RPC_OBJECT
