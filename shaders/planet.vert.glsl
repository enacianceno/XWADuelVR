#version 450
/* Planet vertex: pass world normal + local position for procedural bands. */
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;

layout(set = 1, binding = 0) uniform VSUbo {
    mat4 mvp;
    mat4 model;
} ubo;

layout(location = 0) out vec3 v_normal_w;
layout(location = 1) out vec3 v_local;

void main() {
    v_normal_w = normalize(mat3(ubo.model) * in_normal);
    v_local = in_pos;
    gl_Position = ubo.mvp * vec4(in_pos, 1.0);
}
