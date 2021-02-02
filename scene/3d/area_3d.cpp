/*************************************************************************/
/*  area_3d.cpp                                                          */
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

#include "area_3d.h"

#include "core/object_db.h"
#include "core/callable_method_pointer.h"
#include "core/method_bind.h"
#include "scene/scene_string_names.h"
#include "servers/audio_server.h"
#include "servers/physics_server_3d.h"

IMPL_GDCLASS(Area3D)
VARIANT_ENUM_CAST(Area3D::SpaceOverride);

void Area3D::set_space_override_mode(SpaceOverride p_mode) {

    space_override = p_mode;
    PhysicsServer3D::get_singleton()->area_set_space_override_mode(get_rid(), PhysicsServer3D::AreaSpaceOverrideMode(p_mode));
}
Area3D::SpaceOverride Area3D::get_space_override_mode() const {

    return space_override;
}

void Area3D::set_gravity_is_point(bool p_enabled) {

    gravity_is_point = p_enabled;
    PhysicsServer3D::get_singleton()->area_set_param(get_rid(), PhysicsServer3D::AREA_PARAM_GRAVITY_IS_POINT, p_enabled);
}
bool Area3D::is_gravity_a_point() const {

    return gravity_is_point;
}

void Area3D::set_gravity_distance_scale(real_t p_scale) {

    gravity_distance_scale = p_scale;
    PhysicsServer3D::get_singleton()->area_set_param(get_rid(), PhysicsServer3D::AREA_PARAM_GRAVITY_DISTANCE_SCALE, p_scale);
}

real_t Area3D::get_gravity_distance_scale() const {
    return gravity_distance_scale;
}

void Area3D::set_gravity_vector(const Vector3 &p_vec) {

    gravity_vec = p_vec;
    PhysicsServer3D::get_singleton()->area_set_param(get_rid(), PhysicsServer3D::AREA_PARAM_GRAVITY_VECTOR, p_vec);
}
Vector3 Area3D::get_gravity_vector() const {

    return gravity_vec;
}

void Area3D::set_gravity(real_t p_gravity) {

    gravity = p_gravity;
    PhysicsServer3D::get_singleton()->area_set_param(get_rid(), PhysicsServer3D::AREA_PARAM_GRAVITY, p_gravity);
}
real_t Area3D::get_gravity() const {

    return gravity;
}
void Area3D::set_linear_damp(real_t p_linear_damp) {

    linear_damp = p_linear_damp;
    PhysicsServer3D::get_singleton()->area_set_param(get_rid(), PhysicsServer3D::AREA_PARAM_LINEAR_DAMP, p_linear_damp);
}
real_t Area3D::get_linear_damp() const {

    return linear_damp;
}

void Area3D::set_angular_damp(real_t p_angular_damp) {

    angular_damp = p_angular_damp;
    PhysicsServer3D::get_singleton()->area_set_param(get_rid(), PhysicsServer3D::AREA_PARAM_ANGULAR_DAMP, p_angular_damp);
}

real_t Area3D::get_angular_damp() const {

    return angular_damp;
}

void Area3D::set_priority(real_t p_priority) {

    priority = p_priority;
    PhysicsServer3D::get_singleton()->area_set_param(get_rid(), PhysicsServer3D::AREA_PARAM_PRIORITY, p_priority);
}
real_t Area3D::get_priority() const {

    return priority;
}

void Area3D::_body_enter_tree(ObjectID p_id) {

    Object *obj = ObjectDB::get_instance(p_id);
    Node *node = object_cast<Node>(obj);
    ERR_FAIL_COND(!node);

    auto E = body_map.find(p_id);
    ERR_FAIL_COND(E==body_map.end());
    ERR_FAIL_COND(E->second.in_tree);

    E->second.in_tree = true;
    emit_signal(SceneStringNames::body_entered,  Variant(node));
    for (size_t i = 0; i < E->second.shapes.size(); i++) {

        emit_signal(SceneStringNames::body_shape_entered, Variant::from(p_id), Variant(node), E->second.shapes[i].body_shape, E->second.shapes[i].area_shape);
    }
}

