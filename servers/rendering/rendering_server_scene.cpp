/*************************************************************************/
/*  rendering_server_scene.cpp                                              */
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

#include "rendering_server_scene.h"

#include "rendering_server_globals.h"
#include "rendering_server_raster.h"

#include "core/ecs_registry.h"
#include "core/external_profiler.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/map.h"
#include <new>

namespace {
struct InstanceGeometryData {
    List<VisualServerScene::Instance *> lighting;
    List<VisualServerScene::Instance *> reflection_probes;
    List<VisualServerScene::Instance *> gi_probes;
    List<VisualServerScene::Instance *> lightmap_captures;
};

struct InstanceComponent {
    VisualServerScene::Instance * instance;
    InstanceComponent(VisualServerScene::Instance *c) : instance(c) {}
    InstanceComponent() = default;
};
struct Dirty {
    //aabb stuff
    bool update_aabb : 1;
    bool update_materials : 1;

    constexpr Dirty() : update_aabb(false), update_materials(false) { }

    constexpr Dirty(bool aabb,bool material) : update_aabb(aabb),update_materials(material) {

    }

};
struct GeometryComponent {
    InstanceGeometryData *Data;
    bool lighting_dirty : 1;
    bool can_cast_shadows : 1;
    bool material_is_animated : 1;
    bool reflection_dirty : 1;
    bool gi_probes_dirty: 1;

    GeometryComponent() {
        Data = nullptr;
        lighting_dirty = false;
        reflection_dirty = true;
        can_cast_shadows = true;
        material_is_animated = true;
        gi_probes_dirty = true;
    }
    GeometryComponent(InstanceGeometryData * _Data) {
        Data = _Data;
        lighting_dirty = false;
        reflection_dirty = true;
        can_cast_shadows = true;
        material_is_animated = true;
        gi_probes_dirty = true;
    }
};
struct InstanceBoundsComponent {

    AABB aabb;
    AABB transformed_aabb;
    AABB custom_aabb; // <Zylann> would using aabb directly with a bool be better?
    float extra_margin = 0.0f;
    bool use_custom_aabb = false;

};
template<typename T>
bool has_component(entt::entity id) {
    return VSG::ecs->registry.valid(id) && VSG::ecs->registry.has<T>(id);
}
template<typename T>
T & get_component(RID id) {

    CRASH_COND(!VSG::ecs->registry.valid(id.eid) );
    CRASH_COND(!VSG::ecs->registry.has<T>(id.eid) );

    return VSG::ecs->registry.get<T>(id.eid);
}
template<typename T>
void clear_component(RID id) {
    if (VSG::ecs->registry.valid(id.eid) && VSG::ecs->registry.has<T>(id.eid))
    {
        VSG::ecs->registry.remove<T>(id.eid);
    }
}
InstanceGeometryData *get_instance_geometry(RID id) {

    if (has_component<GeometryComponent>(id.eid)) {
        return VSG::ecs->registry.get<GeometryComponent>(id.eid).Data;
    }
    return nullptr;
}

void set_dirty(RID id, bool p_update_aabb, bool p_update_materials) {

    auto &reg = VSG::ecs->registry;
    if (!has_component<Dirty>(id.eid)) {
        reg.emplace_or_replace<Dirty>(id.eid, p_update_aabb,p_update_materials);
    }
    else if(p_update_aabb|| p_update_materials) {
        auto &c_data(get_component<Dirty>(id));
        c_data.update_aabb |= p_update_aabb;
        c_data.update_materials |= p_update_materials;
    }
}
_FORCE_INLINE_ static uint32_t _gi_bake_find_cell(
        const VisualServerScene::GIProbeDataCell *cells, int x, int y, int z, int p_cell_subdiv) {

    uint32_t cell = 0;

    int ofs_x = 0;
    int ofs_y = 0;
    int ofs_z = 0;
    int size = 1 << (p_cell_subdiv - 1);
    int half = size / 2;

    if (x < 0 || x >= size)
        return ~0U;
    if (y < 0 || y >= size)
        return ~0U;
    if (z < 0 || z >= size)
        return ~0U;

    for (int i = 0; i < p_cell_subdiv - 1; i++) {

        const VisualServerScene::GIProbeDataCell *bc = &cells[cell];

        int child = 0;
        if (x >= ofs_x + half) {
            child |= 1;
            ofs_x += half;
        }
        if (y >= ofs_y + half) {
            child |= 2;
            ofs_y += half;
        }
        if (z >= ofs_z + half) {
            child |= 4;
            ofs_z += half;
        }

        cell = bc->children[child];
        if (cell == 0xFFFFFFFF)
            return 0xFFFFFFFF;

        half >>= 1;
    }

    return cell;
}

static float _get_normal_advance(const Vector3 &p_normal) {

    Vector3 normal = p_normal;
    Vector3 unorm = normal.abs();

    if ((unorm.x >= unorm.y) && (unorm.x >= unorm.z)) {
        // x code
        unorm = Vector3(copysignf(1.0f,normal.x), 0.0, 0.0);
    } else if ((unorm.y > unorm.x) && (unorm.y >= unorm.z)) {
        // y code
        unorm = Vector3(0.0, copysignf(1.0f,normal.y), 0.0);
    } else if ((unorm.z > unorm.x) && (unorm.z > unorm.y)) {
        // z code
        unorm = Vector3(0.0, 0.0f, copysignf(1.0f,normal.z));
    } else {
        // oh-no we messed up code
        // has to be
        unorm = Vector3(1.0, 0.0, 0.0);
    }

    return 1.0f / normal.dot(unorm);
}

static void _bake_gi_downscale_light(int p_idx, int p_level, const VisualServerScene::GIProbeDataCell *p_cells,
        const VisualServerScene::GIProbeDataHeader *p_header, VisualServerScene::InstanceGIProbeData::LocalData *p_local_data, float p_propagate) {

    //average light to upper level

    float divisor = 0;
    float sum[3] = { 0.0, 0.0, 0.0 };

    for (int i = 0; i < 8; i++) {

        uint32_t child = p_cells[p_idx].children[i];

        if (child == 0xFFFFFFFF)
            continue;

        if (p_level + 1 < (int)p_header->cell_subdiv - 1) {
            _bake_gi_downscale_light(child, p_level + 1, p_cells, p_header, p_local_data, p_propagate);
        }

        sum[0] += p_local_data[child].energy[0];
        sum[1] += p_local_data[child].energy[1];
        sum[2] += p_local_data[child].energy[2];
        divisor += 1.0f;
    }

    divisor = Math::lerp((float)8.0, divisor, p_propagate);
    sum[0] /= divisor;
    sum[1] /= divisor;
    sum[2] /= divisor;

    //divide by eight for average
    p_local_data[p_idx].energy[0] = Math::fast_ftoi(sum[0]);
    p_local_data[p_idx].energy[1] = Math::fast_ftoi(sum[1]);
    p_local_data[p_idx].energy[2] = Math::fast_ftoi(sum[2]);
}
void _gi_probe_fill_local_data(int p_idx, int p_level, int p_x, int p_y, int p_z,
        const VisualServerScene::GIProbeDataCell *p_cell, const VisualServerScene::GIProbeDataHeader *p_header,
        VisualServerScene::InstanceGIProbeData::LocalData *p_local_data, Vector<uint32_t> *prev_cell) {

    if ((uint32_t)p_level == p_header->cell_subdiv - 1) {

        Vector3 emission;
        emission.x = (p_cell[p_idx].emission >> 24) / 255.0f;
        emission.y = ((p_cell[p_idx].emission >> 16) & 0xFF) / 255.0f;
        emission.z = ((p_cell[p_idx].emission >> 8) & 0xFF) / 255.0f;
        float l = (p_cell[p_idx].emission & 0xFF) / 255.0f;
        l *= 8.0f;

        emission *= l;

        p_local_data[p_idx].energy[0] = uint16_t(emission.x * 1024); //go from 0 to 1024 for light
        p_local_data[p_idx].energy[1] = uint16_t(emission.y * 1024); //go from 0 to 1024 for light
        p_local_data[p_idx].energy[2] = uint16_t(emission.z * 1024); //go from 0 to 1024 for light
    } else {

        p_local_data[p_idx].energy[0] = 0;
        p_local_data[p_idx].energy[1] = 0;
        p_local_data[p_idx].energy[2] = 0;

        int half = (1 << (p_header->cell_subdiv - 1)) >> (p_level + 1);

        for (int i = 0; i < 8; i++) {

            uint32_t child = p_cell[p_idx].children[i];

            if (child == 0xFFFFFFFF)
                continue;

            int x = p_x;
            int y = p_y;
            int z = p_z;

            if (i & 1)
                x += half;
            if (i & 2)
                y += half;
            if (i & 4)
                z += half;

            _gi_probe_fill_local_data(child, p_level + 1, x, y, z, p_cell, p_header, p_local_data, prev_cell);
        }
    }

    //position for each part of the mipmaped texture
    p_local_data[p_idx].pos[0] = p_x >> (p_header->cell_subdiv - p_level - 1);
    p_local_data[p_idx].pos[1] = p_y >> (p_header->cell_subdiv - p_level - 1);
    p_local_data[p_idx].pos[2] = p_z >> (p_header->cell_subdiv - p_level - 1);

    prev_cell[p_level].emplace_back(p_idx);
}

static bool _check_gi_probe(VisualServerScene::Instance *p_gi_probe) {

    VisualServerScene::InstanceGIProbeData *probe_data = static_cast<VisualServerScene::InstanceGIProbeData *>(p_gi_probe->base_data);

    probe_data->dynamic.light_cache_changes.clear();

    bool all_equal = true;

    for (VisualServerScene::Instance *E : p_gi_probe->scenario->directional_lights) {

        if (VSG::storage->light_get_bake_mode(E->base) == RS::LightBakeMode::LIGHT_BAKE_DISABLED)
            continue;

        VisualServerScene::InstanceGIProbeData::LightCache lc;
        lc.type = VSG::storage->light_get_type(E->base);
        lc.color = VSG::storage->light_get_color(E->base);
        lc.energy = VSG::storage->light_get_param(E->base, RS::LIGHT_PARAM_ENERGY) * VSG::storage->light_get_param(E->base, RS::LIGHT_PARAM_INDIRECT_ENERGY);
        lc.radius = VSG::storage->light_get_param(E->base, RS::LIGHT_PARAM_RANGE);
        lc.attenuation = VSG::storage->light_get_param(E->base, RS::LIGHT_PARAM_ATTENUATION);
        lc.spot_angle = VSG::storage->light_get_param(E->base, RS::LIGHT_PARAM_SPOT_ANGLE);
        lc.spot_attenuation = VSG::storage->light_get_param(E->base, RS::LIGHT_PARAM_SPOT_ATTENUATION);
        lc.transform = probe_data->dynamic.light_to_cell_xform * E->transform;
        lc.visible = E->visible;

        if (!probe_data->dynamic.light_cache.contains(E->self) || probe_data->dynamic.light_cache[E->self] != lc) {
            all_equal = false;
        }

        probe_data->dynamic.light_cache_changes[E->self] = lc;
    }

    for (VisualServerScene::Instance * E : probe_data->lights) {

        if (VSG::storage->light_get_bake_mode(E->base) == RS::LightBakeMode::LIGHT_BAKE_DISABLED)
            continue;

        VisualServerScene::InstanceGIProbeData::LightCache lc;
        lc.type = VSG::storage->light_get_type(E->base);
        lc.color = VSG::storage->light_get_color(E->base);
        lc.energy = VSG::storage->light_get_param(E->base, RS::LIGHT_PARAM_ENERGY) * VSG::storage->light_get_param(E->base, RS::LIGHT_PARAM_INDIRECT_ENERGY);
        lc.radius = VSG::storage->light_get_param(E->base, RS::LIGHT_PARAM_RANGE);
        lc.attenuation = VSG::storage->light_get_param(E->base, RS::LIGHT_PARAM_ATTENUATION);
        lc.spot_angle = VSG::storage->light_get_param(E->base, RS::LIGHT_PARAM_SPOT_ANGLE);
        lc.spot_attenuation = VSG::storage->light_get_param(E->base, RS::LIGHT_PARAM_SPOT_ATTENUATION);
        lc.transform = probe_data->dynamic.light_to_cell_xform * E->transform;
        lc.visible = E->visible;

        if (!probe_data->dynamic.light_cache.contains(E->self) || probe_data->dynamic.light_cache[E->self] != lc) {
            all_equal = false;
        }

        probe_data->dynamic.light_cache_changes[E->self] = lc;
    }

    //lighting changed from after to before, must do some updating
    return !all_equal || probe_data->dynamic.light_cache_changes.size() != probe_data->dynamic.light_cache.size();
}
} // end of anonymous namespace

/* CAMERA API */

RID VisualServerScene::camera_create() {
    auto eid = VSG::ecs->registry.create();
    VSG::ecs->registry.emplace<Camera3D>(eid);
    RID newid;
    newid.eid = eid;

    return newid;
}

void VisualServerScene::camera_set_perspective(RID p_camera, float p_fovy_degrees, float p_z_near, float p_z_far) {

    ERR_FAIL_COND(!VSG::ecs->registry.valid(p_camera.eid) || !VSG::ecs->registry.has<Camera3D>(p_camera.eid));

    Camera3D &camera = VSG::ecs->registry.get<Camera3D>(p_camera.eid);

    camera.type = Camera3D::PERSPECTIVE;
    camera.fov = p_fovy_degrees;
    camera.znear = p_z_near;
    camera.zfar = p_z_far;
}

void VisualServerScene::camera_set_orthogonal(RID p_camera, float p_size, float p_z_near, float p_z_far) {
    ERR_FAIL_COND(!VSG::ecs->registry.valid(p_camera.eid) || !VSG::ecs->registry.has<Camera3D>(p_camera.eid));
    Camera3D &camera = VSG::ecs->registry.get<Camera3D>(p_camera.eid);

    camera.type = Camera3D::ORTHOGONAL;
    camera.size = p_size;
    camera.znear = p_z_near;
    camera.zfar = p_z_far;
}

void VisualServerScene::camera_set_frustum(RID p_camera, float p_size, Vector2 p_offset, float p_z_near, float p_z_far) {
    ERR_FAIL_COND(!VSG::ecs->registry.valid(p_camera.eid) || !VSG::ecs->registry.has<Camera3D>(p_camera.eid));

    Camera3D &camera = VSG::ecs->registry.get<Camera3D>(p_camera.eid);

    camera.type = Camera3D::FRUSTUM;
    camera.size = p_size;
    camera.offset = p_offset;
    camera.znear = p_z_near;
    camera.zfar = p_z_far;
}

void VisualServerScene::camera_set_transform(RID p_camera, const Transform &p_transform) {
    ERR_FAIL_COND(!VSG::ecs->registry.valid(p_camera.eid) || !VSG::ecs->registry.has<Camera3D>(p_camera.eid));

    Camera3D &camera = VSG::ecs->registry.get<Camera3D>(p_camera.eid);

    camera.transform = p_transform.orthonormalized();
}

void VisualServerScene::camera_set_cull_mask(RID p_camera, uint32_t p_layers) {
    ERR_FAIL_COND(!VSG::ecs->registry.valid(p_camera.eid) || !VSG::ecs->registry.has<Camera3D>(p_camera.eid));

    Camera3D &camera = VSG::ecs->registry.get<Camera3D>(p_camera.eid);

    camera.visible_layers = p_layers;
}

void VisualServerScene::camera_set_environment(RID p_camera, RID p_env) {
    ERR_FAIL_COND(!VSG::ecs->registry.valid(p_camera.eid) || !VSG::ecs->registry.has<Camera3D>(p_camera.eid));
    Camera3D &camera = VSG::ecs->registry.get<Camera3D>(p_camera.eid);

    camera.env = p_env;
}

/* SPATIAL PARTITIONING */
VisualServerScene::SpatialPartitionID VisualServerScene::SpatialPartitioningScene_BVH::create(Instance *p_userdata, const AABB &p_aabb, int p_subindex, bool p_pairable, uint32_t p_pairable_type, uint32_t p_pairable_mask) {
    return _bvh.create(p_userdata, p_aabb, p_subindex, p_pairable, p_pairable_type, p_pairable_mask) + 1;
}

