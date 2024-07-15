# C++ Coding Standards

## Introduction

This guide attempts to describe a few coding standards that are being
used in GJS.

For formatting we follow the [Google C++ Style Guide][google].
This guide won't repeat all the rules that you can read there.
Instead, it covers rules that can't be checked "mechanically" with an
automated style checker.
It is not meant to be exhaustive.

This guide is based on the [LLVM coding standards][llvm] (source code
[here][llvm-source].)
No coding standards should be regarded as absolute requirements to be
followed in all instances, but they are important to keep a large
complicated codebase readable.

Many of these rules are not uniformly followed in the code base.
This is because most of GJS was written before they were put in place.
Our long term goal is for the entire codebase to follow the conventions,
but we explicitly *do not* want patches that do large-scale reformatting
of existing code.
On the other hand, it is reasonable to rename the methods of a class if
you're about to change it in some other way.
Just do the reformatting as a separate commit from the functionality
change.

The ultimate goal of these guidelines is to increase the readability and
maintainability of our code base.
If you have suggestions for topics to be included, please open an issue
at <https://gitlab.gnome.org/GNOME/gjs>.

[google]: https://google.github.io/styleguide/cppguide.html
[llvm]: https://llvm.org/docs/CodingStandards.html
[llvm-source]: https://raw.githubusercontent.com/llvm-mirror/llvm/HEAD/docs/CodingStandards.rst

## Languages, Libraries, and Standards

Most source code in GJS using these coding standards is C++ code.
There are some places where C code is used due to environment
restrictions or historical reasons.
Generally, our preference is for standards conforming, modern, and
portable C++ code as the implementation language of choice.

### C++ Standard Versions

GJS is currently written using C++17 conforming code, although we
restrict ourselves to features which are available in the major
toolchains.

Regardless of the supported features, code is expected to (when
reasonable) be standard, portable, and modern C++17 code.
We avoid unnecessary vendor-specific extensions, etc., including
`g_autoptr()` and friends.

### C++ Standard Library

Use the C++ standard library facilities whenever they are available for
a particular task.
In particular, use STL containers rather than `GList*` and `GHashTable*`
and friends, for their type safety and memory management.

There are some exceptions such as the standard I/O streams library which
is avoided, and use in space-constrained situations.

### Supported C++17 Language and Library Features

While GJS and SpiderMonkey use C++17, not all features are available in
all of the toolchains which we support.
A good rule of thumb is to check whether SpiderMonkey uses the feature.
If so, it's okay to use in GJS.

### Other Languages

Any code written in JavaScript is not subject to the formatting rules
below.
Instead, we adopt the formatting rules enforced by the
[`eslint`][eslint] tool.

[eslint]: https://eslint.org/

## Mechanical Source Issues

All source code formatting should follow the
[Google C++ Style Guide][google] with a few exceptions:

* We use four-space indentation, to match the previous GJS coding style
  so that the auto-formatter doesn't make a huge mess.
* Likewise we keep short return statements on separate lines instead of
  allowing them on single lines.

Our tools (clang-format and cpplint) have the last word on acceptable
formatting.
It may happen that the tools are not configured correctly, or contradict
each other.
In that case we accept merge requests to fix that, rather than code that
the tools reject.

[google]: https://google.github.io/styleguide/cppguide.html

### Source Code Formatting

#### Commenting

Comments are one critical part of readability and maintainability.
Everyone knows they should comment their code, and so should you.
When writing comments, write them as English prose, which means they
should use proper capitalization, punctuation, etc.
Aim to describe what the code is trying to do and why, not *how* it does
it at a micro level.
Here are a few critical things to document:

##### File Headers

Every source file should have a header on it that describes the basic
purpose of the file.
If a file does not have a header, it should not be checked into the
tree.
The standard header looks like this:

```c++
/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT OR LGPL-2.0-or-later
// SPDX-FileCopyrightText: YEAR NAME <EMAIL>

#include <HEADERS>

// gi/private.cpp - private "imports._gi" module with operations that we need
// to use from JS in order to create GObject classes, but should not be exposed
// to client code.
```

A few things to note about this particular format: The "`-*-`" string on
the first line is there to tell editors that the source file is a C++
file, not a C file (since C++ and C headers both share the `.h`
extension.)
This is originally an Emacs convention, but other editors use it too.

