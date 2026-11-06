#version 450
// Deterministic offscreen frame: a fixed gradient with a diagonal
// band. No time input — the same frame renders to the same pixels on
// every run, so the handoff hash is a receipt, not a screenshot.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform Push { float width; float height; } pc;

void main() {
    vec2 px = vec2(v_uv.x * pc.width, (1.0 - v_uv.y) * pc.height);
    float band = smoothstep(0.45, 0.5, fract((px.x + px.y) / 97.0))
               * smoothstep(0.55, 0.5, fract((px.x + px.y) / 97.0));
    vec3 base = mix(vec3(0.05, 0.02, 0.10), vec3(0.10, 0.55, 0.65), v_uv.x);
    base = mix(base, vec3(0.95, 0.45, 0.75), v_uv.y * 0.45);
    vec3 col = base + band * vec3(0.25, 0.20, 0.05);
    out_color = vec4(col, 1.0);
}
