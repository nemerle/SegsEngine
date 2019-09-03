/*************************************************************************/
/*  string_name.cpp                                                      */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2019 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2019 Godot Engine contributors (cf. AUTHORS.md)    */
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

#include "string_name.h"

#include "core/os/os.h"
#include "core/print_string.h"
#include "core/ustring.h"

namespace
{
template <typename L, typename R>
_FORCE_INLINE_ bool is_str_less(const L *l_ptr, const R *r_ptr) {

    while (true) {

        if (*l_ptr == 0 && *r_ptr == 0)
            return false;
        else if (*l_ptr == 0)
            return true;
        else if (*r_ptr == 0)
            return false;
        else if (*l_ptr < *r_ptr)
            return true;
        else if (*l_ptr > *r_ptr)
            return false;

        l_ptr++;
        r_ptr++;
    }
}
} // end of anonymous namespace

struct StringName::_Data {
    SafeRefCount refcount;
    const char *cname;
    String name;

    String get_name() const { return cname ? String(cname) : name; }
    int idx;
    uint32_t hash;
    _Data *prev;
    _Data *next;
    _Data() {
        cname = nullptr;
        next = prev = nullptr;
        idx = 0;
        hash = 0;
    }
};

StringName::_Data *StringName::_table[STRING_TABLE_LEN];
bool StringName::configured = false;
Mutex *StringName::lock = nullptr;


void StringName::setup() {

    lock = Mutex::create();

    ERR_FAIL_COND(configured)
    for (int i = 0; i < STRING_TABLE_LEN; i++) {

        _table[i] = nullptr;
    }
    configured = true;
}

void StringName::cleanup() {

    lock->lock();

    int lost_strings = 0;
    for (int i = 0; i < STRING_TABLE_LEN; i++) {

        while (_table[i]) {

            _Data *d = _table[i];
            lost_strings++;
            if (OS::get_singleton()->is_stdout_verbose()) {
                if (d->cname) {
                    print_line("Orphan StringName: " + String(d->cname));
                } else {
                    print_line("Orphan StringName: " + String(d->name));
                }
            }

            _table[i] = _table[i]->next;
            delete d;
        }
    }
    if (lost_strings) {
        print_verbose("StringName: " + itos(lost_strings) + " unclaimed string names at exit.");
    }
    lock->unlock();

    memdelete(lock);
}

void StringName::unref() {

    ERR_FAIL_COND(!configured)

    if (_data && _data->refcount.unref()) {

        lock->lock();

        if (_data->prev) {
            _data->prev->next = _data->next;
        } else {
            if (_table[_data->idx] != _data) {
                ERR_PRINT("BUG!")
            }
            _table[_data->idx] = _data->next;
        }

        if (_data->next) {
            _data->next->prev = _data->prev;
        }
        delete _data;
        lock->unlock();
    }

    _data = nullptr;
}

StringName::operator const void*() const {
    return (_data && (_data->cname || !_data->name.empty())) ? (void *)1 : nullptr;
}

bool StringName::operator==(const QString &p_name) const {

    if (!_data) {

        return (p_name.length() == 0);
    }

    return (_data->get_name() == p_name);
}

bool StringName::operator==(const char *p_name) const {

    if (!_data) {

        return (p_name[0] == 0);
    }

    return (_data->get_name() == p_name);
}

bool StringName::operator!=(const QString &p_name) const {

    return !(operator==(p_name));
}

uint32_t StringName::hash() const {

    return _data ? _data->hash : 0;
}

bool StringName::operator!=(const StringName &p_name) const {

    // the real magic of all this mess happens here.
    // this is why path comparisons are very fast
    return _data != p_name._data;
}

StringName &StringName::operator=(StringName &&p_name)
{
    if(this==&p_name)
        return *this;
    unref();
    _data = p_name._data;
    p_name._data = nullptr;
    return *this;
}

StringName::operator String() const {

    if (!_data)
        return String();

    if (_data->cname)
        return String(_data->cname);

    return _data->name;

}

String StringName::asString() const { return (String)*this; }

StringName &StringName::operator=(const StringName &p_name) {

    if (this == &p_name)
        return *this;

    unref();

    if (p_name._data && p_name._data->refcount.ref()) {

        _data = p_name._data;
    }
    return *this;
}

StringName::StringName(const StringName &p_name) {

    _data = nullptr;

    ERR_FAIL_COND(!configured)

    if (p_name._data && p_name._data->refcount.ref()) {
        _data = p_name._data;
    }
}

StringName::StringName(StringName &&p_name)
{
    _data = p_name._data;
    p_name._data = nullptr;
}

