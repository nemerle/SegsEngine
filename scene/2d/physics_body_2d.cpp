/*************************************************************************/
/*  physics_body_2d.cpp                                                  */
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

#include "physics_body_2d.h"

#include "core/callable_method_pointer.h"
#include "core/core_string_names.h"
#include "core/engine.h"
#include "core/list.h"
#include "core/math/math_funcs.h"
#include "core/method_bind.h"
#include "core/object.h"
#include "core/object_db.h"
#include "core/object_tooling.h"
#include "core/project_settings.h"
#include "core/rid.h"
#include "core/script_language.h"
#include "core/translation_helpers.h"
#include "scene/scene_string_names.h"

IMPL_GDCLASS(PhysicsBody2D)
IMPL_GDCLASS(StaticBody2D)
IMPL_GDCLASS(RigidBody2D)
IMPL_GDCLASS(KinematicBody2D)
IMPL_GDCLASS(KinematicCollision2D)
VARIANT_ENUM_CAST(RigidBody2D::Mode);
VARIANT_ENUM_CAST(RigidBody2D::CCDMode);

void PhysicsBody2D::_notification(int p_what) {
}

void PhysicsBody2D::_set_layers(uint32_t p_mask) {

    set_collision_layer(p_mask);
    set_collision_mask(p_mask);
}

uint32_t PhysicsBody2D::_get_layers() const {

    return get_collision_layer();
}

void PhysicsBody2D::_bind_methods() {

    MethodBinder::bind_method(D_METHOD("set_collision_layer", {"layer"}), &PhysicsBody2D::set_collision_layer);
    MethodBinder::bind_method(D_METHOD("get_collision_layer"), &PhysicsBody2D::get_collision_layer);
    MethodBinder::bind_method(D_METHOD("set_collision_mask", {"mask"}), &PhysicsBody2D::set_collision_mask);
    MethodBinder::bind_method(D_METHOD("get_collision_mask"), &PhysicsBody2D::get_collision_mask);

    MethodBinder::bind_method(D_METHOD("set_collision_mask_bit", {"bit", "value"}), &PhysicsBody2D::set_collision_mask_bit);
    MethodBinder::bind_method(D_METHOD("get_collision_mask_bit", {"bit"}), &PhysicsBody2D::get_collision_mask_bit);

    MethodBinder::bind_method(D_METHOD("set_collision_layer_bit", {"bit", "value"}), &PhysicsBody2D::set_collision_layer_bit);
    MethodBinder::bind_method(D_METHOD("get_collision_layer_bit", {"bit"}), &PhysicsBody2D::get_collision_layer_bit);

    MethodBinder::bind_method(D_METHOD("_set_layers", {"mask"}), &PhysicsBody2D::_set_layers);
    MethodBinder::bind_method(D_METHOD("_get_layers"), &PhysicsBody2D::_get_layers);

    MethodBinder::bind_method(D_METHOD("get_collision_exceptions"), &PhysicsBody2D::get_collision_exceptions);
    MethodBinder::bind_method(D_METHOD("add_collision_exception_with", {"body"}), &PhysicsBody2D::add_collision_exception_with);
    MethodBinder::bind_method(D_METHOD("remove_collision_exception_with", {"body"}), &PhysicsBody2D::remove_collision_exception_with);
    ADD_PROPERTY(PropertyInfo(VariantType::INT, "layers", PropertyHint::Layers2DPhysics, "", 0), "_set_layers", "_get_layers"); //for backwards compat

    ADD_GROUP("Collision", "collision_");
    ADD_PROPERTY(PropertyInfo(VariantType::INT, "collision_layer", PropertyHint::Layers2DPhysics), "set_collision_layer", "get_collision_layer");
    ADD_PROPERTY(PropertyInfo(VariantType::INT, "collision_mask", PropertyHint::Layers2DPhysics), "set_collision_mask", "get_collision_mask");
}

void PhysicsBody2D::set_collision_layer(uint32_t p_layer) {

    collision_layer = p_layer;
    PhysicsServer2D::get_singleton()->body_set_collision_layer(get_rid(), p_layer);
}

uint32_t PhysicsBody2D::get_collision_layer() const {

    return collision_layer;
}

void PhysicsBody2D::set_collision_mask(uint32_t p_mask) {

    collision_mask = p_mask;
    PhysicsServer2D::get_singleton()->body_set_collision_mask(get_rid(), p_mask);
}

uint32_t PhysicsBody2D::get_collision_mask() const {

    return collision_mask;
}

void PhysicsBody2D::set_collision_mask_bit(int p_bit, bool p_value) {

    uint32_t mask = get_collision_mask();
    if (p_value)
        mask |= 1 << p_bit;
    else
        mask &= ~(1 << p_bit);
    set_collision_mask(mask);
}
bool PhysicsBody2D::get_collision_mask_bit(int p_bit) const {

    return get_collision_mask() & (1 << p_bit);
}

void PhysicsBody2D::set_collision_layer_bit(int p_bit, bool p_value) {

    uint32_t collision_layer = get_collision_layer();
    if (p_value)
        collision_layer |= 1 << p_bit;
    else
        collision_layer &= ~(1 << p_bit);
    set_collision_layer(collision_layer);
}

bool PhysicsBody2D::get_collision_layer_bit(int p_bit) const {

    return get_collision_layer() & (1 << p_bit);
}

PhysicsBody2D::PhysicsBody2D(PhysicsServer2D::BodyMode p_mode) :
        CollisionObject2D(PhysicsServer2D::get_singleton()->body_create(), false) {

    PhysicsServer2D::get_singleton()->body_set_mode(get_rid(), p_mode);
    collision_layer = 1;
    collision_mask = 1;
    set_pickable(false);
}

Array PhysicsBody2D::get_collision_exceptions() {
    Vector<RID> exceptions;
    PhysicsServer2D::get_singleton()->body_get_collision_exceptions(get_rid(), &exceptions);
    Array ret;
    for (RID body : exceptions) {
        ObjectID instance_id = PhysicsServer2D::get_singleton()->body_get_object_instance_id(body);
        Object *obj = ObjectDB::get_instance(instance_id);
        PhysicsBody2D *physics_body = object_cast<PhysicsBody2D>(obj);
        ret.append(Variant(physics_body));
    }
    return ret;
}

void PhysicsBody2D::add_collision_exception_with(Node *p_node) {

    ERR_FAIL_NULL(p_node);
    PhysicsBody2D *physics_body = object_cast<PhysicsBody2D>(p_node);
    ERR_FAIL_COND_MSG(!physics_body, "Collision exception only works between two objects of PhysicsBody3D type.");
    PhysicsServer2D::get_singleton()->body_add_collision_exception(get_rid(), physics_body->get_rid());
}

void PhysicsBody2D::remove_collision_exception_with(Node *p_node) {

    ERR_FAIL_NULL(p_node);
    PhysicsBody2D *physics_body = object_cast<PhysicsBody2D>(p_node);
    ERR_FAIL_COND_MSG(!physics_body, "Collision exception only works between two objects of PhysicsBody3D type.");
    PhysicsServer2D::get_singleton()->body_remove_collision_exception(get_rid(), physics_body->get_rid());
}

void StaticBody2D::set_constant_linear_velocity(const Vector2 &p_vel) {

    constant_linear_velocity = p_vel;
    PhysicsServer2D::get_singleton()->body_set_state(get_rid(), PhysicsServer2D::BODY_STATE_LINEAR_VELOCITY, constant_linear_velocity);
}

void StaticBody2D::set_constant_angular_velocity(real_t p_vel) {

    constant_angular_velocity = p_vel;
    PhysicsServer2D::get_singleton()->body_set_state(get_rid(), PhysicsServer2D::BODY_STATE_ANGULAR_VELOCITY, constant_angular_velocity);
}

Vector2 StaticBody2D::get_constant_linear_velocity() const {

    return constant_linear_velocity;
}
real_t StaticBody2D::get_constant_angular_velocity() const {

    return constant_angular_velocity;
}

