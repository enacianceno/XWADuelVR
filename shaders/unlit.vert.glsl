#version 450
/* Unlit vertex: MVP transform + pass color/uv. Shared by lasers, reticle,
 * HUD bars, particles, sky points. */
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec2 in_uv;

layout(set = 1, binding = 0) uniform VSUbo {
    mat4 mvp;
    float point_size;
    float _p0;
    float _p1;
    float _p2;
} ubo;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;

void main() {
    v_color = in_color;
    v_uv = in_uv;
    gl_Position = ubo.mvp * vec4(in_pos, 1.0);
    gl_PointSize = ubo.point_size;
}