void VisualServerScene::camera_set_use_vertical_aspect(RID p_camera, bool p_enable) {
    ERR_FAIL_COND(!VSG::ecs->registry.valid(p_camera.eid) || !VSG::ecs->registry.has<Camera3D>(p_camera.eid));

    Camera3D &camera = VSG::ecs->registry.get<Camera3D>(p_camera.eid);

    camera.vaspect = p_enable;
}
bool VisualServerScene::owns_camera(RID p_camera) {
    return VSG::ecs->registry.valid(p_camera.eid) && VSG::ecs->registry.has<Camera3D>(p_camera.eid);
}
/* SCENARIO API */

void *VisualServerScene::_instance_pair(void *p_self, SpatialPartitionID, Instance *p_A, int, SpatialPartitionID, Instance *p_B, int) {

    //VisualServerScene *self = (VisualServerScene*)p_self;
    Instance *A = p_A;
    Instance *B = p_B;

    //instance indices are designed so greater always contains lesser
    if (A->base_type > B->base_type) {
        SWAP(A, B); //lesser always first
    }

    if (B->base_type == RS::INSTANCE_LIGHT && has_component<GeometryComponent>(A->self.eid)) {

        InstanceLightData *light = static_cast<InstanceLightData *>(B->base_data);
        InstanceGeometryData *geom = get_instance_geometry(A->self);

        InstanceLightData::PairInfo pinfo;
        pinfo.geometry = A;
        pinfo.L = geom->lighting.insert(geom->lighting.end(),B);

        List<InstanceLightData::PairInfo>::iterator E = light->geometries.insert(light->geometries.end(),pinfo);
        GeometryComponent &cm_geom(get_component<GeometryComponent>(A->self));
        if (cm_geom.can_cast_shadows) {

            light->shadow_dirty = true;
        }
        cm_geom.lighting_dirty = true;

        return E.mpNode; //this element should make freeing faster
    } else if (B->base_type == RS::INSTANCE_REFLECTION_PROBE && has_component<GeometryComponent>(A->self.eid)) {

        InstanceReflectionProbeData *reflection_probe = static_cast<InstanceReflectionProbeData *>(B->base_data);
        InstanceGeometryData *geom = get_instance_geometry(A->self);

        InstanceReflectionProbeData::PairInfo pinfo;
        pinfo.geometry = A;
        pinfo.L = geom->reflection_probes.insert(geom->reflection_probes.end(),B);

        List<InstanceReflectionProbeData::PairInfo>::iterator E = reflection_probe->geometries.insert(reflection_probe->geometries.end(),pinfo);

        get_component<GeometryComponent>(A->self).reflection_dirty = true;

        return E.mpNode; //this element should make freeing faster
    } else if (B->base_type == RS::INSTANCE_LIGHTMAP_CAPTURE && has_component<GeometryComponent>(A->self.eid)) {

        InstanceLightmapCaptureData *lightmap_capture = static_cast<InstanceLightmapCaptureData *>(B->base_data);
        InstanceGeometryData *geom = get_instance_geometry(A->self);

        InstanceLightmapCaptureData::PairInfo pinfo;
        pinfo.geometry = A;
        pinfo.L = geom->lightmap_captures.insert(geom->lightmap_captures.end(),B);

        List<InstanceLightmapCaptureData::PairInfo>::iterator E = lightmap_capture->geometries.insert(lightmap_capture->geometries.end(),pinfo);
        ((VisualServerScene *)p_self)->_instance_queue_update(A, false, false); //need to update capture

        return E.mpNode; //this element should make freeing faster
    } else if (B->base_type == RS::INSTANCE_GI_PROBE && has_component<GeometryComponent>(A->self.eid)) {

        InstanceGIProbeData *gi_probe = static_cast<InstanceGIProbeData *>(B->base_data);
        InstanceGeometryData *geom = get_instance_geometry(A->self);

        InstanceGIProbeData::PairInfo pinfo;
        pinfo.geometry = A;
        pinfo.L = geom->gi_probes.insert(geom->gi_probes.end(),B);

        List<InstanceGIProbeData::PairInfo>::iterator E = gi_probe->geometries.insert(gi_probe->geometries.end(),pinfo);

        get_component<GeometryComponent>(A->self).gi_probes_dirty = true;

        return E.mpNode; //this element should make freeing faster

    } else if (B->base_type == RS::INSTANCE_GI_PROBE && A->base_type == RS::INSTANCE_LIGHT) {

        InstanceGIProbeData *gi_probe = static_cast<InstanceGIProbeData *>(B->base_data);
        gi_probe->lights.insert(A);
        return A;
    }

    return nullptr;
}
void VisualServerScene::_instance_unpair(void *p_self, SpatialPartitionID, Instance *p_A, int, SpatialPartitionID, Instance *p_B, int, void *udata) {
    static_assert(sizeof(List<InstanceLightData::PairInfo>::iterator)==sizeof(void*));
    //VisualServerScene *self = (VisualServerScene*)p_self;
    Instance *A = p_A;
    Instance *B = p_B;

    //instance indices are designed so greater always contains lesser
    if (A->base_type > B->base_type) {
        SWAP(A, B); //lesser always first
    }

    if (B->base_type == RS::INSTANCE_LIGHT && (has_component<GeometryComponent>(A->self.eid))) {

        InstanceLightData *light = static_cast<InstanceLightData *>(B->base_data);
        InstanceGeometryData *geom = get_instance_geometry(A->self);

        List<InstanceLightData::PairInfo>::iterator E(reinterpret_cast<eastl::ListNode<InstanceLightData::PairInfo> *>(udata));

        geom->lighting.erase(E->L);
        light->geometries.erase(E);
        GeometryComponent &cm_geom(get_component<GeometryComponent>(A->self));
        if (cm_geom.can_cast_shadows) {
            light->shadow_dirty = true;
        }
        cm_geom.lighting_dirty = true;

    } else if (B->base_type == RS::INSTANCE_REFLECTION_PROBE && (has_component<GeometryComponent>(A->self.eid))) {

        InstanceReflectionProbeData *reflection_probe = static_cast<InstanceReflectionProbeData *>(B->base_data);
        InstanceGeometryData *geom = get_instance_geometry(A->self);

        List<InstanceReflectionProbeData::PairInfo>::iterator E(reinterpret_cast<eastl::ListNode<InstanceReflectionProbeData::PairInfo> *>(udata));

        geom->reflection_probes.erase(E->L);
        reflection_probe->geometries.erase(E);

        get_component<GeometryComponent>(A->self).reflection_dirty = true;

    } else if (B->base_type == RS::INSTANCE_LIGHTMAP_CAPTURE && (has_component<GeometryComponent>(A->self.eid))) {

        InstanceLightmapCaptureData *lightmap_capture = static_cast<InstanceLightmapCaptureData *>(B->base_data);
        InstanceGeometryData *geom = get_instance_geometry(A->self);

        List<InstanceLightmapCaptureData::PairInfo>::iterator E(reinterpret_cast<eastl::ListNode<InstanceLightmapCaptureData::PairInfo> *>(udata));

        geom->lightmap_captures.erase(E->L);
        lightmap_capture->geometries.erase(E);
        ((VisualServerScene *)p_self)->_instance_queue_update(A, false, false); //need to update capture

    } else if (B->base_type == RS::INSTANCE_GI_PROBE && (has_component<GeometryComponent>(A->self.eid))) {

        InstanceGIProbeData *gi_probe = static_cast<InstanceGIProbeData *>(B->base_data);
        InstanceGeometryData *geom = get_instance_geometry(A->self);

        List<InstanceGIProbeData::PairInfo>::iterator E(reinterpret_cast<eastl::ListNode<InstanceGIProbeData::PairInfo> *>(udata));

        geom->gi_probes.erase(E->L);
        gi_probe->geometries.erase(E);

        get_component<GeometryComponent>(A->self).gi_probes_dirty = true;

    } else if (B->base_type == RS::INSTANCE_GI_PROBE && A->base_type == RS::INSTANCE_LIGHT) {

        InstanceGIProbeData *gi_probe = static_cast<InstanceGIProbeData *>(B->base_data);
        Instance *E = reinterpret_cast<Instance *>(udata);

        gi_probe->lights.erase(E);
    }
}

RID VisualServerScene::scenario_create() {

    Scenario *scenario = memnew(Scenario);
    ERR_FAIL_COND_V(!scenario, RID());
    RID scenario_rid = scenario_owner.make_rid(scenario);

    scenario->self = scenario_rid;
    //scenario->octree.set_balance(T_GLOBAL_GET<float>("rendering/quality/spatial_partitioning/render_tree_balance"));
    scenario->sps.set_pair_callback(_instance_pair, this);
    scenario->sps.set_unpair_callback(_instance_unpair, this);
    scenario->reflection_probe_shadow_atlas = VSG::scene_render->shadow_atlas_create();
    VSG::scene_render->shadow_atlas_set_size(scenario->reflection_probe_shadow_atlas, 1024); //make enough shadows for close distance, don't bother with rest
    VSG::scene_render->shadow_atlas_set_quadrant_subdivision(scenario->reflection_probe_shadow_atlas, 0, 4);
    VSG::scene_render->shadow_atlas_set_quadrant_subdivision(scenario->reflection_probe_shadow_atlas, 1, 4);
    VSG::scene_render->shadow_atlas_set_quadrant_subdivision(scenario->reflection_probe_shadow_atlas, 2, 4);
    VSG::scene_render->shadow_atlas_set_quadrant_subdivision(scenario->reflection_probe_shadow_atlas, 3, 8);
    scenario->reflection_atlas = VSG::scene_render->reflection_atlas_create();

    return scenario_rid;
}

void VisualServerScene::scenario_set_debug(RID p_scenario, RS::ScenarioDebugMode p_debug_mode) {

    Scenario *scenario = scenario_owner.get(p_scenario);
    ERR_FAIL_COND(!scenario);
    scenario->debug = p_debug_mode;
}

void VisualServerScene::scenario_set_environment(RID p_scenario, RID p_environment) {

    Scenario *scenario = scenario_owner.get(p_scenario);
    ERR_FAIL_COND(!scenario);
    scenario->environment = p_environment;
}

void VisualServerScene::scenario_set_fallback_environment(RID p_scenario, RID p_environment) {

    Scenario *scenario = scenario_owner.get(p_scenario);
    ERR_FAIL_COND(!scenario);
    scenario->fallback_environment = p_environment;
}

void VisualServerScene::scenario_set_reflection_atlas_size(RID p_scenario, int p_size, int p_subdiv) {

    Scenario *scenario = scenario_owner.get(p_scenario);
    ERR_FAIL_COND(!scenario);
    VSG::scene_render->reflection_atlas_set_size(scenario->reflection_atlas, p_size);
    VSG::scene_render->reflection_atlas_set_subdivision(scenario->reflection_atlas, p_subdiv);
}

/* INSTANCING API */

void VisualServerScene::_instance_queue_update(Instance *p_instance, bool p_update_aabb, bool p_update_materials) {

    set_dirty(p_instance->self, p_update_aabb, p_update_materials);

}

RID VisualServerScene::instance_create() {

    Instance *instance = memnew(Instance);
    ERR_FAIL_COND_V(!instance, RID());

    RID instance_rid = instance_owner.make_rid(instance);
    instance_rid.eid = VSG::ecs->registry.create();

    instance->self = instance_rid;
    VSG::ecs->registry.emplace_or_replace<InstanceComponent>(instance_rid.eid, instance);
    VSG::ecs->registry.emplace_or_replace<InstanceBoundsComponent>(instance_rid.eid);
    return instance_rid;
}

