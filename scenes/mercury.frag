// @name Mercury
// @desc liquid chrome that sings each band
// @busy 0.55
// @bloom 0.7
// @palettes silver,glacier,ember,solar
out vec4 fragColor;

const int BLOBS = 6;
vec4 blob[BLOBS];  // xyz centre, radius

void setup() {
    float f = uFlow * 0.22, spread = 1.0 + 0.35 * uDrop + 0.15 * uMid;
    for (int i = 0; i < BLOBS; i++) {
        float fi = float(i), h = hash11(fi * 7.1 + uSeed * 13.0);
        vec3 p = vec3(sin(f * (0.7 + 0.3 * h) + fi * 2.1), sin(f * (0.5 + 0.4 * hash11(fi + 3.3)) + fi * 1.3) * 0.75,
                      cos(f * (0.6 + 0.2 * h) + fi * 2.7)) * (0.55 + 0.3 * h) * spread;
        float band = spec(0.08 + 0.8 * fi / float(BLOBS - 1));  // each blob swells with its own band
        blob[i] = vec4(p, 0.3 + 0.08 * h + 0.13 * band + 0.05 * uBeat + 0.05 * uBass);
    }
}

float smin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}

float map(vec3 p) {
    float d = 1e9, k = 0.45 + 0.35 * uBass;
    for (int i = 0; i < BLOBS; i++) d = smin(d, length(p - blob[i].xyz) - blob[i].w, k);
    return d;
}

vec3 normal(vec3 p) {
    const vec2 e = vec2(0.0015, -0.0015);
    return normalize(e.xyy * map(p + e.xyy) + e.yyx * map(p + e.yyx) + e.yxy * map(p + e.yxy) + e.xxx * map(p + e.xxx));
}

// Dark studio: horizon line, an overhead softbox, two thin vertical strips.
vec3 env(vec3 r) {
    vec3 c = mix(uPal[1] * 0.35, uPal[0] * 0.3, smoothstep(-0.1, 0.9, r.y));
    c = mix(uPal[1] * 0.18 + uPal[2] * 0.06 * smoothstep(-1.0, 0.0, r.y), c, smoothstep(-0.02, 0.02, r.y));  // floor
    c += mix(uPal[2], uPal[3], 0.5) * 1.6 * exp(-abs(r.y) * 60.0) * (0.7 + 0.6 * uMid);  // horizon line
    if (r.y > 0.2) {  // softbox: a rounded rectangle on the ceiling
        vec2 q = rot(uFlow * 0.03 + uVariant.z * 3.0) * (r.xz / r.y) - vec2(0.15, 0.0);
        vec2 b = abs(q) - vec2(0.55, 0.22);
        float d = length(max(b, 0.0)) + min(max(b.x, b.y), 0.0) - 0.08;
        c += uPal[4] * 2.2 * (1.0 - smoothstep(-0.04, 0.04, d)) * (0.85 + 0.5 * uBeat);
    }
    float a = atan(r.x, r.z) + uFlow * 0.04 + uVariant.y * 6.28;
    float strips = (1.0 - smoothstep(0.015, 0.05, abs(sin(a)))) * (1.0 - smoothstep(0.45, 0.6, abs(r.y - 0.15)));
    float lit = 0.8 + 0.9 * uBeat + 1.2 * uDrop + 0.6 * spec(0.15);
    c += mix(uPal[3], uPal[4], 0.25 + 0.5 * step(0.0, cos(a))) * strips * 2.4 * lit;
    vec3 key = normalize(vec3(-0.6, 0.5, 0.6));  // broad, dim key light models the forms
    c += mix(uPal[2], uPal[4], 0.3) * 0.45 * smoothstep(0.2, 1.0, dot(r, key));
    vec3 g = r * 40.0;  // highs: glints scattered across the surface
    float sp = hash12(floor(g.xy) + floor(g.z) * 17.0 + floor(uTime * 8.0));
    c += uPal[4] * step(0.9985, sp) * 2.0 * uHigh * uHigh * uHigh;
    return c;
}

float march(vec3 ro, vec3 rd, int steps, out vec3 p) {
    float t = 0.0;
    for (int i = 0; i < 96; i++) {
        if (i >= steps) break;
        p = ro + rd * t;
        float d = map(p);
        if (d < 0.0015 * t) return t;
        t += d * 0.9;
        if (t > 9.0) break;
    }
    return -1.0;
}

void main() {
    setup();
    vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    float az = uVariant.x * 6.28 + uFlow * 0.03, dist = 4.8 - 0.25 * uBass - 0.3 * uEnergySlow;
    vec3 ro = vec3(sin(az) * dist, 0.6 + 0.4 * sin(uFlow * 0.05), cos(az) * dist);
    vec3 fw = normalize(-ro), rt = normalize(cross(vec3(0, 1, 0), fw)), up = cross(fw, rt);
    vec3 rd = normalize(fw * 1.6 + uv.x * rt + uv.y * up);

    vec3 bg = mix(uPal[0] * 0.4, uPal[1] * 0.3, smoothstep(-0.6, 0.6, uv.y)) + uPal[2] * 0.07 * exp(-4.0 * dot(uv, uv));
    vec3 col = bg, p;
    int steps = int(36.0 + 40.0 * uQuality);
    float t = march(ro, rd, steps, p);
    if (t > 0.0) {
        vec3 n = normal(p), r = reflect(rd, n);
        float fres = 0.55 + 0.45 * pow(1.0 - max(dot(-rd, n), 0.0), 4.0);
        vec3 refl = env(r);
        if (uQuality > 0.55) {  // one bounce: blobs see each other
            vec3 q;
            float t2 = march(p + n * 0.02, r, steps / 2, q);
            if (t2 > 0.0) refl = env(reflect(r, normal(q))) * 0.6;
        }
        col = refl * fres * mix(vec3(1.0), uPal[4], 0.25);
        col *= 0.8 + 0.2 * smoothstep(-0.5, 0.8, n.y);  // ambient occlusion-ish underside
    }
    fragColor = vec4(col, 1.0);
}
