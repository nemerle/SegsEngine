/*************************************************************************/
/*  animated_sprite.h                                                    */
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

#include "scene/2d/node_2d.h"
#include "scene/resources/texture.h"
#include "core/string.h"

class GODOT_EXPORT SpriteFrames : public Resource {

    GDCLASS(SpriteFrames,Resource)

    struct Anim {

        float speed;
        bool loop;
        PoolVector<Ref<Texture> > frames;

        Anim() {
            loop = true;
            speed = 5;
        }

        StringName normal_name;
    };

    HashMap<StringName, Anim> animations;

    static void report_missing_animation(const char *name);
public:
    Array _get_frames() const;
    void _set_frames(const Array &p_frames);

    Array _get_animations() const;
    void _set_animations(const Array &p_animations);

    PoolVector<String> _get_animation_list() const;
protected:
    static void _bind_methods();

public:
    void add_animation(const StringName &p_anim);
    bool has_animation(const StringName &p_anim) const;
    void remove_animation(const StringName &p_anim);
    void rename_animation(const StringName &p_prev, const StringName &p_next);

    void get_animation_list(List<StringName> *r_animations) const;
    PoolVector<String> get_animation_names() const;

    const HashMap<StringName, Anim> & animation_name_map() const { return animations; }

    void set_animation_speed(const StringName &p_anim, float p_fps);
    float get_animation_speed(const StringName &p_anim) const;

    void set_animation_loop(const StringName &p_anim, bool p_loop);
    bool get_animation_loop(const StringName &p_anim) const;

    void add_frame(const StringName &p_anim, const Ref<Texture> &p_frame, int p_at_pos = -1);
    int get_frame_count(const StringName &p_anim) const;
    _FORCE_INLINE_ Ref<Texture> get_frame(const StringName &p_anim, int p_idx) const {

        const HashMap<StringName, Anim>::const_iterator E = animations.find(p_anim);
        if (unlikely(E==animations.end())) {
            report_missing_animation(p_anim.asCString());
            _err_print_error(FUNCTION_STR, __FILE__, __LINE__, "Animation missing: " + String(p_anim),{});
            return Ref<Texture>();
        }
        ERR_FAIL_COND_V(p_idx < 0, Ref<Texture>());
        if (p_idx >= E->second.frames.size())
            return Ref<Texture>();

        return E->second.frames[p_idx];
    }

    _FORCE_INLINE_ Ref<Texture> get_normal_frame(const StringName &p_anim, int p_idx) const {

        const HashMap<StringName, Anim>::const_iterator E = animations.find(p_anim);
        if (unlikely(E==animations.end())) {
            report_missing_animation(p_anim.asCString());
            _err_print_error(FUNCTION_STR, __FILE__, __LINE__, "Animation missing: " + String(p_anim),{});
            return Ref<Texture>();
        }
        ERR_FAIL_COND_V(p_idx < 0, Ref<Texture>());

        const HashMap<StringName, Anim>::const_iterator EN = animations.find(E->second.normal_name);

        if (EN==animations.end() || p_idx >= EN->second.frames.size())
            return Ref<Texture>();

        return EN->second.frames[p_idx];
    }

    void set_frame(const StringName &p_anim, int p_idx, const Ref<Texture> &p_frame) {
        HashMap<StringName, Anim>::iterator E = animations.find(p_anim);
        if (unlikely(E==animations.end())) {
            report_missing_animation(p_anim.asCString());
            _err_print_error(FUNCTION_STR, __FILE__, __LINE__, "Animation missing: " + String(p_anim),{});
            return;
        }
        ERR_FAIL_COND(p_idx < 0);
        if (p_idx >= E->second.frames.size())
            return;
        E->second.frames.set(p_idx,p_frame);
    }
    void remove_frame(const StringName &p_anim, int p_idx);
    void clear(const StringName &p_anim);
    void clear_all();

    SpriteFrames();
};

class GODOT_EXPORT AnimatedSprite2D : public Node2D {

    GDCLASS(AnimatedSprite2D,Node2D)

    Ref<SpriteFrames> frames;
    bool playing;
    bool backwards;
    StringName animation;
    int frame;
    float speed_scale;

    bool centered;
    Point2 offset;

    bool is_over;
    float timeout;

    bool hflip;
    bool vflip;

    void _res_changed();
public:
    float _get_frame_duration();
    void _reset_timeout();
    void _set_playing(bool p_playing);
    bool _is_playing() const;
    Rect2 _get_rect() const;

protected:
    static void _bind_methods();
    void _notification(int p_what);
    void _validate_property(PropertyInfo &property) const override;

public:
#ifdef TOOLS_ENABLED
    Dictionary _edit_get_state() const override;
    void _edit_set_state(const Dictionary &p_state) override;

    void _edit_set_pivot(const Point2 &p_pivot) override;
    Point2 _edit_get_pivot() const override;
    bool _edit_use_pivot() const override;
    Rect2 _edit_get_rect() const override;
    bool _edit_use_rect() const override;
#endif
    Rect2 get_anchorable_rect() const override;

    void set_sprite_frames(const Ref<SpriteFrames> &p_frames);
    Ref<SpriteFrames> get_sprite_frames() const;

    void play(const StringName &p_animation = StringName(), const bool p_backwards = false);
    void stop();
    bool is_playing() const;

    void set_animation(const StringName &p_animation);
    StringName get_animation() const;

    void set_frame(int p_frame);
    int get_frame() const;

    void set_speed_scale(float p_speed_scale);
    float get_speed_scale() const;

    void set_centered(bool p_center);
    bool is_centered() const;

    void set_offset(const Point2 &p_offset);
    Point2 get_offset() const;

    void set_flip_h(bool p_flip);
    bool is_flipped_h() const;

    void set_flip_v(bool p_flip);
    bool is_flipped_v() const;

    void set_modulate(const Color &p_color);
    Color get_modulate() const;

    String get_configuration_warning() const override;
    AnimatedSprite2D();
};