void VisualServerScene::instance_set_base(RID p_instance, RID p_base) {

    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    Scenario *scenario = instance->scenario;

    if (instance->base_type != RS::INSTANCE_NONE) {
        //free anything related to that base

        VSG::storage->instance_remove_dependency(instance->base, instance);

        if (instance->base_type == RS::INSTANCE_GI_PROBE) {
            //if gi probe is baking, wait until done baking, else race condition may happen when removing it
            //from octree
            InstanceGIProbeData *gi_probe = static_cast<InstanceGIProbeData *>(instance->base_data);

            //make sure probes are done baking
            while (!probe_bake_list.empty()) {
                OS::get_singleton()->delay_usec(1);
            }
            //make sure this one is done baking

            while (gi_probe->dynamic.updating_stage == GIUpdateStage::LIGHTING) {
                //wait until bake is done if it's baking
                OS::get_singleton()->delay_usec(1);
            }
        }

        if (scenario && instance->spatial_partition_id) {
            scenario->sps.erase(instance->spatial_partition_id);
            instance->spatial_partition_id = 0;
        }

        switch (instance->base_type) {
            case RS::INSTANCE_LIGHT: {

                InstanceLightData *light = static_cast<InstanceLightData *>(instance->base_data);

                if (instance->scenario && light->D) {
                    instance->scenario->directional_lights.erase_first(instance);
                    light->D = false;
                }
                VSG::scene_render->free(light->instance);
            } break;
            case RS::INSTANCE_REFLECTION_PROBE: {

                InstanceReflectionProbeData *reflection_probe = static_cast<InstanceReflectionProbeData *>(instance->base_data);
                VSG::scene_render->free(reflection_probe->instance);
                if (reflection_probe->update_list.in_list()) {
                    reflection_probe_render_list.remove(&reflection_probe->update_list);
                }
            } break;
            case RS::INSTANCE_LIGHTMAP_CAPTURE: {

                InstanceLightmapCaptureData *lightmap_capture = static_cast<InstanceLightmapCaptureData *>(instance->base_data);
                //erase dependencies, since no longer a lightmap
                while (!lightmap_capture->users.empty()) {
                    instance_set_use_lightmap((*lightmap_capture->users.begin())->self, RID(), RID());
                }
            } break;
            case RS::INSTANCE_GI_PROBE: {

                InstanceGIProbeData *gi_probe = static_cast<InstanceGIProbeData *>(instance->base_data);

                if (gi_probe->update_element.in_list()) {
                    gi_probe_update_list.remove(&gi_probe->update_element);
                }
                if (gi_probe->dynamic.probe_data.is_valid()) {
                    VSG::storage->free(gi_probe->dynamic.probe_data);
                }

                if (instance->lightmap_capture) {
                    Instance *capture = (Instance *)instance->lightmap_capture;
                    InstanceLightmapCaptureData *lightmap_capture = static_cast<InstanceLightmapCaptureData *>(capture->base_data);
                    lightmap_capture->users.erase(instance);
                    instance->lightmap_capture = nullptr;
                    instance->lightmap = RID();
                }

                VSG::scene_render->free(gi_probe->probe_instance);

            } break;
            default: {
            }
        }

        if (instance->base_data) {
            memdelete(instance->base_data);
            instance->base_data = nullptr;
        }

        instance->blend_values.clear();

        for (int i = 0; i < instance->materials.size(); i++) {
            if (instance->materials[i].is_valid()) {
                VSG::storage->material_remove_instance_owner(instance->materials[i], instance);
            }
        }
        instance->materials.clear();
    }

    instance->base_type = RS::INSTANCE_NONE;
    instance->base = RID();

    if (!p_base.is_valid()) {
        return;
    }

    instance->base_type = VSG::storage->get_base_type(p_base);
    ERR_FAIL_COND(instance->base_type == RS::INSTANCE_NONE);

    switch (instance->base_type) {
        case RS::INSTANCE_LIGHT: {

            InstanceLightData *light = memnew(InstanceLightData);

            if (scenario && VSG::storage->light_get_type(p_base) == RS::LIGHT_DIRECTIONAL) {
                scenario->directional_lights.push_back(instance);
                light->D = true;
            }

            light->instance = VSG::scene_render->light_instance_create(p_base);

            instance->base_data = light;
        } break;
        case RS::INSTANCE_MESH:
        case RS::INSTANCE_MULTIMESH:
        case RS::INSTANCE_IMMEDIATE:
        case RS::INSTANCE_PARTICLES: {

            InstanceGeometryData *geom = memnew(InstanceGeometryData);
            VSG::ecs->registry.emplace_or_replace<GeometryComponent>(instance->self.eid,geom);

            if (instance->base_type == RS::INSTANCE_MESH) {
                instance->blend_values.resize(VSG::storage->mesh_get_blend_shape_count(p_base));
            }
        } break;
        case RS::INSTANCE_REFLECTION_PROBE: {

            InstanceReflectionProbeData *reflection_probe = memnew(InstanceReflectionProbeData);
            reflection_probe->owner = instance;
            instance->base_data = reflection_probe;

            reflection_probe->instance = VSG::scene_render->reflection_probe_instance_create(p_base);
        } break;
        case RS::INSTANCE_LIGHTMAP_CAPTURE: {

            InstanceLightmapCaptureData *lightmap_capture = memnew(InstanceLightmapCaptureData);
            instance->base_data = lightmap_capture;
            //lightmap_capture->instance = VSG::scene_render->lightmap_capture_instance_create(p_base);
        } break;
        case RS::INSTANCE_GI_PROBE: {

            InstanceGIProbeData *gi_probe = memnew(InstanceGIProbeData);
            instance->base_data = gi_probe;
            gi_probe->owner = instance;

            if (scenario && !gi_probe->update_element.in_list()) {
                gi_probe_update_list.add(&gi_probe->update_element);
            }

            gi_probe->probe_instance = VSG::scene_render->gi_probe_instance_create();

        } break;
        default: {
        }
    }

    VSG::storage->instance_add_dependency(p_base, instance);

    instance->base = p_base;

    if (scenario)
        _instance_queue_update(instance, true, true);
}
void VisualServerScene::instance_set_scenario(RID p_instance, RID p_scenario) {

    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    Scenario *old_scene = instance->scenario;

    if (old_scene) {

        old_scene->instances.remove(&instance->scenario_item);

        if (instance->spatial_partition_id) {
            old_scene->sps.erase(instance->spatial_partition_id);
            instance->spatial_partition_id = 0;
        }

        switch (instance->base_type) {

            case RS::INSTANCE_LIGHT: {

                InstanceLightData *light = static_cast<InstanceLightData *>(instance->base_data);
                if (light->D) {
                    old_scene->directional_lights.erase_first(instance);
                    light->D = false;
                }
            } break;
            case RS::INSTANCE_REFLECTION_PROBE: {

                InstanceReflectionProbeData *reflection_probe = static_cast<InstanceReflectionProbeData *>(instance->base_data);
                VSG::scene_render->reflection_probe_release_atlas_index(reflection_probe->instance);
            } break;
            case RS::INSTANCE_GI_PROBE: {

                InstanceGIProbeData *gi_probe = static_cast<InstanceGIProbeData *>(instance->base_data);
                if (gi_probe->update_element.in_list()) {
                    gi_probe_update_list.remove(&gi_probe->update_element);
                }
            } break;
            default: {
            }
        }

        instance->scenario = nullptr;
    }
    if(!p_scenario.is_valid())
        return;

    Scenario *scenario = scenario_owner.get(p_scenario);
    ERR_FAIL_COND(!scenario);

    instance->scenario = scenario;

    scenario->instances.add(&instance->scenario_item);

    switch (instance->base_type) {

        case RS::INSTANCE_LIGHT: {

            InstanceLightData *light = static_cast<InstanceLightData *>(instance->base_data);

            if (VSG::storage->light_get_type(instance->base) == RS::LIGHT_DIRECTIONAL) {
                scenario->directional_lights.push_back(instance);
                light->D = true;
            }
        } break;
        case RS::INSTANCE_GI_PROBE: {

            InstanceGIProbeData *gi_probe = static_cast<InstanceGIProbeData *>(instance->base_data);
            if (!gi_probe->update_element.in_list()) {
                gi_probe_update_list.add(&gi_probe->update_element);
            }
        } break;
        default: {
        }
    }

    _instance_queue_update(instance, true, true);

}
void VisualServerScene::instance_set_layer_mask(RID p_instance, uint32_t p_mask) {

    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    instance->layer_mask = p_mask;
}
void VisualServerScene::instance_set_transform(RID p_instance, const Transform &p_transform) {

    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    if (instance->transform == p_transform)
        return; //must be checked to avoid worst evil

#ifdef DEBUG_ENABLED

    for (int i = 0; i < 4; i++) {
        const Vector3 &v = i < 3 ? p_transform.basis.elements[i] : p_transform.origin;
        ERR_FAIL_COND(Math::is_inf(v.x));
        ERR_FAIL_COND(Math::is_nan(v.x));
        ERR_FAIL_COND(Math::is_inf(v.y));
        ERR_FAIL_COND(Math::is_nan(v.y));
        ERR_FAIL_COND(Math::is_inf(v.z));
        ERR_FAIL_COND(Math::is_nan(v.z));
    }

#endif
    instance->transform = p_transform;
    _instance_queue_update(instance, true);
}
void VisualServerScene::instance_attach_object_instance_id(RID p_instance, ObjectID p_id) {

    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    instance->object_id = p_id;
}
void VisualServerScene::instance_set_blend_shape_weight(RID p_instance, int p_shape, float p_weight) {

    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    if (!has_component<Dirty>(p_instance.eid)) { // not marked for update, do it now?
        _update_dirty_instance(instance);
    }

    ERR_FAIL_INDEX(p_shape, instance->blend_values.size());
    instance->blend_values[p_shape] = p_weight;
}

void VisualServerScene::instance_set_surface_material(RID p_instance, int p_surface, RID p_material) {

    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    if (instance->base_type == RS::INSTANCE_MESH) {
        //may not have been updated yet
        instance->materials.resize(VSG::storage->mesh_get_surface_count(instance->base));
    }

    ERR_FAIL_INDEX(p_surface, instance->materials.size());

    if (instance->materials[p_surface].is_valid()) {
        VSG::storage->material_remove_instance_owner(instance->materials[p_surface], instance);
    }
    instance->materials[p_surface] = p_material;
    instance->base_changed(false, true);

    if (instance->materials[p_surface].is_valid()) {
        VSG::storage->material_add_instance_owner(instance->materials[p_surface], instance);
    }
}

void VisualServerScene::instance_set_visible(RID p_instance, bool p_visible) {
    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    if (instance->visible == p_visible)
        return;

    instance->visible = p_visible;
    // when showing or hiding geometry, lights must be kept up to date to show / hide shadows
    if ((1 << instance->base_type) & RS::INSTANCE_GEOMETRY_MASK) {
        InstanceGeometryData *geom = get_instance_geometry(instance->self);
        auto &cm_geom(get_component<GeometryComponent>(instance->self));

        if (cm_geom.can_cast_shadows) {
            for (auto E : geom->lighting) {
                InstanceLightData *light = static_cast<InstanceLightData *>(E->base_data);
                light->shadow_dirty = true;
            }
        }
    }

    switch (instance->base_type) {
        case RS::INSTANCE_LIGHT: {
            if (VSG::storage->light_get_type(instance->base) != RS::LIGHT_DIRECTIONAL && instance->spatial_partition_id && instance->scenario) {
                instance->scenario->sps.set_pairable(instance->spatial_partition_id, p_visible, 1 << RS::INSTANCE_LIGHT, p_visible ? RS::INSTANCE_GEOMETRY_MASK : 0);
            }

        } break;
        case RS::INSTANCE_REFLECTION_PROBE: {
            if (instance->spatial_partition_id && instance->scenario) {
                instance->scenario->sps.set_pairable(instance->spatial_partition_id, p_visible, 1 << RS::INSTANCE_REFLECTION_PROBE, p_visible ? RS::INSTANCE_GEOMETRY_MASK : 0);
            }

        } break;
        case RS::INSTANCE_LIGHTMAP_CAPTURE: {
            if (instance->spatial_partition_id && instance->scenario) {
                instance->scenario->sps.set_pairable(instance->spatial_partition_id, p_visible, 1 << RS::INSTANCE_LIGHTMAP_CAPTURE, p_visible ? RS::INSTANCE_GEOMETRY_MASK : 0);
            }

        } break;
        case RS::INSTANCE_GI_PROBE: {
            if (instance->spatial_partition_id && instance->scenario) {
                instance->scenario->sps.set_pairable(instance->spatial_partition_id, p_visible, 1 << RS::INSTANCE_GI_PROBE, p_visible ? (RS::INSTANCE_GEOMETRY_MASK | (1 << RS::INSTANCE_LIGHT)) : 0);
            }

        } break;
        default: {
        }
    }
}

inline bool is_geometry_instance(RS::InstanceType p_type) {
    return p_type == RS::INSTANCE_MESH || p_type == RS::INSTANCE_MULTIMESH || p_type == RS::INSTANCE_PARTICLES || p_type == RS::INSTANCE_IMMEDIATE;
}

void VisualServerScene::instance_set_use_lightmap(RID p_instance, RID p_lightmap_instance, RID p_lightmap) {

    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    if (instance->lightmap_capture) {
        InstanceLightmapCaptureData *lightmap_capture = static_cast<InstanceLightmapCaptureData *>(((Instance *)instance->lightmap_capture)->base_data);
        lightmap_capture->users.erase(instance);
        instance->lightmap = RID();
        instance->lightmap_capture = nullptr;
    }

    if (p_lightmap_instance.is_valid()) {
        Instance *lightmap_instance = instance_owner.get(p_lightmap_instance);
        ERR_FAIL_COND(!lightmap_instance);
        ERR_FAIL_COND(lightmap_instance->base_type != RS::INSTANCE_LIGHTMAP_CAPTURE);
        instance->lightmap_capture = lightmap_instance;

        InstanceLightmapCaptureData *lightmap_capture = static_cast<InstanceLightmapCaptureData *>(((Instance *)instance->lightmap_capture)->base_data);
        lightmap_capture->users.insert(instance);
        instance->lightmap = p_lightmap;
    }
}

void VisualServerScene::instance_set_custom_aabb(RID p_instance, AABB p_aabb) {

    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);
    ERR_FAIL_COND(!is_geometry_instance(instance->base_type));

    InstanceBoundsComponent& bounds = get_component<InstanceBoundsComponent>(p_instance);

    if (p_aabb != AABB()) {

        bounds.custom_aabb = p_aabb;
        bounds.use_custom_aabb = true;
    } else {

        // Clear custom AABB
        bounds.use_custom_aabb = false;
    }

    if (instance->scenario)
        _instance_queue_update(instance, true, false);
}

void VisualServerScene::instance_attach_skeleton(RID p_instance, RID p_skeleton) {

    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    if (instance->skeleton == p_skeleton)
        return;

    if (instance->skeleton.is_valid()) {
        VSG::storage->instance_remove_skeleton(instance->skeleton, instance);
    }

    instance->skeleton = p_skeleton;

    if (instance->skeleton.is_valid()) {
        VSG::storage->instance_add_skeleton(instance->skeleton, instance);
    }

    _instance_queue_update(instance, true);
}

void VisualServerScene::instance_set_extra_visibility_margin(RID p_instance, real_t p_margin) {
    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    InstanceBoundsComponent& bounds = get_component<InstanceBoundsComponent>(p_instance);
    bounds.extra_margin = p_margin;
    _instance_queue_update(instance, true, false);
}

Vector<ObjectID> VisualServerScene::instances_cull_aabb(const AABB &p_aabb, RID p_scenario) const {

    Vector<ObjectID> instances;
    Scenario *scenario = scenario_owner.get(p_scenario);
    ERR_FAIL_COND_V(!scenario, instances);

    const_cast<VisualServerScene *>(this)->update_dirty_instances(); // check dirty instances before culling

    Instance *cull[1024];
    int culled = scenario->sps.cull_aabb(p_aabb, cull, 1024);

    instances.reserve(culled/2);

    for (int i = 0; i < culled; i++) {

        Instance *instance = cull[i];
        ERR_CONTINUE(!instance);
        if (instance->object_id.is_null())
            continue;

        instances.push_back(instance->object_id);
    }

    return instances;
}
Vector<ObjectID> VisualServerScene::instances_cull_ray(const Vector3 &p_from, const Vector3 &p_to, RID p_scenario) const {

    Vector<ObjectID> instances;
    Scenario *scenario = scenario_owner.get(p_scenario);
    ERR_FAIL_COND_V(!scenario, instances);
    const_cast<VisualServerScene *>(this)->update_dirty_instances(); // check dirty instances before culling

    Instance *cull[1024];
    int culled = scenario->sps.cull_segment(p_from, p_from + p_to * 10000, cull, 1024);

    instances.reserve(culled/2);
    for (int i = 0; i < culled; i++) {
        Instance *instance = cull[i];
        ERR_CONTINUE(!instance);
        if (instance->object_id.is_null())
            continue;

        instances.push_back(instance->object_id);
    }

    return instances;
}
Vector<ObjectID> VisualServerScene::instances_cull_convex(Span<const Plane> p_convex, RID p_scenario) const {

    Vector<ObjectID> instances;
    Scenario *scenario = scenario_owner.get(p_scenario);
    ERR_FAIL_COND_V(!scenario, instances);
    const_cast<VisualServerScene *>(this)->update_dirty_instances(); // check dirty instances before culling

    int culled = 0;
    Instance *cull[1024];

    culled = scenario->sps.cull_convex(p_convex, cull, 1024);

    for (int i = 0; i < culled; i++) {

        Instance *instance = cull[i];
        ERR_CONTINUE(!instance);
        if (instance->object_id.is_null())
            continue;

        instances.push_back(instance->object_id);
    }

    return instances;
}

void VisualServerScene::instance_geometry_set_flag(RID p_instance, RS::InstanceFlags p_flags, bool p_enabled) {

    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    switch (p_flags) {

        case RS::INSTANCE_FLAG_USE_BAKED_LIGHT: {

            instance->baked_light = p_enabled;

        } break;
        case RS::INSTANCE_FLAG_DRAW_NEXT_FRAME_IF_VISIBLE: {

            instance->redraw_if_visible = p_enabled;

        } break;
        default: {
        }
    }
}
void VisualServerScene::instance_geometry_set_cast_shadows_setting(RID p_instance, RS::ShadowCastingSetting p_shadow_casting_setting) {

    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    instance->cast_shadows = p_shadow_casting_setting;
    instance->base_changed(false, true); // to actually compute if shadows are visible or not
}
void VisualServerScene::instance_geometry_set_material_override(RID p_instance, RID p_material) {

    Instance *instance = instance_owner.get(p_instance);
    ERR_FAIL_COND(!instance);

    if (instance->material_override.is_valid()) {
        VSG::storage->material_remove_instance_owner(instance->material_override, instance);
    }
    instance->material_override = p_material;
    instance->base_changed(false, true);

    if (instance->material_override.is_valid()) {
        VSG::storage->material_add_instance_owner(instance->material_override, instance);
    }
}

void VisualServerScene::instance_geometry_set_draw_range(RID p_instance, float p_min, float p_max, float p_min_margin, float p_max_margin) {
}
void VisualServerScene::instance_geometry_set_as_instance_lod(RID p_instance, RID p_as_lod_of_instance) {
}