void Area3D::_body_exit_tree(ObjectID p_id) {

    Object *obj = ObjectDB::get_instance(p_id);
    Node *node = object_cast<Node>(obj);
    ERR_FAIL_COND(!node);
    auto E = body_map.find(p_id);
    ERR_FAIL_COND(E==body_map.end());
    ERR_FAIL_COND(!E->second.in_tree);
    E->second.in_tree = false;
    emit_signal(SceneStringNames::body_exited, Variant(node));
    for (size_t i = 0; i < E->second.shapes.size(); i++) {

        emit_signal(SceneStringNames::body_shape_exited, Variant::from(p_id), Variant(node), E->second.shapes[i].body_shape, E->second.shapes[i].area_shape);
    }
}

void Area3D::_body_inout(int p_status, const RID &p_body, ObjectID p_instance, int p_body_shape, int p_area_shape) {

    bool body_in = p_status == PhysicsServer3D::AREA_BODY_ADDED;
    ObjectID objid = p_instance;

    Object *obj = ObjectDB::get_instance(objid);
    Node *node = object_cast<Node>(obj);

    auto E = body_map.find(objid);

    if (!body_in && E==body_map.end()) {
        return; //likely removed from the tree
    }

    locked = true;

    if (body_in) {
        if (E==body_map.end()) {

            E = body_map.emplace(objid, BodyState()).first;
            E->second.rc = 0;
            E->second.in_tree = node && node->is_inside_tree();
            if (node) {
                node->connect(SceneStringNames::tree_entered,
                              callable_mp(this, &Area3D::_body_enter_tree), make_binds(objid));
                node->connect(SceneStringNames::tree_exiting,
                              callable_mp(this, &Area3D::_body_exit_tree), make_binds(objid));
                if (E->second.in_tree) {
                    emit_signal(SceneStringNames::body_entered, Variant(node));
                }
            }
        }
        E->second.rc++;
        if (node)
            E->second.shapes.emplace(p_body_shape, p_area_shape);

        if (E->second.in_tree) {
            emit_signal(SceneStringNames::body_shape_entered, Variant::from(objid), Variant(node), p_body_shape, p_area_shape);
        }

    } else {

        E->second.rc--;

        if (node)
            E->second.shapes.erase(ShapePair(p_body_shape, p_area_shape));

        bool in_tree = E->second.in_tree;
        if (E->second.rc == 0) {
            body_map.erase(E);
            if (node) {
                node->disconnect(SceneStringNames::tree_entered, callable_mp(this, &Area3D::_body_enter_tree));
                node->disconnect(SceneStringNames::tree_exiting, callable_mp(this, &Area3D::_body_exit_tree));

                if (in_tree)
                    emit_signal(SceneStringNames::body_exited, Variant(obj));
            }
        }
        if (node && in_tree) {
            emit_signal(SceneStringNames::body_shape_exited, Variant::from(objid), Variant(obj), Variant(p_body_shape),
                    Variant(p_area_shape));
        }
    }

    locked = false;
}

