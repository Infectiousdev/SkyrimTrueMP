#pragma once

// Class boilerplate macros. Use inside a class body: `TP_NOCOPYMOVE(MyClass);`

#define TP_NOCOPY(ClassName)                  \
    ClassName(const ClassName&) = delete;     \
    ClassName& operator=(const ClassName&) = delete

#define TP_NOMOVE(ClassName)                  \
    ClassName(ClassName&&) = delete;          \
    ClassName& operator=(ClassName&&) = delete

#define TP_NOCOPYMOVE(ClassName) \
    TP_NOCOPY(ClassName);        \
    TP_NOMOVE(ClassName)