//WARN_DEPRECATED_MSG("The method set_friction has been deprecated and will be removed in the future, use physics material instead.");
//WARN_DEPRECATED_MSG("The method get_friction has been deprecated and will be removed in the future, use physics material instead.");
//WARN_DEPRECATED_MSG("The method set_bounce has been deprecated and will be removed in the future, use physics material instead.");
//WARN_DEPRECATED_MSG("The method get_bounce has been deprecated and will be removed in the future, use physics material instead.");

void StaticBody2D::set_physics_material_override(const Ref<PhysicsMaterial> &p_physics_material_override) {
    if (physics_material_override) {
        if (physics_material_override->is_connected(CoreStringNames::get_singleton()->changed, callable_mp(this, &StaticBody2D::_reload_physics_characteristics))) {
            physics_material_override->disconnect(CoreStringNames::get_singleton()->changed, callable_mp(this, &StaticBody2D::_reload_physics_characteristics));
        }
    }

    physics_material_override = p_physics_material_override;

    if (physics_material_override) {
        physics_material_override->connect(CoreStringNames::get_singleton()->changed, callable_mp(this, &StaticBody2D::_reload_physics_characteristics));
    }
    _reload_physics_characteristics();
}

Ref<PhysicsMaterial> StaticBody2D::get_physics_material_override() const {
    return physics_material_override;
}

void StaticBody2D::_bind_methods() {

    MethodBinder::bind_method(D_METHOD("set_constant_linear_velocity", {"vel"}), &StaticBody2D::set_constant_linear_velocity);
    MethodBinder::bind_method(D_METHOD("set_constant_angular_velocity", {"vel"}), &StaticBody2D::set_constant_angular_velocity);
    MethodBinder::bind_method(D_METHOD("get_constant_linear_velocity"), &StaticBody2D::get_constant_linear_velocity);
    MethodBinder::bind_method(D_METHOD("get_constant_angular_velocity"), &StaticBody2D::get_constant_angular_velocity);

    MethodBinder::bind_method(D_METHOD("set_physics_material_override", {"physics_material_override"}), &StaticBody2D::set_physics_material_override);
    MethodBinder::bind_method(D_METHOD("get_physics_material_override"), &StaticBody2D::get_physics_material_override);

    ADD_PROPERTY(PropertyInfo(VariantType::VECTOR2, "constant_linear_velocity"), "set_constant_linear_velocity", "get_constant_linear_velocity");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "constant_angular_velocity"), "set_constant_angular_velocity", "get_constant_angular_velocity");
    ADD_PROPERTY(PropertyInfo(VariantType::OBJECT, "physics_material_override", PropertyHint::ResourceType, "PhysicsMaterial"), "set_physics_material_override", "get_physics_material_override");
}

StaticBody2D::StaticBody2D() :
        PhysicsBody2D(PhysicsServer2D::BODY_MODE_STATIC) {

    constant_angular_velocity = 0;
}

StaticBody2D::~StaticBody2D() {
}

void StaticBody2D::_reload_physics_characteristics() {
    if (not physics_material_override) {
        PhysicsServer2D::get_singleton()->body_set_param(get_rid(), PhysicsServer2D::BODY_PARAM_BOUNCE, 0);
        PhysicsServer2D::get_singleton()->body_set_param(get_rid(), PhysicsServer2D::BODY_PARAM_FRICTION, 1);
    } else {
        PhysicsServer2D::get_singleton()->body_set_param(get_rid(), PhysicsServer2D::BODY_PARAM_BOUNCE, physics_material_override->computed_bounce());
        PhysicsServer2D::get_singleton()->body_set_param(get_rid(), PhysicsServer2D::BODY_PARAM_FRICTION, physics_material_override->computed_friction());
    }
}

void RigidBody2D::_body_enter_tree(ObjectID p_id) {

    Object *obj = ObjectDB::get_instance(p_id);
    Node *node = object_cast<Node>(obj);
    ERR_FAIL_COND(!node);

    ERR_FAIL_COND(!contact_monitor);
    HashMap<ObjectID, BodyState>::iterator E = contact_monitor->body_map.find(p_id);
    ERR_FAIL_COND(E==contact_monitor->body_map.end());
    ERR_FAIL_COND(E->second.in_scene);

    contact_monitor->locked = true;

    E->second.in_scene = true;
    emit_signal(SceneStringNames::body_entered, Variant(node));

    for (size_t i = 0; i < E->second.shapes.size(); i++) {

        emit_signal(SceneStringNames::body_shape_entered, Variant::from(p_id), Variant(node), E->second.shapes[i].body_shape, E->second.shapes[i].local_shape);
    }

    contact_monitor->locked = false;
}

void RigidBody2D::_body_exit_tree(ObjectID p_id) {

    Object *obj = ObjectDB::get_instance(p_id);
    Node *node = object_cast<Node>(obj);
    ERR_FAIL_COND(!node);
    ERR_FAIL_COND(!contact_monitor);
    HashMap<ObjectID, BodyState>::iterator E = contact_monitor->body_map.find(p_id);
    ERR_FAIL_COND(E==contact_monitor->body_map.end());
    ERR_FAIL_COND(!E->second.in_scene);
    E->second.in_scene = false;

    contact_monitor->locked = true;

    emit_signal(SceneStringNames::body_exited, Variant(node));

    for (size_t i = 0; i < E->second.shapes.size(); i++) {

        emit_signal(SceneStringNames::body_shape_exited, Variant::from(p_id), Variant(node), E->second.shapes[i].body_shape, E->second.shapes[i].local_shape);
    }

    contact_monitor->locked = false;
}

void RigidBody2D::_body_inout(int p_status, ObjectID p_instance, int p_body_shape, int p_local_shape) {

    bool body_in = p_status == 1;
    ObjectID objid = p_instance;

    Object *obj = ObjectDB::get_instance(objid);
    Node *node = object_cast<Node>(obj);

    ERR_FAIL_COND(!contact_monitor);
    HashMap<ObjectID, BodyState>::iterator E = contact_monitor->body_map.find(objid);

    ERR_FAIL_COND(!body_in && E==contact_monitor->body_map.end());

    if (body_in) {
        if (E==contact_monitor->body_map.end()) {

            E = contact_monitor->body_map.emplace(objid, BodyState()).first;
            //E.second.rc=0;
            E->second.in_scene = node && node->is_inside_tree();
            if (node) {
                node->connect(SceneStringNames::tree_entered, callable_mp(this, &RigidBody2D::_body_enter_tree), make_binds(objid));
                node->connect(SceneStringNames::tree_exiting, callable_mp(this, &RigidBody2D::_body_exit_tree), make_binds(objid));
                if (E->second.in_scene) {
                    emit_signal(SceneStringNames::body_entered, Variant(node));
                }
            }

            //E.second.rc++;
        }

        if (node) {
            E->second.shapes.insert(ShapePair(p_body_shape, p_local_shape));
        }

        if (E->second.in_scene) {
            emit_signal(SceneStringNames::body_shape_entered, Variant::from(objid), Variant(node), p_body_shape, p_local_shape);
        }

    } else {

        //E.second.rc--;

        if (node)
            E->second.shapes.erase(ShapePair(p_body_shape, p_local_shape));

        bool in_scene = E->second.in_scene;

        if (E->second.shapes.empty()) {

            if (node) {
                node->disconnect(SceneStringNames::tree_entered, callable_mp(this, &RigidBody2D::_body_enter_tree));
                node->disconnect(SceneStringNames::tree_exiting, callable_mp(this, &RigidBody2D::_body_exit_tree));
                if (in_scene)
                    emit_signal(SceneStringNames::body_exited, Variant(node));
            }

            contact_monitor->body_map.erase(E);
        }
        if (node && in_scene) {
            emit_signal(SceneStringNames::body_shape_exited, Variant::from(objid), Variant(node), p_body_shape, p_local_shape);
        }
    }
}

struct _RigidBody2DInOut {

    ObjectID id;
    int shape;
    int local_shape;
};