StringName::StringName(const char *p_name) {

    _data = nullptr;

    ERR_FAIL_COND(!configured)

    if (!p_name || p_name[0] == 0)
        return; //empty, ignore

    lock->lock();

    uint32_t hash = StringUtils::hash(p_name);

    uint32_t idx = hash & STRING_TABLE_MASK;

    _data = _table[idx];

    while (_data) {

        // compare hash first
        if (_data->hash == hash && _data->get_name() == p_name)
            break;
        _data = _data->next;
    }

    if (_data) {
        if (_data->refcount.ref()) {
            // exists
            lock->unlock();
            return;
        }
    }

    _data = new _Data;
    _data->refcount.init();
    _data->cname = nullptr;
    _data->name = p_name;
    _data->idx = idx;
    _data->hash = hash;
    _data->next = _table[idx];
    _data->prev = nullptr;
    if (_table[idx])
        _table[idx]->prev = _data;
    _table[idx] = _data;

    lock->unlock();
}

void StringName::setupFromCString(const StaticCString &p_static_string) {
    lock->lock();

    uint32_t hash = StringUtils::hash(p_static_string.ptr);

    uint32_t idx = hash & STRING_TABLE_MASK;

    _data = _table[idx];

    while (_data) {

        // compare hash first
        if (_data->hash == hash && _data->get_name() == p_static_string.ptr)
            break;
        _data = _data->next;
    }

    if (_data) {
        if (_data->refcount.ref()) {
            // exists
            lock->unlock();
            return;
        }
    }

    _data = new _Data;

    _data->refcount.init();
    _data->cname = p_static_string.ptr;
    _data->idx = idx;
    _data->hash = hash;
    _data->next = _table[idx];
    _data->prev = nullptr;
    if (_table[idx])
        _table[idx]->prev = _data;
    _table[idx] = _data;

    lock->unlock();
}

StringName::StringName(const String &p_name) {

    _data = nullptr;

    ERR_FAIL_COND(!configured)

    if (p_name.empty())
        return;

    lock->lock();

    uint32_t hash = StringUtils::hash(p_name);

    uint32_t idx = hash & STRING_TABLE_MASK;

    _data = _table[idx];

    while (_data) {

        if (_data->hash == hash && _data->get_name() == p_name)
            break;
        _data = _data->next;
    }

    if (_data) {
        if (_data->refcount.ref()) {
            // exists
            lock->unlock();
            return;
        }
    }

    _data = new _Data;
    _data->name = p_name;
    _data->refcount.init();
    _data->hash = hash;
    _data->idx = idx;
    _data->cname = nullptr;
    _data->next = _table[idx];
    _data->prev = nullptr;
    if (_table[idx])
        _table[idx]->prev = _data;
    _table[idx] = _data;

    lock->unlock();
}

StringName StringName::search(const char *p_name) {

    ERR_FAIL_COND_V(!configured, StringName())

    ERR_FAIL_COND_V(!p_name, StringName())
    if (!p_name[0])
        return StringName();

    lock->lock();

    uint32_t hash = StringUtils::hash(p_name);

    uint32_t idx = hash & STRING_TABLE_MASK;

    _Data *_data = _table[idx];

    while (_data) {

        // compare hash first
        if (_data->hash == hash && _data->get_name() == p_name)
            break;
        _data = _data->next;
    }

    if (_data && _data->refcount.ref()) {
        lock->unlock();

        return StringName(_data);
    }

    lock->unlock();
    return StringName(); //does not exist
}

StringName StringName::search(const CharType *p_name) {

    ERR_FAIL_COND_V(!configured, StringName())

    ERR_FAIL_COND_V(!p_name, StringName())
    if (QChar(0)==p_name[0])
        return StringName();

    lock->lock();

    uint32_t hash = StringUtils::hash(p_name);

    uint32_t idx = hash & STRING_TABLE_MASK;

    _Data *_data = _table[idx];

    while (_data) {

        // compare hash first
        if (_data->hash == hash && _data->get_name() == p_name)
            break;
        _data = _data->next;
    }

    if (_data && _data->refcount.ref()) {
        lock->unlock();
        return StringName(_data);
    }

    lock->unlock();
    return StringName(); //does not exist
}
StringName StringName::search(const String &p_name) {

    ERR_FAIL_COND_V(p_name == "", StringName())

    lock->lock();

    uint32_t hash = StringUtils::hash(p_name);

    uint32_t idx = hash & STRING_TABLE_MASK;

    _Data *_data = _table[idx];

    while (_data) {

        // compare hash first
        if (_data->hash == hash && p_name == _data->get_name())
            break;
        _data = _data->next;
    }

    if (_data && _data->refcount.ref()) {
        lock->unlock();
        return StringName(_data);
    }

    lock->unlock();
    return StringName(); //does not exist
}



StringName::~StringName() {

    unref();
}

bool StringName::AlphCompare(const StringName &l, const StringName &r) {

    const char *l_cname = l._data ? l._data->cname : "";
    const char *r_cname = r._data ? r._data->cname : "";

    if (l_cname) {

        if (r_cname)
            return is_str_less(l_cname, r_cname);
        else
            return is_str_less(l_cname, r._data->name.cdata());
    } else {

        if (r_cname)
            return is_str_less(l._data->name.cdata(), r_cname);
        else
            return is_str_less(l._data->name.cdata(), r._data->name.cdata());
    }
}
