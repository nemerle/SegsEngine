/*************************************************************************/
/*  physics_2d_server_wrap_mt.h                                          */
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

#include "core/command_queue_mt.h"
#include "core/os/thread.h"
#include "core/os/os.h"
#include "core/rid.h"
#include "core/project_settings.h"
#include "servers/physics_server_2d.h"

#ifdef DEBUG_SYNC
#define SYNC_DEBUG print_line("sync on: " + String(__FUNCTION__));
#else
#define SYNC_DEBUG
#endif

class Physics2DServerWrapMT : public PhysicsServer2D {


    mutable CommandQueueMT command_queue;
    Thread::ID main_thread;
    Thread thread;
    Mutex alloc_mutex;
    Semaphore step_sem;
    mutable PhysicsServer2D *physics_server_2d;
    int step_pending;
    int pool_max_size;

    volatile bool exit;
    volatile bool step_thread_up;
    bool create_thread;
    bool first_frame;


    static void _thread_callback(void *_instance);
    void thread_loop();


    void thread_step(real_t p_delta);
    void thread_flush();

    void thread_exit();


public:
#define ServerName PhysicsServer2D
#define ServerNameWrapMT Physics2DServerWrapMT
#define server_name physics_server_2d
#include "servers/server_wrap_mt_common.h"

    //FUNC1RID(shape,ShapeType); todo fix
    FUNCRID(line_shape)
    FUNCRID(ray_shape)
    FUNCRID(segment_shape)
    FUNCRID(circle_shape)
    FUNCRID(rectangle_shape)
    FUNCRID(capsule_shape)
    FUNCRID(convex_polygon_shape)
    FUNCRID(concave_polygon_shape)

    FUNC2(shape_set_data, RID, const Variant &);
    FUNC2(shape_set_custom_solver_bias, RID, real_t);

    FUNC1RC(ShapeType, shape_get_type, RID);
    FUNC1RC(Variant, shape_get_data, RID);
    FUNC1RC(real_t, shape_get_custom_solver_bias, RID);

    //these work well, but should be used from the main thread only
    bool shape_collide(RID p_shape_A, const Transform2D &p_xform_A, const Vector2 &p_motion_A, RID p_shape_B, const Transform2D &p_xform_B, const Vector2 &p_motion_B, Vector2 *r_results, int p_result_max, int &r_result_count) override {

        ERR_FAIL_COND_V(main_thread != Thread::get_caller_id(), false);
        return physics_server_2d->shape_collide(p_shape_A, p_xform_A, p_motion_A, p_shape_B, p_xform_B, p_motion_B, r_results, p_result_max, r_result_count);
    }

    /* SPACE API */

    FUNCRID(space);
    FUNC2(space_set_active, RID, bool);
    FUNC1RC(bool, space_is_active, RID);

    FUNC3(space_set_param, RID, SpaceParameter, real_t);
    FUNC2RC(real_t, space_get_param, RID, SpaceParameter);

    // this function only works on physics process, errors and returns null otherwise
    PhysicsDirectSpaceState2D *space_get_direct_state(RID p_space) override {

        ERR_FAIL_COND_V(main_thread != Thread::get_caller_id(), nullptr);
        return physics_server_2d->space_get_direct_state(p_space);
    }

    FUNC2(space_set_debug_contacts, RID, int);
    const Vector<Vector2> &space_get_contacts(RID p_space) const override {

        ERR_FAIL_COND_V(main_thread != Thread::get_caller_id(), null_vec2_pvec);
        return physics_server_2d->space_get_contacts(p_space);
    }

    int space_get_contact_count(RID p_space) const override {

        ERR_FAIL_COND_V(main_thread != Thread::get_caller_id(), 0);
        return physics_server_2d->space_get_contact_count(p_space);
    }

    /* AREA API */

    //FUNC0RID(area);
    FUNCRID(area);

    FUNC2(area_set_space, RID, RID);
    FUNC1RC(RID, area_get_space, RID);

    FUNC2(area_set_space_override_mode, RID, AreaSpaceOverrideMode);
    FUNC1RC(AreaSpaceOverrideMode, area_get_space_override_mode, RID);

    FUNC4(area_add_shape, RID, RID, const Transform2D &, bool);
    FUNC3(area_set_shape, RID, int, RID);
    FUNC3(area_set_shape_transform, RID, int, const Transform2D &);
    FUNC3(area_set_shape_disabled, RID, int, bool);

    FUNC1RC(int, area_get_shape_count, RID);
    FUNC2RC(RID, area_get_shape, RID, int);
    FUNC2RC(Transform2D, area_get_shape_transform, RID, int);
    FUNC2(area_remove_shape, RID, int);
    FUNC1(area_clear_shapes, RID);

    FUNC2(area_attach_object_instance_id, RID, ObjectID);
    FUNC1RC(ObjectID, area_get_object_instance_id, RID);

    FUNC2(area_attach_canvas_instance_id, RID, ObjectID);
    FUNC1RC(ObjectID, area_get_canvas_instance_id, RID);

