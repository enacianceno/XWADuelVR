#version 450
/* Lit fragment: baseColor * (ambient + directional) + emissive. */
layout(set = 2, binding = 0) uniform sampler2D tex_base;
layout(set = 2, binding = 1) uniform sampler2D tex_emissive;

layout(set = 3, binding = 0) uniform FSUbo {
    vec3 light_dir_w;   /* direction TO the light, world */
    float ambient;
    float dir_intensity;
    float emissive_strength;
    float alpha;
    float _pad;
} fsu;

layout(location = 0) in vec3 v_normal_w;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_pos_w;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 base = texture(tex_base, v_uv).rgb;
    vec3 emis = texture(tex_emissive, v_uv).rgb;
    float ndl = max(dot(normalize(v_normal_w), normalize(fsu.light_dir_w)), 0.0);
    vec3 lit = base * (fsu.ambient + fsu.dir_intensity * ndl);
    vec3 col = lit + emis * fsu.emissive_strength;
    out_color = vec4(col, fsu.alpha);
}
