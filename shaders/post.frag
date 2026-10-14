// Bloom, drop-only chromatic aberration, tonemap, vignette, panel backdrops, dither.
// Output rows are top-down so the CPU reads the image without flipping.
uniform sampler2D uBloom;
uniform float uGlow, uDrop, uExposure, uTime;
uniform vec4 uRects[8];  // TUI panels, 0..1 top-left origin
uniform int uRectN;
uniform vec3 uTint;
out vec4 o;

float rbox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec2 suv = vec2(vUv.x, 1.0 - vUv.y);  // scene texture coordinate
    vec2 scr = vUv;                       // screen position, top-left origin
    vec3 c;
    float ab = uDrop * 0.0045;
    if (ab > 0.0004) {
        vec2 d = (suv - 0.5) * ab;
        c = vec3(scene(suv + d).r, scene(suv).g, scene(suv - d).b);
    } else {
        c = scene(suv);
    }
    vec3 bl = texture(uBloom, suv).rgb;
    c += max(bl - 0.12, 0.0) * uGlow;
    c = 1.0 - exp(-c * uExposure);

    vec2 asp = vec2(uRes.x / uRes.y, 1.0);
    float v = length((scr - 0.5) * asp * vec2(0.85, 1.0));
    c *= mix(1.0, 0.68, smoothstep(0.35, 1.05, v));
    c = pow(c, vec3(1.0 / 2.2));

    for (int i = 0; i < 8; i++) {
        if (i >= uRectN) break;
        vec4 r = uRects[i];
        vec2 px = scr * uRes, a = r.xy * uRes, b = r.zw * uRes;
        float d = rbox(px - 0.5 * (a + b), 0.5 * (b - a), min(6.0, 0.25 * min(b.x - a.x, b.y - a.y)));
        float inside = 1.0 - smoothstep(-0.75, 0.75, d);
        vec3 frost = pow(1.0 - exp(-bl * uExposure), vec3(1.0 / 2.2)) * 0.32 + pow(uTint, vec3(1.0 / 2.2)) * 0.06 + 0.012;
        c = mix(c, frost, inside * 0.94);
        c += vec3(0.07) * (1.0 - smoothstep(0.0, 1.2, abs(d + 0.6))) * inside;  // hairline edge
    }

    vec2 fc = gl_FragCoord.xy + fract(uTime * 7.31) * 61.0;
    c += (h21(fc) + h21(fc + 17.13) - 1.0) / 255.0;
    o = vec4(c, 1.0);
}
