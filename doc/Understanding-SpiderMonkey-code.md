## Basics

- SpiderMonkey is the Javascript engine from Mozilla Firefox. It's also known as "mozjs" in most Linux distributions, and sometimes as "JSAPI" in code.
- Like most browsers' JS engines, SpiderMonkey works standalone, which is what allows GJS to work. In Mozilla terminology, this is known as "embedding", and GJS is an "embedder."
- Functions that start with `JS_` or `JS::`, or types that start with `JS`, are part of the SpiderMonkey API.
- Functions that start with `js_` or `js::` are part of the "JS Friend" API, which is a section of the SpiderMonkey API which is supposedly less stable. (Although SpiderMonkey isn't API stable in the first place.)
- We use the SpiderMonkey from the ESR (Extended Support Release) of Firefox. These ESRs are released approximately once a year.
- Since ESR 24, the official releases of standalone SpiderMonkey have fallen by the wayside. (Arguably, that was because nobody, including us, was using them.) The SpiderMonkey team may make official releases again sometime, but it's a low priority.
- When reading GJS code, to quickly find out what a SpiderMonkey API function does, you can go to https://searchfox.org/ and search for it. This is literally faster than opening `jsapi.h` in your editor, and you can click through to other functions, and find everywhere a function is used.
- Don't trust the wiki on MDN as documentation for SpiderMonkey, as it is mostly out of date and can be quite misleading.

## Coding conventions

- Most API functions take a `JSContext *` as their first parameter. This context contains the state of the JS engine.
- `cx` stands for "context."
- Many API functions return a `bool`. As in many other APIs, these should return `true` for success and `false` for failure.
- Specific to SpiderMonkey is the convention that if an API function returns `false`, an  exception should have been thrown (a JS exception, not a C++ exception, which would terminate the program!) This is also described as "an exception should be _pending_ on `cx`". Likewise, if the function returns `true`, an exception should not be pending.
- There are two ways to violate that condition:
  - Returning `false` with no exception pending. This will fail assertions in debug builds.
  - Returning `true` while an exception is pending. This can easily happen by forgetting to check the return value of a SpiderMonkey function, and is a programmer error but not too serious. It will probably cause some warnings.
- Likewise if an API function returns a pointer such as `JSObject*` (this is less common), the convention is that it should return `nullptr` on failure, in which case an exception should be pending.
