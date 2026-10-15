// @name Galaxy
// @desc a spiral of 60k stars; every beat rings out from the core
// @busy 0.4
// @bloom 1.0
// @palettes glacier,dusk,orchid,indigo
out vec4 fragColor;

mat3 camera(out vec3 ro) {
    float az = uVariant.x * 6.28 + uFlow * 0.02, inc = 1.0 + 0.25 * uVariant.y;
    float dist = 2.05 - 0.2 * uBass - 0.35 * uDrop - 0.2 * uEnergySlow;
    ro = dist * vec3(sin(inc) * sin(az), cos(inc), sin(inc) * cos(az));
    vec3 fw = normalize(-ro), rt = normalize(cross(vec3(0, 1, 0), fw));
    return mat3(rt, cross(fw, rt), fw);
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    vec3 ro;
    mat3 cam = camera(ro);
    vec3 rd = normalize(cam * vec3(uv, 1.6));
    vec3 col = uPal[0] * 0.5 + uPal[1] * 0.12 * (0.5 + 0.5 * rd.y);

    // Glow of the disc plane: core haze and arm nebulae, dust lanes darken.
    float t = -ro.y / rd.y;
    if (t > 0.0) {
        vec2 p = (ro + rd * t).xz;
        float r = length(p), arms = 2.0 + floor(uVariant.z * 2.0);
        float th = atan(p.y, p.x) - log(max(r, 0.02)) / 0.32 + uFlow * 0.05 + 0.5 * fbm(p * 3.0 + uSeed * 9.0, 3);  // ragged, filamentary arms
        float arm = pow(0.5 + 0.5 * cos(th * arms), 3.0);
        float neb = arm * exp(-r * 1.7) * (0.35 + 0.65 * smoothstep(0.05, 0.4, r)) * (1.0 - smoothstep(0.45, 1.4, r)) * (0.55 + 0.45 * fbm(p * 5.0 + uFlow * 0.02, 4));
        float dust = smoothstep(0.2, 0.9, pow(0.5 + 0.5 * cos(th * arms + 0.5), 8.0)) * smoothstep(0.1, 0.4, r);
        col += mix(uPal[3], uPal[2], smoothstep(0.1, 0.9, r)) * neb * (0.3 + 0.5 * uMid) * (1.0 - 0.8 * dust);
        col *= 1.0 - 0.35 * dust * exp(-r * 1.5);
        col += mix(uPal[4], vec3(1.0, 0.8, 0.6), 0.5) * exp(-r * 16.0) * (0.2 + 0.3 * uBass + 0.2 * uBeat + 0.3 * uDrop);
    }
    // The bulge is a volume: it glows along the whole ray, also seen edge-on.
    float miss = length(cross(ro, rd));
    col += mix(uPal[4], vec3(1.0, 0.8, 0.6), 0.6) * (0.04 * exp(-miss * miss * 30.0) + 0.015 * exp(-miss * 4.0)) * (0.6 + 0.4 * uBass + 0.3 * uEnergySlow);
    // Faint background stars.
    vec3 g = rd * 220.0, id = floor(g), o = hash33(id + 3.0);
    vec3 f = fract(g) - o;
    col += uPal[4] * step(0.96, hash12(id.xy + id.z * 7.0)) * 0.3 * exp(-30.0 * dot(f, f));

    // Short persistence: motion leaves soft trails.
    vec3 prev = texture(uPrev, gl_FragCoord.xy / uRes).rgb;
    fragColor = vec4(col + prev * pow(0.3, uDt * 30.0), 1.0);
}
