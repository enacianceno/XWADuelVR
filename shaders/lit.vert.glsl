#version 450
/* Lit textured ship/cockpit vertex shader. SDL_GPU Vulkan convention:
 * vertex uniforms in set 1, fragment samplers in set 2, fragment uniforms set 3. */
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(set = 1, binding = 0) uniform VSUbo {
    mat4 mvp;
    mat4 model;
} ubo;

layout(location = 0) out vec3 v_normal_w;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec3 v_pos_w;

void main() {
    vec4 wp = ubo.model * vec4(in_pos, 1.0);
    v_pos_w = wp.xyz;
    v_normal_w = normalize(mat3(ubo.model) * in_normal);
    v_uv = in_uv;
    gl_Position = ubo.mvp * vec4(in_pos, 1.0);
}
