#version 450
/* Procedural gas-giant fragment: latitude bands + limb darkening + day/night.
 * P0 placeholder until an XWA backdrop asset is wired (see FUTURE.md). */
layout(set = 3, binding = 0) uniform FSUbo {
    vec3 light_dir_w;
    float ambient;
    vec3 band_a;
    float band_freq;
    vec3 band_b;
    float _pad;
} fsu;

layout(location = 0) in vec3 v_normal_w;
layout(location = 1) in vec3 v_local;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 n = normalize(v_normal_w);
    vec3 l = normalize(fsu.light_dir_w);
    float lat = normalize(v_local).y;
    float band = 0.5 + 0.5 * sin(lat * fsu.band_freq + 1.3 * sin(lat * 7.0));
    band = smoothstep(0.25, 0.75, band);
    vec3 base = mix(fsu.band_a, fsu.band_b, band);
    float ndl = max(dot(n, l), 0.0);
    float day = smoothstep(0.0, 0.25, ndl);
    vec3 col = base * (fsu.ambient + day);
    /* limb darkening toward silhouette */
    vec3 vdir = vec3(0.0, 0.0, 1.0); /* approx: planet roughly ahead; cheap */
    float limb = pow(max(dot(n, l) * 0.5 + 0.5, 0.0), 0.5);
    col *= mix(0.55, 1.0, limb);
    out_color = vec4(col, 1.0);
}
