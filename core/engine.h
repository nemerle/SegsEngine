/*************************************************************************/
/*  engine.h                                                             */
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

#include "core/hash_map.h"
#include "core/vector.h"
#include "core/string_name.h"
#include "core/dictionary.h"

class GODOT_EXPORT Engine {

public:
    struct Singleton {
        StringName name;
        Object *ptr;
        Singleton(StringName p_name = StringName(), Object *p_ptr = nullptr);
    };

private:
    friend class Main;

    Vector<Singleton> singletons;
    HashMap<StringName, Object *> singleton_ptrs;
    uint64_t frames_drawn=0;
    uint64_t _frame_ticks=0;
    uint32_t _frame_delay=0;
    float _frame_step=0.0f;

    int ips = 60;
    int _target_fps = 0;
    uint64_t _physics_frames = 0;
    float physics_jitter_fix = 0.5f;
    float _fps = 1.0f;
    float _time_scale = 1.0f;
    float _physics_interpolation_fraction = 0.0f;

    uint64_t _idle_frames=0;
    bool _pixel_snap=false;
    bool _snap_2d_transforms=false;
    bool _snap_2d_viewports;
    bool _in_physics=false;
    bool editor_hint=false;
    bool abort_on_gpu_errors=false;

    static Engine *singleton;

public:
    static Engine *get_singleton();

    virtual void set_iterations_per_second(int p_ips);
    virtual int get_iterations_per_second() const;

    void set_physics_jitter_fix(float p_threshold);
    float get_physics_jitter_fix() const;

    virtual void set_target_fps(int p_fps);
    virtual int get_target_fps() const;

    virtual float get_frames_per_second() const { return _fps; }

    uint64_t get_frames_drawn();

    uint64_t get_physics_frames() const { return _physics_frames; }
    uint64_t get_idle_frames() const { return _idle_frames; }
    bool is_in_physics_frame() const { return _in_physics; }
    uint64_t get_idle_frame_ticks() const { return _frame_ticks; }
    float get_idle_frame_step() const { return _frame_step; }
    float get_physics_interpolation_fraction() const { return _physics_interpolation_fraction; }

    void set_time_scale(float p_scale);
    float get_time_scale() const;

    void set_frame_delay(uint32_t p_msec);
    uint32_t get_frame_delay() const;

    void add_singleton(const Singleton &p_singleton);
    const Vector<Singleton> &get_singletons() { return singletons; }
    bool has_singleton(const StringName &p_name) const;
    Object *get_named_singleton(const StringName &p_name) const;

    bool get_use_pixel_snap() const { return _pixel_snap; }
    bool get_snap_2d_transforms() const { return _snap_2d_transforms; }
    bool get_snap_2d_viewports() const { return _snap_2d_viewports; }

#ifdef TOOLS_ENABLED
    void set_editor_hint(bool p_enabled) { editor_hint = p_enabled; }
    bool is_editor_hint() const { return editor_hint; }
#else
    void set_editor_hint(bool p_enabled) {}
    bool is_editor_hint() const { return false; }
#endif

    Dictionary get_version_info() const;
    Dictionary get_author_info() const;
    Array get_copyright_info() const;
    Dictionary get_donor_info() const;
    Dictionary get_license_info() const;
    String get_license_text() const;

    bool is_abort_on_gpu_errors_enabled() const { return abort_on_gpu_errors; }

    Engine();
    virtual ~Engine() = default;
};

