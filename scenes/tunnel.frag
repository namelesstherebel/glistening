// @name Tunnel
// @desc a hexagonal corridor whose rings replay the last seconds of spectrum
// @busy 0.85
// @bloom 0.8
// @palettes ember,abyss,solar,glacier
out vec4 fragColor;

const float RINGS = 0.8;  // ring spacing, world units
float roll;

vec2 path(float z) { return vec2(sin(z * 0.11 + uSeed * 6.0), cos(z * 0.083 + uSeed * 3.0)) * 1.4; }

// Hexagon "radius" of v (inradius 1 at the walls).
float hexR(vec2 v) {
    v = rot(roll) * v;
    vec2 a = abs(v);
    return max(a.y, dot(a, vec2(0.8660254, 0.5)));
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    uv *= 1.0 - 0.06 * uBass - 0.05 * uDrop;  // bass kicks the field of view
    float camZ = uFlow * 2.2;
    roll = uVariant.x * 6.28 + (uVariant.y > 0.5 ? 1.0 : -1.0) * (uFlow * 0.03 + uDrop * 0.6);
    vec2 c0 = path(camZ), d0 = path(camZ + 0.01) - c0;

    // March outward in depth until the ray leaves the (bending) hexagon, then
    // bisect. The bend fades in with depth so near walls stay put.
    #define OFS(zz) ((path(camZ + (zz)) - c0 - d0 * ((zz) / 0.01)) * 0.5 * smoothstep(1.0, 6.0, (zz)))
    float zl = 0.3, z = 0.3;
    for (int i = 0; i < 52; i++) {
        z = 0.3 * pow(1.11, float(i));
        if (hexR(uv * z - OFS(z)) > 1.0) break;
        zl = z;
    }
    for (int i = 0; i < 7; i++) {
        float m = 0.5 * (zl + z);
        if (hexR(uv * m - OFS(m)) > 1.0) z = m; else zl = m;
    }
    z = min(z, 60.0);
    vec2 o = OFS(z);
    vec2 w = rot(roll) * (uv * z - o);  // wall point in tunnel cross-section
    float Z = camZ + z;

    // Angle from the floor: bass on the floor, highs on the ceiling, mirrored.
    float ang = atan(w.x, -w.y);
    float side = floor((ang + PI) / (PI / 3.0)), sAlong = fract((ang + PI) / (PI / 3.0));
    float q = abs(ang) / PI * 12.0, seg = floor(q);
    float x = (seg + 0.5) / 12.0;
    float age = clamp(z / 26.0, 0.0, 1.0);
    float v = hist(x, age);

    float fw = fwidth(Z);
    float dz = abs(fract(Z / RINGS + 0.5) - 0.5) * RINGS;
    float lw = 0.022 + 0.012 * uBeat;
    float ring = (1.0 - smoothstep(max(lw - fw, 0.0), lw + fw, dz));
    ring = mix(ring, 2.0 * lw / RINGS, smoothstep(0.08, 0.35, fw));
    float gap = smoothstep(0.03, 0.08, min(fract(q), 1.0 - fract(q))); // seams between segments
    float rail = (1.0 - smoothstep(0.0, 0.03, min(sAlong, 1.0 - sAlong))) * 0.25;

    float fog = exp(-z * 0.075);
    float pulse = exp(-pow(z - 22.0 * (1.0 - uBeat), 2.0) * 0.4) * uBeat;  // beat light runs down the tunnel
    float lit = (0.15 + 1.3 * v * v) * (1.0 + 0.5 * uDownbeat) + 1.2 * pulse;
    vec3 ringCol = mix(pal(0.5 + 0.5 * v), uPal[4], 0.3 * uDrop + 0.4 * pulse);
    vec3 col = ringCol * ring * gap * lit * 1.5;
    // Light spill onto the panels around each ring.
    float soft = 0.15 + 1.3 * pow(hist(0.3, age), 2.0) + pulse;  // spill: one level per ring, no segment edges
    col += pal(0.55) * exp(-dz * 6.0) * soft * 0.05;
    col += uPal[2] * rail * (0.3 + 0.7 * uHigh) * (0.5 + v);
    col += uPal[1] * 0.06;
    col *= fog;
    col += uPal[3] * 0.25 * exp(-dot(uv, uv) * 30.0) * (0.4 + 0.6 * uEnergySlow);  // light at the end
    col *= uIntensity * 0.7 + 0.3;
    fragColor = vec4(col, 1.0);
}