bool RigidBody2D::_test_motion(const Vector2 &p_motion, bool p_infinite_inertia, float p_margin, const Ref<Physics2DTestMotionResult> &p_result) {

    PhysicsServer2D::MotionResult *r = nullptr;
    if (p_result)
        r = p_result->get_result_ptr();
    return PhysicsServer2D::get_singleton()->body_test_motion(get_rid(), get_global_transform(), p_motion, p_infinite_inertia, p_margin, r);
}

void RigidBody2D::_direct_state_changed(Object *p_state) {

#ifdef DEBUG_ENABLED
    state = object_cast<PhysicsDirectBodyState2D>(p_state);
#else
    state = (PhysicsDirectBodyState2D *)p_state; //trust it
#endif

    set_block_transform_notify(true); // don't want notify (would feedback loop)
    if (mode != MODE_KINEMATIC)
        set_global_transform(state->get_transform());
    linear_velocity = state->get_linear_velocity();
    angular_velocity = state->get_angular_velocity();
    if (sleeping != state->is_sleeping()) {
        sleeping = state->is_sleeping();
        emit_signal(SceneStringNames::sleeping_state_changed);
    }
    if (get_script_instance())
        get_script_instance()->call("_integrate_forces", Variant(state));
    set_block_transform_notify(false); // want it back

    if (contact_monitor) {

        contact_monitor->locked = true;

        //untag all
        int rc = 0;
        for (eastl::pair<const ObjectID,BodyState> &E : contact_monitor->body_map) {

            for (size_t i = 0; i < E.second.shapes.size(); i++) {

                E.second.shapes[i].tagged = false;
                rc++;
            }
        }

        _RigidBody2DInOut *toadd = (_RigidBody2DInOut *)alloca(state->get_contact_count() * sizeof(_RigidBody2DInOut));
        int toadd_count = 0; //state->get_contact_count();
        RigidBody2D_RemoveAction *toremove = (RigidBody2D_RemoveAction *)alloca(rc * sizeof(RigidBody2D_RemoveAction));
        int toremove_count = 0;

        //put the ones to add

        for (int i = 0; i < state->get_contact_count(); i++) {

            ObjectID obj = state->get_contact_collider_id(i);
            int local_shape = state->get_contact_local_shape(i);
            int shape = state->get_contact_collider_shape(i);

            //bool found=false;

            auto E = contact_monitor->body_map.find(obj);
            if (E==contact_monitor->body_map.end()) {
                toadd[toadd_count].local_shape = local_shape;
                toadd[toadd_count].id = obj;
                toadd[toadd_count].shape = shape;
                toadd_count++;
                continue;
            }

            ShapePair sp(shape, local_shape);
            auto idx = E->second.shapes.find(sp);
            if (idx == E->second.shapes.end()) {

                toadd[toadd_count].local_shape = local_shape;
                toadd[toadd_count].id = obj;
                toadd[toadd_count].shape = shape;
                toadd_count++;
                continue;
            }

            idx->tagged = true;
        }

        //put the ones to remove

        for (eastl::pair<const ObjectID,BodyState> &E : contact_monitor->body_map) {

            for (auto &i : E.second.shapes) {

                if (!i.tagged) {

                    toremove[toremove_count].body_id = E.first;
                    toremove[toremove_count].pair = i;
                    toremove_count++;
                }
            }
        }

        //process remotions

        for (int i = 0; i < toremove_count; i++) {

            _body_inout(0, toremove[i].body_id, toremove[i].pair.body_shape, toremove[i].pair.local_shape);
        }

        //process aditions

        for (int i = 0; i < toadd_count; i++) {

            _body_inout(1, toadd[i].id, toadd[i].shape, toadd[i].local_shape);
        }

        contact_monitor->locked = false;
    }

    state = nullptr;
}

void RigidBody2D::set_mode(Mode p_mode) {

    mode = p_mode;
    switch (p_mode) {

        case MODE_RIGID: {

            PhysicsServer2D::get_singleton()->body_set_mode(get_rid(), PhysicsServer2D::BODY_MODE_RIGID);
        } break;
        case MODE_STATIC: {

            PhysicsServer2D::get_singleton()->body_set_mode(get_rid(), PhysicsServer2D::BODY_MODE_STATIC);

        } break;
        case MODE_KINEMATIC: {

            PhysicsServer2D::get_singleton()->body_set_mode(get_rid(), PhysicsServer2D::BODY_MODE_KINEMATIC);

        } break;
        case MODE_CHARACTER: {
            PhysicsServer2D::get_singleton()->body_set_mode(get_rid(), PhysicsServer2D::BODY_MODE_CHARACTER);

        } break;
    }
}

RigidBody2D::Mode RigidBody2D::get_mode() const {

    return mode;
}

void RigidBody2D::set_mass(real_t p_mass) {

    ERR_FAIL_COND(p_mass <= 0);
    mass = p_mass;
    Object_change_notify(this,"mass");
    Object_change_notify(this,"weight");
    PhysicsServer2D::get_singleton()->body_set_param(get_rid(), PhysicsServer2D::BODY_PARAM_MASS, mass);
}
real_t RigidBody2D::get_mass() const {

    return mass;
}

void RigidBody2D::set_inertia(real_t p_inertia) {

    ERR_FAIL_COND(p_inertia < 0);
    PhysicsServer2D::get_singleton()->body_set_param(get_rid(), PhysicsServer2D::BODY_PARAM_INERTIA, p_inertia);
}

real_t RigidBody2D::get_inertia() const {

    return PhysicsServer2D::get_singleton()->body_get_param(get_rid(), PhysicsServer2D::BODY_PARAM_INERTIA);
}

void RigidBody2D::set_weight(real_t p_weight) {

    set_mass(p_weight / (T_GLOBAL_DEF<float>("physics/2d/default_gravity", 98) / 10));
}

real_t RigidBody2D::get_weight() const {

    return mass * (T_GLOBAL_DEF<float>("physics/2d/default_gravity", 98) / 10);
}

//WARN_DEPRECATED_MSG("The method set_friction has been deprecated and will be removed in the future, use physics material instead.");
//WARN_DEPRECATED_MSG("The method get_friction has been deprecated and will be removed in the future, use physics material instead.");
//WARN_DEPRECATED_MSG("The method set_bounce has been deprecated and will be removed in the future, use physics material instead.");
//WARN_DEPRECATED_MSG("The method get_bounce has been deprecated and will be removed in the future, use physics material instead.");

void RigidBody2D::set_physics_material_override(const Ref<PhysicsMaterial> &p_physics_material_override) {
    if (physics_material_override) {
        if (physics_material_override->is_connected(CoreStringNames::get_singleton()->changed, callable_mp(this, &RigidBody2D::_reload_physics_characteristics))) {
            physics_material_override->disconnect(CoreStringNames::get_singleton()->changed, callable_mp(this, &RigidBody2D::_reload_physics_characteristics));
        }
    }

    physics_material_override = p_physics_material_override;

    if (physics_material_override) {
        physics_material_override->connect(CoreStringNames::get_singleton()->changed, callable_mp(this, &RigidBody2D::_reload_physics_characteristics));
    }
    _reload_physics_characteristics();
}

Ref<PhysicsMaterial> RigidBody2D::get_physics_material_override() const {
    return physics_material_override;
}

void RigidBody2D::set_gravity_scale(real_t p_gravity_scale) {

    gravity_scale = p_gravity_scale;
    PhysicsServer2D::get_singleton()->body_set_param(get_rid(), PhysicsServer2D::BODY_PARAM_GRAVITY_SCALE, gravity_scale);
}
real_t RigidBody2D::get_gravity_scale() const {

    return gravity_scale;
}

void RigidBody2D::set_linear_damp(real_t p_linear_damp) {

    ERR_FAIL_COND(p_linear_damp < -1);
    linear_damp = p_linear_damp;
    PhysicsServer2D::get_singleton()->body_set_param(get_rid(), PhysicsServer2D::BODY_PARAM_LINEAR_DAMP, linear_damp);
}
real_t RigidBody2D::get_linear_damp() const {

    return linear_damp;
}

