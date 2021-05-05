#ifndef BOMBA_CORE // Needed to run in godbolt
#include "bomba_core.hpp"
#endif
#include <algorithm>

#if (_MSC_VER && !__INTEL_COMPILER) || __clang__
#define NO_DEFECT_REPORT_2118
#endif

#ifdef NO_DEFECT_REPORT_2118
#include <vector>
#endif

template<int Size>
struct StringLiteral {
	constexpr StringLiteral(const char (&str)[Size]) {
		std::copy_n(str, Size, value);
	}
	constexpr int size() const {
		return Size;
	}
	constexpr const char* c_str() const {
		return value;
	}
	constexpr char& operator[](int index) {
		return value[index];
	}
	constexpr char operator[](int index) const {
		return value[index];
	}
	constexpr bool operator==(const char* other) const {
		return compare(other);
	}
	template <int Size2>
	constexpr bool operator==(const StringLiteral<Size2>& other) const {
		if constexpr(Size != Size2)
			return false;
		else
			return compare(other.c_str());
	}
	constexpr operator std::string_view() const {
		return std::string_view(c_str());
	}
	char value[Size];

private:
	template <int depth = 0>
	constexpr bool compare(const char* other) const {
		if constexpr(depth == Size)
			return true;
		else {
			if (value[depth] != *other)
				return false;
			return compare<depth + 1>(other + 1);
		}
	}
};

