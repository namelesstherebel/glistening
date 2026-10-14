// @particles 60000
// Stars of a two-armed spiral: log-spiral arms, a bulge, a thin disc.
// Star-forming knots cluster on the arms, a dust lane runs along each inner edge.
// Each beat sends a ring of light out from the core.

mat3 camera(out vec3 ro) {
    float az = uVariant.x * 6.28 + uFlow * 0.02, inc = 1.0 + 0.25 * uVariant.y;  // inclination from the pole
    float dist = 2.05 - 0.2 * uBass - 0.35 * uDrop - 0.2 * uEnergySlow;
    ro = dist * vec3(sin(inc) * sin(az), cos(inc), sin(inc) * cos(az));
    vec3 fw = normalize(-ro), rt = normalize(cross(vec3(0, 1, 0), fw));
    return mat3(rt, cross(fw, rt), fw);
}

void main() {
    float id = float(gl_InstanceID);
    if (id >= uCount) { hide(); return; }
    vec3 h = hash33(vec3(id * 0.731, id * 1.37 + 7.0, uSeed * 91.0));
    vec3 h2 = hash33(vec3(id * 1.913 + 3.0, id * 0.37, 11.0));
    float arms = 2.0 + floor(uVariant.z * 2.0);
    bool bulge = h.z < 0.12;
    vec3 p;
    float r, heat, lane = 0.0;
    bool knot = !bulge && hash11(id * 0.53 + 2.0) < 0.07;  // young clusters: tight clumps on the arm crests
    if (bulge) {
        r = 0.22 * pow(h.x, 1.5);
        float th = h.y * TAU, ph = acos(2.0 * h2.x - 1.0);
        p = r * vec3(sin(ph) * cos(th), cos(ph) * 0.6, sin(ph) * sin(th));
        heat = 1.0;
    } else {
        r = 0.08 + 0.42 * -log(1.0 - 0.95 * h.x);  // exponential disc
        float k = floor(h.x * 28.0);
        if (knot) r = 0.16 + 0.95 * hash11(k * 1.7 + uSeed * 37.0) + 0.02 * (h2.x + h2.z - 1.0);
        float arm = floor((knot ? hash11(k * 3.1 + 5.0) : h2.y) * arms) / arms * TAU;
        float g = (h2.z + hash11(id * 0.17 + 5.0) + hash11(id * 0.29 + 9.0)) / 3.0 - 0.5;  // ~gaussian
        float spread = g * (0.55 + 1.2 * h.y * h.y * h.y) + (h.y > 0.93 ? (h2.z - 0.5) * 3.0 : 0.0);  // most stars hug the arm
        if (knot) spread = (h2.y - 0.5) * 0.1 / r;
        lane = exp(-pow((spread + 0.4) * 5.0, 2.0)) * smoothstep(0.12, 0.35, r);
        float th = log(r) / 0.32 + arm + spread - uFlow * 0.05 + 0.02 * sin(uFlow * 0.3 + id);
        p = vec3(r * cos(th), (h2.x - 0.5) * 0.06 * (1.2 - r), r * sin(th));
        heat = exp(-r * 3.5);
    }
    vec3 ro;
    mat3 cam = camera(ro);
    vec3 q = transpose(cam) * (p - ro);
    if (q.z < 0.05) { hide(); return; }
    vec2 s = q.xy / q.z * 1.6;
    s.x *= uRes.y / uRes.x;

    float ring = r - 1.4 * (1.0 - uBeat);  // beat shockwave: fast out of the core, easing as it fades
    float accent = exp(-ring * ring * 150.0) * (uBeat + uDownbeat);
    float tw = 1.0 + uHigh * (hash11(id + floor(uTime * 12.0)) - 0.5) * 1.6;
    float lum = hash11(id * 3.17 + 1.0);
    float bright = (0.07 + 1.8 * pow(lum, 14.0)) * tw;  // many faint stars, a few bright ones
    bright *= (bulge ? 0.1 + 0.2 * uBass : 1.0) * (0.75 + 0.4 * uEnergySlow + 0.4 * uDrop) * (1.0 + 0.5 * uBeat);
    vec3 young = mix(uPal[3], uPal[4], 0.3 + 0.4 * h.y), old = mix(uPal[4], vec3(1.0, 0.82, 0.6), 0.5);
    vec3 col = mix(young, old, clamp(heat + (bulge ? 1.0 : 0.0), 0.0, 1.0));
    bright *= (1.0 - 0.8 * lane) * (bulge ? 1.0 : 0.3 + 0.7 * smoothstep(0.1, 0.5, r));  // the crowded centre must not burn out
    if (knot) col = mix(uPal[3], uPal[4], 0.25 * lum), bright = (0.2 + 0.3 * lum) * (0.5 + 1.0 * uMid + 0.4 * uBeat) * tw;
    float size = (0.45 + 1.2 * pow(lum, 12.0) + (knot ? 0.5 : 0.0)) * (uRes.y / 540.0) * 2.0 / q.z;
    sprite(s, s, size, col, 0.5 * (bright + accent * 3.0));
}