void VisualServerScene::_update_instance(Instance *p_instance) {

    p_instance->version++;

    InstanceBoundsComponent& bounds = get_component<InstanceBoundsComponent>(p_instance->self);

    if (p_instance->base_type == RS::INSTANCE_LIGHT) {

        InstanceLightData *light = static_cast<InstanceLightData *>(p_instance->base_data);

        VSG::scene_render->light_instance_set_transform(light->instance, p_instance->transform);
        light->shadow_dirty = true;
    }

    if (p_instance->base_type == RS::INSTANCE_REFLECTION_PROBE) {

        InstanceReflectionProbeData *reflection_probe = static_cast<InstanceReflectionProbeData *>(p_instance->base_data);

        VSG::scene_render->reflection_probe_instance_set_transform(reflection_probe->instance, p_instance->transform);
        reflection_probe->reflection_dirty = true;
    }

    if (p_instance->base_type == RS::INSTANCE_PARTICLES) {

        VSG::storage->particles_set_emission_transform(p_instance->base, p_instance->transform);
    }

    if (bounds.aabb.has_no_surface()) {
        return;
    }

    if ((1 << p_instance->base_type) & RS::INSTANCE_GEOMETRY_MASK) {
        InstanceGeometryData *geom = get_instance_geometry(p_instance->self);
        //make sure lights are updated if it casts shadow
        auto &cm_geom(get_component<GeometryComponent>(p_instance->self));
        if (cm_geom.can_cast_shadows) {
            for (Instance * E : geom->lighting) {
                InstanceLightData *light = static_cast<InstanceLightData *>(E->base_data);
                light->shadow_dirty = true;
            }
        }

        if (!p_instance->lightmap_capture && !geom->lightmap_captures.empty()) {
            //affected by lightmap captures, must update capture info!
            _update_instance_lightmap_captures(p_instance);
        } else {
            if (!p_instance->lightmap_capture_data.empty()) {
                p_instance->lightmap_capture_data.resize(0); //not in use, clear capture data
            }
        }
    }

    p_instance->mirror = p_instance->transform.basis.determinant() < 0.0;

    AABB new_aabb = p_instance->transform.xform(bounds.aabb);

    bounds.transformed_aabb = new_aabb;

    if (!p_instance->scenario) {

        return;
    }

    if (p_instance->spatial_partition_id == 0) {

        uint32_t base_type = 1 << p_instance->base_type;
        uint32_t pairable_mask = 0;
        bool pairable = false;

        if (p_instance->base_type == RS::INSTANCE_LIGHT || p_instance->base_type == RS::INSTANCE_REFLECTION_PROBE || p_instance->base_type == RS::INSTANCE_LIGHTMAP_CAPTURE) {

            pairable_mask = p_instance->visible ? RS::INSTANCE_GEOMETRY_MASK : 0;
            pairable = true;
        }

        if (p_instance->base_type == RS::INSTANCE_GI_PROBE) {
            //lights and geometries
            pairable_mask = p_instance->visible ? (RS::INSTANCE_GEOMETRY_MASK | (1 << RS::INSTANCE_LIGHT)) : 0;
            pairable = true;
        }

        // not inside octree
        p_instance->spatial_partition_id = p_instance->scenario->sps.create(p_instance, new_aabb, 0, pairable, base_type, pairable_mask);

    } else {

        /*
        if (new_aabb==p_instance->data.transformed_aabb)
            return;
        */

        p_instance->scenario->sps.move(p_instance->spatial_partition_id, new_aabb);
    }
}

void VisualServerScene::_update_instance_aabb(Instance *p_instance) {

    AABB new_aabb;

    ERR_FAIL_COND(p_instance->base_type != RS::INSTANCE_NONE && !p_instance->base.is_valid());

    InstanceBoundsComponent& bounds = get_component<InstanceBoundsComponent>(p_instance->self);

    switch (p_instance->base_type) {
        case RS::INSTANCE_NONE: {

            // do nothing
        } break;
        case RS::INSTANCE_MESH: {

            if (bounds.use_custom_aabb)
                new_aabb = bounds.custom_aabb;
            else
                new_aabb = VSG::storage->mesh_get_aabb(p_instance->base, p_instance->skeleton);

        } break;

        case RS::INSTANCE_MULTIMESH: {

            if (bounds.use_custom_aabb)
                new_aabb = bounds.custom_aabb;
            else
                new_aabb = VSG::storage->multimesh_get_aabb(p_instance->base);

        } break;
        case RS::INSTANCE_IMMEDIATE: {

            if (bounds.use_custom_aabb)
                new_aabb = bounds.custom_aabb;
            else
                new_aabb = VSG::storage->immediate_get_aabb(p_instance->base);

        } break;
        case RS::INSTANCE_PARTICLES: {

            if (bounds.use_custom_aabb)
                new_aabb = bounds.custom_aabb;
            else
                new_aabb = VSG::storage->particles_get_aabb(p_instance->base);

        } break;
        case RS::INSTANCE_LIGHT: {

            new_aabb = VSG::storage->light_get_aabb(p_instance->base);

        } break;
        case RS::INSTANCE_REFLECTION_PROBE: {

            new_aabb = VSG::storage->reflection_probe_get_aabb(p_instance->base);

        } break;
        case RS::INSTANCE_GI_PROBE: {

            new_aabb = VSG::storage->gi_probe_get_bounds(p_instance->base);

        } break;
        case RS::INSTANCE_LIGHTMAP_CAPTURE: {

            new_aabb = VSG::storage->lightmap_capture_get_bounds(p_instance->base);

        } break;
        default: {
        }
    }

    // <Zylann> This is why I didn't re-use Instance::aabb to implement custom AABBs
    if (bounds.extra_margin)
        new_aabb.grow_by(bounds.extra_margin);

    bounds.aabb = new_aabb;
}

_FORCE_INLINE_ static void _light_capture_sample_octree(const RasterizerStorage::LightmapCaptureOctree *p_octree, int p_cell_subdiv, const Vector3 &p_pos, const Vector3 &p_dir, float p_level, Vector3 &r_color, float &r_alpha) {

    static const Vector3 aniso_normal[6] = {
        Vector3(-1, 0, 0),
        Vector3(1, 0, 0),
        Vector3(0, -1, 0),
        Vector3(0, 1, 0),
        Vector3(0, 0, -1),
        Vector3(0, 0, 1)
    };

    int size = 1 << (p_cell_subdiv - 1);

    int clamp_v = size - 1;
    //first of all, clamp
    Vector3 pos;
    pos.x = CLAMP(p_pos.x, 0, clamp_v);
    pos.y = CLAMP(p_pos.y, 0, clamp_v);
    pos.z = CLAMP(p_pos.z, 0, clamp_v);

    float level = (p_cell_subdiv - 1) - p_level;

    int target_level;
    float level_filter;
    if (level <= 0.0f) {
        level_filter = 0;
        target_level = 0;
    } else {
        target_level = Math::ceil(level);
        level_filter = target_level - level;
    }

    Vector3 color[2][8];
    float alpha[2][8];
    memset(alpha, 0, sizeof(float) * 2 * 8);

    //find cell at given level first

    for (int c = 0; c < 2; c++) {

        int current_level = M_MAX(0, target_level - c);
        int level_cell_size = (1 << (p_cell_subdiv - 1)) >> current_level;

        for (int n = 0; n < 8; n++) {

            int x = int(pos.x);
            int y = int(pos.y);
            int z = int(pos.z);

            if (n & 1)
                x += level_cell_size;
            if (n & 2)
                y += level_cell_size;
            if (n & 4)
                z += level_cell_size;

            int ofs_x = 0;
            int ofs_y = 0;
            int ofs_z = 0;

            x = CLAMP(x, 0, clamp_v);
            y = CLAMP(y, 0, clamp_v);
            z = CLAMP(z, 0, clamp_v);

            int half = size / 2;
            uint32_t cell = 0;
            for (int i = 0; i < current_level; i++) {

                const RasterizerStorage::LightmapCaptureOctree *bc = &p_octree[cell];

                int child = 0;
                if (x >= ofs_x + half) {
                    child |= 1;
                    ofs_x += half;
                }
                if (y >= ofs_y + half) {
                    child |= 2;
                    ofs_y += half;
                }
                if (z >= ofs_z + half) {
                    child |= 4;
                    ofs_z += half;
                }

                cell = bc->children[child];
                if (cell == RasterizerStorage::LightmapCaptureOctree::CHILD_EMPTY)
                    break;

                half >>= 1;
            }

            if (cell == RasterizerStorage::LightmapCaptureOctree::CHILD_EMPTY) {
                alpha[c][n] = 0;
            } else {
                alpha[c][n] = p_octree[cell].alpha;

                for (int i = 0; i < 6; i++) {
                    //anisotropic read light
                    float amount = p_dir.dot(aniso_normal[i]);
                    if (amount < 0)
                        amount = 0;
                    color[c][n].x += p_octree[cell].light[i][0] / 1024.0f * amount;
                    color[c][n].y += p_octree[cell].light[i][1] / 1024.0f * amount;
                    color[c][n].z += p_octree[cell].light[i][2] / 1024.0f * amount;
                }
            }

            //print_line("\tlev " + itos(c) + " - " + itos(n) + " alpha: " + rtos(cells[test_cell].alpha) + " col: " + color[c][n]);
        }
    }

    float target_level_size = size >> target_level;
    Vector3 pos_fract[2];

    pos_fract[0].x = Math::fmod(pos.x, target_level_size) / target_level_size;
    pos_fract[0].y = Math::fmod(pos.y, target_level_size) / target_level_size;
    pos_fract[0].z = Math::fmod(pos.z, target_level_size) / target_level_size;

    target_level_size = size >> M_MAX(0, target_level - 1);

    pos_fract[1].x = Math::fmod(pos.x, target_level_size) / target_level_size;
    pos_fract[1].y = Math::fmod(pos.y, target_level_size) / target_level_size;
    pos_fract[1].z = Math::fmod(pos.z, target_level_size) / target_level_size;

    float alpha_interp[2];
    Vector3 color_interp[2];

    for (int i = 0; i < 2; i++) {

        Vector3 color_x00 = color[i][0].linear_interpolate(color[i][1], pos_fract[i].x);
        Vector3 color_xy0 = color[i][2].linear_interpolate(color[i][3], pos_fract[i].x);
        Vector3 blend_z0 = color_x00.linear_interpolate(color_xy0, pos_fract[i].y);

        Vector3 color_x0z = color[i][4].linear_interpolate(color[i][5], pos_fract[i].x);
        Vector3 color_xyz = color[i][6].linear_interpolate(color[i][7], pos_fract[i].x);
        Vector3 blend_z1 = color_x0z.linear_interpolate(color_xyz, pos_fract[i].y);

        color_interp[i] = blend_z0.linear_interpolate(blend_z1, pos_fract[i].z);

        float alpha_x00 = Math::lerp(alpha[i][0], alpha[i][1], pos_fract[i].x);
        float alpha_xy0 = Math::lerp(alpha[i][2], alpha[i][3], pos_fract[i].x);
        float alpha_z0 = Math::lerp(alpha_x00, alpha_xy0, pos_fract[i].y);

        float alpha_x0z = Math::lerp(alpha[i][4], alpha[i][5], pos_fract[i].x);
        float alpha_xyz = Math::lerp(alpha[i][6], alpha[i][7], pos_fract[i].x);
        float alpha_z1 = Math::lerp(alpha_x0z, alpha_xyz, pos_fract[i].y);

        alpha_interp[i] = Math::lerp(alpha_z0, alpha_z1, pos_fract[i].z);
    }

    r_color = color_interp[0].linear_interpolate(color_interp[1], level_filter);
    r_alpha = Math::lerp(alpha_interp[0], alpha_interp[1], level_filter);

    //print_line("pos: " + p_posf + " level " + rtos(p_level) + " down to " + itos(target_level) + "." + rtos(level_filter) + " color " + r_color + " alpha " + rtos(r_alpha));
}

_FORCE_INLINE_ static Color _light_capture_voxel_cone_trace(const RasterizerStorage::LightmapCaptureOctree *p_octree, const Vector3 &p_pos, const Vector3 &p_dir, float p_aperture, int p_cell_subdiv) {

    float bias = 0.0; //no need for bias here
    float max_distance = (Vector3(1, 1, 1) * (1 << (p_cell_subdiv - 1))).length();

    float dist = bias;
    float alpha = 0.0;
    Vector3 color;

    Vector3 scolor;
    float salpha;

    while (dist < max_distance && alpha < 0.95f) {
        float diameter = M_MAX(1.0, 2.0f * p_aperture * dist);
        _light_capture_sample_octree(p_octree, p_cell_subdiv, p_pos + dist * p_dir, p_dir, log2(diameter), scolor, salpha);
        float a = (1.0f - alpha);
        color += scolor * a;
        alpha += a * salpha;
        dist += diameter * 0.5f;
    }

    return Color(color.x, color.y, color.z, alpha);
}

void VisualServerScene::_update_instance_lightmap_captures(Instance *p_instance) {

    InstanceGeometryData *geom = get_instance_geometry(p_instance->self);

    static const Vector3 cone_traces[12] = {
        Vector3(0, 0, 1),
        Vector3(0.866025f, 0, 0.5f),
        Vector3(0.267617f, 0.823639f, 0.5f),
        Vector3(-0.700629f, 0.509037f, 0.5f),
        Vector3(-0.700629f, -0.509037f, 0.5f),
        Vector3(0.267617f, -0.823639f, 0.5f),
        Vector3(0, 0, -1),
        Vector3(0.866025f, 0, -0.5f),
        Vector3(0.267617f, 0.823639f, -0.5f),
        Vector3(-0.700629f, 0.509037f, -0.5f),
        Vector3(-0.700629f, -0.509037f, -0.5f),
        Vector3(0.267617f, -0.823639f, -0.5f)
    };

    float cone_aperture = 0.577f; // tan(angle) 60 degrees

    if (p_instance->lightmap_capture_data.empty()) {
        p_instance->lightmap_capture_data.resize(12);
    }

    //print_line("update captures for pos: " + p_instance->transform.origin);

    for (int i = 0; i < 12; i++)
        new (&p_instance->lightmap_capture_data.data()[i]) Color;

    //this could use some sort of blending..
    for (Instance * E : geom->lightmap_captures) {
        const PoolVector<RasterizerStorage::LightmapCaptureOctree> *octree = VSG::storage->lightmap_capture_get_octree_ptr(E->base);
        //print_line("octree size: " + itos(octree->size()));
        if (octree->size() == 0)
            continue;
        Transform to_cell_xform = VSG::storage->lightmap_capture_get_octree_cell_transform(E->base);
        int cell_subdiv = VSG::storage->lightmap_capture_get_octree_cell_subdiv(E->base);
        to_cell_xform = to_cell_xform * E->transform.affine_inverse();

        PoolVector<RasterizerStorage::LightmapCaptureOctree>::Read octree_r = octree->read();

        Vector3 pos = to_cell_xform.xform(p_instance->transform.origin);

        const float capture_energy = VSG::storage->lightmap_capture_get_energy(E->base);

        for (int i = 0; i < 12; i++) {

            Vector3 dir = to_cell_xform.basis.xform(cone_traces[i]).normalized();
            Color capture = _light_capture_voxel_cone_trace(octree_r.ptr(), pos, dir, cone_aperture, cell_subdiv);
            capture.r *= capture_energy;
            capture.g *= capture_energy;
            capture.b *= capture_energy;
            p_instance->lightmap_capture_data[i] += capture;
        }
    }
}

