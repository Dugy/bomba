# Bomba
C++20 library for convenient serialisation and REST API. Working on it, current version supports only JSON serialisation and deserialisation.

Needs no code generation and no macros and is entirely header-only.

Works on GCC 11, Clang 12 or MSVC 19. Before C++23 reflection (`reflexpr` expression) is available, a GCC-only trick relying on Defect Report 2118 is used. Clang and MSVC have to use a different implementation with slighly worse performance. It's embedded-friendly as it can be used without any dynamic allocation when compiled by GCC and it needs dynamic allocation only during initialisation if compiled with Clang or MSVC.

## Usage

### Serialisation
Define a serialisable class:
```C++
#include "bomba_object.hpp"
//...

struct Point : Bomba::Serialisable<Point> {
	float x = key<"x"> = 1;
	float y = key<"y"> = 1;
	bool visible = key<"visible"> = true;
};
```
This creates a class with two `float` members initialised to `1` by default and one `bool` member initialised to `false`. It provides the serialisation functionality through an `ISerialisable` interface.

Then it can be serialised and deserialised into string using this:
```C++
#include "bomba_json.hpp"
//...
// assuming there is an object point of class Point we want to serialise
std::string written = point.serialise<Bomba::BasicJson<>>();

// now, deserialising from a std::string instance named reading
point.deserialise<Bomba::BasicJson<>>(reading);
```

### Custom string type
If you can't use `std::string` for some reasons (like restrictions regarding dynamic allocation), you can define your own string type (assuming it's called `String`) and serialise JSON as `Bomba::BasicJson<String>`. I recommend aliasing that type with `using`.

Your string type has to be convertible to `std::string_view`, needs to have a `clear()` method and needs to have the `+=` operator overloaded for `char` and `const char*`, similarly to `std::string`. Other traits are not needed.