void Area3D::_clear_monitoring() {

    ERR_FAIL_COND_MSG(locked, "This function can't be used during the in/out signal.");

    {
        auto bmcopy = eastl::move(body_map); // move map into temporary
        body_map.clear(); // clear the moved-from
        //disconnect all monitored stuff

        for (eastl::pair<const ObjectID,BodyState> &E : bmcopy) {

            Object *obj = ObjectDB::get_instance(E.first);
            Node *node = object_cast<Node>(obj);

            if (!node) //node may have been deleted in previous frame or at other legitimate point
                continue;
            //ERR_CONTINUE(!node);

            if (!E.second.in_tree)
                continue;

            for (size_t i = 0; i < E.second.shapes.size(); i++) {

                emit_signal(SceneStringNames::body_shape_exited, Variant::from(E.first), Variant(node), E.second.shapes[i].body_shape, E.second.shapes[i].area_shape);
            }

            emit_signal(SceneStringNames::body_exited, Variant(node));

            node->disconnect(SceneStringNames::tree_entered, callable_mp(this, &Area3D::_body_enter_tree));
            node->disconnect(SceneStringNames::tree_exiting, callable_mp(this, &Area3D::_body_exit_tree));
        }
    }

    {

        HashMap<ObjectID, AreaState> bmcopy = eastl::move(area_map); //move to prevent allocation here
        area_map.clear();
        //disconnect all monitored stuff

        for (eastl::pair<const ObjectID,AreaState> &E : bmcopy) {

            Object *obj = ObjectDB::get_instance(E.first);
            Node *node = object_cast<Node>(obj);

            if (!node) //node may have been deleted in previous frame or at other legiminate point
                continue;
            //ERR_CONTINUE(!node);

            if (!E.second.in_tree)
                continue;

            for (size_t i = 0; i < E.second.shapes.size(); i++) {

                emit_signal(SceneStringNames::area_shape_exited, Variant::from(E.first), Variant(node), E.second.shapes[i].area_shape, E.second.shapes[i].self_shape);
            }

            emit_signal(SceneStringNames::area_exited, Variant(obj));

            node->disconnect(SceneStringNames::tree_entered, callable_mp(this, &Area3D::_body_enter_tree));
            node->disconnect(SceneStringNames::tree_exiting, callable_mp(this, &Area3D::_body_exit_tree));
        }
    }
}
void Area3D::_notification(int p_what) {

    if (p_what == NOTIFICATION_EXIT_TREE) {
        _clear_monitoring();
    }
}

void Area3D::set_monitoring(bool p_enable) {

    ERR_FAIL_COND_MSG(locked, "Function blocked during in/out signal. Use set_deferred(\"monitoring\", true/false).");

    if (p_enable == monitoring)
        return;

    monitoring = p_enable;

    if (monitoring) {

        PhysicsServer3D::get_singleton()->area_set_monitor_callback(get_rid(), callable_mp(this, &Area3D::_body_inout));
        PhysicsServer3D::get_singleton()->area_set_area_monitor_callback(get_rid(), callable_mp(this, &Area3D::_area_inout));
    } else {
        PhysicsServer3D::get_singleton()->area_set_monitor_callback(get_rid(), Callable());
        PhysicsServer3D::get_singleton()->area_set_area_monitor_callback(get_rid(), Callable());
        _clear_monitoring();
    }
}

void Area3D::_area_enter_tree(ObjectID p_id) {

    Object *obj = ObjectDB::get_instance(p_id);
    Node *node = object_cast<Node>(obj);
    ERR_FAIL_COND(!node);

    HashMap<ObjectID, AreaState>::iterator E = area_map.find(p_id);
    ERR_FAIL_COND(E==area_map.end());
    ERR_FAIL_COND(E->second.in_tree);

    E->second.in_tree = true;
    emit_signal(SceneStringNames::area_entered, Variant(node));
    for (size_t i = 0; i < E->second.shapes.size(); i++) {

        emit_signal(SceneStringNames::area_shape_entered, Variant::from(p_id), Variant(node), E->second.shapes[i].area_shape, E->second.shapes[i].self_shape);
    }
}

void Area3D::_area_exit_tree(ObjectID p_id) {

    Object *obj = ObjectDB::get_instance(p_id);
    Node *node = object_cast<Node>(obj);
    ERR_FAIL_COND(!node);
    HashMap<ObjectID, AreaState>::iterator E = area_map.find(p_id);
    ERR_FAIL_COND(E==area_map.end());
    ERR_FAIL_COND(!E->second.in_tree);
    E->second.in_tree = false;
    emit_signal(SceneStringNames::area_exited, Variant(node));
    for (size_t i = 0; i < E->second.shapes.size(); i++) {

        emit_signal(SceneStringNames::area_shape_exited, Variant::from(p_id), Variant(node), E->second.shapes[i].area_shape, E->second.shapes[i].self_shape);
    }
}