void RigidBody2D::set_angular_damp(real_t p_angular_damp) {

    ERR_FAIL_COND(p_angular_damp < -1);
    angular_damp = p_angular_damp;
    PhysicsServer2D::get_singleton()->body_set_param(get_rid(), PhysicsServer2D::BODY_PARAM_ANGULAR_DAMP, angular_damp);
}
real_t RigidBody2D::get_angular_damp() const {

    return angular_damp;
}

void RigidBody2D::set_axis_velocity(const Vector2 &p_axis) {

    Vector2 v = state ? state->get_linear_velocity() : linear_velocity;
    Vector2 axis = p_axis.normalized();
    v -= axis * axis.dot(v);
    v += p_axis;
    if (state) {
        set_linear_velocity(v);
    } else {
        PhysicsServer2D::get_singleton()->body_set_axis_velocity(get_rid(), p_axis);
        linear_velocity = v;
    }
}

void RigidBody2D::set_linear_velocity(const Vector2 &p_velocity) {

    linear_velocity = p_velocity;
    if (state)
        state->set_linear_velocity(linear_velocity);
    else {

        PhysicsServer2D::get_singleton()->body_set_state(get_rid(), PhysicsServer2D::BODY_STATE_LINEAR_VELOCITY, linear_velocity);
    }
}

Vector2 RigidBody2D::get_linear_velocity() const {

    return linear_velocity;
}

void RigidBody2D::set_angular_velocity(real_t p_velocity) {

    angular_velocity = p_velocity;
    if (state)
        state->set_angular_velocity(angular_velocity);
    else
        PhysicsServer2D::get_singleton()->body_set_state(get_rid(), PhysicsServer2D::BODY_STATE_ANGULAR_VELOCITY, angular_velocity);
}
real_t RigidBody2D::get_angular_velocity() const {

    return angular_velocity;
}

void RigidBody2D::set_use_custom_integrator(bool p_enable) {

    if (custom_integrator == p_enable)
        return;

    custom_integrator = p_enable;
    PhysicsServer2D::get_singleton()->body_set_omit_force_integration(get_rid(), p_enable);
}
bool RigidBody2D::is_using_custom_integrator() {

    return custom_integrator;
}

void RigidBody2D::set_sleeping(bool p_sleeping) {

    sleeping = p_sleeping;
    PhysicsServer2D::get_singleton()->body_set_state(get_rid(), PhysicsServer2D::BODY_STATE_SLEEPING, sleeping);
}

void RigidBody2D::set_can_sleep(bool p_active) {

    can_sleep = p_active;
    PhysicsServer2D::get_singleton()->body_set_state(get_rid(), PhysicsServer2D::BODY_STATE_CAN_SLEEP, p_active);
}

bool RigidBody2D::is_able_to_sleep() const {

    return can_sleep;
}

bool RigidBody2D::is_sleeping() const {

    return sleeping;
}

void RigidBody2D::set_max_contacts_reported(int p_amount) {

    max_contacts_reported = p_amount;
    PhysicsServer2D::get_singleton()->body_set_max_contacts_reported(get_rid(), p_amount);
}

int RigidBody2D::get_max_contacts_reported() const {

    return max_contacts_reported;
}

void RigidBody2D::apply_central_impulse(const Vector2 &p_impulse) {
    PhysicsServer2D::get_singleton()->body_apply_central_impulse(get_rid(), p_impulse);
}

void RigidBody2D::apply_impulse(const Vector2 &p_offset, const Vector2 &p_impulse) {

    PhysicsServer2D::get_singleton()->body_apply_impulse(get_rid(), p_offset, p_impulse);
}

void RigidBody2D::apply_torque_impulse(float p_torque) {
    PhysicsServer2D::get_singleton()->body_apply_torque_impulse(get_rid(), p_torque);
}

void RigidBody2D::set_applied_force(const Vector2 &p_force) {

    PhysicsServer2D::get_singleton()->body_set_applied_force(get_rid(), p_force);
}

Vector2 RigidBody2D::get_applied_force() const {

    return PhysicsServer2D::get_singleton()->body_get_applied_force(get_rid());
}

void RigidBody2D::set_applied_torque(const float p_torque) {

    PhysicsServer2D::get_singleton()->body_set_applied_torque(get_rid(), p_torque);
}

float RigidBody2D::get_applied_torque() const {

    return PhysicsServer2D::get_singleton()->body_get_applied_torque(get_rid());
}

void RigidBody2D::add_central_force(const Vector2 &p_force) {
    PhysicsServer2D::get_singleton()->body_add_central_force(get_rid(), p_force);
}

void RigidBody2D::add_force(const Vector2 &p_offset, const Vector2 &p_force) {

    PhysicsServer2D::get_singleton()->body_add_force(get_rid(), p_offset, p_force);
}

void RigidBody2D::add_torque(const float p_torque) {
    PhysicsServer2D::get_singleton()->body_add_torque(get_rid(), p_torque);
}

void RigidBody2D::set_continuous_collision_detection_mode(CCDMode p_mode) {

    ccd_mode = p_mode;
    PhysicsServer2D::get_singleton()->body_set_continuous_collision_detection_mode(get_rid(), PhysicsServer2D::CCDMode(p_mode));
}

RigidBody2D::CCDMode RigidBody2D::get_continuous_collision_detection_mode() const {

    return ccd_mode;
}

Array RigidBody2D::get_colliding_bodies() const {

    ERR_FAIL_COND_V(!contact_monitor, Array());

    Array ret;
    ret.resize(contact_monitor->body_map.size());
    int idx = 0;
    for (const eastl::pair<const ObjectID,BodyState> &E : contact_monitor->body_map) {
        Object *obj = ObjectDB::get_instance(E.first);
        if (!obj) {
            ret.resize(ret.size() - 1); //ops
        } else {
            ret[idx++] = Variant(obj);
        }
    }

    return ret;
}

void RigidBody2D::set_contact_monitor(bool p_enabled) {

    if (p_enabled == is_contact_monitor_enabled())
        return;

    if (!p_enabled) {

        ERR_FAIL_COND_MSG(contact_monitor->locked, "Can't disable contact monitoring during in/out callback. Use call_deferred(\"set_contact_monitor\", false) instead.");

        for (eastl::pair<const ObjectID,BodyState> &E : contact_monitor->body_map) {

            //clean up mess
            Object *obj = ObjectDB::get_instance(E.first);
            Node *node = object_cast<Node>(obj);

            if (node) {
                node->disconnect(SceneStringNames::tree_entered, callable_mp(this, &RigidBody2D::_body_enter_tree));
                node->disconnect(SceneStringNames::tree_exiting, callable_mp(this, &RigidBody2D::_body_exit_tree));
            }
        }

        memdelete(contact_monitor);
        contact_monitor = nullptr;
    } else {

        contact_monitor = memnew(ContactMonitor);
        contact_monitor->locked = false;
    }
}

bool RigidBody2D::is_contact_monitor_enabled() const {

    return contact_monitor != nullptr;
}

void RigidBody2D::_notification(int p_what) {

#ifdef TOOLS_ENABLED
    if (p_what == NOTIFICATION_ENTER_TREE) {
        if (Engine::get_singleton()->is_editor_hint()) {
            set_notify_local_transform(true); //used for warnings and only in editor
        }
    }

    if (p_what == NOTIFICATION_LOCAL_TRANSFORM_CHANGED) {
        if (Engine::get_singleton()->is_editor_hint()) {
            update_configuration_warning();
        }
    }

#endif
}

String RigidBody2D::get_configuration_warning() const {

    Transform2D t = get_transform();

    String warning(CollisionObject2D::get_configuration_warning());

    if ((get_mode() == MODE_RIGID || get_mode() == MODE_CHARACTER) && (ABS(t.elements[0].length() - 1.0) > 0.05 || ABS(t.elements[1].length() - 1.0) > 0.05)) {
        if (!warning.empty()) {
            warning += "\n\n";
        }
        warning += TTR("Size changes to RigidBody2D (in character or rigid modes) will be overridden by the physics engine when running.\nChange the size in children collision shapes instead.");
    }

    return String(warning);
}