namespace Bomba {

// Suppress GCC warnings that I should not do this
#ifdef __GNUC__
#ifndef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-template-friend"
#endif
#endif

using SerialisingFunction = void(*)(IStructuredOutput& out, const ISerialisable* parent,
		int order, int offset, SerialisationFlags::Flags flags);
using DeserialisingFunction = void(*)(IStructuredInput& in, ISerialisable* parent,
		int offset, SerialisationFlags::Flags flags);


template <typename T, StringLiteral Name>
void serialiseWithOffset(IStructuredOutput& out, const ISerialisable* parent,
			int order, int offset, SerialisationFlags::Flags flags) {
	T& member = *reinterpret_cast<T*>(uint64_t(parent) + offset);
	out.introduceObjectMember(flags, Name, order);
	serialiseMember(out, member, flags);
}

template <typename T, StringLiteral Name>
void deserialiseWithOffset(IStructuredInput& in, ISerialisable* parent,
			int offset, SerialisationFlags::Flags flags) {
	T& member = *reinterpret_cast<T*>(uint64_t(parent) + offset);
	deserialiseMember(in, member, flags);
}

#ifndef NO_DEFECT_REPORT_2118

template <typename Child, int Index>
struct SerialiserStorage {
	friend constexpr SerialisingFunction serialiser(SerialiserStorage<Child, Index>);
	friend constexpr DeserialisingFunction deserialiser(SerialiserStorage<Child, Index>);
	friend constexpr SerialisationFlags::Flags memberFlags(SerialiserStorage<Child, Index>);
	friend constexpr auto name(SerialiserStorage<Child, Index>);
	constexpr friend auto boolIfDeclared(SerialiserStorage<Child, Index>);
};

template <typename Child, int Index, SerialisingFunction Serialiser,
		DeserialisingFunction Deserialiser, StringLiteral Name, SerialisationFlags::Flags Flags>
struct SerialiserSaver {
	friend constexpr SerialisingFunction serialiser(SerialiserStorage<Child, Index>) {
		return Serialiser;
	}
	friend constexpr DeserialisingFunction deserialiser(SerialiserStorage<Child, Index>) {
		return Deserialiser;
	}
	friend constexpr auto name(SerialiserStorage<Child, Index>) {
		return Name;
	}
	friend constexpr SerialisationFlags::Flags memberFlags(SerialiserStorage<Child, Index>) {
		return Flags;
	}
	constexpr friend auto boolIfDeclared(SerialiserStorage<Child, Index>) {
		return true;
	}
	constexpr static bool instantiated = true;
};

template <typename Child, int Index /* set to 0*/, SerialisingFunction Serialiser,
		DeserialisingFunction Deserialiser, StringLiteral Name,
		SerialisationFlags::Flags Flags, typename SFINAE = void>
struct addMember {
	constexpr static bool done = SerialiserSaver<Child, Index, Serialiser, Deserialiser,
			Name, Flags>::instantiated;
};

template <typename Child, int Index, SerialisingFunction Serialiser,
		DeserialisingFunction Deserialiser, StringLiteral Name,
		SerialisationFlags::Flags Flags>
struct addMember<Child, Index, Serialiser, Deserialiser, Name, Flags, std::enable_if_t<
			std::is_same_v<bool, decltype(boolIfDeclared(SerialiserStorage<Child, Index>()))>>> {
	constexpr static bool done = addMember<Child, Index + 1, Serialiser, Deserialiser,
			Name, Flags, void>::done;
};

template <typename Child, int Index = 0, typename SFINAE = void>
struct ObjectInfo {
	static void serialise(IStructuredOutput& out, const ISerialisable* parent, SerialisationFlags::Flags flags) {}
	static void deserialise(IStructuredInput& in, ISerialisable* parent,
			SerialisationFlags::Flags flags, std::string_view memberName) {}
	constexpr static int size = 0;
	static std::array<int, size + Index>& getOffsets() {
		static std::array<int, size + Index> offsets;
		return offsets;
	}
};

template <typename Child, int Index>
struct ObjectInfo<Child, Index, std::enable_if_t<
			std::is_same_v<bool, decltype(boolIfDeclared(SerialiserStorage<Child, Index>()))>>> {
	constexpr static SerialiserStorage<Child, Index> store = {};
	static void serialise(IStructuredOutput& out, const ISerialisable* parent, SerialisationFlags::Flags flags) {
		constexpr SerialiserStorage<Child, Index> store;
		serialiser(store)(out, parent, Index, getOffsets()[Index],
				SerialisationFlags::Flags(flags | memberFlags(store)));
		ObjectInfo<Child, Index + 1, void>::serialise(out, parent, flags);
	}
	static void deserialise(IStructuredInput& in, ISerialisable* parent,
				SerialisationFlags::Flags flags, std::string_view memberName) {
		if (memberName == name(store))
			deserialiser(store)(in, parent, getOffsets()[Index],
					SerialisationFlags::Flags(flags | memberFlags(store)));
		else
			ObjectInfo<Child, Index + 1, void>::deserialise(in, parent, flags, memberName);
	}
	constexpr static int size = ObjectInfo<Child, Index + 1, void>::size + 1;
	
	static std::array<int, size + Index>& getOffsets() {
		if constexpr(Index == 0) {
			static std::array<int, size> offsets;
			return offsets;
		} else {
			return ObjectInfo<Child, 0>::getOffsets();
		}
	}
};

#ifdef __GNUC__
#ifndef __clang__
#pragma GCC diagnostic pop
#endif // __GNUC__
#endif // not __clang__

#endif // not NO_DEFECT_REPORT_2118

template <typename Child>
class Serialisable : public ISerialisable {

#ifdef NO_DEFECT_REPORT_2118
	struct Information {
		SerialisingFunction serialiser;
		DeserialisingFunction deserialiser;
		const char* name;
		SerialisationFlags::Flags flags;
		int offset;
	};
#endif

	constexpr static int8_t garbageNumber1 = 13;
	constexpr static int8_t garbageNumber2 = -13;

	struct MappingElement {
		int size;
		std::array<int, 2> lastInitialisedBefore;
		const char* name;
	};
	enum class InitialisationState : uint8_t {
		UNINITIALISED,
		INITIALISING,
		INITIALISING_AGAIN,
		INITIALISED
	};
	