void Area3D::_area_inout(int p_status, const RID &p_area, ObjectID p_instance, int p_area_shape, int p_self_shape) {

    bool area_in = p_status == PhysicsServer3D::AREA_BODY_ADDED;
    ObjectID objid = p_instance;

    Object *obj = ObjectDB::get_instance(objid);
    Node *node = object_cast<Node>(obj);

    HashMap<ObjectID, AreaState>::iterator E = area_map.find(objid);

    if (!area_in && E==area_map.end()) {
        return; //likely removed from the tree
    }

    locked = true;

    if (area_in) {
        if (E==area_map.end()) {

            E = area_map.emplace(objid, AreaState()).first;
            E->second.rc = 0;
            E->second.in_tree = node && node->is_inside_tree();
            if (node) {
                node->connect(SceneStringNames::tree_entered, callable_mp(this, &Area3D::_area_enter_tree), make_binds(objid));
                node->connect(SceneStringNames::tree_exiting, callable_mp(this, &Area3D::_area_exit_tree), make_binds(objid));
                if (E->second.in_tree) {
                    emit_signal(SceneStringNames::area_entered, Variant(node));
                }
            }
        }
        E->second.rc++;
        if (node)
            E->second.shapes.insert(AreaShapePair(p_area_shape, p_self_shape));

        if (!node || E->second.in_tree) {
            emit_signal(SceneStringNames::area_shape_entered, Variant::from(objid), Variant(node), p_area_shape, p_self_shape);
        }

    } else {

        E->second.rc--;

        if (node)
            E->second.shapes.erase(AreaShapePair(p_area_shape, p_self_shape));

        bool in_tree = E->second.in_tree;

        if (E->second.rc == 0) {
            area_map.erase(E);
            if (node) {
                node->disconnect(SceneStringNames::tree_entered, callable_mp(this, &Area3D::_area_enter_tree));
                node->disconnect(SceneStringNames::tree_exiting, callable_mp(this, &Area3D::_area_exit_tree));

                if (in_tree)
                    emit_signal(SceneStringNames::area_exited, Variant(obj));
            }
        }
        if (!node || in_tree) {
            emit_signal(SceneStringNames::area_shape_exited, Variant::from(objid), Variant(obj), Variant(p_area_shape),
                    Variant(p_area_shape));
        }

    }

    locked = false;
}

bool Area3D::is_monitoring() const {

    return monitoring;
}

Array Area3D::get_overlapping_bodies() const {

    ERR_FAIL_COND_V(!monitoring, Array());
    Array ret;
    ret.resize(body_map.size());
    int idx = 0;
    for (const eastl::pair<const ObjectID,BodyState> &E : body_map) {
        Object *obj = ObjectDB::get_instance(E.first);
        if (!obj) {
            ret.resize(ret.size() - 1); //ops
        } else {
            ret[idx++] = Variant(obj);
        }
    }

    return ret;
}

void Area3D::set_monitorable(bool p_enable) {

    ERR_FAIL_COND_MSG(locked || (is_inside_tree() && PhysicsServer3D::get_singleton()->is_flushing_queries()), "Function blocked during in/out signal. Use set_deferred(\"monitorable\", true/false).");

    if (p_enable == monitorable)
        return;

    monitorable = p_enable;

    PhysicsServer3D::get_singleton()->area_set_monitorable(get_rid(), monitorable);
}

bool Area3D::is_monitorable() const {

    return monitorable;
}

Array Area3D::get_overlapping_areas() const {

    ERR_FAIL_COND_V(!monitoring, Array());
    Array ret;
    ret.resize(area_map.size());
    int idx = 0;
    for (const eastl::pair<const ObjectID,AreaState> &E : area_map) {
        Object *obj = ObjectDB::get_instance(E.first);
        if (!obj) {
            ret.resize(ret.size() - 1); //ops
        } else {
            ret[idx++] = Variant(obj);
        }
    }

    return ret;
}

