#version 450
/* Unlit fragment: flat vertex color (additive/alpha chosen by pipeline). */
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = v_color;
}
