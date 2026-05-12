// XAtlasCompile.cpp — Compiles xatlas as part of the ViewGen module.
// xatlas is MIT licensed: see ThirdParty/xatlas/LICENSE

// Suppress UE warnings that xatlas triggers
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4018) // signed/unsigned mismatch
#pragma warning(disable: 4100) // unreferenced formal parameter
#pragma warning(disable: 4127) // conditional expression is constant
#pragma warning(disable: 4189) // local variable is initialized but not referenced
#pragma warning(disable: 4244) // conversion from 'double' to 'float'
#pragma warning(disable: 4267) // conversion from 'size_t' to 'uint32_t'
#pragma warning(disable: 4456) // declaration hides previous local declaration
#pragma warning(disable: 4457) // declaration hides function parameter
#pragma warning(disable: 4701) // potentially uninitialized local variable
#pragma warning(disable: 4702) // unreachable code
#endif

#include "xatlas.inl"

#ifdef _MSC_VER
#pragma warning(pop)
#endif