The next lines in the file are machine-readable SPDX comments describing the file's copyright and the license that the file is released under.
These comments should follow the [REUSE specification][reuse].
This makes it perfectly clear what terms the source code can be
distributed under and should not be modified.
Names can be added to the copyright when making a substantial
contribution to the file, not just a function or two.

After the header includes comes a paragraph or two about what code the
file contains.
If an algorithm is being implemented or something tricky is going on,
this should be explained here, as well as any notes or *gotchas* in the
code to watch out for.

[reuse]: https://reuse.software/

##### Class overviews

Classes are one fundamental part of a good object oriented design.
As such, a class definition should have a comment block that explains
what the class is used for and how it works.
Every non-trivial class is expected to have such a comment block.

##### Method information

Methods defined in a class (as well as any global functions) should also
be documented properly.
A quick note about what it does and a description of the borderline
behaviour is all that is necessary here (unless something particularly
tricky or insidious is going on).
The hope is that people can figure out how to use your interfaces
without reading the code itself.

#### Comment Formatting

Either C++ style comments (`//`) or C style (`/* */`) comments are
acceptable.
However, when documenting a method or function, use [gtk-doc style]
comments which are based on C style (`/** */`).
When C style comments take more than one line, put an asterisk (`*`) at
the beginning of each line:

```c++
/* a list of all GClosures installed on this object (from
 * signals, trampolines and explicit GClosures), used when tracing */
```

Commenting out large blocks of code is discouraged, but if you really
have to do this (for documentation purposes or as a suggestion for debug printing), use `#if 0` and `#endif`.
These nest properly and are better behaved in general than C style
comments.

[gtk-doc style]: https://developer.gnome.org/gtk-doc-manual/unstable/documenting.html.en

### Language and Compiler Issues

#### Treat Compiler Warnings Like Errors

If your code has compiler warnings in it, something is wrong — you
aren't casting values correctly, you have questionable constructs in
your code, or you are doing something legitimately wrong.
Compiler warnings can cover up legitimate errors in output and make
dealing with a translation unit difficult.

It is not possible to prevent all warnings from all compilers, nor is it
desirable.
Instead, pick a standard compiler (like GCC) that provides a good
thorough set of warnings, and stick to it.
Currently we use GCC and the set of warnings defined by the
[`ax_compiler_flags`][ax-compiler-flags] macro.
In the future, we will use Meson's highest `warning_level` setting as
the arbiter.

[ax-compiler-flags]: https://www.gnu.org/software/autoconf-archive/ax_compiler_flags.html#ax_compiler_flags

#### Write Portable Code

In almost all cases, it is possible and within reason to write
completely portable code.
If there are cases where it isn't possible to write portable code,
isolate it behind a well defined (and well documented) interface.

In practice, this means that you shouldn't assume much about the host
compiler (and Visual Studio tends to be the lowest common denominator).

#### Use of `class` and `struct` Keywords

In C++, the `class` and `struct` keywords can be used almost
interchangeably.
The only difference is when they are used to declare a class: `class`
makes all members private by default while `struct` makes all members
public by default.

Unfortunately, not all compilers follow the rules and some will generate
different symbols based on whether `class` or `struct` was used to
declare the symbol (e.g., MSVC).
This can lead to problems at link time.

* All declarations and definitions of a given `class` or `struct` must
  use the same keyword.  For example:

    ```c++
    class Foo;

    // Breaks mangling in MSVC.
    struct Foo {
        int data;
    };
    ```

* As a rule of thumb, `struct` should be kept to structures where *all*
  members are declared public.

    ```c++
    // Foo feels like a class... this is strange.
    struct Foo {
       private:
        int m_data;

       public:
        Foo() : m_data(0) {}
        int getData() const { return m_data; }
        void setData(int d) { m_data = d; }
    };

    // Bar isn't POD, but it does look like a struct.
    struct Bar {
        int m_data;
        Bar() : m_data(0) {}
    };
    ```

#### Use `auto` Type Deduction to Make Code More Readable

Some are advocating a policy of "almost always `auto`" in C++11 and
later, but GJS uses a more moderate stance.
Use `auto` only if it makes the code more readable or easier to
maintain.
Don't "almost always" use `auto`, but do use `auto` with initializers
like `cast<Foo>(...)` or other places where the type is already obvious
from the context.
Another time when `auto` works well for these purposes is when the type
would have been abstracted away anyway, often behind a container's
typedef such as `std::vector<T>::iterator`.

#### Beware unnecessary copies with ``auto``