bool Area3D::overlaps_area(Node *p_area) const {

    ERR_FAIL_NULL_V(p_area, false);
    const HashMap<ObjectID, AreaState>::const_iterator E = area_map.find(p_area->get_instance_id());
    if (E==area_map.end())
        return false;
    return E->second.in_tree;
}

bool Area3D::overlaps_body(Node *p_body) const {

    ERR_FAIL_NULL_V(p_body, false);
    const auto E = body_map.find(p_body->get_instance_id());
    if (E==body_map.end())
        return false;
    return E->second.in_tree;
}
void Area3D::set_collision_mask(uint32_t p_mask) {

    collision_mask = p_mask;
    PhysicsServer3D::get_singleton()->area_set_collision_mask(get_rid(), p_mask);
}

uint32_t Area3D::get_collision_mask() const {

    return collision_mask;
}
void Area3D::set_collision_layer(uint32_t p_layer) {

    collision_layer = p_layer;
    PhysicsServer3D::get_singleton()->area_set_collision_layer(get_rid(), p_layer);
}

uint32_t Area3D::get_collision_layer() const {

    return collision_layer;
}

void Area3D::set_collision_mask_bit(int p_bit, bool p_value) {

    uint32_t mask = get_collision_mask();
    if (p_value)
        mask |= 1 << p_bit;
    else
        mask &= ~(1 << p_bit);
    set_collision_mask(mask);
}

bool Area3D::get_collision_mask_bit(int p_bit) const {

    return get_collision_mask() & (1 << p_bit);
}

void Area3D::set_collision_layer_bit(int p_bit, bool p_value) {

    uint32_t layer = get_collision_layer();
    if (p_value)
        layer |= 1 << p_bit;
    else
        layer &= ~(1 << p_bit);
    set_collision_layer(layer);
}

bool Area3D::get_collision_layer_bit(int p_bit) const {

    return get_collision_layer() & (1 << p_bit);
}

void Area3D::set_audio_bus_override(bool p_override) {

    audio_bus_override = p_override;
}

bool Area3D::is_overriding_audio_bus() const {

    return audio_bus_override;
}

void Area3D::set_audio_bus(const StringName &p_audio_bus) {

    audio_bus = p_audio_bus;
}
StringName Area3D::get_audio_bus() const {

    for (int i = 0; i < AudioServer::get_singleton()->get_bus_count(); i++) {
        if (AudioServer::get_singleton()->get_bus_name(i) == audio_bus) {
            return audio_bus;
        }
    }
    return "Master";
}

void Area3D::set_use_reverb_bus(bool p_enable) {

    use_reverb_bus = p_enable;
}
bool Area3D::is_using_reverb_bus() const {

    return use_reverb_bus;
}

void Area3D::set_reverb_bus(const StringName &p_audio_bus) {

    reverb_bus = p_audio_bus;
}
StringName Area3D::get_reverb_bus() const {

    for (int i = 0; i < AudioServer::get_singleton()->get_bus_count(); i++) {
        if (AudioServer::get_singleton()->get_bus_name(i) == reverb_bus) {
            return reverb_bus;
        }
    }
    return "Master";
}

void Area3D::set_reverb_amount(float p_amount) {

    reverb_amount = p_amount;
}
float Area3D::get_reverb_amount() const {

    return reverb_amount;
}

void Area3D::set_reverb_uniformity(float p_uniformity) {

    reverb_uniformity = p_uniformity;
}
float Area3D::get_reverb_uniformity() const {

    return reverb_uniformity;
}

void Area3D::_validate_property(PropertyInfo &property) const {

    if (property.name == "audio_bus_name" || property.name == "reverb_bus_name") {

        String options;
        for (int i = 0; i < AudioServer::get_singleton()->get_bus_count(); i++) {
            if (i > 0)
                options += ',';
            StringName name(AudioServer::get_singleton()->get_bus_name(i));
            options += String(name);
        }

        property.hint_string = options;
    }
}