void RigidBody2D::_bind_methods() {

    MethodBinder::bind_method(D_METHOD("set_mode", {"mode"}), &RigidBody2D::set_mode);
    MethodBinder::bind_method(D_METHOD("get_mode"), &RigidBody2D::get_mode);

    MethodBinder::bind_method(D_METHOD("set_mass", {"mass"}), &RigidBody2D::set_mass);
    MethodBinder::bind_method(D_METHOD("get_mass"), &RigidBody2D::get_mass);

    MethodBinder::bind_method(D_METHOD("get_inertia"), &RigidBody2D::get_inertia);
    MethodBinder::bind_method(D_METHOD("set_inertia", {"inertia"}), &RigidBody2D::set_inertia);

    MethodBinder::bind_method(D_METHOD("set_weight", {"weight"}), &RigidBody2D::set_weight);
    MethodBinder::bind_method(D_METHOD("get_weight"), &RigidBody2D::get_weight);

    MethodBinder::bind_method(D_METHOD("set_physics_material_override", {"physics_material_override"}), &RigidBody2D::set_physics_material_override);
    MethodBinder::bind_method(D_METHOD("get_physics_material_override"), &RigidBody2D::get_physics_material_override);

    MethodBinder::bind_method(D_METHOD("set_gravity_scale", {"gravity_scale"}), &RigidBody2D::set_gravity_scale);
    MethodBinder::bind_method(D_METHOD("get_gravity_scale"), &RigidBody2D::get_gravity_scale);

    MethodBinder::bind_method(D_METHOD("set_linear_damp", {"linear_damp"}), &RigidBody2D::set_linear_damp);
    MethodBinder::bind_method(D_METHOD("get_linear_damp"), &RigidBody2D::get_linear_damp);

    MethodBinder::bind_method(D_METHOD("set_angular_damp", {"angular_damp"}), &RigidBody2D::set_angular_damp);
    MethodBinder::bind_method(D_METHOD("get_angular_damp"), &RigidBody2D::get_angular_damp);

    MethodBinder::bind_method(D_METHOD("set_linear_velocity", {"linear_velocity"}), &RigidBody2D::set_linear_velocity);
    MethodBinder::bind_method(D_METHOD("get_linear_velocity"), &RigidBody2D::get_linear_velocity);

    MethodBinder::bind_method(D_METHOD("set_angular_velocity", {"angular_velocity"}), &RigidBody2D::set_angular_velocity);
    MethodBinder::bind_method(D_METHOD("get_angular_velocity"), &RigidBody2D::get_angular_velocity);

    MethodBinder::bind_method(D_METHOD("set_max_contacts_reported", {"amount"}), &RigidBody2D::set_max_contacts_reported);
    MethodBinder::bind_method(D_METHOD("get_max_contacts_reported"), &RigidBody2D::get_max_contacts_reported);

    MethodBinder::bind_method(D_METHOD("set_use_custom_integrator", {"enable"}), &RigidBody2D::set_use_custom_integrator);
    MethodBinder::bind_method(D_METHOD("is_using_custom_integrator"), &RigidBody2D::is_using_custom_integrator);

    MethodBinder::bind_method(D_METHOD("set_contact_monitor", {"enabled"}), &RigidBody2D::set_contact_monitor);
    MethodBinder::bind_method(D_METHOD("is_contact_monitor_enabled"), &RigidBody2D::is_contact_monitor_enabled);

    MethodBinder::bind_method(D_METHOD("set_continuous_collision_detection_mode", {"mode"}), &RigidBody2D::set_continuous_collision_detection_mode);
    MethodBinder::bind_method(D_METHOD("get_continuous_collision_detection_mode"), &RigidBody2D::get_continuous_collision_detection_mode);

    MethodBinder::bind_method(D_METHOD("set_axis_velocity", {"axis_velocity"}), &RigidBody2D::set_axis_velocity);
    MethodBinder::bind_method(D_METHOD("apply_central_impulse", {"impulse"}), &RigidBody2D::apply_central_impulse);
    MethodBinder::bind_method(D_METHOD("apply_impulse", {"offset", "impulse"}), &RigidBody2D::apply_impulse);
    MethodBinder::bind_method(D_METHOD("apply_torque_impulse", {"torque"}), &RigidBody2D::apply_torque_impulse);

    MethodBinder::bind_method(D_METHOD("set_applied_force", {"force"}), &RigidBody2D::set_applied_force);
    MethodBinder::bind_method(D_METHOD("get_applied_force"), &RigidBody2D::get_applied_force);

    MethodBinder::bind_method(D_METHOD("set_applied_torque", {"torque"}), &RigidBody2D::set_applied_torque);
    MethodBinder::bind_method(D_METHOD("get_applied_torque"), &RigidBody2D::get_applied_torque);

    MethodBinder::bind_method(D_METHOD("add_central_force", {"force"}), &RigidBody2D::add_central_force);
    MethodBinder::bind_method(D_METHOD("add_force", {"offset", "force"}), &RigidBody2D::add_force);
    MethodBinder::bind_method(D_METHOD("add_torque", {"torque"}), &RigidBody2D::add_torque);

    MethodBinder::bind_method(D_METHOD("set_sleeping", {"sleeping"}), &RigidBody2D::set_sleeping);
    MethodBinder::bind_method(D_METHOD("is_sleeping"), &RigidBody2D::is_sleeping);

    MethodBinder::bind_method(D_METHOD("set_can_sleep", {"able_to_sleep"}), &RigidBody2D::set_can_sleep);
    MethodBinder::bind_method(D_METHOD("is_able_to_sleep"), &RigidBody2D::is_able_to_sleep);

    MethodBinder::bind_method(D_METHOD("test_motion", {"motion", "infinite_inertia", "margin", "result"}), &RigidBody2D::_test_motion, {DEFVAL(true), DEFVAL(0.08), DEFVAL(Variant())});

    MethodBinder::bind_method(D_METHOD("_direct_state_changed"), &RigidBody2D::_direct_state_changed);

    MethodBinder::bind_method(D_METHOD("get_colliding_bodies"), &RigidBody2D::get_colliding_bodies);

    BIND_VMETHOD(MethodInfo("_integrate_forces", PropertyInfo(VariantType::OBJECT, "state", PropertyHint::ResourceType, "PhysicsDirectBodyState2D")));

    ADD_PROPERTY(PropertyInfo(VariantType::INT, "mode", PropertyHint::Enum, "Rigid,Static,Character,Kinematic"), "set_mode", "get_mode");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "mass", PropertyHint::ExpRange, "0.01,65535,0.01"), "set_mass", "get_mass");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "inertia", PropertyHint::ExpRange, "0.01,65535,0.01", 0), "set_inertia", "get_inertia");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "weight", PropertyHint::ExpRange, "0.01,65535,0.01", PROPERTY_USAGE_EDITOR), "set_weight", "get_weight");

    ADD_PROPERTY(PropertyInfo(VariantType::OBJECT, "physics_material_override", PropertyHint::ResourceType, "PhysicsMaterial"), "set_physics_material_override", "get_physics_material_override");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "gravity_scale", PropertyHint::Range, "-128,128,0.01"), "set_gravity_scale", "get_gravity_scale");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "custom_integrator"), "set_use_custom_integrator", "is_using_custom_integrator");
    ADD_PROPERTY(PropertyInfo(VariantType::INT, "continuous_cd", PropertyHint::Enum, "Disabled,Cast Ray,Cast Shape"), "set_continuous_collision_detection_mode", "get_continuous_collision_detection_mode");
    ADD_PROPERTY(PropertyInfo(VariantType::INT, "contacts_reported", PropertyHint::Range, "0,64,1,or_greater"), "set_max_contacts_reported", "get_max_contacts_reported");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "contact_monitor"), "set_contact_monitor", "is_contact_monitor_enabled");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "sleeping"), "set_sleeping", "is_sleeping");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "can_sleep"), "set_can_sleep", "is_able_to_sleep");
    ADD_GROUP("Linear", "linear_");
    ADD_PROPERTY(PropertyInfo(VariantType::VECTOR2, "linear_velocity"), "set_linear_velocity", "get_linear_velocity");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "linear_damp", PropertyHint::Range, "-1,100,0.001,or_greater"), "set_linear_damp", "get_linear_damp");
    ADD_GROUP("Angular", "angular_");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "angular_velocity"), "set_angular_velocity", "get_angular_velocity");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "angular_damp", PropertyHint::Range, "-1,100,0.001,or_greater"), "set_angular_damp", "get_angular_damp");
    ADD_GROUP("Applied Forces", "applied_");
    ADD_PROPERTY(PropertyInfo(VariantType::VECTOR2, "applied_force"), "set_applied_force", "get_applied_force");
    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "applied_torque"), "set_applied_torque", "get_applied_torque");

    ADD_SIGNAL(MethodInfo("body_shape_entered", PropertyInfo(VariantType::INT, "body_id"), PropertyInfo(VariantType::OBJECT, "body", PropertyHint::ResourceType, "Node"), PropertyInfo(VariantType::INT, "body_shape"), PropertyInfo(VariantType::INT, "local_shape")));
    ADD_SIGNAL(MethodInfo("body_shape_exited", PropertyInfo(VariantType::INT, "body_id"), PropertyInfo(VariantType::OBJECT, "body", PropertyHint::ResourceType, "Node"), PropertyInfo(VariantType::INT, "body_shape"), PropertyInfo(VariantType::INT, "local_shape")));
    ADD_SIGNAL(MethodInfo("body_entered", PropertyInfo(VariantType::OBJECT, "body", PropertyHint::ResourceType, "Node")));
    ADD_SIGNAL(MethodInfo("body_exited", PropertyInfo(VariantType::OBJECT, "body", PropertyHint::ResourceType, "Node")));
    ADD_SIGNAL(MethodInfo("sleeping_state_changed"));

    BIND_ENUM_CONSTANT(MODE_RIGID);
    BIND_ENUM_CONSTANT(MODE_STATIC);
    BIND_ENUM_CONSTANT(MODE_CHARACTER);
    BIND_ENUM_CONSTANT(MODE_KINEMATIC);

    BIND_ENUM_CONSTANT(CCD_MODE_DISABLED);
    BIND_ENUM_CONSTANT(CCD_MODE_CAST_RAY);
    BIND_ENUM_CONSTANT(CCD_MODE_CAST_SHAPE);
}