The convenience of `auto` makes it easy to forget that its default
behaviour is a copy.
Particularly in range-based `for` loops, careless copies are expensive.

As a rule of thumb, use `auto&` unless you need to copy the result,
and use `auto*` when copying pointers.

```c++
// Typically there's no reason to copy.
for (const auto& val : container)
    observe(val);
for (auto& val : container)
    val.change();

// Remove the reference if you really want a new copy.
for (auto val : container) {
    val.change();
    save_somewhere(val);
}

// Copy pointers, but make it clear that they're pointers.
for (const auto* ptr : container)
    observe(*ptr);
for (auto* ptr : container)
    ptr->change();
```

#### Beware of non-determinism due to ordering of pointers

In general, there is no relative ordering among pointers.
As a result, when unordered containers like sets and maps are used with
pointer keys the iteration order is undefined.
Hence, iterating such containers may result in non-deterministic code
generation.
While the generated code might not necessarily be "wrong code", this
non-determinism might result in unexpected runtime crashes or simply
hard to reproduce bugs on the customer side making it harder to debug
and fix.

As a rule of thumb, in case an ordered result is expected, remember to
sort an unordered container before iteration.
Or use ordered containers like `std::vector` if you want to iterate
pointer keys.

#### Beware of non-deterministic sorting order of equal elements

`std::sort` uses a non-stable sorting algorithm in which the order of
equal elements is not guaranteed to be preserved.
Thus using `std::sort` for a container having equal elements may result
in non-determinstic behaviour.

## Style Issues

### The High-Level Issues

#### Self-contained Headers

Header files should be self-contained (compile on their own) and end in
`.h`.
Non-header files that are meant for inclusion should end in `.inc` and
be used sparingly.

All header files should be self-contained.
Users and refactoring tools should not have to adhere to special
conditions to include the header.
Specifically, a header should have header guards and include all other
headers it needs.

There are rare cases where a file designed to be included is not
self-contained.
These are typically intended to be included at unusual locations, such
as the middle of another file.
They might not use header guards, and might not include their
prerequisites.
Name such files with the `.inc` extension.
Use sparingly, and prefer self-contained headers when possible.

#### `#include` as Little as Possible

`#include` hurts compile time performance.
Don't do it unless you have to, especially in header files.

But wait! Sometimes you need to have the definition of a class to use
it, or to inherit from it.
In these cases go ahead and `#include` that header file.
Be aware however that there are many cases where you don't need to have
the full definition of a class.
If you are using a pointer or reference to a class, you don't need the
header file.
If you are simply returning a class instance from a prototyped function
or method, you don't need it.
In fact, for most cases, you simply don't need the definition of a
class.
And not `#include`ing speeds up compilation.

It is easy to try to go too overboard on this recommendation, however.
You **must** include all of the header files that you are using — you
can include them either directly or indirectly through another header
file.
To make sure that you don't accidentally forget to include a header file
in your module header, make sure to include your module header **first**
in the implementation file (as mentioned above).
This way there won't be any hidden dependencies that you'll find out
about later.

The tool [IWYU][iwyu] can help with this, but it generates a lot of
false positives, so we don't automate it.

In many cases, header files with SpiderMonkey types will only need to
include one SpiderMonkey header, `<js/TypeDecls.h>`, unless they have
inline functions or SpiderMonkey member types.
This header file contains a number of forward declarations and nothing
else.

[iwyu]: https://include-what-you-use.org/

#### Header inclusion order

Headers should be included in the following order:

- `<config.h>`
- C system headers
- C++ system headers
- GNOME library headers
- SpiderMonkey library headers
- GJS headers

Each of these groups must be separated by blank lines.
Within each group, all the headers should be alphabetized.
The first five groups should use angle brackets for the includes.

Note that the header `<config.h>` must be included before any
SpiderMonkey headers.

GJS headers should use quotes, _except_ in public header files (any
header file included from `<gjs/gjs.h>`.)

If you need to include headers conditionally, add the conditional
after the group that it belongs to, separated by a blank line.

If it is not obvious, you may add a comment after the include,
explaining what this header is included for.
This makes it easier to figure out whether to remove a header later if
its functionality is no longer used in the file.

Here is an example of all of the above rules together:

```c++
#include <config.h>  // for ENABLE_PROFILER

#include <string.h>  // for strlen

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#endif

#include <vector>

#include <girepository.h>
#include <glib.h>

#include <js/GCHashTable.h>  // for GCHashMap
#include <jsapi.h>           // for JS_New, JSAutoRealm, JS_GetProperty
#include <mozilla/Unused.h>

#include "gjs/atoms.h"
#include "gjs/context-private.h"
#include "gjs/jsapi-util.h"
```

#### Keep "Internal" Headers Private

Many modules have a complex implementation that causes them to use more
than one implementation (`.cpp`) file.  It is often tempting to put
the internal communication interface (helper classes, extra functions,
etc.) in the public module header file.
Don't do this!

If you really need to do something like this, put a private header file
in the same directory as the source files, and include it locally.
This ensures that your private interface remains private and undisturbed
by outsiders.

It's okay to put extra implementation methods in a public class itself.
Just make them private (or protected) and all is well.

#### Use Early Exits and `continue` to Simplify Code

When reading code, keep in mind how much state and how many previous
decisions have to be remembered by the reader to understand a block of
code.
Aim to reduce indentation where possible when it doesn't make it more
difficult to understand the code.
One great way to do this is by making use of early exits and the
`continue` keyword in long loops.
As an example of using an early exit from a function, consider this
"bad" code:

```c++
Value* do_something(Instruction* in) {
    if (!is_a<TerminatorInst>(in) && in->has_one_use() && do_other_thing(in)) {
        ... some long code....
    }

    return nullptr;
}
```

This code has several problems if the body of the `if` is large.
When you're looking at the top of the function, it isn't immediately
clear that this *only* does interesting things with non-terminator
instructions, and only applies to things with the other predicates.
Second, it is relatively difficult to describe (in comments) why these
predicates are important because the `if` statement makes it difficult
to lay out the comments.
Third, when you're deep within the body of the code, it is indented an
extra level.
Finally, when reading the top of the function, it isn't clear what the
result is if the predicate isn't true; you have to read to the end of
the function to know that it returns null.

It is much preferred to format the code like this:

```c++
Value* do_something(Instruction* in) {
    // Terminators never need 'something' done to them because ...
    if (is_a<TerminatorInst>(in))
        return nullptr;

    // We conservatively avoid transforming instructions with multiple uses
    // because goats like cheese.
    if (!in->has_one_use())
        return nullptr;

    // This is really just here for example.
    if (!do_other_thing(in))
        return nullptr;

    ... some long code....
}
```

This fixes these problems.
A similar problem frequently happens in `for` loops.
A silly example is something like this:

```c++
for (Instruction& in : bb) {
    if (auto* bo = dyn_cast<BinaryOperator>(&in)) {
        Value* lhs = bo->get_operand(0);
        Value* rhs = bo->get_operand(1);
        if (lhs != rhs) {
            ...
        }
    }
}
```

When you have very small loops, this sort of structure is fine.
But if it exceeds more than 10-15 lines, it becomes difficult for people
to read and understand at a glance.
The problem with this sort of code is that it gets very nested very
quickly, meaning that the reader of the code has to keep a lot of
context in their brain to remember what is going immediately on in the
loop, because they don't know if/when the `if` conditions will have
`else`s etc.
It is strongly preferred to structure the loop like this:

```c++
for (Instruction& in : bb) {
    auto* bo = dyn_cast<BinaryOperator>(&in);
    if (!bo)
        continue;

    Value* lhs = bo->get_operand(0);
    Value* rhs = bo->get_operand(1);
    if (lhs == rhs)
        continue;

    ...
}
```

This has all the benefits of using early exits for functions: it reduces
nesting of the loop, it makes it easier to describe why the conditions
are true, and it makes it obvious to the reader that there is no `else`
coming up that they have to push context into their brain for.
If a loop is large, this can be a big understandability win.

#### Don't use `else` after a `return`

For similar reasons above (reduction of indentation and easier reading),
please do not use `else` or `else if` after something that interrupts
control flow — like `return`, `break`, `continue`, `goto`, etc.
For example, this is *bad*:

```c++
case 'J': {
    if (is_signed) {
        type = cx.getsigjmp_buf_type();
        if (type.is_null()) {
            error = ASTContext::ge_missing_sigjmp_buf;
            return QualType();
        } else {
            break;
        }
    } else {
        type = cx.getjmp_buf_type();
        if (type.is_null()) {
            error = ASTContext::ge_missing_jmp_buf;
            return QualType();
        } else {
            break;
        }
    }
}
```
It is better to write it like this:

```c++
case 'J':
    if (is_signed) {
        type = cx.getsigjmp_buf_type();
        if (type.is_null()) {
            error = ASTContext::ge_missing_sigjmp_buf;
            return QualType();
        }
    } else {
        type = cx.getjmp_buf_type();
        if (type.is_null()) {
            error = ASTContext::ge_missing_jmp_buf;
            return QualType();
        }
    }
    break;
```

Or better yet (in this case) as:

```c++
case 'J':
    if (is_signed)
        type = cx.getsigjmp_buf_type();
    else
        type = cx.getjmp_buf_type();

    if (type.is_null()) {
        error = is_signed ? ASTContext::ge_missing_sigjmp_buf
                          : ASTContext::ge_missing_jmp_buf;
        return QualType();
    }
    break;
```

The idea is to reduce indentation and the amount of code you have to
keep track of when reading the code.

#### Turn Predicate Loops into Predicate Functions

It is very common to write small loops that just compute a boolean
value.
There are a number of ways that people commonly write these, but an
example of this sort of thing is:

```c++
bool found_foo = false;
for (unsigned ix = 0, len = bar_list.size(); ix != len; ++ix)
    if (bar_list[ix]->is_foo()) {
        found_foo = true;
        break;
    }

if (found_foo) {
    ...
}
```

This sort of code is awkward to write, and is almost always a bad sign.
Instead of this sort of loop, we strongly prefer to use a predicate
function (which may be `static`) that uses early exits to compute the
predicate.
We prefer the code to be structured like this:

```c++
/* Helper function: returns true if the specified list has an element that is
 * a foo. */
static bool contains_foo(const std::vector<Bar*> &list) {
    for (unsigned ix = 0, len = list.size(); ix != len; ++ix)
        if (list[ix]->is_foo())
            return true;
    return false;
}
...

if (contains_foo(bar_list)) {
    ...
}
```

There are many reasons for doing this: it reduces indentation and
factors out code which can often be shared by other code that checks for
the same predicate.
More importantly, it *forces you to pick a name* for the function, and
forces you to write a comment for it.
In this silly example, this doesn't add much value.
However, if the condition is complex, this can make it a lot easier for
the reader to understand the code that queries for this predicate.
Instead of being faced with the in-line details of how we check to see
if the `bar_list` contains a foo, we can trust the function name and
continue reading with better locality.

### The Low-Level Issues

#### Name Types, Functions, Variables, and Enumerators Properly

Poorly-chosen names can mislead the reader and cause bugs.
We cannot stress enough how important it is to use *descriptive* names.
Pick names that match the semantics and role of the underlying entities,
within reason.
Avoid abbreviations unless they are well known.
After picking a good name, make sure to use consistent capitalization
for the name, as inconsistency requires clients to either memorize the
APIs or to look it up to find the exact spelling.

Different kinds of declarations have different rules:

* **Type names** (including classes, structs, enums, typedefs, etc.)
  should be nouns and should be named in camel case, starting with an
  upper-case letter (e.g. `ObjectInstance`).

* **Variable names** should be nouns (as they represent state).
  The name should be snake case (e.g. `count` or `new_param`).
  Private member variables should start with `m_` to distinguish them
  from local variables representing the same thing.

* **Function names** should be verb phrases (as they represent actions),
  and command-like function should be imperative.
  The name should be snake case (e.g. `open_file()` or `is_foo()`).

* **Enum declarations** (e.g. `enum Foo {...}`) are types, so they
  should follow the naming conventions for types.
  A common use for enums is as a discriminator for a union, or an
  indicator of a subclass.
  When an enum is used for something like this, it should have a `Kind`
  suffix (e.g. `ValueKind`).

* **Enumerators** (e.g. `enum { Foo, Bar }`) and **public member
  variables** should start with an upper-case letter, just like types.
  Unless the enumerators are defined in their own small namespace or
  inside a class, enumerators should have a prefix corresponding to the
  enum declaration name.
  For example, `enum ValueKind { ... };` may contain enumerators like
  `VK_Argument`, `VK_BasicBlock`, etc.
  Enumerators that are just convenience constants are exempt from the
  requirement for a prefix.
  For instance:

  ```c++
  enum {
      MaxSize = 42,
      Density = 12
  };
  ```

Here are some examples of good and bad names:

```c++
class VehicleMaker {
    ...
    Factory<Tire> m_f;             // Bad -- abbreviation and non-descriptive.
    Factory<Tire> m_factory;       // Better.
    Factory<Tire> m_tire_factory;  // Even better -- if VehicleMaker has more
                                   // than one kind of factories.
};

Vehicle make_vehicle(VehicleType Type) {
    VehicleMaker m;             // Might be OK if having a short life-span.
    Tire tmp1 = m.make_tire();  // Bad -- 'Tmp1' provides no information.
    Light headlight = m.make_light("head");  // Good -- descriptive.
    ...
}
```

#### Assert Liberally

Use the `g_assert()` macro to its fullest.
Check all of your preconditions and assumptions, you never know when a
bug (not necessarily even yours) might be caught early by an assertion,
which reduces debugging time dramatically.

To further assist with debugging, usually you should put some kind of
error message in the assertion statement, which is printed if the
assertion is tripped.
This helps the poor debugger make sense of why an assertion is being
made and enforced, and hopefully what to do about it.
Here is one complete example:

```c++
inline Value* get_operand(unsigned ix) {
    g_assert(ix < operands.size() && "get_operand() out of range!");
    return operands[ix];
}
```

To indicate a piece of code that should not be reached, use
`g_assert_not_reached()`.
When assertions are enabled, this will print the message if it's ever
reached and then exit the program.
When assertions are disabled (i.e. in release builds),
`g_assert_not_reached()` becomes a hint to compilers to skip generating
code for this branch.
If the compiler does not support this, it will fall back to the
`abort()` implementation.

Neither assertions or `g_assert_not_reached()` will abort the program on
a release build.
If the error condition can be triggered by user input then the
recoverable error mechanism of `GError*` should be used instead.
In cases where this is not practical, either use `g_critical()` and
continue execution as best as possible, or use `g_error()` to abort with
a fatal error.

For this reason, don't use `g_assert()` or `g_assert_not_reached()` in unit tests!
Otherwise the tests will crash in a release build.
In unit tests, use `g_assert_true()`, `g_assert_false()`, `g_assert_cmpint()`, etc.
Likewise, don't use these unit test assertions in the main code!

Another issue is that values used only by assertions will produce an
"unused value" warning when assertions are disabled.
For example, this code will warn:

```c++
unsigned size = v.size();
g_assert(size > 42 && "Vector smaller than it should be");

bool new_to_set = my_set.insert(value);
g_assert(new_to_set && "The value shouldn't be in the set yet");
```

These are two interesting different cases.
In the first case, the call to `v.size()` is only useful for the assert,
and we don't want it executed when assertions are disabled.
Code like this should move the call into the assert itself.
In the second case, the side effects of the call must happen whether the
assert is enabled or not.
In this case, the value should be cast to void to disable the warning.
To be specific, it is preferred to write the code like this:

```c++
g_assert(v.size() > 42 && "Vector smaller than it should be");

bool new_to_set = my_set.insert(value);
(void)new_to_set;
g_assert(new_to_set && "The value shouldn't be in the set yet");
```

#### Do Not Use `using namespace std`

In GJS, we prefer to explicitly prefix all identifiers from the standard
namespace with an `std::` prefix, rather than rely on `using namespace
std;`.

In header files, adding a `using namespace XXX` directive pollutes the
namespace of any source file that `#include`s the header.
This is clearly a bad thing.