void Area3D::_bind_methods() {

    MethodBinder::bind_method(D_METHOD("set_space_override_mode", {"enable"}), &Area3D::set_space_override_mode);
    MethodBinder::bind_method(D_METHOD("get_space_override_mode"), &Area3D::get_space_override_mode);

    MethodBinder::bind_method(D_METHOD("set_gravity_is_point", {"enable"}), &Area3D::set_gravity_is_point);
    MethodBinder::bind_method(D_METHOD("is_gravity_a_point"), &Area3D::is_gravity_a_point);

    MethodBinder::bind_method(D_METHOD("set_gravity_distance_scale", {"distance_scale"}), &Area3D::set_gravity_distance_scale);
    MethodBinder::bind_method(D_METHOD("get_gravity_distance_scale"), &Area3D::get_gravity_distance_scale);

    MethodBinder::bind_method(D_METHOD("set_gravity_vector", {"vector"}), &Area3D::set_gravity_vector);
    MethodBinder::bind_method(D_METHOD("get_gravity_vector"), &Area3D::get_gravity_vector);

    MethodBinder::bind_method(D_METHOD("set_gravity", {"gravity"}), &Area3D::set_gravity);
    MethodBinder::bind_method(D_METHOD("get_gravity"), &Area3D::get_gravity);

    MethodBinder::bind_method(D_METHOD("set_angular_damp", {"angular_damp"}), &Area3D::set_angular_damp);
    MethodBinder::bind_method(D_METHOD("get_angular_damp"), &Area3D::get_angular_damp);

    MethodBinder::bind_method(D_METHOD("set_linear_damp", {"linear_damp"}), &Area3D::set_linear_damp);
    MethodBinder::bind_method(D_METHOD("get_linear_damp"), &Area3D::get_linear_damp);

    MethodBinder::bind_method(D_METHOD("set_priority", {"priority"}), &Area3D::set_priority);
    MethodBinder::bind_method(D_METHOD("get_priority"), &Area3D::get_priority);

    MethodBinder::bind_method(D_METHOD("set_collision_mask", {"collision_mask"}), &Area3D::set_collision_mask);
    MethodBinder::bind_method(D_METHOD("get_collision_mask"), &Area3D::get_collision_mask);

    MethodBinder::bind_method(D_METHOD("set_collision_layer", {"collision_layer"}), &Area3D::set_collision_layer);
    MethodBinder::bind_method(D_METHOD("get_collision_layer"), &Area3D::get_collision_layer);

    MethodBinder::bind_method(D_METHOD("set_collision_mask_bit", {"bit", "value"}), &Area3D::set_collision_mask_bit);
    MethodBinder::bind_method(D_METHOD("get_collision_mask_bit", {"bit"}), &Area3D::get_collision_mask_bit);

    MethodBinder::bind_method(D_METHOD("set_collision_layer_bit", {"bit", "value"}), &Area3D::set_collision_layer_bit);
    MethodBinder::bind_method(D_METHOD("get_collision_layer_bit", {"bit"}), &Area3D::get_collision_layer_bit);

    MethodBinder::bind_method(D_METHOD("set_monitorable", {"enable"}), &Area3D::set_monitorable);
    MethodBinder::bind_method(D_METHOD("is_monitorable"), &Area3D::is_monitorable);

    MethodBinder::bind_method(D_METHOD("set_monitoring", {"enable"}), &Area3D::set_monitoring);
    MethodBinder::bind_method(D_METHOD("is_monitoring"), &Area3D::is_monitoring);

    MethodBinder::bind_method(D_METHOD("get_overlapping_bodies"), &Area3D::get_overlapping_bodies);
    MethodBinder::bind_method(D_METHOD("get_overlapping_areas"), &Area3D::get_overlapping_areas);

    MethodBinder::bind_method(D_METHOD("overlaps_body", {"body"}), &Area3D::overlaps_body);
    MethodBinder::bind_method(D_METHOD("overlaps_area", {"area"}), &Area3D::overlaps_area);

    MethodBinder::bind_method(D_METHOD("set_audio_bus_override", {"enable"}), &Area3D::set_audio_bus_override);
    MethodBinder::bind_method(D_METHOD("is_overriding_audio_bus"), &Area3D::is_overriding_audio_bus);

    MethodBinder::bind_method(D_METHOD("set_audio_bus", {"name"}), &Area3D::set_audio_bus);
    MethodBinder::bind_method(D_METHOD("get_audio_bus"), &Area3D::get_audio_bus);

    MethodBinder::bind_method(D_METHOD("set_use_reverb_bus", {"enable"}), &Area3D::set_use_reverb_bus);
    MethodBinder::bind_method(D_METHOD("is_using_reverb_bus"), &Area3D::is_using_reverb_bus);

    MethodBinder::bind_method(D_METHOD("set_reverb_bus", {"name"}), &Area3D::set_reverb_bus);
    MethodBinder::bind_method(D_METHOD("get_reverb_bus"), &Area3D::get_reverb_bus);

    MethodBinder::bind_method(D_METHOD("set_reverb_amount", {"amount"}), &Area3D::set_reverb_amount);
    MethodBinder::bind_method(D_METHOD("get_reverb_amount"), &Area3D::get_reverb_amount);

    MethodBinder::bind_method(D_METHOD("set_reverb_uniformity", {"amount"}), &Area3D::set_reverb_uniformity);
    MethodBinder::bind_method(D_METHOD("get_reverb_uniformity"), &Area3D::get_reverb_uniformity);

    ADD_SIGNAL(MethodInfo("body_shape_entered", PropertyInfo(VariantType::INT, "body_id"), PropertyInfo(VariantType::OBJECT, "body", PropertyHint::ResourceType, "Node"), PropertyInfo(VariantType::INT, "body_shape"), PropertyInfo(VariantType::INT, "local_shape")));
    ADD_SIGNAL(MethodInfo("body_shape_exited", PropertyInfo(VariantType::INT, "body_id"), PropertyInfo(VariantType::OBJECT, "body", PropertyHint::ResourceType, "Node"), PropertyInfo(VariantType::INT, "body_shape"), PropertyInfo(VariantType::INT, "local_shape")));
    ADD_SIGNAL(MethodInfo("body_entered", PropertyInfo(VariantType::OBJECT, "body", PropertyHint::ResourceType, "Node")));
    ADD_SIGNAL(MethodInfo("body_exited", PropertyInfo(VariantType::OBJECT, "body", PropertyHint::ResourceType, "Node")));

    ADD_SIGNAL(MethodInfo("area_shape_entered", PropertyInfo(VariantType::INT, "area_id"), PropertyInfo(VariantType::OBJECT, "area", PropertyHint::ResourceType, "Area3D"), PropertyInfo(VariantType::INT, "area_shape"), PropertyInfo(VariantType::INT, "local_shape")));
    ADD_SIGNAL(MethodInfo("area_shape_exited", PropertyInfo(VariantType::INT, "area_id"), PropertyInfo(VariantType::OBJECT, "area", PropertyHint::ResourceType, "Area3D"), PropertyInfo(VariantType::INT, "area_shape"), PropertyInfo(VariantType::INT, "local_shape")));
    ADD_SIGNAL(MethodInfo("area_entered", PropertyInfo(VariantType::OBJECT, "area", PropertyHint::ResourceType, "Area3D")));
    ADD_SIGNAL(MethodInfo("area_exited", PropertyInfo(VariantType::OBJECT, "area", PropertyHint::ResourceType, "Area3D")));

    ADD_PROPERTY(PropertyInfo(VariantType::INT, "space_override", PropertyHint::Enum, "Disabled,Combine,Combine-Replace,Replace,Replace-Combine"), "set_space_override_mode", "get_space_override_mode");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "gravity_point"), "set_gravity_is_point", "is_gravity_a_point");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "gravity_distance_scale", PropertyHint::ExpRange, "0,1024,0.001,or_greater"), "set_gravity_distance_scale", "get_gravity_distance_scale");
    ADD_PROPERTY(PropertyInfo(VariantType::VECTOR3, "gravity_vec"), "set_gravity_vector", "get_gravity_vector");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "gravity", PropertyHint::Range, "-1024,1024,0.01"), "set_gravity", "get_gravity");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "linear_damp", PropertyHint::Range, "0,100,0.001,or_greater"), "set_linear_damp", "get_linear_damp");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "angular_damp", PropertyHint::Range, "0,100,0.001,or_greater"), "set_angular_damp", "get_angular_damp");
    ADD_PROPERTY(PropertyInfo(VariantType::INT, "priority", PropertyHint::Range, "0,128,1"), "set_priority", "get_priority");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "monitoring"), "set_monitoring", "is_monitoring");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "monitorable"), "set_monitorable", "is_monitorable");
    ADD_GROUP("Collision", "collision_");
    ADD_PROPERTY(PropertyInfo(VariantType::INT, "collision_layer", PropertyHint::Layers3DPhysics), "set_collision_layer", "get_collision_layer");
    ADD_PROPERTY(PropertyInfo(VariantType::INT, "collision_mask", PropertyHint::Layers3DPhysics), "set_collision_mask", "get_collision_mask");
    ADD_GROUP("Audio Bus", "audio_bus_");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "audio_bus_override"), "set_audio_bus_override", "is_overriding_audio_bus");
    ADD_PROPERTY(PropertyInfo(VariantType::STRING_NAME, "audio_bus_name", PropertyHint::Enum, ""), "set_audio_bus", "get_audio_bus");
    ADD_GROUP("Reverb Bus", "reverb_bus_");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "reverb_bus_enable"), "set_use_reverb_bus", "is_using_reverb_bus");
    ADD_PROPERTY(PropertyInfo(VariantType::STRING_NAME, "reverb_bus_name", PropertyHint::Enum, ""), "set_reverb_bus", "get_reverb_bus");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "reverb_bus_amount", PropertyHint::Range, "0,1,0.01"), "set_reverb_amount", "get_reverb_amount");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "reverb_bus_uniformity", PropertyHint::Range, "0,1,0.01"), "set_reverb_uniformity", "get_reverb_uniformity");

    BIND_ENUM_CONSTANT(SPACE_OVERRIDE_DISABLED);
    BIND_ENUM_CONSTANT(SPACE_OVERRIDE_COMBINE);
    BIND_ENUM_CONSTANT(SPACE_OVERRIDE_COMBINE_REPLACE);
    BIND_ENUM_CONSTANT(SPACE_OVERRIDE_REPLACE);
    BIND_ENUM_CONSTANT(SPACE_OVERRIDE_REPLACE_COMBINE);

}

Area3D::Area3D() :
        CollisionObject3D(PhysicsServer3D::get_singleton()->area_create(), true) {

    space_override = SPACE_OVERRIDE_DISABLED;
    set_gravity(9.8f);
    locked = false;
    set_gravity_vector(Vector3(0, -1, 0));
    gravity_is_point = false;
    gravity_distance_scale = 0;
    linear_damp = 0.1f;
    angular_damp = 0.1f;
    priority = 0;
    monitoring = false;
    monitorable = false;
    collision_mask = 1;
    collision_layer = 1;
    set_monitoring(true);
    set_monitorable(true);

    audio_bus_override = false;
    audio_bus = "Master";

    use_reverb_bus = false;
    reverb_bus = "Master";
    reverb_amount = 0.0;
    reverb_uniformity = 0.0;
}

Area3D::~Area3D() {
}