bool VisualServerScene::_light_instance_update_shadow(Instance *p_instance, const Transform &p_cam_transform, const CameraMatrix &p_cam_projection, bool p_cam_orthogonal, RID p_shadow_atlas, Scenario *p_scenario) {

    InstanceLightData *light = static_cast<InstanceLightData *>(p_instance->base_data);

    Transform light_transform = p_instance->transform;
    light_transform.orthonormalize(); //scale does not count on lights

    bool animated_material_found = false;

    switch (VSG::storage->light_get_type(p_instance->base)) {

        case RS::LIGHT_DIRECTIONAL: {

            float max_distance = p_cam_projection.get_z_far();
            float shadow_max = VSG::storage->light_get_param(p_instance->base, RS::LIGHT_PARAM_SHADOW_MAX_DISTANCE);
            if (shadow_max > 0 && !p_cam_orthogonal) { //its impractical (and leads to unwanted behaviors) to set max distance in orthogonal camera
                max_distance = MIN(shadow_max, max_distance);
            }
            max_distance = M_MAX(max_distance, p_cam_projection.get_z_near() + 0.001f);
            float min_distance = MIN(p_cam_projection.get_z_near(), max_distance);

            RS::LightDirectionalShadowDepthRangeMode depth_range_mode = VSG::storage->light_directional_get_shadow_depth_range_mode(p_instance->base);

            if (depth_range_mode == RS::LIGHT_DIRECTIONAL_SHADOW_DEPTH_RANGE_OPTIMIZED) {
                //optimize min/max
                Frustum planes = p_cam_projection.get_projection_planes(p_cam_transform);
                int cull_count = p_scenario->sps.cull_convex(planes, instance_shadow_cull_result, MAX_INSTANCE_CULL, RS::INSTANCE_GEOMETRY_MASK);
                Plane base(p_cam_transform.origin, -p_cam_transform.basis.get_axis(2));
                //check distance max and min

                bool found_items = false;
                float z_max = -1e20f;
                float z_min = 1e20f;

                for (int i = 0; i < cull_count; i++) {

                    Instance *instance = instance_shadow_cull_result[i];
                    if (!instance->visible || !(has_component<GeometryComponent>(instance->self.eid)) ) {
                        continue;
                    }

                    auto &cm_geom(get_component<GeometryComponent>(instance->self));
                    if(!cm_geom.can_cast_shadows)
                        continue;

                    if (cm_geom.material_is_animated) {
                        animated_material_found = true;
                    }

                    float max, min;
                    get_component<InstanceBoundsComponent>(instance->self).transformed_aabb.project_range_in_plane(base, min, max);

                    if (max > z_max) {
                        z_max = max;
                    }

                    if (min < z_min) {
                        z_min = min;
                    }

                    found_items = true;
                }

                if (found_items) {
                    min_distance = M_MAX(min_distance, z_min);
                    max_distance = MIN(max_distance, z_max);
                }
            }

            float range = max_distance - min_distance;

            int splits = 0;
            switch (VSG::storage->light_directional_get_shadow_mode(p_instance->base)) {
                case RS::LIGHT_DIRECTIONAL_SHADOW_ORTHOGONAL: splits = 1; break;
                case RS::LIGHT_DIRECTIONAL_SHADOW_PARALLEL_2_SPLITS: splits = 2; break;
                case RS::LIGHT_DIRECTIONAL_SHADOW_PARALLEL_4_SPLITS: splits = 4; break;
            }

            float distances[5];

            distances[0] = min_distance;
            for (int i = 0; i < splits; i++) {
                distances[i + 1] = min_distance + VSG::storage->light_get_param(p_instance->base, RS::LightParam(RS::LIGHT_PARAM_SHADOW_SPLIT_1_OFFSET + i)) * range;
            }

            distances[splits] = max_distance;

            float texture_size = VSG::scene_render->get_directional_light_shadow_size(light->instance);

            bool overlap = VSG::storage->light_directional_get_blend_splits(p_instance->base);

            float first_radius = 0.0;

            for (int i = 0; i < splits; i++) {

                // setup a camera matrix for that range!
                CameraMatrix camera_matrix;

                float aspect = p_cam_projection.get_aspect();

                if (p_cam_orthogonal) {

                    Vector2 vp_he = p_cam_projection.get_viewport_half_extents();

                    camera_matrix.set_orthogonal(vp_he.y * 2.0f, aspect, distances[(i == 0 || !overlap) ? i : i - 1], distances[i + 1], false);
                } else {

                    float fov = p_cam_projection.get_fov();
                    camera_matrix.set_perspective(fov, aspect, distances[(i == 0 || !overlap) ? i : i - 1], distances[i + 1], false);
                }

                //obtain the frustum endpoints

                Vector3 endpoints[8]; // frustum plane endpoints
                bool res = camera_matrix.get_endpoints(p_cam_transform, endpoints);
                ERR_CONTINUE(!res);

                // obtain the light frustm ranges (given endpoints)

                Transform transform = light_transform; //discard scale and stabilize light

                Vector3 x_vec = transform.basis.get_axis(Vector3::AXIS_X).normalized();
                Vector3 y_vec = transform.basis.get_axis(Vector3::AXIS_Y).normalized();
                Vector3 z_vec = transform.basis.get_axis(Vector3::AXIS_Z).normalized();
                //z_vec points agsint the camera, like in default opengl

                float x_min = 0.f, x_max = 0.f;
                float y_min = 0.f, y_max = 0.f;
                float z_min = 0.f, z_max = 0.f;

                // FIXME: z_max_cam is defined, computed, but not used below when setting up
                // ortho_camera. Commented out for now to fix warnings but should be investigated.
                float x_min_cam = 0.f, x_max_cam = 0.f;
                float y_min_cam = 0.f, y_max_cam = 0.f;
                float z_min_cam = 0.f;
                //float z_max_cam = 0.f;

                float bias_scale = 1.0;

                //used for culling

                for (int j = 0; j < 8; j++) {

                    float d_x = x_vec.dot(endpoints[j]);
                    float d_y = y_vec.dot(endpoints[j]);
                    float d_z = z_vec.dot(endpoints[j]);

                    if (j == 0 || d_x < x_min)
                        x_min = d_x;
                    if (j == 0 || d_x > x_max)
                        x_max = d_x;

                    if (j == 0 || d_y < y_min)
                        y_min = d_y;
                    if (j == 0 || d_y > y_max)
                        y_max = d_y;

                    if (j == 0 || d_z < z_min)
                        z_min = d_z;
                    if (j == 0 || d_z > z_max)
                        z_max = d_z;
                }

                {
                    //camera viewport stuff

                    Vector3 center;

                    for (int j = 0; j < 8; j++) {

                        center += endpoints[j];
                    }
                    center /= 8.0;

                    //center=x_vec*(x_max-x_min)*0.5 + y_vec*(y_max-y_min)*0.5 + z_vec*(z_max-z_min)*0.5;

                    float radius = 0;

                    for (int j = 0; j < 8; j++) {

                        float d = center.distance_to(endpoints[j]);
                        if (d > radius)
                            radius = d;
                    }

                    radius *= texture_size / (texture_size - 2.0f); //add a texel by each side

                    if (i == 0) {
                        first_radius = radius;
                    } else {
                        bias_scale = radius / first_radius;
                    }

                    x_max_cam = x_vec.dot(center) + radius;
                    x_min_cam = x_vec.dot(center) - radius;
                    y_max_cam = y_vec.dot(center) + radius;
                    y_min_cam = y_vec.dot(center) - radius;
                    //z_max_cam = z_vec.dot(center) + radius;
                    z_min_cam = z_vec.dot(center) - radius;

                    if (depth_range_mode == RS::LIGHT_DIRECTIONAL_SHADOW_DEPTH_RANGE_STABLE) {
                        //this trick here is what stabilizes the shadow (make potential jaggies to not move)
                        //at the cost of some wasted resolution. Still the quality increase is very well worth it

                        float unit = radius * 2.0f / texture_size;

                        x_max_cam = Math::stepify(x_max_cam, unit);
                        x_min_cam = Math::stepify(x_min_cam, unit);
                        y_max_cam = Math::stepify(y_max_cam, unit);
                        y_min_cam = Math::stepify(y_min_cam, unit);
                    }
                }

                //now that we now all ranges, we can proceed to make the light frustum planes, for culling octree

                Frustum light_frustum_planes;

                //right/left
                light_frustum_planes[0] = Plane(x_vec, x_max);
                light_frustum_planes[1] = Plane(-x_vec, -x_min);
                //top/bottom
                light_frustum_planes[2] = Plane(y_vec, y_max);
                light_frustum_planes[3] = Plane(-y_vec, -y_min);
                //near/far
                light_frustum_planes[4] = Plane(z_vec, z_max + 1e6f);
                light_frustum_planes[5] = Plane(-z_vec, -z_min); // z_min is ok, since casters further than far-light plane are not needed

                int cull_count = p_scenario->sps.cull_convex(light_frustum_planes, instance_shadow_cull_result, MAX_INSTANCE_CULL, RS::INSTANCE_GEOMETRY_MASK);

                // a pre pass will need to be needed to determine the actual z-near to be used

                Plane near_plane(light_transform.origin, -light_transform.basis.get_axis(2));

                for (int j = 0; j < cull_count; j++) {

                    float min, max;
                    Instance *instance = instance_shadow_cull_result[j];
                    if (!instance->visible || !has_component<GeometryComponent>(instance->self.eid) ||
                            !get_component<GeometryComponent>(instance->self).can_cast_shadows) {
                        cull_count--;
                        SWAP(instance_shadow_cull_result[j], instance_shadow_cull_result[cull_count]);
                        j--;
                        continue;
                    }

                    get_component<InstanceBoundsComponent>(instance->self).transformed_aabb.project_range_in_plane(Plane(z_vec, 0), min, max);
                    instance->depth = near_plane.distance_to(instance->transform.origin);
                    instance->depth_layer = 0;
                    if (max > z_max)
                        z_max = max;
                }

                {

                    CameraMatrix ortho_camera;
                    real_t half_x = (x_max_cam - x_min_cam) * 0.5f;
                    real_t half_y = (y_max_cam - y_min_cam) * 0.5f;

                    ortho_camera.set_orthogonal(-half_x, half_x, -half_y, half_y, 0, (z_max - z_min_cam));

                    Transform ortho_transform;
                    ortho_transform.basis = transform.basis;
                    ortho_transform.origin = x_vec * (x_min_cam + half_x) + y_vec * (y_min_cam + half_y) + z_vec * z_max;

                    VSG::scene_render->light_instance_set_shadow_transform(light->instance, ortho_camera, ortho_transform, 0, distances[i + 1], i, bias_scale);
                }

                VSG::scene_render->render_shadow(light->instance, p_shadow_atlas, i, (RasterizerScene::InstanceBase **)instance_shadow_cull_result, cull_count);
            }

        } break;
        case RS::LIGHT_OMNI: {

            RS::LightOmniShadowMode shadow_mode = VSG::storage->light_omni_get_shadow_mode(p_instance->base);

            if (shadow_mode == RS::LIGHT_OMNI_SHADOW_DUAL_PARABOLOID || !VSG::scene_render->light_instances_can_render_shadow_cube()) {

                for (int i = 0; i < 2; i++) {

                    //using this one ensures that raster deferred will have it

                    float radius = VSG::storage->light_get_param(p_instance->base, RS::LIGHT_PARAM_RANGE);

                    float z = i == 0 ? -1 : 1;
                    Plane planes[6] = {
                        light_transform.xform(Plane(Vector3(0, 0, z), radius)),
                        light_transform.xform(Plane(Vector3(1, 0, z).normalized(), radius)),
                        light_transform.xform(Plane(Vector3(-1, 0, z).normalized(), radius)),
                        light_transform.xform(Plane(Vector3(0, 1, z).normalized(), radius)),
                        light_transform.xform(Plane(Vector3(0, -1, z).normalized(), radius)),
                        light_transform.xform(Plane(Vector3(0, 0, -z).normalized(), radius)),
                    };

                    int cull_count = p_scenario->sps.cull_convex(planes, instance_shadow_cull_result, MAX_INSTANCE_CULL, RS::INSTANCE_GEOMETRY_MASK);
                    Plane near_plane(light_transform.origin, light_transform.basis.get_axis(2) * z);

                    for (int j = 0; j < cull_count; j++) {

                        Instance *instance = instance_shadow_cull_result[j];
                        if (!instance->visible || !has_component<GeometryComponent>(instance->self.eid) ||
                                !get_component<GeometryComponent>(instance->self).can_cast_shadows) {
                            cull_count--;
                            SWAP(instance_shadow_cull_result[j], instance_shadow_cull_result[cull_count]);
                            j--;
                        } else {
                            if (get_component<GeometryComponent>(instance->self).material_is_animated) {
                                animated_material_found = true;
                            }

                            instance->depth = near_plane.distance_to(instance->transform.origin);
                            instance->depth_layer = 0;
                        }
                    }

                    VSG::scene_render->light_instance_set_shadow_transform(light->instance, CameraMatrix(), light_transform, radius, 0, i);
                    VSG::scene_render->render_shadow(light->instance, p_shadow_atlas, i, (RasterizerScene::InstanceBase **)instance_shadow_cull_result, cull_count);
                }
            } else { //shadow cube

                float radius = VSG::storage->light_get_param(p_instance->base, RS::LIGHT_PARAM_RANGE);
                CameraMatrix cm;
                cm.set_perspective(90, 1, 0.01f, radius);

                for (int i = 0; i < 6; i++) {

                    //using this one ensures that raster deferred will have it

                    static constexpr const Vector3 view_normals[6] = {
                        Vector3(-1, 0, 0),
                        Vector3(+1, 0, 0),
                        Vector3(0, -1, 0),
                        Vector3(0, +1, 0),
                        Vector3(0, 0, -1),
                        Vector3(0, 0, +1)
                    };
                    static constexpr const Vector3 view_up[6] = {
                        Vector3(0, -1, 0),
                        Vector3(0, -1, 0),
                        Vector3(0, 0, -1),
                        Vector3(0, 0, +1),
                        Vector3(0, -1, 0),
                        Vector3(0, -1, 0)
                    };

                    Transform xform = light_transform * Transform().looking_at(view_normals[i], view_up[i]);

                    Frustum planes = cm.get_projection_planes(xform);

                    int cull_count = p_scenario->sps.cull_convex(planes, instance_shadow_cull_result, MAX_INSTANCE_CULL, RS::INSTANCE_GEOMETRY_MASK);

                    Plane near_plane(xform.origin, -xform.basis.get_axis(2));
                    for (int j = 0; j < cull_count; j++) {

                        Instance *instance = instance_shadow_cull_result[j];
                        if (!instance->visible || !has_component<GeometryComponent>(instance->self.eid) ||
                                !get_component<GeometryComponent>(instance->self).can_cast_shadows) {
                            cull_count--;
                            SWAP(instance_shadow_cull_result[j], instance_shadow_cull_result[cull_count]);
                            j--;
                        } else {
                            if (get_component<GeometryComponent>(instance->self).material_is_animated) {
                                animated_material_found = true;
                            }
                            instance->depth = near_plane.distance_to(instance->transform.origin);
                            instance->depth_layer = 0;
                        }
                    }

                    VSG::scene_render->light_instance_set_shadow_transform(light->instance, cm, xform, radius, 0, i);
                    VSG::scene_render->render_shadow(light->instance, p_shadow_atlas, i, (RasterizerScene::InstanceBase **)instance_shadow_cull_result, cull_count);
                }

                //restore the regular DP matrix
                VSG::scene_render->light_instance_set_shadow_transform(light->instance, CameraMatrix(), light_transform, radius, 0, 0);
            }

        } break;
        case RS::LIGHT_SPOT: {

            float radius = VSG::storage->light_get_param(p_instance->base, RS::LIGHT_PARAM_RANGE);
            float angle = VSG::storage->light_get_param(p_instance->base, RS::LIGHT_PARAM_SPOT_ANGLE);

            CameraMatrix cm;
            cm.set_perspective(angle * 2.0f, 1.0, 0.01f, radius);

            Frustum planes = cm.get_projection_planes(light_transform);
            int cull_count = p_scenario->sps.cull_convex(planes, instance_shadow_cull_result, MAX_INSTANCE_CULL, RS::INSTANCE_GEOMETRY_MASK);

            Plane near_plane(light_transform.origin, -light_transform.basis.get_axis(2));
            for (int j = 0; j < cull_count; j++) {

                Instance *instance = instance_shadow_cull_result[j];
                if (!instance->visible || !has_component<GeometryComponent>(instance->self.eid) ||
                        !get_component<GeometryComponent>(instance->self).can_cast_shadows) {
                    cull_count--;
                    SWAP(instance_shadow_cull_result[j], instance_shadow_cull_result[cull_count]);
                    j--;
                } else {
                    if (get_component<GeometryComponent>(instance->self).material_is_animated) {
                        animated_material_found = true;
                    }
                    instance->depth = near_plane.distance_to(instance->transform.origin);
                    instance->depth_layer = 0;
                }
            }

            VSG::scene_render->light_instance_set_shadow_transform(light->instance, cm, light_transform, radius, 0, 0);
            VSG::scene_render->render_shadow(light->instance, p_shadow_atlas, 0, (RasterizerScene::InstanceBase **)instance_shadow_cull_result, cull_count);

        } break;
    }

    return animated_material_found;
}

void VisualServerScene::render_camera(RID p_camera, RID p_scenario, Size2 p_viewport_size, RID p_shadow_atlas) {
// render to mono camera
#ifndef _3D_DISABLED
    Camera3D *camera = VSG::ecs->registry.try_get<Camera3D>(p_camera.eid);

    ERR_FAIL_COND(!camera);

    /* STEP 1 - SETUP CAMERA */
    CameraMatrix camera_matrix;
    bool ortho = false;

    switch (camera->type) {
        case Camera3D::ORTHOGONAL: {

            camera_matrix.set_orthogonal(
                    camera->size,
                    p_viewport_size.width / (float)p_viewport_size.height,
                    camera->znear,
                    camera->zfar,
                    camera->vaspect);
            ortho = true;
        } break;
        case Camera3D::PERSPECTIVE: {

            camera_matrix.set_perspective(
                    camera->fov,
                    p_viewport_size.width / (float)p_viewport_size.height,
                    camera->znear,
                    camera->zfar,
                    camera->vaspect);
            ortho = false;

        } break;
        case Camera3D::FRUSTUM: {

            camera_matrix.set_frustum(
                    camera->size,
                    p_viewport_size.width / (float)p_viewport_size.height,
                    camera->offset,
                    camera->znear,
                    camera->zfar,
                    camera->vaspect);
            ortho = false;
        } break;
    }

    _prepare_scene(camera->transform, camera_matrix, ortho, camera->env, camera->visible_layers, p_scenario, p_shadow_atlas, RID());
    _render_scene(camera->transform, camera_matrix, ortho, camera->env, p_scenario, p_shadow_atlas, RID(), -1);
#endif
}