    FUNC3(area_set_param, RID, AreaParameter, const Variant &);
    FUNC2(area_set_transform, RID, const Transform2D &);

    FUNC2RC(Variant, area_get_param, RID, AreaParameter);
    FUNC1RC(Transform2D, area_get_transform, RID);

    FUNC2(area_set_collision_mask, RID, uint32_t);
    FUNC2(area_set_collision_layer, RID, uint32_t);

    FUNC2(area_set_monitorable, RID, bool);
    FUNC2(area_set_pickable, RID, bool);

    void area_set_monitor_callback(RID p1, Callable && p2) override
    {
        if (Thread::get_caller_id() != server_thread)
        {
            command_queue.push([this,p1, p2{ eastl::move(p2) }]() mutable {server_name->area_set_monitor_callback(p1, eastl::move(p2));});
        } else { server_name->area_set_monitor_callback(p1, eastl::move(p2)); }
    }
    void area_set_area_monitor_callback(RID p1, Callable&& p2) override
    {
        if (Thread::get_caller_id() != server_thread)
        {
            command_queue.push([this,p1,p2{eastl::move(p2)}]()mutable {server_name->area_set_area_monitor_callback(p1, eastl::move(p2));});
        } else {
            server_name->area_set_area_monitor_callback(p1, eastl::move(p2));
        }
    }

    /* BODY API */

    //FUNC2RID(body,BodyMode,bool);
    FUNCRID(body)

    FUNC2(body_set_space, RID, RID);
    FUNC1RC(RID, body_get_space, RID);

    FUNC2(body_set_mode, RID, BodyMode);
    FUNC1RC(BodyMode, body_get_mode, RID);

    FUNC4(body_add_shape, RID, RID, const Transform2D &, bool);
    FUNC3(body_set_shape, RID, int, RID);
    FUNC3(body_set_shape_transform, RID, int, const Transform2D &);
    FUNC3(body_set_shape_metadata, RID, int, const Variant &);

    FUNC1RC(int, body_get_shape_count, RID);
    FUNC2RC(Transform2D, body_get_shape_transform, RID, int);
    FUNC2RC(Variant, body_get_shape_metadata, RID, int);
    FUNC2RC(RID, body_get_shape, RID, int);

    FUNC3(body_set_shape_disabled, RID, int, bool);
    FUNC4(body_set_shape_as_one_way_collision, RID, int, bool, float);

    FUNC2(body_remove_shape, RID, int);
    FUNC1(body_clear_shapes, RID);

    FUNC2(body_attach_object_instance_id, RID, ObjectID);
    FUNC1RC(ObjectID, body_get_object_instance_id, RID);

    FUNC2(body_attach_canvas_instance_id, RID, ObjectID);
    FUNC1RC(ObjectID, body_get_canvas_instance_id, RID);

    FUNC2(body_set_continuous_collision_detection_mode, RID, CCDMode);
    FUNC1RC(CCDMode, body_get_continuous_collision_detection_mode, RID);

    FUNC2(body_set_collision_layer, RID, uint32_t);
    FUNC1RC(uint32_t, body_get_collision_layer, RID);

    FUNC2(body_set_collision_mask, RID, uint32_t);
    FUNC1RC(uint32_t, body_get_collision_mask, RID);

    FUNC3(body_set_param, RID, BodyParameter, real_t);
    FUNC2RC(real_t, body_get_param, RID, BodyParameter);

    FUNC3(body_set_state, RID, BodyState, const Variant &);
    FUNC2RC(Variant, body_get_state, RID, BodyState);

    FUNC2(body_set_applied_force, RID, const Vector2 &);
    FUNC1RC(Vector2, body_get_applied_force, RID);

    FUNC2(body_set_applied_torque, RID, real_t);
    FUNC1RC(real_t, body_get_applied_torque, RID);

    FUNC2(body_add_central_force, RID, const Vector2 &);
    FUNC3(body_add_force, RID, const Vector2 &, const Vector2 &);
    FUNC2(body_add_torque, RID, real_t);
    FUNC2(body_apply_central_impulse, RID, const Vector2 &);
    FUNC2(body_apply_torque_impulse, RID, real_t);
    FUNC3(body_apply_impulse, RID, const Vector2 &, const Vector2 &);
    FUNC2(body_set_axis_velocity, RID, const Vector2 &);

    FUNC2(body_add_collision_exception, RID, RID);
    FUNC2(body_remove_collision_exception, RID, RID);
    FUNC2S(body_get_collision_exceptions, RID, Vector<RID> *);

    FUNC2(body_set_max_contacts_reported, RID, int);
    FUNC1RC(int, body_get_max_contacts_reported, RID);

    FUNC2(body_set_contacts_reported_depth_threshold, RID, real_t);
    FUNC1RC(real_t, body_get_contacts_reported_depth_threshold, RID);

