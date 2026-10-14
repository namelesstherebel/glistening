// @name Ridgeline
// @desc the last seconds of spectrum as a contoured landscape at dusk
// @busy 0.5
// @bloom 0.7
// @palettes dusk,solar,abyss,indigo
out vec4 fragColor;

const float ROW = 0.12;      // world z per history row
const float NEAR = 1.2;      // z where the newest spectrum is laid down
const float BAND = 3.2;      // half-width of the spectrum strip; bass in the middle

float scrollZ() { return uHistPos * ROW; }

float base(vec2 p) {  // ground the music is laid on, scrolling with the history
    vec2 q = vec2(p.x, p.y + scrollZ()) * 0.35 + uSeed * 20.0;
    float r = 1.0 - abs(gnoise(q * 0.7));
    return 0.12 + 0.16 * fbm(q, 2) + 0.5 * r * r * smoothstep(BAND * 0.6, BAND * 1.8, abs(p.x));
}

float height(vec2 p) {
    float age = (p.y - NEAR) / (ROW * 94.0);
    float x = abs(p.x) / BAND;
    float m = 0.0;
    if (age > 0.0 && age < 1.0 && x < 1.0) {
        float hx = pow(x, 0.8);
        float v = 0.25 * hist(hx, age - 0.012) + 0.5 * hist(hx, age) + 0.25 * hist(hx, age + 0.012);  // smoothed along time
        m = v * v * (0.32 + 0.2 * uEnergySlow + 0.15 * uDrop) * (1.0 - smoothstep(0.75, 1.0, x)) * smoothstep(0.0, 0.04, age) * (1.0 - smoothstep(0.85, 1.0, age));
    }
    return base(p) + m;
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    float sway = 0.6 * sin(uTime * 0.05 + uVariant.x * 6.28);
    vec3 ro = vec3(sway, 1.5 + 0.15 * uVariant.y - 0.06 * uBeat - 0.04 * uBass, 0.0);
    vec3 fw = normalize(vec3(-sway * 0.08, -0.3, 1.0)), rt = normalize(cross(vec3(0, 1, 0), fw)), up = cross(fw, rt);
    vec3 rd = normalize(fw * 1.5 + uv.x * rt + uv.y * up);

    vec3 sun = normalize(vec3(0.15 * sin(uVariant.z * 6.28), 0.06, 1.0));
    float sd = max(dot(rd, sun), 0.0);
    vec3 sky = mix(uPal[2] * 0.35, uPal[0] * 0.5, smoothstep(-0.05, 0.4, rd.y));
    sky += uPal[3] * (pow(sd, 400.0) * (3.0 + 2.0 * uDrop) + pow(sd, 12.0) * (0.25 + 0.25 * uBeat + 0.3 * uBass));
    vec3 col = sky;

    float t = 0.4, h = 0.0, prevD = 1.0, prevT = t;
    bool hit = false;
    int steps = int(40.0 + 50.0 * uQuality);
    for (int i = 0; i < 120; i++) {
        if (i >= steps || t > 22.0) break;
        vec3 p = ro + rd * t;
        h = height(p.xz);
        float d = p.y - h;
        if (d < 0.002 * t) {
            float a = prevT, b = t;  // refine between the last two samples
            for (int k = 0; k < 5; k++) {
                float m = 0.5 * (a + b);
                vec3 q = ro + rd * m;
                if (q.y - height(q.xz) < 0.0) b = m; else a = m;
            }
            t = 0.5 * (a + b);
            hit = true;
            break;
        }
        prevT = t;
        t += max(0.02 * t, d * 0.55);
    }
    if (hit) {
        vec3 p = ro + rd * t;
        h = height(p.xz);
        vec2 e = vec2(0.02, 0.0);
        vec3 n = normalize(vec3(height(p.xz - e.xy) - height(p.xz + e.xy), 2.0 * e.x, height(p.xz - e.yx) - height(p.xz + e.yx)));
        float dif = max(dot(n, normalize(sun + vec3(0, 0.4, 0))), 0.0), rim = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
        vec3 ground = uPal[0] * 0.6 + uPal[1] * 0.35 * dif + uPal[3] * 0.25 * rim * (0.5 + uBass);
        // Contours every 0.07 in height; finer ones carry the highs.
        float fw = fwidth(h) + 1e-4;
        float c1 = abs(fract(h / 0.07 + 0.5) - 0.5) * 0.07;
        float c2 = abs(fract(h / 0.0175 + 0.5) - 0.5) * 0.0175;
        float line = (1.0 - smoothstep(0.3 * fw, 1.2 * fw, c1)), fine = (1.0 - smoothstep(0.2 * fw, fw, c2)) * step(fw, 0.006);
        float lift = smoothstep(0.02, 0.4, h - base(p.xz) + 0.02);  // music lines glow, plain ground stays quiet
        vec3 lc = pal(0.45 + 0.55 * clamp(h * 1.4, 0.0, 1.0));
        col = ground + lc * line * (0.25 + 1.4 * lift) * (0.8 + 2.2 * uBeat) + lc * fine * (0.05 + 0.6 * uHigh) * lift;
        col = mix(col, sky, smoothstep(6.0, 21.0, t));  // haze
    }
    fragColor = vec4(col, 1.0);
}