void VisualServerScene::render_camera(Ref<ARVRInterface> &p_interface, ARVREyes p_eye, RID p_camera, RID p_scenario, Size2 p_viewport_size, RID p_shadow_atlas) {
    // render for AR/VR interface

    Camera3D *camera = VSG::ecs->registry.try_get<Camera3D>(p_camera.eid); //camera_owner.getornull(p_camera);
    ERR_FAIL_COND(!camera);

    /* SETUP CAMERA, we are ignoring type and FOV here */
    float aspect = p_viewport_size.width / (float)p_viewport_size.height;
    CameraMatrix camera_matrix = p_interface->get_projection_for_eye(p_eye, aspect, camera->znear, camera->zfar);

    // We also ignore our camera position, it will have been positioned with a slightly old tracking position.
    // Instead we take our origin point and have our ar/vr interface add fresh tracking data! Whoohoo!
    Transform world_origin = ARVRServer::get_singleton()->get_world_origin();
    Transform cam_transform = p_interface->get_transform_for_eye(p_eye, world_origin);

    // For stereo render we only prepare for our left eye and then reuse the outcome for our right eye
    if (p_eye == ARVREyes::EYE_LEFT) {
        ///@TODO possibly move responsibility for this into our ARVRServer or ARVRInterface?

        // Center our transform, we assume basis is equal.
        Transform mono_transform = cam_transform;
        Transform right_transform = p_interface->get_transform_for_eye(ARVREyes::EYE_RIGHT, world_origin);
        mono_transform.origin += right_transform.origin;
        mono_transform.origin *= 0.5;

        // We need to combine our projection frustums for culling.
        // Ideally we should use our clipping planes for this and combine them,
        // however our shadow map logic uses our projection matrix.
        // Note: as our left and right frustums should be mirrored, we don't need our right projection matrix.

        // - get some base values we need
        float eye_dist = (mono_transform.origin - cam_transform.origin).length();
        float z_near = camera_matrix.get_z_near(); // get our near plane
        float z_far = camera_matrix.get_z_far(); // get our far plane
        float width = (2.0f * z_near) / camera_matrix.matrix[0][0];
        float x_shift = width * camera_matrix.matrix[2][0];
        float height = (2.0f * z_near) / camera_matrix.matrix[1][1];
        float y_shift = height * camera_matrix.matrix[2][1];

        // printf("Eye_dist = %f, Near = %f, Far = %f, Width = %f, Shift = %f\n", eye_dist, z_near, z_far, width, x_shift);

        // - calculate our near plane size (horizontal only, right_near is mirrored)
        float left_near = -eye_dist - ((width - x_shift) * 0.5f);

        // - calculate our far plane size (horizontal only, right_far is mirrored)
        float left_far = -eye_dist - (z_far * (width - x_shift) * 0.5f / z_near);
        float left_far_right_eye = eye_dist - (z_far * (width + x_shift) * 0.5f / z_near);
        if (left_far > left_far_right_eye) {
            // on displays smaller then double our iod, the right eye far frustrum can overtake the left eyes.
            left_far = left_far_right_eye;
        }

        // - figure out required z-shift
        float slope = (left_far - left_near) / (z_far - z_near);
        float z_shift = (left_near / slope) - z_near;

        // - figure out new vertical near plane size (this will be slightly oversized thanks to our z-shift)
        float top_near = (height - y_shift) * 0.5f;
        top_near += (top_near / z_near) * z_shift;
        float bottom_near = -(height + y_shift) * 0.5f;
        bottom_near += (bottom_near / z_near) * z_shift;

        // printf("Left_near = %f, Left_far = %f, Top_near = %f, Bottom_near = %f, Z_shift = %f\n", left_near, left_far, top_near, bottom_near, z_shift);

        // - generate our frustum
        CameraMatrix combined_matrix;
        combined_matrix.set_frustum(left_near, -left_near, bottom_near, top_near, z_near + z_shift, z_far + z_shift);

        // and finally move our camera back
        Transform apply_z_shift;
        apply_z_shift.origin = Vector3(0.0, 0.0, z_shift); // z negative is forward so this moves it backwards
        mono_transform *= apply_z_shift;

        // now prepare our scene with our adjusted transform projection matrix
        _prepare_scene(mono_transform, combined_matrix, false, camera->env, camera->visible_layers, p_scenario, p_shadow_atlas, RID());
    } else if (p_eye == ARVREyes::EYE_MONO) {
        // For mono render, prepare as per usual
        _prepare_scene(cam_transform, camera_matrix, false, camera->env, camera->visible_layers, p_scenario, p_shadow_atlas, RID());
    }

    // And render our scene...
    _render_scene(cam_transform, camera_matrix, false, camera->env, p_scenario, p_shadow_atlas, RID(), -1);
}

void VisualServerScene::_prepare_scene(const Transform &p_cam_transform, const CameraMatrix &p_cam_projection, bool p_cam_orthogonal, RID p_force_environment, uint32_t p_visible_layers, RID p_scenario, RID p_shadow_atlas, RID p_reflection_probe) {
    SCOPE_AUTONAMED

    // Note, in stereo rendering:
    // - p_cam_transform will be a transform in the middle of our two eyes
    // - p_cam_projection is a wider frustrum that encompasses both eyes

    Scenario *scenario = scenario_owner.getornull(p_scenario);

    render_pass++;
    uint32_t camera_layer_mask = p_visible_layers;

    VSG::scene_render->set_scene_pass(render_pass);

    //rasterizer->set_camera(camera->transform, camera_matrix,ortho);

    Frustum planes = p_cam_projection.get_projection_planes(p_cam_transform);

    Plane near_plane(p_cam_transform.origin, -p_cam_transform.basis.get_axis(2).normalized());
    float z_far = p_cam_projection.get_z_far();

    /* STEP 2 - CULL */
    instance_cull_count = scenario->sps.cull_convex(planes, instance_cull_result, MAX_INSTANCE_CULL);
    light_cull_count = 0;

    reflection_probe_cull_count = 0;

    //light_samplers_culled=0;

    /*
    print_line("OT: "+rtos( (OS::get_singleton()->get_ticks_usec()-t)/1000.0));
    print_line("OTO: "+itos(p_scenario->octree.get_octant_count()));
    print_line("OTE: "+itos(p_scenario->octree.get_elem_count()));
    print_line("OTP: "+itos(p_scenario->octree.get_pair_count()));
    */

    /* STEP 3 - PROCESS PORTALS, VALIDATE ROOMS */
    //removed, will replace with culling

    /* STEP 4 - REMOVE FURTHER CULLED OBJECTS, ADD LIGHTS */

    for (int i = 0; i < instance_cull_count; i++) {

        Instance *ins = instance_cull_result[i];

        bool keep = false;

        if ((camera_layer_mask & ins->layer_mask) == 0) {

            //failure
        } else if (ins->base_type == RS::INSTANCE_LIGHT && ins->visible) {

            if (light_cull_count < MAX_LIGHTS_CULLED) {

                InstanceLightData *light = static_cast<InstanceLightData *>(ins->base_data);

                if (!light->geometries.empty()) {
                    //do not add this light if no geometry is affected by it..
                    light_cull_result[light_cull_count] = ins;
                    light_instance_cull_result[light_cull_count] = light->instance;
                    if (p_shadow_atlas.is_valid() && VSG::storage->light_has_shadow(ins->base)) {
                        VSG::scene_render->light_instance_mark_visible(light->instance); //mark it visible for shadow allocation later
                    }

                    light_cull_count++;
                }
            }
        } else if (ins->base_type == RS::INSTANCE_REFLECTION_PROBE && ins->visible) {

            if (reflection_probe_cull_count < MAX_REFLECTION_PROBES_CULLED) {

                InstanceReflectionProbeData *reflection_probe = static_cast<InstanceReflectionProbeData *>(ins->base_data);

                if (p_reflection_probe != reflection_probe->instance) {
                    //avoid entering The Matrix

                    if (!reflection_probe->geometries.empty()) {
                        //do not add this light if no geometry is affected by it..

                        if (reflection_probe->reflection_dirty || VSG::scene_render->reflection_probe_instance_needs_redraw(reflection_probe->instance)) {
                            if (!reflection_probe->update_list.in_list()) {
                                reflection_probe->render_step = 0;
                                reflection_probe_render_list.add_last(&reflection_probe->update_list);
                            }

                            reflection_probe->reflection_dirty = false;
                        }

                        if (VSG::scene_render->reflection_probe_instance_has_reflection(reflection_probe->instance)) {
                            reflection_probe_instance_cull_result[reflection_probe_cull_count] = reflection_probe->instance;
                            reflection_probe_cull_count++;
                        }
                    }
                }
            }

        } else if (ins->base_type == RS::INSTANCE_GI_PROBE && ins->visible) {

            InstanceGIProbeData *gi_probe = static_cast<InstanceGIProbeData *>(ins->base_data);
            if (!gi_probe->update_element.in_list()) {
                gi_probe_update_list.add(&gi_probe->update_element);
            }

        } else if (has_component<GeometryComponent>(ins->self.eid) && ins->visible && ins->cast_shadows != RS::SHADOW_CASTING_SETTING_SHADOWS_ONLY) {

            keep = true;

            InstanceGeometryData *geom = get_instance_geometry(ins->self);
            GeometryComponent & gcomp = get_component<GeometryComponent>(ins->self);
            if (ins->redraw_if_visible) {
                RenderingServerRaster::redraw_request();
            }

            if (ins->base_type == RS::INSTANCE_PARTICLES) {
                //particles visible? process them
                if (VSG::storage->particles_is_inactive(ins->base)) {
                    //but if nothing is going on, don't do it.
                    keep = false;
                } else {
                    VSG::storage->particles_request_process(ins->base);
                    //particles visible? request redraw
                    RenderingServerRaster::redraw_request();
                }
            }

            if (gcomp.gi_probes_dirty) {
                int l = 0;
                //only called when lights AABB enter/exit this geometry
                ins->light_instances.resize(geom->lighting.size());
                auto &l_wr(ins->light_instances);
                for (Instance * E : geom->lighting) {

                    InstanceLightData *light = static_cast<InstanceLightData *>(E->base_data);

                    l_wr[l++] = light->instance;
                }

                gcomp.lighting_dirty = false;
            }

            if (gcomp.reflection_dirty) {
                int l = 0;
                //only called when reflection probe AABB enter/exit this geometry
                ins->reflection_probe_instances.resize(geom->reflection_probes.size());
                auto &wr(ins->reflection_probe_instances);
                for (Instance * E : geom->reflection_probes) {

                    InstanceReflectionProbeData *reflection_probe = static_cast<InstanceReflectionProbeData *>(E->base_data);

                    wr[l++] = reflection_probe->instance;
                }

                gcomp.reflection_dirty = false;
            }

            if (gcomp.gi_probes_dirty) {
                int l = 0;
                //only called when reflection probe AABB enter/exit this geometry
                ins->gi_probe_instances.resize(geom->gi_probes.size());
                auto &wr(ins->gi_probe_instances);

                for (Instance * E : geom->gi_probes) {

                    InstanceGIProbeData *gi_probe = static_cast<InstanceGIProbeData *>(E->base_data);

                    wr[l++] = gi_probe->probe_instance;
                }

                gcomp.gi_probes_dirty = false;
            }

            ins->depth = near_plane.distance_to(ins->transform.origin);
            ins->depth_layer = CLAMP(int(ins->depth * 16 / z_far), 0, 15);
        }

        if (!keep) {
            // remove, no reason to keep
            instance_cull_count--;
            SWAP(instance_cull_result[i], instance_cull_result[instance_cull_count]);
            i--;
            ins->last_render_pass = 0; // make invalid
        } else {

            ins->last_render_pass = render_pass;
        }
    }

    /* STEP 5 - PROCESS LIGHTS */

    RID *directional_light_ptr = &light_instance_cull_result[light_cull_count];
    directional_light_count = 0;

    // directional lights
    {

        Instance **lights_with_shadow = (Instance **)alloca(sizeof(Instance *) * scenario->directional_lights.size());
        int directional_shadow_count = 0;

        for (Instance *E : scenario->directional_lights) {

            if (light_cull_count + directional_light_count >= MAX_LIGHTS_CULLED) {
                break;
            }

            if (!E->visible)
                continue;

            InstanceLightData *light = static_cast<InstanceLightData *>(E->base_data);

            //check shadow..

            if (light) {
                if (p_shadow_atlas.is_valid() && VSG::storage->light_has_shadow(E->base)) {
                    lights_with_shadow[directional_shadow_count++] = E;
                }
                //add to list
                directional_light_ptr[directional_light_count++] = light->instance;
            }
        }

        VSG::scene_render->set_directional_shadow_count(directional_shadow_count);

        for (int i = 0; i < directional_shadow_count; i++) {

            _light_instance_update_shadow(lights_with_shadow[i], p_cam_transform, p_cam_projection, p_cam_orthogonal, p_shadow_atlas, scenario);
        }
    }

    { //setup shadow maps

        //SortArray<Instance*,_InstanceLightsort> sorter;
        //sorter.sort(light_cull_result,light_cull_count);
        for (int i = 0; i < light_cull_count; i++) {

            Instance *ins = light_cull_result[i];

            if (!p_shadow_atlas.is_valid() || !VSG::storage->light_has_shadow(ins->base))
                continue;

            InstanceLightData *light = static_cast<InstanceLightData *>(ins->base_data);

            float coverage = 0.f;

            { //compute coverage

                Transform cam_xf = p_cam_transform;
                float zn = p_cam_projection.get_z_near();
                Plane p(cam_xf.origin + cam_xf.basis.get_axis(2) * -zn, -cam_xf.basis.get_axis(2)); //camera near plane

                // near plane half width and height
                Vector2 vp_half_extents = p_cam_projection.get_viewport_half_extents();

                switch (VSG::storage->light_get_type(ins->base)) {

                    case RS::LIGHT_OMNI: {

                        float radius = VSG::storage->light_get_param(ins->base, RS::LIGHT_PARAM_RANGE);

                        //get two points parallel to near plane
                        Vector3 points[2] = {
                            ins->transform.origin,
                            ins->transform.origin + cam_xf.basis.get_axis(0) * radius
                        };

                        if (!p_cam_orthogonal) {
                            //if using perspetive, map them to near plane
                            for (int j = 0; j < 2; j++) {
                                if (p.distance_to(points[j]) < 0) {
                                    points[j].z = -zn; //small hack to keep size constant when hitting the screen
                                }

                                p.intersects_segment(cam_xf.origin, points[j], &points[j]); //map to plane
                            }
                        }

                        float screen_diameter = points[0].distance_to(points[1]) * 2;
                        coverage = screen_diameter / (vp_half_extents.x + vp_half_extents.y);
                    } break;
                    case RS::LIGHT_SPOT: {

                        float radius = VSG::storage->light_get_param(ins->base, RS::LIGHT_PARAM_RANGE);
                        float angle = VSG::storage->light_get_param(ins->base, RS::LIGHT_PARAM_SPOT_ANGLE);

                        float w = radius * Math::sin(Math::deg2rad(angle));
                        float d = radius * Math::cos(Math::deg2rad(angle));

                        Vector3 base = ins->transform.origin - ins->transform.basis.get_axis(2).normalized() * d;

                        Vector3 points[2] = {
                            base,
                            base + cam_xf.basis.get_axis(0) * w
                        };

                        if (!p_cam_orthogonal) {
                            //if using perspetive, map them to near plane
                            for (int j = 0; j < 2; j++) {
                                if (p.distance_to(points[j]) < 0) {
                                    points[j].z = -zn; //small hack to keep size constant when hitting the screen
                                }

                                p.intersects_segment(cam_xf.origin, points[j], &points[j]); //map to plane
                            }
                        }

                        float screen_diameter = points[0].distance_to(points[1]) * 2;
                        coverage = screen_diameter / (vp_half_extents.x + vp_half_extents.y);

                    } break;
                    default: {
                        ERR_PRINT("Invalid Light Type");
                    }
                }
            }

            if (light->shadow_dirty) {
                light->last_version++;
                light->shadow_dirty = false;
            }

            bool redraw = VSG::scene_render->shadow_atlas_update_light(p_shadow_atlas, light->instance, coverage, light->last_version);

            if (redraw) {
                //must redraw!
                light->shadow_dirty = _light_instance_update_shadow(ins, p_cam_transform, p_cam_projection, p_cam_orthogonal, p_shadow_atlas, scenario);
            }
        }
    }
}

