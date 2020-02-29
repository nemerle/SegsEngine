/*************************************************************************/
/*  packed_scene.h                                                       */
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

#include "core/resource.h"
#include "core/string.h"
#include "core/map.h"
#include "core/hash_map.h"
#include "scene/main/node.h"

class PackedScene;
enum PackedGenEditState : uint8_t {
    GEN_EDIT_STATE_DISABLED,
    GEN_EDIT_STATE_INSTANCE,
    GEN_EDIT_STATE_MAIN,
};
class GODOT_EXPORT SceneState : public RefCounted {

    GDCLASS(SceneState,RefCounted)

    Vector<StringName> names;
    Vector<Variant> variants;
    Vector<NodePath> node_paths;
    Vector<NodePath> editable_instances;
    mutable HashMap<NodePath, int> node_path_cache;
    mutable Map<int, int> base_scene_node_remap;

    int base_scene_idx;

    enum {
        NO_PARENT_SAVED = 0x7FFFFFFF,
        NAME_INDEX_BITS = 18,
        NAME_MASK = (1 << NAME_INDEX_BITS) - 1,
    };

    struct NodeData {

        int parent;
        int owner;
        int type;
        int name;
        int instance;
        int index;

        struct Property {

            int name;
            int value;
        };

        Vector<Property> properties;
        Vector<int> groups;
    };

    struct PackState {
        Ref<SceneState> state;
        int node=-1;
    };

    Vector<NodeData> nodes;

    struct ConnectionData {

        int from;
        int to;
        int signal;
        int method;
        int flags;
        Vector<int> bind_indices;
    };

    Vector<ConnectionData> connections;

    Error _parse_node(Node *p_owner, Node *p_node, int p_parent_idx, Map<StringName, int> &name_map, HashMap<Variant, int, Hasher<Variant>, VariantComparator> &variant_map, HashMap<Node *, int> &node_map, HashMap<Node *, int> &nodepath_map);
    Error _parse_connections(Node *p_owner, Node *p_node, Map<StringName, int> &name_map, HashMap<Variant, int, Hasher<Variant>, VariantComparator> &variant_map, HashMap<Node *, int> &node_map, HashMap<Node *, int> &nodepath_map);

    String path;

    uint64_t last_modified_time;

    _FORCE_INLINE_ Ref<SceneState> _get_base_scene_state() const;

    static bool disable_placeholders;
public:
    enum {
        FLAG_ID_IS_PATH = (1 << 30),
        TYPE_INSTANCED = 0x7FFFFFFF,
        FLAG_INSTANCE_IS_PLACEHOLDER = (1 << 30),
        FLAG_MASK = (1 << 24) - 1,
    };

    PoolVector<String> _get_node_groups(int p_idx) const;

    int _find_base_scene_node_remap_key(int p_idx) const;

protected:
    static void _bind_methods();
    bool handleProperties(PackedGenEditState p_edit_state, Node* node, Span<Node*> ret_nodes,const NodeData& n, Map<Ref<Resource>, Ref<Resource> >& resources_local_to_scene) const;
    void handleConnections(int nc, Span<Node*> ret_nodes) const;

public:


    static void set_disable_placeholders(bool p_disable);

    int find_node_by_path(const NodePath &p_node) const;
    Variant get_property_value(int p_node, const StringName &p_property, bool &found) const;
    bool is_node_in_group(int p_node, const StringName &p_group) const;
    bool is_connection(int p_node, const StringName &p_signal, int p_to_node, const StringName &p_to_method) const;

    void set_bundled_scene(const Dictionary &p_dictionary);
    Dictionary get_bundled_scene() const;

    Error pack(Node *p_scene);

    void set_path(StringView p_path);
    const String &get_path() const;

    void clear();

    bool can_instance() const;
    Node *instance(PackedGenEditState p_edit_state) const;

    //unbuild API

    int get_node_count() const;
    StringName get_node_type(int p_idx) const;
    StringName get_node_name(int p_idx) const;
    NodePath get_node_path(int p_idx, bool p_for_parent = false) const;
    NodePath get_node_owner_path(int p_idx) const;
    Ref<PackedScene> get_node_instance(int p_idx) const;
    String get_node_instance_placeholder(int p_idx) const;
    bool is_node_instance_placeholder(int p_idx) const;
    Vector<StringName> get_node_groups(int p_idx) const;
    int get_node_index(int p_idx) const;

    int get_node_property_count(int p_idx) const;
    StringName get_node_property_name(int p_idx, int p_prop) const;
    Variant get_node_property_value(int p_idx, int p_prop) const;

    int get_connection_count() const;
    NodePath get_connection_source(int p_idx) const;
    StringName get_connection_signal(int p_idx) const;
    NodePath get_connection_target(int p_idx) const;
    StringName get_connection_method(int p_idx) const;
    int get_connection_flags(int p_idx) const;
    Array get_connection_binds(int p_idx) const;

    bool has_connection(const NodePath &p_node_from, const StringName &p_signal, const NodePath &p_node_to, const StringName &p_method);

    const Vector<NodePath> &get_editable_instances() const;

    //build API

    int add_name(const StringName &p_name);
    int find_name(const StringName &p_name) const;
    int add_value(const Variant &p_value);
    int add_node_path(const NodePath &p_path);
    int add_node(int p_parent, int p_owner, int p_type, int p_name, int p_instance, int p_index);
    void add_node_property(int p_node, int p_name, int p_value);
    void add_node_group(int p_node, int p_group);
    void set_base_scene(int p_idx);
    void add_connection(int p_from, int p_to, int p_signal, int p_method, int p_flags, Vector<int> &&p_binds);
    void add_editable_instance(const NodePath &p_path);

    virtual void set_last_modified_time(uint64_t p_time) { last_modified_time = p_time; }
    uint64_t get_last_modified_time() const { return last_modified_time; }

    SceneState();
};


class GODOT_EXPORT PackedScene : public Resource {

    GDCLASS(PackedScene,Resource)

    RES_BASE_EXTENSION("scn")

    Ref<SceneState> state;
public:
    void _set_bundled_scene(const Dictionary &p_scene);
    Dictionary _get_bundled_scene() const;

protected:
    bool editor_can_reload_from_file() override { return false; } // this is handled by editor better
    static void _bind_methods();

public:


    Error pack(Node *p_scene);

    void clear();

    bool can_instance() const;
    Node *instance(PackedGenEditState p_edit_state = GEN_EDIT_STATE_DISABLED) const;

    void recreate_state();
    void replace_state(Ref<SceneState> p_by);

    void set_path(const ResourcePath &p_path, bool p_take_over = false) override;
#ifdef TOOLS_ENABLED
    void set_last_modified_time(uint64_t p_time) override { state->set_last_modified_time(p_time); }

#endif
    const Ref<SceneState> &get_state() const { return state; }

    PackedScene();
};