    FUNC2(body_set_omit_force_integration, RID, bool);
    FUNC1RC(bool, body_is_omitting_force_integration, RID);

    void body_set_force_integration_callback(RID p1, Callable && p2) override
    {
        if (Thread::get_caller_id() != server_thread)
        {
            command_queue.push([this,p1,p2=eastl::move(p2)]() mutable {server_name->body_set_force_integration_callback(p1, eastl::move(p2));});
        } else { server_name->body_set_force_integration_callback(p1, eastl::move(p2)); }
    };

    bool body_collide_shape(RID p_body, int p_body_shape, RID p_shape, const Transform2D &p_shape_xform, const Vector2 &p_motion, Vector2 *r_results, int p_result_max, int &r_result_count) override {
        return physics_server_2d->body_collide_shape(p_body, p_body_shape, p_shape, p_shape_xform, p_motion, r_results, p_result_max, r_result_count);
    }

    FUNC2(body_set_pickable, RID, bool);

    bool body_test_motion(RID p_body, const Transform2D &p_from, const Vector2 &p_motion, bool p_infinite_inertia, real_t p_margin = 0.001, MotionResult *r_result = nullptr, bool p_exclude_raycast_shapes = true) override {

        ERR_FAIL_COND_V(main_thread != Thread::get_caller_id(), false);
        return physics_server_2d->body_test_motion(p_body, p_from, p_motion, p_infinite_inertia, p_margin, r_result, p_exclude_raycast_shapes);
    }

    int body_test_ray_separation(RID p_body, const Transform2D &p_transform, bool p_infinite_inertia, Vector2 &r_recover_motion, SeparationResult *r_results, int p_result_max, float p_margin = 0.001) override {

        ERR_FAIL_COND_V(main_thread != Thread::get_caller_id(), false);
        return physics_server_2d->body_test_ray_separation(p_body, p_transform, p_infinite_inertia, r_recover_motion, r_results, p_result_max, p_margin);
    }

    // this function only works on physics process, errors and returns null otherwise
    PhysicsDirectBodyState2D *body_get_direct_state(RID p_body) override {

        ERR_FAIL_COND_V(main_thread != Thread::get_caller_id(), nullptr);
        return physics_server_2d->body_get_direct_state(p_body);
    }

    /* JOINT API */

    FUNC3(joint_set_param, RID, JointParam, real_t);
    FUNC2RC(real_t, joint_get_param, RID, JointParam);

    FUNC2(joint_disable_collisions_between_bodies, RID, const bool);
    FUNC1RC(bool, joint_is_disabled_collisions_between_bodies, RID);

    ///FUNC3RID(pin_joint,const Vector2&,RID,RID);
    ///FUNC5RID(groove_joint,const Vector2&,const Vector2&,const Vector2&,RID,RID);
    ///FUNC4RID(damped_spring_joint,const Vector2&,const Vector2&,RID,RID);

    //TODO need to convert this to FUNCRID, but it's a hassle..

    FUNC3R(RID, pin_joint_create, const Vector2 &, RID, RID);
    FUNC5R(RID, groove_joint_create, const Vector2 &, const Vector2 &, const Vector2 &, RID, RID);
    FUNC4R(RID, damped_spring_joint_create, const Vector2 &, const Vector2 &, RID, RID);

    FUNC3(pin_joint_set_param, RID, PinJointParam, real_t);
    FUNC2RC(real_t, pin_joint_get_param, RID, PinJointParam);

    FUNC3(damped_string_joint_set_param, RID, DampedStringParam, real_t);
    FUNC2RC(real_t, damped_string_joint_get_param, RID, DampedStringParam);

    FUNC1RC(JointType, joint_get_type, RID);

    /* MISC */

    FUNC1(free_rid, RID);
    FUNC1(set_active, bool);

    void init() override;
    void step(real_t p_step) override;
    void sync() override;
    void end_sync() override;
    void flush_queries() override;
    void finish() override;

    bool is_flushing_queries() const override {
        return physics_server_2d->is_flushing_queries();
    }

    int get_process_info(ProcessInfo p_info) override {
        return physics_server_2d->get_process_info(p_info);
    }

    Physics2DServerWrapMT(PhysicsServer2D *p_contained, bool p_create_thread);
    ~Physics2DServerWrapMT() override;

    template <class T>
    static PhysicsServer2D *init_server() {

        auto tm = T_GLOBAL_DEF<OS::RenderThreadMode>("physics/2d/thread_model", OS::RENDER_THREAD_SAFE);
        assert(tm != 0); // single unsafe
        if (tm == OS::RENDER_THREAD_SAFE) // single safe
            return memnew(Physics2DServerWrapMT(memnew(T), false));
        else // multi threaded
            return memnew(Physics2DServerWrapMT(memnew(T), true));
    }

#undef ServerNameWrapMT
#undef ServerName
#undef server_name
};

#ifdef DEBUG_SYNC
#undef DEBUG_SYNC
#endif
#undef SYNC_DEBUG