In implementation files (e.g. `.cpp` files), the rule is more of a
stylistic rule, but is still important.
Basically, using explicit namespace prefixes makes the code **clearer**, because it is immediately obvious what facilities are being used and
where they are coming from.
And **more portable**, because namespace clashes cannot occur between
LLVM code and other namespaces.
The portability rule is important because different standard library
implementations expose different symbols (potentially ones they
shouldn't), and future revisions to the C++ standard will add more
symbols to the `std` namespace.
As such, we never use `using namespace std;` in GJS.

The exception to the general rule (i.e. it's not an exception for the
`std` namespace) is for implementation files.
For example, in the future we might decide to put GJS code inside a
`Gjs` namespace.
In that case, it is OK, and actually clearer, for the `.cpp` files to
have a `using namespace Gjs;` directive at the top, after the
`#include`s.
This reduces indentation in the body of the file for source editors that
indent based on braces, and keeps the conceptual context cleaner.
The general form of this rule is that any `.cpp` file that implements
code in any namespace may use that namespace (and its parents'), but
should not use any others.

#### Provide a Virtual Method Anchor for Classes in Headers

If a class is defined in a header file and has a vtable (either it has
virtual methods or it derives from classes with virtual methods), it
must always have at least one out-of-line virtual method in the class.
Without this, the compiler will copy the vtable and RTTI into every `.o`
file that `#include`s the header, bloating `.o` file sizes and
increasing link times.

#### Don't use default labels in fully covered switches over enumerations

`-Wswitch` warns if a switch, without a default label, over an
enumeration, does not cover every enumeration value.
If you write a default label on a fully covered switch over an
enumeration then the `-Wswitch` warning won't fire when new elements are
added to that enumeration.
To help avoid adding these kinds of defaults, Clang has the warning `-Wcovered-switch-default`.

A knock-on effect of this stylistic requirement is that when building
GJS with GCC you may get warnings related to "control may reach end of
non-void function" if you return from each case of a covered
switch-over-enum because GCC assumes that the enum expression may take
any representable value, not just those of individual enumerators.
To suppress this warning, use `g_assert_not_reached()` after the switch.

#### Use range-based `for` loops wherever possible

The introduction of range-based `for` loops in C++11 means that explicit
manipulation of iterators is rarely necessary. We use range-based `for`
loops wherever possible for all newly added code. For example:

```c++
for (GClosure* closure : m_closures)
    ... use closure ...;
```

#### Don't evaluate `end()` every time through a loop

In cases where range-based `for` loops can't be used and it is necessary
to write an explicit iterator-based loop, pay close attention to whether
`end()` is re-evaluted on each loop iteration.
One common mistake is to write a loop in this style:

```c++
for (auto* closure = m_closures->begin(); closure != m_closures->end();
     ++closure)
    ... use closure ...
```

The problem with this construct is that it evaluates `m_closures->end()`
every time through the loop.
Instead of writing the loop like this, we strongly prefer loops to be
written so that they evaluate it once before the loop starts.
A convenient way to do this is like so:

```c++
for (auto* closure = m_closures->begin(), end = m_closures->end();
     closure != end; ++closure)
    ... use closure ...
```

The observant may quickly point out that these two loops may have
different semantics: if the container is being mutated, then
`m_closures->end()` may change its value every time through the loop and
the second loop may not in fact be correct.
If you actually do depend on this behavior, please write the loop in the
first form and add a comment indicating that you did it intentionally.

Why do we prefer the second form (when correct)?
Writing the loop in the first form has two problems.
First it may be less efficient than evaluating it at the start of the
loop.
In this case, the cost is probably minor — a few extra loads every time
through the loop.
However, if the base expression is more complex, then the cost can rise
quickly.
If the end expression was actually something like `some_map[x]->end()`,
map lookups really aren't cheap.
By writing it in the second form consistently, you eliminate the issue
entirely and don't even have to think about it.

The second (even bigger) issue is that writing the loop in the first
form hints to the reader that the loop is mutating the container (which
a comment would handily confirm!)
If you write the loop in the second form, it is immediately obvious
without even looking at the body of the loop that the container isn't
being modified, which makes it easier to read the code and understand
what it does.

While the second form of the loop is a few extra keystrokes, we do
strongly prefer it.

#### Avoid `std::endl`

The `std::endl` modifier, when used with `iostreams`, outputs a newline
to the output stream specified.
In addition to doing this, however, it also flushes the output stream.
In other words, these are equivalent:

```c++
std::cout << std::endl;
std::cout << '\n' << std::flush;
```

Most of the time, you probably have no reason to flush the output
stream, so it's better to use a literal `'\n'`.

#### Don't use `inline` when defining a function in a class definition

A member function defined in a class definition is implicitly inline, so
don't put the `inline` keyword in this case.

Don't:

```c++
class Foo {
   public:
    inline void bar() {
        // ...
    }
};
```

Do:

```c++
class Foo {
   public:
    void bar() {
        // ...
    }
};
```

#### Don't use C++ standard library UTF-8/UTF-16 encoding facilities

There are
[bugs](https://social.msdn.microsoft.com/Forums/en-US/8f40dcd8-c67f-4eba-9134-a19b9178e481/vs-2015-rc-linker-stdcodecvt-error?forum=vcgeneral)
in Visual Studio that make `wstring_convert` non-portable.
Instead, use `g_utf8_to_utf16()` and friends (unfortunately not
typesafe) or `mozilla::ConvertUtf8toUtf16()` and friends (when that
becomes possible; it is currently not possible due to a linker bug.)

