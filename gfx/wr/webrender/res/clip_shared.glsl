/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include rect,render_task,gpu_cache,snap,transform

#ifdef WR_VERTEX_SHADER

in int aClipRenderTaskAddress;
in int aClipTransformId;
in int aPrimTransformId;
in int aClipSegment;
in ivec4 aClipDataResourceAddress;
in vec2 aClipLocalPos;
in vec4 aClipTileRect;
in vec4 aClipDeviceArea;
in vec4 aClipSnapOffsets;

struct ClipMaskInstance {
    int render_task_address;
    int clip_transform_id;
    int prim_transform_id;
    ivec2 clip_data_address;
    ivec2 resource_address;
    vec2 local_pos;
    RectWithSize tile_rect;
    RectWithSize sub_rect;
    vec4 snap_offsets;
};

ClipMaskInstance fetch_clip_item() {
    ClipMaskInstance cmi;

    cmi.render_task_address = aClipRenderTaskAddress;
    cmi.clip_transform_id = aClipTransformId;
    cmi.prim_transform_id = aPrimTransformId;
    cmi.clip_data_address = aClipDataResourceAddress.xy;
    cmi.resource_address = aClipDataResourceAddress.zw;
    cmi.local_pos = aClipLocalPos;
    cmi.tile_rect = RectWithSize(aClipTileRect.xy, aClipTileRect.zw);
    cmi.sub_rect = RectWithSize(aClipDeviceArea.xy, aClipDeviceArea.zw);
    cmi.snap_offsets = aClipSnapOffsets;

    return cmi;
}

struct ClipVertexInfo {
    vec3 local_pos;
    RectWithSize clipped_local_rect;
};

RectWithSize intersect_rect(RectWithSize a, RectWithSize b) {
    vec4 p = clamp(vec4(a.p0, a.p0 + a.size), b.p0.xyxy, b.p0.xyxy + b.size.xyxy);
    return RectWithSize(p.xy, max(vec2(0.0), p.zw - p.xy));
}

// The transformed vertex function that always covers the whole clip area,
// which is the intersection of all clip instances of a given primitive
ClipVertexInfo write_clip_tile_vertex(RectWithSize local_clip_rect,
                                      Transform prim_transform,
                                      Transform clip_transform,
                                      ClipArea area,
                                      RectWithSize sub_rect,
                                      vec4 snap_offsets) {
    vec2 device_pos = area.screen_origin + sub_rect.p0 +
                      aPosition.xy * sub_rect.size;

    // If the primitive we are drawing a clip mask for was snapped, then
    // remove the effect of that snapping, so that the local position
    // interpolation below works correctly relative to the clip item.
    vec2 snap_offset = mix(
        snap_offsets.xy,
        snap_offsets.zw,
        aPosition.xy
    );

    device_pos -= snap_offset;

    vec2 world_pos = device_pos / area.device_pixel_scale;

    vec4 pos = prim_transform.m * vec4(world_pos, 0.0, 1.0);
    pos.xyz /= pos.w;

    vec4 p = get_node_pos(pos.xy, clip_transform);
    vec3 local_pos = p.xyw * pos.w;

    vec4 vertex_pos = vec4(
        area.common_data.task_rect.p0 + sub_rect.p0 + aPosition.xy * sub_rect.size,
        0.0,
        1.0
    );

    gl_Position = uTransform * vertex_pos;

    init_transform_vs(vec4(local_clip_rect.p0, local_clip_rect.p0 + local_clip_rect.size));

    ClipVertexInfo vi = ClipVertexInfo(local_pos, local_clip_rect);
    return vi;
}

#endif //WR_VERTEX_SHADER