RigidBody2D::RigidBody2D() :
        PhysicsBody2D(PhysicsServer2D::BODY_MODE_RIGID) {

    mode = MODE_RIGID;

    mass = 1;

    gravity_scale = 1;
    linear_damp = -1;
    angular_damp = -1;

    max_contacts_reported = 0;
    state = nullptr;

    angular_velocity = 0;
    sleeping = false;
    ccd_mode = CCD_MODE_DISABLED;

    custom_integrator = false;
    contact_monitor = nullptr;
    can_sleep = true;

    PhysicsServer2D::get_singleton()->body_set_force_integration_callback(get_rid(),callable_mp(this,&RigidBody2D::_direct_state_changed));
}

RigidBody2D::~RigidBody2D() {
    memdelete(contact_monitor);
}

void RigidBody2D::_reload_physics_characteristics() {
    if (not physics_material_override) {
        PhysicsServer2D::get_singleton()->body_set_param(get_rid(), PhysicsServer2D::BODY_PARAM_BOUNCE, 0);
        PhysicsServer2D::get_singleton()->body_set_param(get_rid(), PhysicsServer2D::BODY_PARAM_FRICTION, 1);
    } else {
        PhysicsServer2D::get_singleton()->body_set_param(get_rid(), PhysicsServer2D::BODY_PARAM_BOUNCE, physics_material_override->computed_bounce());
        PhysicsServer2D::get_singleton()->body_set_param(get_rid(), PhysicsServer2D::BODY_PARAM_FRICTION, physics_material_override->computed_friction());
    }
}

//////////////////////////

Ref<KinematicCollision2D> KinematicBody2D::_move(const Vector2 &p_motion, bool p_infinite_inertia, bool p_exclude_raycast_shapes, bool p_test_only) {

    Collision col;

    if (move_and_collide(p_motion, p_infinite_inertia, col, p_exclude_raycast_shapes, p_test_only)) {
        if (not motion_cache) {
            motion_cache = make_ref_counted<KinematicCollision2D>();
            motion_cache->owner = this;
        }

        motion_cache->collision = col;

        return motion_cache;
    }

    return Ref<KinematicCollision2D>();
}

bool KinematicBody2D::separate_raycast_shapes(bool p_infinite_inertia, Collision &r_collision) {

    PhysicsServer2D::SeparationResult sep_res[8]; //max 8 rays

    Transform2D gt = get_global_transform();

    Vector2 recover;
    int hits = PhysicsServer2D::get_singleton()->body_test_ray_separation(get_rid(), gt, p_infinite_inertia, recover, sep_res, 8, margin);
    int deepest = -1;
    float deepest_depth;
    for (int i = 0; i < hits; i++) {
        if (deepest == -1 || sep_res[i].collision_depth > deepest_depth) {
            deepest = i;
            deepest_depth = sep_res[i].collision_depth;
        }
    }

    gt.elements[2] += recover;
    set_global_transform(gt);

    if (deepest != -1) {
        r_collision.collider = sep_res[deepest].collider_id;
        r_collision.collider_metadata = sep_res[deepest].collider_metadata;
        r_collision.collider_shape = sep_res[deepest].collider_shape;
        r_collision.collider_vel = sep_res[deepest].collider_velocity;
        r_collision.collision = sep_res[deepest].collision_point;
        r_collision.normal = sep_res[deepest].collision_normal;
        r_collision.local_shape = sep_res[deepest].collision_local_shape;
        r_collision.travel = recover;
        r_collision.remainder = Vector2();

        return true;
    } else {
        return false;
    }
}

bool KinematicBody2D::move_and_collide(const Vector2 &p_motion, bool p_infinite_inertia, Collision &r_collision, bool p_exclude_raycast_shapes, bool p_test_only) {

    if (sync_to_physics) {
        ERR_PRINT("Functions move_and_slide and move_and_collide do not work together with 'sync to physics' option. Please read the documentation.");
    }
    Transform2D gt = get_global_transform();
    PhysicsServer2D::MotionResult result;
    bool colliding = PhysicsServer2D::get_singleton()->body_test_motion(get_rid(), gt, p_motion, p_infinite_inertia, margin, &result, p_exclude_raycast_shapes);

    if (colliding) {
        r_collision.collider_metadata = result.collider_metadata;
        r_collision.collider_shape = result.collider_shape;
        r_collision.collider_vel = result.collider_velocity;
        r_collision.collision = result.collision_point;
        r_collision.normal = result.collision_normal;
        r_collision.collider = result.collider_id;
        r_collision.collider_rid = result.collider;
        r_collision.travel = result.motion;
        r_collision.remainder = result.remainder;
        r_collision.local_shape = result.collision_local_shape;
    }

    if (!p_test_only) {
        gt.elements[2] += result.motion;
        set_global_transform(gt);
    }

    return colliding;
}

//so, if you pass 45 as limit, avoid numerical precision errors when angle is 45.
static constexpr float FLOOR_ANGLE_THRESHOLD = 0.01f;