void VisualServerScene::_render_scene(const Transform &p_cam_transform, const CameraMatrix &p_cam_projection, bool p_cam_orthogonal, RID p_force_environment, RID p_scenario, RID p_shadow_atlas, RID p_reflection_probe, int p_reflection_probe_pass) {
    SCOPE_AUTONAMED

    Scenario *scenario = scenario_owner.getornull(p_scenario);

    /* ENVIRONMENT */

    RID environment;
    if (p_force_environment.is_valid()) //camera has more environment priority
        environment = p_force_environment;
    else if (scenario->environment.is_valid())
        environment = scenario->environment;
    else
        environment = scenario->fallback_environment;

    /* PROCESS GEOMETRY AND DRAW SCENE */

    VSG::scene_render->render_scene(p_cam_transform, p_cam_projection, p_cam_orthogonal, (RasterizerScene::InstanceBase **)instance_cull_result, instance_cull_count, light_instance_cull_result, light_cull_count + directional_light_count, reflection_probe_instance_cull_result, reflection_probe_cull_count, environment, p_shadow_atlas, scenario->reflection_atlas, p_reflection_probe, p_reflection_probe_pass);
}

void VisualServerScene::render_empty_scene(RID p_scenario, RID p_shadow_atlas) {

#ifndef _3D_DISABLED

    Scenario *scenario = scenario_owner.getornull(p_scenario);

    RID environment;
    if (scenario->environment.is_valid())
        environment = scenario->environment;
    else
        environment = scenario->fallback_environment;
    VSG::scene_render->render_scene(Transform(), CameraMatrix(), true, nullptr, 0, nullptr, 0, nullptr, 0, environment, p_shadow_atlas, scenario->reflection_atlas, RID(), 0);
#endif
}

bool VisualServerScene::_render_reflection_probe_step(Instance *p_instance, int p_step) {

    InstanceReflectionProbeData *reflection_probe = static_cast<InstanceReflectionProbeData *>(p_instance->base_data);
    Scenario *scenario = p_instance->scenario;
    ERR_FAIL_COND_V(!scenario, true);

    RenderingServerRaster::redraw_request(); //update, so it updates in editor

    if (p_step == 0) {

        if (!VSG::scene_render->reflection_probe_instance_begin_render(reflection_probe->instance, scenario->reflection_atlas)) {
            return true; //sorry, all full :(
        }
    }

    if (p_step >= 0 && p_step < 6) {

        static const Vector3 view_normals[6] = {
            Vector3(-1, 0, 0),
            Vector3(+1, 0, 0),
            Vector3(0, -1, 0),
            Vector3(0, +1, 0),
            Vector3(0, 0, -1),
            Vector3(0, 0, +1)
        };

        Vector3 extents = VSG::storage->reflection_probe_get_extents(p_instance->base);
        Vector3 origin_offset = VSG::storage->reflection_probe_get_origin_offset(p_instance->base);
        float max_distance = VSG::storage->reflection_probe_get_origin_max_distance(p_instance->base);

        Vector3 edge = view_normals[p_step] * extents;
        float distance = ABS(view_normals[p_step].dot(edge) - view_normals[p_step].dot(origin_offset)); //distance from origin offset to actual view distance limit

        max_distance = M_MAX(max_distance, distance);

        //render cubemap side
        CameraMatrix cm;
        cm.set_perspective(90, 1, 0.01f, max_distance);

        static const Vector3 view_up[6] = {
            Vector3(0, -1, 0),
            Vector3(0, -1, 0),
            Vector3(0, 0, -1),
            Vector3(0, 0, +1),
            Vector3(0, -1, 0),
            Vector3(0, -1, 0)
        };

        Transform local_view;
        local_view.set_look_at(origin_offset, origin_offset + view_normals[p_step], view_up[p_step]);

        Transform xform = p_instance->transform * local_view;

        RID shadow_atlas;

        if (VSG::storage->reflection_probe_renders_shadows(p_instance->base)) {

            shadow_atlas = scenario->reflection_probe_shadow_atlas;
        }

        _prepare_scene(xform, cm, false, RID(), VSG::storage->reflection_probe_get_cull_mask(p_instance->base), p_instance->scenario->self, shadow_atlas, reflection_probe->instance);
        _render_scene(xform, cm, false, RID(), p_instance->scenario->self, shadow_atlas, reflection_probe->instance, p_step);

    } else {
        //do roughness postprocess step until it believes it's done
        return VSG::scene_render->reflection_probe_instance_postprocess_step(reflection_probe->instance);
    }

    return false;
}


void VisualServerScene::_gi_probe_bake_threads(void *self) {

    VisualServerScene *vss = (VisualServerScene *)self;
    vss->_gi_probe_bake_thread();
}

void _setup_gi_probe(VisualServerScene::Instance *p_instance) {

    VisualServerScene::InstanceGIProbeData *probe = static_cast<VisualServerScene::InstanceGIProbeData *>(p_instance->base_data);

    if (probe->dynamic.probe_data.is_valid()) {
        VSG::storage->free(probe->dynamic.probe_data);
        probe->dynamic.probe_data = RID();
    }

    probe->dynamic.light_data = VSG::storage->gi_probe_get_dynamic_data(p_instance->base);

    if (probe->dynamic.light_data.empty())
        return;
    //using dynamic data
    PoolVector<int>::Read r = probe->dynamic.light_data.read();

    const VisualServerScene::GIProbeDataHeader *header = (VisualServerScene::GIProbeDataHeader *)r.ptr();

    probe->dynamic.local_data.resize(header->cell_count);

    int cell_count = probe->dynamic.local_data.size();
    Vector<VisualServerScene::InstanceGIProbeData::LocalData> &ldw = probe->dynamic.local_data;
    const VisualServerScene::GIProbeDataCell *cells = (VisualServerScene::GIProbeDataCell *)&r[16];

    probe->dynamic.level_cell_lists.resize(header->cell_subdiv);

    _gi_probe_fill_local_data(0, 0, 0, 0, 0, cells, header, ldw.data(), probe->dynamic.level_cell_lists.data());

    probe->dynamic.probe_data = VSG::storage->gi_probe_dynamic_data_create(header->width, header->height, header->depth);

    probe->dynamic.bake_dynamic_range = VSG::storage->gi_probe_get_dynamic_range(p_instance->base);

    probe->dynamic.mipmaps_3d.clear();
    probe->dynamic.propagate = VSG::storage->gi_probe_get_propagation(p_instance->base);

    probe->dynamic.grid_size[0] = header->width;
    probe->dynamic.grid_size[1] = header->height;
    probe->dynamic.grid_size[2] = header->depth;

    int size_limit = 1;
    int size_divisor = 1;

    for (int i = 0; i < (int)header->cell_subdiv; i++) {

        int x = header->width >> i;
        int y = header->height >> i;
        int z = header->depth >> i;

        //create and clear mipmap
        int size = x * y * z * 4;
        size /= size_divisor;

        Vector<uint8_t> mipmap(size,uint8_t(0));

        probe->dynamic.mipmaps_3d.emplace_back(eastl::move(mipmap));

        if (x <= size_limit || y <= size_limit || z <= size_limit)
            break;
    }

    probe->dynamic.updating_stage = GIUpdateStage::CHECK;
    probe->invalid = false;
    probe->dynamic.enabled = true;

    Transform cell_to_xform = VSG::storage->gi_probe_get_to_cell_xform(p_instance->base);
    AABB bounds = VSG::storage->gi_probe_get_bounds(p_instance->base);
    float cell_size = VSG::storage->gi_probe_get_cell_size(p_instance->base);

    probe->dynamic.light_to_cell_xform = cell_to_xform * p_instance->transform.affine_inverse();

    VSG::scene_render->gi_probe_instance_set_light_data(probe->probe_instance, p_instance->base, probe->dynamic.probe_data);
    VSG::scene_render->gi_probe_instance_set_transform_to_data(probe->probe_instance, probe->dynamic.light_to_cell_xform);

    VSG::scene_render->gi_probe_instance_set_bounds(probe->probe_instance, bounds.size / cell_size);

    probe->base_version = VSG::storage->gi_probe_get_version(p_instance->base);

}

void VisualServerScene::_gi_probe_bake_thread() {

    while (true) {

        probe_bake_sem.wait();
        if (probe_bake_thread_exit) {
            break;
        }

        Instance *to_bake = nullptr;

        {
            MutexLock guard(probe_bake_mutex);

            if (!probe_bake_list.empty()) {
                to_bake = probe_bake_list.front();
                probe_bake_list.pop_front();
            }
        }

        if (!to_bake)
            continue;

        _bake_gi_probe(to_bake);
    }
}

void VisualServerScene::_bake_gi_probe_light(const GIProbeDataHeader *header, const GIProbeDataCell *cells,
        InstanceGIProbeData::LocalData *local_data, const uint32_t *leaves, int p_leaf_count,
        const InstanceGIProbeData::LightCache &light_cache, int p_sign) {

    int light_r = int(light_cache.color.r * light_cache.energy * 1024.0f) * p_sign;
    int light_g = int(light_cache.color.g * light_cache.energy * 1024.0f) * p_sign;
    int light_b = int(light_cache.color.b * light_cache.energy * 1024.0f) * p_sign;

    float limits[3] = { float(header->width), float(header->height), float(header->depth) };
    int clip_planes = 0;

    switch (light_cache.type) {

        case RS::LIGHT_DIRECTIONAL: {
            Plane clip[3];

            float max_len = Vector3(limits[0], limits[1], limits[2]).length() * 1.1f;

            Vector3 light_axis = -light_cache.transform.basis.get_axis(2).normalized();

            for (int i = 0; i < 3; i++) {

                if (Math::is_zero_approx(light_axis[i]))
                    continue;

                clip[clip_planes].normal[i] = 1.0;

                if (light_axis[i] < 0) {

                    clip[clip_planes].d = limits[i] + 1;
                } else {
                    clip[clip_planes].d -= 1.0f;
                }

                clip_planes++;
            }

            float distance_adv = _get_normal_advance(light_axis);

            int success_count = 0;

            // uint64_t us = OS::get_singleton()->get_ticks_usec();

            for (int i = 0; i < p_leaf_count; i++) {

                uint32_t idx = leaves[i];

                const GIProbeDataCell *cell = &cells[idx];
                InstanceGIProbeData::LocalData *light = &local_data[idx];

                Vector3 to(light->pos[0] + 0.5f, light->pos[1] + 0.5f, light->pos[2] + 0.5f);
                to += -light_axis.sign() * 0.47f; //make it more likely to receive a ray

                Vector3 norm(
                        (((cells[idx].normal >> 16) & 0xFF) / 255.0f) * 2.0f - 1.0f,
                        (((cells[idx].normal >> 8) & 0xFF) / 255.0f) * 2.0f - 1.0f,
                        (((cells[idx].normal >> 0) & 0xFF) / 255.0f) * 2.0f - 1.0f);

                float att = norm.dot(-light_axis);
                if (att < 0.001f) {
                    //not lighting towards this
                    continue;
                }

                Vector3 from = to - max_len * light_axis;

                for (int j = 0; j < clip_planes; j++) {

                    clip[j].intersects_segment(from, to, &from);
                }

                float distance = (to - from).length();
                distance += distance_adv - Math::fmod(distance, distance_adv); //make it reach the center of the box always
                from = to - light_axis * distance;

                uint32_t result = 0xFFFFFFFF;

                while (distance > -distance_adv) { //use this to avoid precision errors

                    result = _gi_bake_find_cell(cells, int(floor(from.x)), int(floor(from.y)), int(floor(from.z)), header->cell_subdiv);
                    if (result != 0xFFFFFFFF) {
                        break;
                    }

                    from += light_axis * distance_adv;
                    distance -= distance_adv;
                }

                if (result == idx) {
                    //cell hit itself! hooray!
                    light->energy[0] += int32_t(light_r * att * ((cell->albedo >> 16) & 0xFF) / 255.0f);
                    light->energy[1] += int32_t(light_g * att * ((cell->albedo >> 8) & 0xFF) / 255.0f);
                    light->energy[2] += int32_t(light_b * att * ((cell->albedo) & 0xFF) / 255.0f);
                    success_count++;
                }
            }

            // print_line("BAKE TIME: " + rtos((OS::get_singleton()->get_ticks_usec() - us) / 1000000.0));
            // print_line("valid cells: " + itos(success_count));

        } break;
        case RS::LIGHT_OMNI:
        case RS::LIGHT_SPOT: {
            Plane clip[3];

            // uint64_t us = OS::get_singleton()->get_ticks_usec();

            Vector3 light_pos = light_cache.transform.origin;
            Vector3 spot_axis = -light_cache.transform.basis.get_axis(2).normalized();

            float local_radius = light_cache.radius * light_cache.transform.basis.get_axis(2).length();

            for (int i = 0; i < p_leaf_count; i++) {

                uint32_t idx = leaves[i];

                const GIProbeDataCell *cell = &cells[idx];
                InstanceGIProbeData::LocalData *light = &local_data[idx];

                Vector3 to(light->pos[0] + 0.5f, light->pos[1] + 0.5f, light->pos[2] + 0.5f);
                to += (light_pos - to).sign() * 0.47f; //make it more likely to receive a ray

                Vector3 norm(
                        (((cells[idx].normal >> 16) & 0xFF) / 255.0f) * 2.0f - 1.0f,
                        (((cells[idx].normal >> 8) & 0xFF) / 255.0f) * 2.0f - 1.0f,
                        (((cells[idx].normal >> 0) & 0xFF) / 255.0f) * 2.0f - 1.0f);

                Vector3 light_axis = (to - light_pos).normalized();
                float distance_adv = _get_normal_advance(light_axis);

                float att = norm.dot(-light_axis);
                if (att < 0.001) {
                    //not lighting towards this
                    continue;
                }

                {
                    float d = light_pos.distance_to(to);
                    if (d + distance_adv > local_radius)
                        continue; // too far away

                    float dt = CLAMP((d + distance_adv) / local_radius, 0, 1);
                    att *= powf(1.0f - dt, light_cache.attenuation);
                }

                if (light_cache.type == RS::LIGHT_SPOT) {

                    float angle = Math::rad2deg(acos(light_axis.dot(spot_axis)));
                    if (angle > light_cache.spot_angle)
                        continue;

                    float d = CLAMP(angle / light_cache.spot_angle, 0, 1);
                    att *= powf(1.0f - d, light_cache.spot_attenuation);
                }

                clip_planes = 0;

                for (int c = 0; c < 3; c++) {

                    if (Math::is_zero_approx(light_axis[c]))
                        continue;
                    clip[clip_planes].normal[c] = 1.0;

                    if (light_axis[c] < 0) {

                        clip[clip_planes].d = limits[c] + 1;
                    } else {
                        clip[clip_planes].d -= 1.0f;
                    }

                    clip_planes++;
                }

                Vector3 from = light_pos;

                for (int j = 0; j < clip_planes; j++) {

                    clip[j].intersects_segment(from, to, &from);
                }

                float distance = (to - from).length();

                distance -= Math::fmod(distance, distance_adv); //make it reach the center of the box always, but this tame make it closer
                from = to - light_axis * distance;

                uint32_t result = 0xFFFFFFFF;

                while (distance > -distance_adv) { //use this to avoid precision errors

                    result = _gi_bake_find_cell(cells, int(floor(from.x)), int(floor(from.y)), int(floor(from.z)), header->cell_subdiv);
                    if (result != 0xFFFFFFFF) {
                        break;
                    }

                    from += light_axis * distance_adv;
                    distance -= distance_adv;
                }

                if (result == idx) {
                    //cell hit itself! hooray!

                    light->energy[0] += int32_t(light_r * att * ((cell->albedo >> 16) & 0xFF) / 255.0f);
                    light->energy[1] += int32_t(light_g * att * ((cell->albedo >> 8) & 0xFF) / 255.0f);
                    light->energy[2] += int32_t(light_b * att * ((cell->albedo) & 0xFF) / 255.0f);
                }
            }
            //print_line("BAKE TIME: " + rtos((OS::get_singleton()->get_ticks_usec() - us) / 1000000.0));
        } break;
    }
}


