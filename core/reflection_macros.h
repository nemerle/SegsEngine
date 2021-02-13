#pragma once

// This file contains no-op macros used by the reflection compilaton system to properly process c++ things

// Defines a namespace, all definitions following will be put into this namespace.
// This is active until matching SE_END or end of currently processed file.
#define SE_NAMESPACE(x)
#define SE_CONSTANT(x, ...)
#define SE_ENUM(x, ...)
//
#define SE_CLASS(...)
/**
    similar syntax to Q_PROPERTY
    (type name
           (READ getFunction [WRITE setFunction] |
            MEMBER memberName [(READ getFunction | WRITE setFunction)])
           [RESET resetFunction]
           [NOTIFY notifySignal]
           [USAGE STORAGE|...] // any of PropertyUsageFlags without the leading PROPERTY_USAGE_
           [HINT "text"]
    )
)
*/
#define SE_PROPERTY(...)
#define SE_END()

#define SE_INVOCABLE
#define SE_SIGNAL
//TODO: valid until next access specifier (public:/private:/protected:) or end of class.
//#define SE_SIGNALS
