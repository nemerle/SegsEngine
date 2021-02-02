/*************************************************************************/
/*  ref_ptr.h                                                            */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2019 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2019 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#pragma once

/**
    @author Juan Linietsky <reduzio@gmail.com>
 * This class exists to workaround a limitation in C++ but keep the design OK.
 * It's basically an opaque container of a Reference reference, so Variant can use it.
*/

#include "core/godot_export.h"
#include <stdint.h>

class RID;
class GODOT_EXPORT RefPtr {

    mutable intptr_t data; // actual stored value is a Ref<RefCounted> *
public:
    bool is_null() const;
    RefPtr &operator=(const RefPtr &p_other);
    RefPtr &operator=(RefPtr &&p_other) noexcept {
        // Do a swap here and assume p_other will be destroyed by the caller, so that our previous data will be freed
        intptr_t t = data;
        data = p_other.data;
        p_other.data = t;
        return *this;
    }
    bool operator==(const RefPtr &p_other) const noexcept;
    bool operator!=(const RefPtr &p_other) const noexcept { return !(*this==p_other); }
    RID get_rid() const;
    void unref();
    void *get() const { return &data; }
    RefPtr(const RefPtr &p_other);
    RefPtr(RefPtr &&p_other) noexcept : data(p_other.data) { p_other.data = 0; }
    RefPtr() noexcept;
    ~RefPtr();
};