void VisualServerScene::_bake_gi_probe(Instance *p_gi_probe) {

    InstanceGIProbeData *probe_data = static_cast<InstanceGIProbeData *>(p_gi_probe->base_data);

    PoolVector<int>::Read r = probe_data->dynamic.light_data.read();

    const GIProbeDataHeader *header = (const GIProbeDataHeader *)r.ptr();
    const GIProbeDataCell *cells = (const GIProbeDataCell *)&r[16];

    int leaf_count = probe_data->dynamic.level_cell_lists[header->cell_subdiv - 1].size();
    const uint32_t *leaves = probe_data->dynamic.level_cell_lists[header->cell_subdiv - 1].data();

    InstanceGIProbeData::LocalData *local_data = probe_data->dynamic.local_data.data();

    //remove what must be removed
    for (eastl::pair<const RID,InstanceGIProbeData::LightCache> &E : probe_data->dynamic.light_cache) {

        RID rid = E.first;
        const InstanceGIProbeData::LightCache &lc = E.second;

        if ((!probe_data->dynamic.light_cache_changes.contains(rid) || probe_data->dynamic.light_cache_changes[rid] != lc) && lc.visible) {
            //erase light data

            _bake_gi_probe_light(header, cells, local_data, leaves, leaf_count, lc, -1);
        }
    }

    //add what must be added
    for (eastl::pair<const RID,InstanceGIProbeData::LightCache> &E : probe_data->dynamic.light_cache_changes) {

        RID rid = E.first;
        const InstanceGIProbeData::LightCache &lc = E.second;

        if ((!probe_data->dynamic.light_cache.contains(rid) || probe_data->dynamic.light_cache[rid] != lc) && lc.visible) {
            //add light data

            _bake_gi_probe_light(header, cells, local_data, leaves, leaf_count, lc, 1);
        }
    }

    SWAP(probe_data->dynamic.light_cache_changes, probe_data->dynamic.light_cache);

    //downscale to lower res levels
    _bake_gi_downscale_light(0, 0, cells, header, local_data, probe_data->dynamic.propagate);

    //plot result to 3D texture!


    for (int i = 0; i < (int)header->cell_subdiv; i++) {

        int stage = header->cell_subdiv - i - 1;

        if (stage >= probe_data->dynamic.mipmaps_3d.size())
            continue; //no mipmap for this one

        //print_line("generating mipmap stage: " + itos(stage));
        int level_cell_count = probe_data->dynamic.level_cell_lists[i].size();
        const uint32_t *level_cells = probe_data->dynamic.level_cell_lists[i].data();

        uint8_t *mipmapw = probe_data->dynamic.mipmaps_3d[stage].data();

        uint32_t sizes[3] = { header->width >> stage, header->height >> stage, header->depth >> stage };

        for (int j = 0; j < level_cell_count; j++) {

            uint32_t idx = level_cells[j];

            uint32_t r2 = (uint32_t(local_data[idx].energy[0]) / probe_data->dynamic.bake_dynamic_range) >> 2;
            uint32_t g = (uint32_t(local_data[idx].energy[1]) / probe_data->dynamic.bake_dynamic_range) >> 2;
            uint32_t b = (uint32_t(local_data[idx].energy[2]) / probe_data->dynamic.bake_dynamic_range) >> 2;
            uint32_t a = (cells[idx].level_alpha >> 8) & 0xFF;

            uint32_t mm_ofs = sizes[0] * sizes[1] * (local_data[idx].pos[2]) + sizes[0] * (local_data[idx].pos[1]) + (local_data[idx].pos[0]);
            mm_ofs *= 4; //for RGBA (4 bytes)

            mipmapw[mm_ofs + 0] = uint8_t(MIN(r2, 255));
            mipmapw[mm_ofs + 1] = uint8_t(MIN(g, 255));
            mipmapw[mm_ofs + 2] = uint8_t(MIN(b, 255));
            mipmapw[mm_ofs + 3] = uint8_t(MIN(a, 255));
        }
    }

    //send back to main thread to update un little chunks
    {
        MutexLock guard(probe_bake_mutex);
        probe_data->dynamic.updating_stage = GIUpdateStage::UPLOADING;
    }
}

void VisualServerScene::render_probes() {

    /* REFLECTION PROBES */

    IntrusiveListNode<InstanceReflectionProbeData> *ref_probe = reflection_probe_render_list.first();

    bool busy = false;

    while (ref_probe) {

        IntrusiveListNode<InstanceReflectionProbeData> *next = ref_probe->next();
        RID base = ref_probe->self()->owner->base;

        switch (VSG::storage->reflection_probe_get_update_mode(base)) {

            case RS::REFLECTION_PROBE_UPDATE_ONCE: {
                if (busy) //already rendering something
                    break;

                bool done = _render_reflection_probe_step(ref_probe->self()->owner, ref_probe->self()->render_step);
                if (done) {
                    reflection_probe_render_list.remove(ref_probe);
                } else {
                    ref_probe->self()->render_step++;
                }

                busy = true; //do not render another one of this kind
            } break;
            case RS::REFLECTION_PROBE_UPDATE_ALWAYS: {

                int step = 0;
                bool done = false;
                while (!done) {
                    done = _render_reflection_probe_step(ref_probe->self()->owner, step);
                    step++;
                }

                reflection_probe_render_list.remove(ref_probe);
            } break;
        }

        ref_probe = next;
    }

    /* GI PROBES */

    IntrusiveListNode<InstanceGIProbeData> *gi_probe = gi_probe_update_list.first();

    while (gi_probe) {

        IntrusiveListNode<InstanceGIProbeData> *next = gi_probe->next();

        InstanceGIProbeData *probe = gi_probe->self();
        Instance *instance_probe = probe->owner;

        //check if probe must be setup, but don't do if on the lighting thread

        bool force_lighting = false;

        if (probe->invalid || (probe->dynamic.updating_stage == GIUpdateStage::CHECK &&
              probe->base_version != VSG::storage->gi_probe_get_version(instance_probe->base))) {

            _setup_gi_probe(instance_probe);
            force_lighting = true;
        }

        float propagate = VSG::storage->gi_probe_get_propagation(instance_probe->base);

        if (probe->dynamic.propagate != propagate) {
            probe->dynamic.propagate = propagate;
            force_lighting = true;
        }

        if (!probe->invalid && probe->dynamic.enabled) {

            switch (probe->dynamic.updating_stage) {
                case GIUpdateStage::CHECK: {

                    if (_check_gi_probe(instance_probe) || force_lighting) { //send to lighting thread
                        {
                            MutexLock guard(probe_bake_mutex);
                            probe->dynamic.updating_stage = GIUpdateStage::LIGHTING;
                            probe_bake_list.emplace_back(instance_probe);
                        }
                        probe_bake_sem.post();
                    }
                } break;
                case GIUpdateStage::LIGHTING: {
                    //do none, wait til done!

                } break;
                case GIUpdateStage::UPLOADING: {

                    //uint64_t us = OS::get_singleton()->get_ticks_usec();

                    for (int i = 0; i < (int)probe->dynamic.mipmaps_3d.size(); i++) {

                        const Vector<uint8_t> &r(probe->dynamic.mipmaps_3d[i]);
                        VSG::storage->gi_probe_dynamic_data_update(probe->dynamic.probe_data, 0, probe->dynamic.grid_size[2] >> i, i, r.data());
                    }

                    probe->dynamic.updating_stage = GIUpdateStage::CHECK;

                    //print_line("UPLOAD TIME: " + rtos((OS::get_singleton()->get_ticks_usec() - us) / 1000000.0));
                } break;
            }
        }
        //_update_gi_probe(gi_probe->self()->owner);

        gi_probe = next;
    }
}
_FORCE_INLINE_ void VisualServerScene::_update_dirty_instance(Instance *p_instance)
{
    const Dirty & dt = get_component<Dirty>(p_instance->self);

    if (dt.update_aabb) {
        _update_instance_aabb(p_instance);
    }

    if (dt.update_materials) {

        _update_instance_material(p_instance);
    }

    _update_instance(p_instance);
    clear_component<Dirty>(p_instance->self);
}
void VisualServerScene::_update_instance_material(Instance *p_instance) {

    if (p_instance->base_type == RS::INSTANCE_MESH) {
        //remove materials no longer used and un-own them

        int new_mat_count = VSG::storage->mesh_get_surface_count(p_instance->base);
        for (int i = p_instance->materials.size() - 1; i >= new_mat_count; i--) {
            if (p_instance->materials[i].is_valid()) {
                VSG::storage->material_remove_instance_owner(p_instance->materials[i], p_instance);
            }
        }
        p_instance->materials.resize(new_mat_count);

        int new_blend_shape_count = VSG::storage->mesh_get_blend_shape_count(p_instance->base);
        if (new_blend_shape_count != p_instance->blend_values.size()) {
            p_instance->blend_values.resize(new_blend_shape_count);
            for (int i = 0; i < new_blend_shape_count; i++) {
                p_instance->blend_values[i] = 0;
            }
        }
    }
    if (has_component<GeometryComponent>(p_instance->self.eid)) {

        InstanceGeometryData *geom = get_instance_geometry(p_instance->self);
        auto & gcomp = get_component<GeometryComponent>(p_instance->self);

        bool can_cast_shadows = true;
        bool is_animated = false;

        if (p_instance->cast_shadows == RS::SHADOW_CASTING_SETTING_OFF) {
            can_cast_shadows = false;
        } else if (p_instance->material_override.is_valid()) {
            can_cast_shadows = VSG::storage->material_casts_shadows(p_instance->material_override);
            is_animated = VSG::storage->material_is_animated(p_instance->material_override);
        } else {

            if (p_instance->base_type == RS::INSTANCE_MESH) {
                RID mesh = p_instance->base;

                if (mesh.is_valid()) {
                    bool cast_shadows = false;

                    for (int i = 0; i < p_instance->materials.size(); i++) {

                        RID mat = p_instance->materials[i].is_valid() ? p_instance->materials[i] : VSG::storage->mesh_surface_get_material(mesh, i);

                        if (!mat.is_valid()) {
                            cast_shadows = true;
                        } else {

                            if (VSG::storage->material_casts_shadows(mat)) {
                                cast_shadows = true;
                            }

                            if (VSG::storage->material_is_animated(mat)) {
                                is_animated = true;
                            }
                        }
                    }

                    if (!cast_shadows) {
                        can_cast_shadows = false;
                    }
                }

            } else if (p_instance->base_type == RS::INSTANCE_MULTIMESH) {
                RID mesh = VSG::storage->multimesh_get_mesh(p_instance->base);
                if (mesh.is_valid()) {

                    bool cast_shadows = false;

                    int sc = VSG::storage->mesh_get_surface_count(mesh);
                    for (int i = 0; i < sc; i++) {

                        RID mat = VSG::storage->mesh_surface_get_material(mesh, i);

                        if (!mat.is_valid()) {
                            cast_shadows = true;

                        } else {

                            if (VSG::storage->material_casts_shadows(mat)) {
                                cast_shadows = true;
                            }
                            if (VSG::storage->material_is_animated(mat)) {
                                is_animated = true;
                            }
                        }
                    }

                    if (!cast_shadows) {
                        can_cast_shadows = false;
                    }
                }
            } else if (p_instance->base_type == RS::INSTANCE_IMMEDIATE) {

                RID mat = VSG::storage->immediate_get_material(p_instance->base);

                can_cast_shadows = !mat.is_valid() || VSG::storage->material_casts_shadows(mat);

                if (mat.is_valid() && VSG::storage->material_is_animated(mat)) {
                    is_animated = true;
                }
            } else if (p_instance->base_type == RS::INSTANCE_PARTICLES) {

                bool cast_shadows = false;

                int dp = VSG::storage->particles_get_draw_passes(p_instance->base);

                for (int i = 0; i < dp; i++) {

                    RID mesh = VSG::storage->particles_get_draw_pass_mesh(p_instance->base, i);
                    if (!mesh.is_valid()) {
                        continue;
                    }

                    int sc = VSG::storage->mesh_get_surface_count(mesh);
                    for (int j = 0; j < sc; j++) {

                        RID mat = VSG::storage->mesh_surface_get_material(mesh, j);

                        if (!mat.is_valid()) {
                            cast_shadows = true;
                        } else {

                            if (VSG::storage->material_casts_shadows(mat)) {
                                cast_shadows = true;
                            }

                            if (VSG::storage->material_is_animated(mat)) {
                                is_animated = true;
                            }
                        }
                    }
                }

                if (!cast_shadows) {
                    can_cast_shadows = false;
                }
            }
        }

        if (can_cast_shadows != gcomp.can_cast_shadows) {
            //ability to cast shadows change, let lights now
            for (Instance * E : geom->lighting) {
                InstanceLightData *light = static_cast<InstanceLightData *>(E->base_data);
                light->shadow_dirty = true;
            }

            gcomp.can_cast_shadows = can_cast_shadows;
        }

        gcomp.material_is_animated = is_animated;
    }

    clear_component<Dirty>(p_instance->self);

    _update_instance(p_instance);

}

void VisualServerScene::update_dirty_instances() {

    SCOPE_AUTONAMED

    {
        SCOPE_PROFILE(update_resources);
        VSG::storage->update_dirty_resources();
    }

    //auto view = VSG::ecs->registry.view<InstanceComponent, Dirty>();
    auto view = VSG::ecs->registry.group<>(entt::get<InstanceComponent, Dirty>);
    FixedVector<Scenario *,16,true> scenarios_to_update;
    for (auto entity : view) {
        Instance *p_instance = view.get<InstanceComponent>(entity).instance;
        const Dirty & dt = view.get<Dirty>(entity);
        if (dt.update_aabb) {
            _update_instance_aabb(p_instance);
        }
        if (dt.update_materials) {
            _update_instance_material(p_instance);
        }
        _update_instance(p_instance);
        if(p_instance->scenario && !scenarios_to_update.contains(p_instance->scenario)) {
            scenarios_to_update.emplace_back(p_instance->scenario);
        }
    }
    //remove dirty for everything
    VSG::ecs->registry.clear<Dirty>();
    for(auto scn : scenarios_to_update) {
        scn->sps.update();
    }
}

bool VisualServerScene::free(RID p_rid) {


    if (scenario_owner.owns(p_rid)) {

        Scenario *scenario = scenario_owner.get(p_rid);

        while (scenario->instances.first()) {
            instance_set_scenario(scenario->instances.first()->self()->self, RID());
        }
        VSG::scene_render->free(scenario->reflection_probe_shadow_atlas);
        VSG::scene_render->free(scenario->reflection_atlas);
        scenario_owner.free(p_rid);
        memdelete(scenario);

    } else if (instance_owner.owns(p_rid)) {
        // delete the instance

        update_dirty_instances();

        Instance *instance = instance_owner.get(p_rid);

        instance_set_use_lightmap(p_rid, RID(), RID());
        instance_set_scenario(p_rid, RID());
        instance_set_base(p_rid, RID());
        instance_geometry_set_material_override(p_rid, RID());
        instance_attach_skeleton(p_rid, RID());

        update_dirty_instances(); //in case something changed this

        instance_owner.free(p_rid);
        memdelete(instance);
    } else {
        return false;
    }

    if (VSG::ecs->registry.valid(p_rid.eid)) {
        VSG::ecs->registry.destroy(p_rid.eid);
    }
    return true;
}

VisualServerScene *VisualServerScene::singleton = nullptr;

VisualServerScene::VisualServerScene() {

    probe_bake_thread.start(_gi_probe_bake_threads, this);
    probe_bake_thread_exit = false;

    render_pass = 1;
    singleton = this;
}

VisualServerScene::~VisualServerScene() {
    probe_bake_thread_exit = true;
    probe_bake_sem.post();
    probe_bake_thread.wait_to_finish();
}