Vector2 KinematicBody2D::move_and_slide(const Vector2 &p_linear_velocity, const Vector2 &p_up_direction, bool p_stop_on_slope, int p_max_slides, float p_floor_max_angle, bool p_infinite_inertia) {

    Vector2 body_velocity = p_linear_velocity;
    Vector2 body_velocity_normal = body_velocity.normalized();

    Vector2 current_floor_velocity = floor_velocity;
    if (on_floor && on_floor_body.is_valid()) {
        //this approach makes sure there is less delay between the actual body velocity and the one we saved
        PhysicsDirectBodyState2D *bs = PhysicsServer2D::get_singleton()->body_get_direct_state(on_floor_body);
        if (bs) {
            current_floor_velocity = bs->get_linear_velocity();
        }
    }

    // Hack in order to work with calling from _process as well as from _physics_process; calling from thread is risky
    Vector2 motion = (current_floor_velocity + body_velocity) * (Engine::get_singleton()->is_in_physics_frame() ? get_physics_process_delta_time() : get_process_delta_time());

    on_floor = false;
    on_floor_body = RID();
    on_ceiling = false;
    on_wall = false;
    colliders.clear();
    floor_normal = Vector2();
    floor_velocity = Vector2();

    while (p_max_slides) {

        Collision collision;
        bool found_collision = false;

        for (int i = 0; i < 2; ++i) {
            bool collided;
            if (i == 0) { //collide
                collided = move_and_collide(motion, p_infinite_inertia, collision);
                if (!collided) {
                    motion = Vector2(); //clear because no collision happened and motion completed
                }
            } else { //separate raycasts (if any)
                collided = separate_raycast_shapes(p_infinite_inertia, collision);
                if (collided) {
                    collision.remainder = motion; //keep
                    collision.travel = Vector2();
                }
            }

            if (collided) {
                found_collision = true;

                colliders.push_back(collision);
                motion = collision.remainder;

                if (p_up_direction == Vector2()) {
                    //all is a wall
                    on_wall = true;
                } else {
                    if (Math::acos(collision.normal.dot(p_up_direction)) <= p_floor_max_angle + FLOOR_ANGLE_THRESHOLD) { //floor

                        on_floor = true;
                        floor_normal = collision.normal;
                        on_floor_body = collision.collider_rid;
                        floor_velocity = collision.collider_vel;

                        if (p_stop_on_slope) {
                            if ((body_velocity_normal + p_up_direction).length() < 0.01f && collision.travel.length() < 1) {
                                Transform2D gt = get_global_transform();
                                gt.elements[2] -= collision.travel.slide(p_up_direction);
                                set_global_transform(gt);
                                return Vector2();
                            }
                        }
                    } else if (Math::acos(collision.normal.dot(-p_up_direction)) <= p_floor_max_angle + FLOOR_ANGLE_THRESHOLD) { //ceiling
                        on_ceiling = true;
                    } else {
                        on_wall = true;
                    }
                }

                motion = motion.slide(collision.normal);
                body_velocity = body_velocity.slide(collision.normal);
            }
        }

        if (!found_collision || motion == Vector2())
            break;

        --p_max_slides;
    }

    return body_velocity;
}


Vector2 KinematicBody2D::move_and_slide_with_snap(const Vector2 &p_linear_velocity, const Vector2 &p_snap, const Vector2 &p_up_direction, bool p_stop_on_slope, int p_max_slides, float p_floor_max_angle, bool p_infinite_inertia) {

    bool was_on_floor = on_floor;

    Vector2 ret = move_and_slide(p_linear_velocity, p_up_direction, p_stop_on_slope, p_max_slides, p_floor_max_angle, p_infinite_inertia);
    if (!was_on_floor || p_snap == Vector2()) {
        return ret;
    }

    Collision col;
    Transform2D gt = get_global_transform();

    if (move_and_collide(p_snap, p_infinite_inertia, col, false, true)) {
        bool apply = true;
        if (p_up_direction != Vector2()) {
            if (Math::acos(p_up_direction.normalized().dot(col.normal)) < p_floor_max_angle) {
                on_floor = true;
                floor_normal = col.normal;
                on_floor_body = col.collider_rid;
                floor_velocity = col.collider_vel;
                if (p_stop_on_slope) {
                    // move and collide may stray the object a bit because of pre un-stucking,
                    // so only ensure that motion happens on floor direction in this case.
                    col.travel = p_up_direction * p_up_direction.dot(col.travel);
                }

            } else {
                apply = false;
            }
        }

        if (apply) {
            gt.elements[2] += col.travel;
            set_global_transform(gt);
        }
    }

    return ret;
}

bool KinematicBody2D::is_on_floor() const {

    return on_floor;
}
bool KinematicBody2D::is_on_wall() const {

    return on_wall;
}
bool KinematicBody2D::is_on_ceiling() const {

    return on_ceiling;
}

Vector2 KinematicBody2D::get_floor_normal() const {

    return floor_normal;
}

Vector2 KinematicBody2D::get_floor_velocity() const {

    return floor_velocity;
}

bool KinematicBody2D::test_move(const Transform2D &p_from, const Vector2 &p_motion, bool p_infinite_inertia) {

    ERR_FAIL_COND_V(!is_inside_tree(), false);

    return PhysicsServer2D::get_singleton()->body_test_motion(get_rid(), p_from, p_motion, p_infinite_inertia, margin);
}

void KinematicBody2D::set_safe_margin(float p_margin) {

    margin = p_margin;
}

float KinematicBody2D::get_safe_margin() const {

    return margin;
}

int KinematicBody2D::get_slide_count() const {

    return colliders.size();
}

KinematicBody2D::Collision KinematicBody2D::get_slide_collision(int p_bounce) const {
    ERR_FAIL_INDEX_V(p_bounce, colliders.size(), Collision());
    return colliders[p_bounce];
}

Ref<KinematicCollision2D> KinematicBody2D::_get_slide_collision(int p_bounce) {

    ERR_FAIL_INDEX_V(p_bounce, colliders.size(), Ref<KinematicCollision2D>());
    if (p_bounce >= slide_colliders.size()) {
        slide_colliders.resize(p_bounce + 1);
    }

    if (not slide_colliders[p_bounce]) {
        slide_colliders[p_bounce] = make_ref_counted<KinematicCollision2D>();
        slide_colliders[p_bounce]->owner = this;
    }

    slide_colliders[p_bounce]->collision = colliders[p_bounce];
    return slide_colliders[p_bounce];
}

void KinematicBody2D::set_sync_to_physics(bool p_enable) {

    if (sync_to_physics == p_enable) {
        return;
    }
    sync_to_physics = p_enable;

    if (Engine::get_singleton()->is_editor_hint())
        return;

    if (p_enable) {
        PhysicsServer2D::get_singleton()->body_set_force_integration_callback(get_rid(), callable_mp(this,&KinematicBody2D::_direct_state_changed));
        set_only_update_transform_changes(true);
        set_notify_local_transform(true);
    } else {
        PhysicsServer2D::get_singleton()->body_set_force_integration_callback(get_rid(), {});
        set_only_update_transform_changes(false);
        set_notify_local_transform(false);
    }
}

bool KinematicBody2D::is_sync_to_physics_enabled() const {
    return sync_to_physics;
}

void KinematicBody2D::_direct_state_changed(Object *p_state) {

    if (!sync_to_physics)
        return;

    PhysicsDirectBodyState2D *state = object_cast<PhysicsDirectBodyState2D>(p_state);

    last_valid_transform = state->get_transform();
    set_notify_local_transform(false);
    set_global_transform(last_valid_transform);
    set_notify_local_transform(true);
}

