#pragma once

// This file contains no-op macros used by the reflection compilaton system to properly process c++ things

// Defines a namespace, all definitions following will be put into this namespace.
// This is active until matching SE_END or end of currently processed file.
#define SE_NAMESPACE(x)
#define SE_CONSTANT(x, ...)
#define SE_ENUM(x, ...)
//
#define SE_CLASS(...)
#define SE_PROPERTY(...)
#define SE_END()
#define SE_INVOCABLE

#define INVOCABLE
