/*************************************************************************/
/*  packed_data_container.h                                              */
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

#include "core/resources_subsystem/resource.h"
#include "core/pool_vector.h"

class PackedDataContainer : public Resource {

    GDCLASS(PackedDataContainer,Resource)

    enum {
        TYPE_DICT = 0xFFFFFFFF,
        TYPE_ARRAY = 0xFFFFFFFE,
    };

    struct DictKey {
        uint32_t hash;
        Variant key;
        bool operator<(const DictKey &p_key) const { return hash < p_key.hash; }
    };

    PoolVector<uint8_t> data;
    int datalen;
public:
    uint32_t _pack(const Variant &p_data, Vector<uint8_t> &tmpdata, DefMap<String, uint32_t> &string_cache);

    Variant _iter_init_ofs(const Array &p_iter, uint32_t p_offset);
    Variant _iter_next_ofs(const Array &p_iter, uint32_t p_offset);
    Variant _iter_get_ofs(const Variant &p_iter, uint32_t p_offset);

    Variant _iter_init(const Array &p_iter);
    Variant _iter_next(const Array &p_iter);
    Variant _iter_get(const Variant &p_iter);

    friend class PackedDataContainerRef;
    Variant _key_at_ofs(uint32_t p_ofs, const Variant &p_key, bool &err) const;
    Variant _get_at_ofs(uint32_t p_ofs, const uint8_t *p_buf, bool &err) const;
    uint32_t _type_at_ofs(uint32_t p_ofs) const;
    int _size(uint32_t p_ofs) const;

    void _set_data(const PoolVector<uint8_t> &p_data);
    PoolVector<uint8_t> _get_data() const;
protected:
    static void _bind_methods();

public:
    Variant getvar(const Variant &p_key, bool *r_valid = nullptr) const override;
    Error pack(const Variant &p_data);

    int size() const;

    PackedDataContainer();
};

class PackedDataContainerRef : public RefCounted {
    GDCLASS(PackedDataContainerRef,RefCounted)

    friend class PackedDataContainer;
    Ref<PackedDataContainer> from;
    uint32_t offset;

protected:
    static void _bind_methods();

public:
    Variant _iter_init(const Array &p_iter);
    Variant _iter_next(const Array &p_iter);
    Variant _iter_get(const Variant &p_iter);
    bool _is_dictionary() const;

    int size() const;
    Variant getvar(const Variant &p_key, bool *r_valid = nullptr) const override;

    PackedDataContainerRef() {}
};
