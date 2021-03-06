/*************************************************************************/
/*  rendering_server_raster.cpp                                             */
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

#include "rendering_server_raster.h"

#include "core/external_profiler.h"
#include "core/os/os.h"
#include "core/ecs_registry.h"
#include "core/project_settings.h"
#include "rendering_server_canvas.h"
#include "rendering_server_globals.h"
#include "rendering_server_scene.h"

// careful, these may run in different threads than the visual server

int RenderingServerRaster::changes = 0;

/* BLACK BARS */

void RenderingServerRaster::black_bars_set_margins(int p_left, int p_top, int p_right, int p_bottom) {

    black_margin[(int8_t)Margin::Left] = p_left;
    black_margin[(int8_t)Margin::Top] = p_top;
    black_margin[(int8_t)Margin::Right] = p_right;
    black_margin[(int8_t)Margin::Bottom] = p_bottom;
}

void RenderingServerRaster::black_bars_set_images(RID p_left, RID p_top, RID p_right, RID p_bottom) {

    black_image[(int8_t)Margin::Left] = p_left;
    black_image[(int8_t)Margin::Top] = p_top;
    black_image[(int8_t)Margin::Right] = p_right;
    black_image[(int8_t)Margin::Bottom] = p_bottom;
}

void RenderingServerRaster::_draw_margins() {

    VSG::canvas_render->draw_window_margins(black_margin, black_image);
}

/* FREE */

void RenderingServerRaster::free_rid(RID p_rid) {

    if (VSG::storage->free(p_rid))
        return;
    if (VSG::canvas->free(p_rid))
        return;
    if (VSG::viewport->free(p_rid))
        return;
    if (VSG::scene->free(p_rid))
        return;
    if (VSG::scene_render->free(p_rid))
        return;
}

/* EVENT QUEUING */

void RenderingServerRaster::request_frame_drawn_callback(Callable&& cb) {

    ERR_FAIL_COND(cb.is_null());

    frame_drawn_callbacks.emplace_back(eastl::move(cb));
}

void RenderingServerRaster::draw(bool p_swap_buffers, double frame_step) {
    SCOPE_AUTONAMED;

    //needs to be done before changes is reset to 0, to not force the editor to redraw
    RenderingServer::get_singleton()->emit_signal("frame_pre_draw");

    changes = 0;

    VSG::rasterizer->begin_frame(frame_step);
    PROFILER_STARTFRAME("viewport");

    VSG::scene->update_dirty_instances(); //update scene stuff

    VSG::viewport->draw_viewports();
    VSG::scene->render_probes();
    _draw_margins();
    VSG::rasterizer->end_frame(p_swap_buffers);
    PROFILER_ENDFRAME("viewport");

    {
        SCOPE_PROFILE("frame_drawn_callbacks");
        while (!frame_drawn_callbacks.empty()) {

            Object *obj = frame_drawn_callbacks.front().get_object();
            if (obj) {
                Callable::CallError ce;
                Variant ret;
                frame_drawn_callbacks.front().call(nullptr,0,ret,ce);
                if (ce.error != Callable::CallError::CALL_OK) {
                    String err = Variant::get_callable_error_text(frame_drawn_callbacks.front(), nullptr, 0, ce);
                    ERR_PRINT("Error calling frame drawn function: " + err);
                }
            }

            frame_drawn_callbacks.pop_front();
        }
    }
    {
        SCOPE_PROFILE("frame_post_draw");
        RenderingServer::get_singleton()->emit_signal("frame_post_draw");
    }
}

bool RenderingServerRaster::has_changed() const {

    return changes > 0;
}
void RenderingServerRaster::init() {

    VSG::rasterizer->initialize();
}
void RenderingServerRaster::finish() {

    VSG::rasterizer->finalize();
}

/* STATUS INFORMATION */

int RenderingServerRaster::get_render_info(RS::RenderInfo p_info) {

    return VSG::storage->get_render_info(p_info);
}
const char *RenderingServerRaster::get_video_adapter_name() const {

    return VSG::storage->get_video_adapter_name();
}

const char *RenderingServerRaster::get_video_adapter_vendor() const {

    return VSG::storage->get_video_adapter_vendor();
}
/* TESTING */

void RenderingServerRaster::set_boot_image(const Ref<Image> &p_image, const Color &p_color, bool p_scale, bool p_use_filter) {

    redraw_request();
    VSG::rasterizer->set_boot_image(p_image, p_color, p_scale, p_use_filter);
}
void RenderingServerRaster::set_default_clear_color(const Color &p_color) {
    VSG::viewport->set_default_clear_color(p_color);
}

bool RenderingServerRaster::has_feature(RS::Features p_feature) const {

    return false;
}

bool RenderingServerRaster::has_os_feature(const StringName &p_feature) const {

    return VSG::storage->has_os_feature(p_feature);
}

void RenderingServerRaster::set_debug_generate_wireframes(bool p_generate) {

    VSG::storage->set_debug_generate_wireframes(p_generate);
}

void RenderingServerRaster::call_set_use_vsync(bool p_enable) {
    OS::get_singleton()->_set_use_vsync(p_enable);
}

// bool VisualServerRaster::is_low_end() const {
//     return VSG::rasterizer->is_low_end();
// }
RenderingServerRaster::RenderingServerRaster() {
    submission_thread_singleton = this;
    VSG::canvas = memnew(VisualServerCanvas);
    VSG::viewport = memnew(VisualServerViewport);
    VSG::scene = memnew(VisualServerScene);
    VSG::rasterizer = Rasterizer::create();
    VSG::storage = VSG::rasterizer->get_storage();
    VSG::canvas_render = VSG::rasterizer->get_canvas();
    VSG::scene_render = VSG::rasterizer->get_scene();
    VSG::ecs = memnew(ECS_Registry);

    for (int i = 0; i < 4; i++) {
        black_margin[i] = 0;
        black_image[i] = RID();
    }
}

RenderingServerRaster::~RenderingServerRaster() {
    submission_thread_singleton = nullptr;

    memdelete(VSG::canvas);
    memdelete(VSG::viewport);
    memdelete(VSG::rasterizer);
    memdelete(VSG::scene);
    memdelete(VSG::ecs);
}