void KinematicBody2D::_notification(int p_what) {
    if (p_what == NOTIFICATION_ENTER_TREE) {
        last_valid_transform = get_global_transform();

        // Reset move_and_slide() data.
        on_floor = false;
        on_floor_body = RID();
        on_ceiling = false;
        on_wall = false;
        colliders.clear();
        floor_velocity = Vector2();
    }

    if (p_what == NOTIFICATION_LOCAL_TRANSFORM_CHANGED) {
        //used by sync to physics, send the new transform to the physics
        Transform2D new_transform = get_global_transform();
        PhysicsServer2D::get_singleton()->body_set_state(get_rid(), PhysicsServer2D::BODY_STATE_TRANSFORM, new_transform);
        //but then revert changes
        set_notify_local_transform(false);
        set_global_transform(last_valid_transform);
        set_notify_local_transform(true);
    }
}
void KinematicBody2D::_bind_methods() {

    MethodBinder::bind_method(D_METHOD("move_and_collide", {"rel_vec", "infinite_inertia", "exclude_raycast_shapes", "test_only"}), &KinematicBody2D::_move, {DEFVAL(true), DEFVAL(true), DEFVAL(false)});
    MethodBinder::bind_method(D_METHOD("move_and_slide", {"linear_velocity", "up_direction", "stop_on_slope", "max_slides", "floor_max_angle", "infinite_inertia"}), &KinematicBody2D::move_and_slide, {DEFVAL(Vector2(0, 0)), DEFVAL(false), DEFVAL(4), DEFVAL(Math::deg2rad((float)45)), DEFVAL(true)});
    MethodBinder::bind_method(D_METHOD("move_and_slide_with_snap", {"linear_velocity", "snap", "up_direction", "stop_on_slope", "max_slides", "floor_max_angle", "infinite_inertia"}), &KinematicBody2D::move_and_slide_with_snap, {DEFVAL(Vector2(0, 0)), DEFVAL(false), DEFVAL(4), DEFVAL(Math::deg2rad((float)45)), DEFVAL(true)});

    MethodBinder::bind_method(D_METHOD("test_move", {"from", "rel_vec", "infinite_inertia"}), &KinematicBody2D::test_move, {DEFVAL(true)});

    MethodBinder::bind_method(D_METHOD("is_on_floor"), &KinematicBody2D::is_on_floor);
    MethodBinder::bind_method(D_METHOD("is_on_ceiling"), &KinematicBody2D::is_on_ceiling);
    MethodBinder::bind_method(D_METHOD("is_on_wall"), &KinematicBody2D::is_on_wall);
    MethodBinder::bind_method(D_METHOD("get_floor_normal"), &KinematicBody2D::get_floor_normal);
    MethodBinder::bind_method(D_METHOD("get_floor_velocity"), &KinematicBody2D::get_floor_velocity);

    MethodBinder::bind_method(D_METHOD("set_safe_margin", {"pixels"}), &KinematicBody2D::set_safe_margin);
    MethodBinder::bind_method(D_METHOD("get_safe_margin"), &KinematicBody2D::get_safe_margin);

    MethodBinder::bind_method(D_METHOD("get_slide_count"), &KinematicBody2D::get_slide_count);
    MethodBinder::bind_method(D_METHOD("get_slide_collision", {"slide_idx"}), &KinematicBody2D::_get_slide_collision);

    MethodBinder::bind_method(D_METHOD("set_sync_to_physics", {"enable"}), &KinematicBody2D::set_sync_to_physics);
    MethodBinder::bind_method(D_METHOD("is_sync_to_physics_enabled"), &KinematicBody2D::is_sync_to_physics_enabled);

    MethodBinder::bind_method(D_METHOD("_direct_state_changed"), &KinematicBody2D::_direct_state_changed);

    ADD_PROPERTY(PropertyInfo(VariantType::FLOAT, "collision/safe_margin", PropertyHint::Range, "0.001,256,0.001"), "set_safe_margin", "get_safe_margin");
    ADD_PROPERTY(PropertyInfo(VariantType::BOOL, "motion/sync_to_physics"), "set_sync_to_physics", "is_sync_to_physics_enabled");
}

KinematicBody2D::KinematicBody2D() :
        PhysicsBody2D(PhysicsServer2D::BODY_MODE_KINEMATIC) {

    margin = 0.08;

    on_floor = false;
    on_ceiling = false;
    on_wall = false;
    sync_to_physics = false;
}
KinematicBody2D::~KinematicBody2D() {
    if (motion_cache) {
        motion_cache->owner = nullptr;
    }

    for (int i = 0; i < slide_colliders.size(); i++) {
        if (slide_colliders[i]) {
            slide_colliders[i]->owner = nullptr;
        }
    }
}

////////////////////////

Vector2 KinematicCollision2D::get_position() const {

    return collision.collision;
}
Vector2 KinematicCollision2D::get_normal() const {
    return collision.normal;
}
Vector2 KinematicCollision2D::get_travel() const {
    return collision.travel;
}
Vector2 KinematicCollision2D::get_remainder() const {
    return collision.remainder;
}
Object *KinematicCollision2D::get_local_shape() const {
    if (!owner) return nullptr;
    uint32_t ownerid = owner->shape_find_owner(collision.local_shape);
    return owner->shape_owner_get_owner(ownerid);
}

Object *KinematicCollision2D::get_collider() const {

    if (collision.collider.is_valid()) {
        return ObjectDB::get_instance(collision.collider);
    }

    return nullptr;
}
ObjectID KinematicCollision2D::get_collider_id() const {

    return collision.collider;
}
Object *KinematicCollision2D::get_collider_shape() const {

    Object *collider = get_collider();
    if (collider) {
        CollisionObject2D *obj2d = object_cast<CollisionObject2D>(collider);
        if (obj2d) {
            uint32_t ownerid = obj2d->shape_find_owner(collision.collider_shape);
            return obj2d->shape_owner_get_owner(ownerid);
        }
    }

    return nullptr;
}
int KinematicCollision2D::get_collider_shape_index() const {

    return collision.collider_shape;
}
Vector2 KinematicCollision2D::get_collider_velocity() const {

    return collision.collider_vel;
}
Variant KinematicCollision2D::get_collider_metadata() const {

    return Variant();
}

void KinematicCollision2D::_bind_methods() {

    MethodBinder::bind_method(D_METHOD("get_position"), &KinematicCollision2D::get_position);
    MethodBinder::bind_method(D_METHOD("get_normal"), &KinematicCollision2D::get_normal);
    MethodBinder::bind_method(D_METHOD("get_travel"), &KinematicCollision2D::get_travel);
    MethodBinder::bind_method(D_METHOD("get_remainder"), &KinematicCollision2D::get_remainder);
    MethodBinder::bind_method(D_METHOD("get_local_shape"), &KinematicCollision2D::get_local_shape);
    MethodBinder::bind_method(D_METHOD("get_collider"), &KinematicCollision2D::get_collider);
    MethodBinder::bind_method(D_METHOD("get_collider_id"), &KinematicCollision2D::get_collider_id);
    MethodBinder::bind_method(D_METHOD("get_collider_shape"), &KinematicCollision2D::get_collider_shape);
    MethodBinder::bind_method(D_METHOD("get_collider_shape_index"), &KinematicCollision2D::get_collider_shape_index);
    MethodBinder::bind_method(D_METHOD("get_collider_velocity"), &KinematicCollision2D::get_collider_velocity);
    MethodBinder::bind_method(D_METHOD("get_collider_metadata"), &KinematicCollision2D::get_collider_metadata);

    ADD_PROPERTY(PropertyInfo(VariantType::VECTOR2, "position"), "", "get_position");
    ADD_PROPERTY(PropertyInfo(VariantType::VECTOR2, "normal"), "", "get_normal");
    ADD_PROPERTY(PropertyInfo(VariantType::VECTOR2, "travel"), "", "get_travel");
    ADD_PROPERTY(PropertyInfo(VariantType::VECTOR2, "remainder"), "", "get_remainder");
    ADD_PROPERTY(PropertyInfo(VariantType::OBJECT, "local_shape"), "", "get_local_shape");
    ADD_PROPERTY(PropertyInfo(VariantType::OBJECT, "collider"), "", "get_collider");
    ADD_PROPERTY(PropertyInfo(VariantType::INT, "collider_id"), "", "get_collider_id");
    ADD_PROPERTY(PropertyInfo(VariantType::OBJECT, "collider_shape"), "", "get_collider_shape");
    ADD_PROPERTY(PropertyInfo(VariantType::INT, "collider_shape_index"), "", "get_collider_shape_index");
    ADD_PROPERTY(PropertyInfo(VariantType::VECTOR2, "collider_velocity"), "", "get_collider_velocity");
    ADD_PROPERTY(PropertyInfo(VariantType::NIL, "collider_metadata", PropertyHint::None, "", PROPERTY_USAGE_NIL_IS_VARIANT), "", "get_collider_metadata");
}

KinematicCollision2D::KinematicCollision2D() {
    collision.collider = 0;
    collision.collider_shape = 0;
    collision.local_shape = 0;
    owner = nullptr;
}