	struct SerialisationSetupData {
#ifdef NO_DEFECT_REPORT_2118
		std::vector<Information>* result;
		std::vector<MappingElement> elements;
#else
		MappingElement* elements;
#endif
		const Child* instance = nullptr;
		int index = {};
		int parentOffset = {};
		InitialisationState initState = InitialisationState::UNINITIALISED;
	};
	static inline SerialisationSetupData* _setupInstance = nullptr;

#ifdef NO_DEFECT_REPORT_2118
	static std::vector<Information> setup() {
		std::vector<Information> result;
#else
	static bool setup() {
		constexpr int memberCount = ObjectInfo<Child>::size;
#endif
	
		SerialisationSetupData setupData;
		_setupInstance = &setupData;
#ifdef NO_DEFECT_REPORT_2118
		setupData.result = &result;
#else
		std::array<MappingElement, memberCount> mappingElements;
		setupData.elements = mappingElements.data();
#endif
		setupData.initState = InitialisationState::INITIALISING;

		// Create the child class in specially prepared garbage
		constexpr int allocatedSize = sizeof(Child) / sizeof(void*) + 1;
		std::array<std::array<void*, allocatedSize>, 2> allocated; // Allocate as void* to have proper padding
		std::array<int8_t*, 2> childBytes;
		struct ChildDestroyer { // We must assure proper destruction of Child, even if an exception is thrown
			Child* child = nullptr;
			~ChildDestroyer() {
				if (child)
					child->~Child();
			}
		};
		std::array<ChildDestroyer, 2> destroyers;
		auto makeChild = [&] (int index) {
			setupData.instance = reinterpret_cast<Child*>(&allocated[index]);
			setupData.index = 0;
			destroyers[index].child = new (&allocated[index]) Child();
			childBytes[index] = reinterpret_cast<int8_t*>(destroyers[index].child);
		};
		makeChild(0);
		setupData.parentOffset = reinterpret_cast<uint64_t>(
				 static_cast<const Serialisable<Child>*>(setupData.instance))
				 - reinterpret_cast<uint64_t>(setupData.instance);
		
		// Do it again
		setupData.initState = InitialisationState::INITIALISING_AGAIN;
		makeChild(1);

		// Check where garbage was left
#ifdef NO_DEFECT_REPORT_2118
		int memberCount = result.size();
#else
		std::array<int, memberCount>& offsets = ObjectInfo<Child>::getOffsets();
#endif
		for (int i = 0; i < memberCount; i++) {
			int start = std::max(setupData.elements[i].lastInitialisedBefore[0],
					setupData.elements[i].lastInitialisedBefore[1]);
			while (childBytes[0][start] == garbageNumber1 && childBytes[1][start] == garbageNumber2) {
				if (start > int(sizeof(Child))) throw std::logic_error("Reflection failed");
				start++;
			}
#ifdef NO_DEFECT_REPORT_2118
			result[i].offset = start;
#else
			offsets[i] = start;
#endif
		}

		setupData.initState = InitialisationState::INITIALISED;
		_setupInstance = nullptr;
		
#ifdef NO_DEFECT_REPORT_2118
		result.shrink_to_fit();
		return result;
#else
		return true;
#endif
	}
	static inline auto _setup = setup();

	template <StringLiteral Name, SerialisationFlags::Flags Flags, typename... Args>
	class InitialiserInitialiser {
		std::tuple<Args...> _args;

		constexpr InitialiserInitialiser(Args... args) :
			_args(std::make_tuple(args...)) {
		}
		
		template <typename T, size_t... Enumeration>
		T makeType(std::index_sequence<Enumeration...>) {
			return T(std::get<Enumeration>(_args)...);
		}
	public:
#if _MSC_VER && !__INTEL_COMPILER
		template <class T>
		struct isInitializerList : std::false_type {};

		template <class T>
		struct isInitializerList<std::initializer_list<T>> : std::true_type {};

		template<typename T, std::enable_if_t<(std::is_class_v<T> && !isInitializerList<T>::value)
			|| std::is_arithmetic_v<T> || std::is_floating_point_v<T> || std::is_enum_v<T>>* = nullptr>
#else
		template <typename T>
#endif
		operator T() {
			if (Serialisable::_setupInstance) [[unlikely]] {
				SerialisationSetupData& info = *Serialisable::_setupInstance;

#ifdef NO_DEFECT_REPORT_2118
				if (int(info.elements.size()) < info.index + 1)
					info.elements.emplace_back();
#endif
				
				info.elements[info.index].size = sizeof(T);
				int8_t garbageNumber = info.initState == InitialisationState::INITIALISING
						? Serialisable::garbageNumber1 : Serialisable::garbageNumber2;

				int lastUninitialised = sizeof(Child) - 1;

				while (lastUninitialised && reinterpret_cast<const int8_t*>(
							info.instance)[lastUninitialised - 1] == garbageNumber)
					lastUninitialised--;

				info.elements[info.index].lastInitialisedBefore[info.initState
						== InitialisationState::INITIALISING_AGAIN] = lastUninitialised;

#ifdef NO_DEFECT_REPORT_2118
				if (info.initState == InitialisationState::INITIALISING) {
					info.result->push_back({&serialiseWithOffset<T, Name>,
							&deserialiseWithOffset<T, Name>, Name.c_str()});
				}
#else
				bool added = addMember<Child, 0, &serialiseWithOffset<T, Name>,
						&deserialiseWithOffset<T, Name>, Name, Flags>::done;
				added = !added && Serialisable::_setup; // Avoid unused variable warning and use the _setup
#endif
				info.index++;
			}
			return makeType<T>(std::make_index_sequence<sizeof...(Args)>());
		}
		friend class Serialisable<Child>;
	};

	template <StringLiteral Name, SerialisationFlags::Flags Flags>
	struct Initialiser : public InitialiserInitialiser<Name, Flags> {

		constexpr Initialiser() = default;
		
		template <typename... Args>
		InitialiserInitialiser<Name, Flags, Args...> init(Args... args) const {
			return InitialiserInitialiser<Name, Flags, Args...>(args...);
		}
		template <typename Arg>
		InitialiserInitialiser<Name, Flags, Arg> operator=(Arg arg) const {
			return InitialiserInitialiser<Name, Flags, Arg>(arg);
		}
	};

protected:

	template <StringLiteral Name, SerialisationFlags::Flags Flags = SerialisationFlags::NONE>
	inline static constexpr Initialiser<Name, Flags> key = {};

	virtual void serialiseInternal(IStructuredOutput& format,
			SerialisationFlags::Flags flags) const {
#ifdef NO_DEFECT_REPORT_2118
		format.startWritingObject(flags, _setup.size());
		for (int i = 0; i < int(_setup.size()); i++) {
			_setup[i].serialiser(format, this, i, _setup[i].offset,
					SerialisationFlags::Flags(flags | _setup[i].flags));
		}
#else
		format.startWritingObject(flags, ObjectInfo<Child>::size);
		ObjectInfo<Child>::serialise(format, this, flags);
#endif
		format.endWritingObject(flags);
	}
	virtual bool deserialiseInternal(IStructuredInput& format,
			SerialisationFlags::Flags flags) {
		format.startReadingObject(flags);
		std::optional<std::string_view> name;
		while ((name = format.nextObjectElement(flags))) {
#ifdef NO_DEFECT_REPORT_2118
			for (int i = 0; i < int(_setup.size()); i++) {
				if (_setup[i].name == name)
					_setup[i].deserialiser(format, this, _setup[i].offset,
							SerialisationFlags::Flags(flags | _setup[i].flags));
				break;
			}
#else
			ObjectInfo<Child>::deserialise(format, this, flags, *name);
#endif
		}
		format.endReadingObject(flags);
		return format.good;
	}

	Serialisable() {
		static_assert(std::is_base_of_v<Serialisable<Child>, Child>, // Must be in a method
				"Serialisable's template argument must be the class that inherits from it");
				
		if (_setupInstance) [[unlikely]] {
			// We need to keep track of what is allocated and what is garbage
			void* start = reinterpret_cast<void*>(reinterpret_cast<uint64_t>(this)
					+ sizeof(Serialisable<Child>));
			uint8_t garbageNumber = (_setupInstance->initState == InitialisationState::INITIALISING)
					? garbageNumber1 : garbageNumber2;
			memset(start, garbageNumber, sizeof(Child) - sizeof(Serialisable<Child>));
		}
	}
	Serialisable(const Serialisable& other) = default;
	Serialisable(Serialisable&& other) = default;

	virtual ~Serialisable() = default;
};

} // namespace Bomba
