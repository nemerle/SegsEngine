#pragma once

#include "core/reflection_macros.h"
#include "core/object.h"
#include "core/reference.h"

#include <stdint.h>

class Propertied {
    SE_CLASS()
    SE_PROPERTY(String label READ get_label WRITE set_label)
};
