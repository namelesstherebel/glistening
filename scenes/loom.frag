// @name Loom
// @desc luminous threads weaving an evolving fabric; bass pulls the threads taut, mids bend the weave, highs embroider it, each phrase sets a new pattern
// @busy 0.35
// @bloom 0.55
// @palettes dusk,orchid,moss,indigo
out vec4 fragColor;

const float N = 26.0;  // threads per screen height

// 1 where the warp (vertical) thread lies over the weft at crossing (i, j), for weave pattern k.
float warpOver(float i, float j, float k) {
    if (k < 0.5) return mod(i + j, 2.0);                                                        // plain
    if (k < 1.5) return step(mod(i + j, 4.0), 1.5);                                             // 2/2 twill
    if (k < 2.5) return step(mod(i * 2.0 + j, 5.0), 0.5);                                       // satin
    if (k < 3.5) return mod(floor(i / 2.0) + floor(j / 2.0), 2.0);                              // basket
    if (k < 4.5) return step(mod((mod(floor(i / 4.0), 2.0) < 0.5 ? i : -i) + j, 4.0), 1.5);     // herringbone
    return step(mod(abs(mod(i, 8.0) - 4.0) + abs(mod(j, 8.0) - 4.0), 4.0), 1.5);                // diamond twill
}

vec3 threadCol(float n, float phrase, float off) {
    float stripe = mod(n, 4.0 + mod(phrase, 3.0) * 2.0);
    return pal(0.42 + 0.56 * hash11(stripe * 1.7 + phrase * 7.3 + off + floor(uSeed * 40.0)));
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    vec2 p = rot(0.12 * (uVariant.x - 0.5)) * uv * N + vec2(uVariant.y * 40.0, uFlow * 0.15);  // the cloth advances as it is woven
    // Mids bend the whole weave like cloth in a draught.
    float bend = 0.4 + 2.2 * uMid;
    p.x += bend * sin(p.y * 0.17 + uFlow * 0.3);
    p.y += 0.6 * bend * sin(p.x * 0.13 - uFlow * 0.25);
    float i = floor(p.x), j = floor(p.y);
    vec2 f = fract(p) - 0.5;
    // Bass is tension: slack threads wander, taut ones run straight and thin.
    float slack = 0.16 * (1.0 - uBass);
    f.x += slack * sin(p.y * 1.3 + hash11(i) * 6.28 + uFlow * 0.4);
    f.y += slack * sin(p.x * 1.1 + hash11(j + 50.0) * 6.28 - uFlow * 0.35);
    f.x += 0.09 * uBeat * sin(p.y * 2.3 + hash11(i + 9.0) * 6.28 + uTime * 24.0);  // each beat plucks the warp
    float w = 0.3 - 0.06 * uBass, aa = N / uRes.y * 1.2;
    float cw = 1.0 - smoothstep(w - aa, w + aa, abs(f.x)), cv = 1.0 - smoothstep(w - aa, w + aa, abs(f.y));

    // Weave pattern and stripes change each 16-beat phrase, eased over the last beat.
    float phrase = floor(uBeats / 16.0) + floor(uSeed * 6.0);
    float into = smoothstep(15.0, 16.0, mod(uBeats, 16.0) + uBeatPhase);
    float over = mix(warpOver(i, j, mod(phrase, 6.0)), warpOver(i, j, mod(phrase + 1.0, 6.0)), into);
    vec3 warpC = mix(threadCol(i, phrase, 0.0), threadCol(i, phrase + 1.0, 0.0), into);
    vec3 weftC = mix(threadCol(j, phrase, 31.0), threadCol(j, phrase + 1.0, 31.0), into);

    // Round threads; the one on top arches over the crossing, the one beneath dips into shadow.
    float tubeX = sqrt(max(0.0, 1.0 - f.x * f.x / (w * w))), tubeY = sqrt(max(0.0, 1.0 - f.y * f.y / (w * w)));
    float glow = 0.3 + 0.35 * uBass + 0.2 * uEnergySlow + 0.25 * uBeat;
    vec3 warp = warpC * tubeX * (0.55 + 0.45 * cos(f.y * PI)) * glow;
    vec3 weft = weftC * tubeY * (0.55 + 0.45 * cos(f.x * PI)) * glow;
    vec3 under = mix(warp * cw * (1.0 - 0.6 * cv), weft * cv * (1.0 - 0.6 * cw), over);
    vec3 top = mix(weft, warp, over);
    float topCover = mix(cv, cw, over);
    vec3 col = uPal[0] * 0.3 + under * 0.4;
    col = mix(col, top, topCover);

    // The shuttle: every beat it carries a bright weft across one row, alternating direction.
    float row = floor((hash11(uBeats * 0.37 + uSeed) - 0.5) * 18.0 + uFlow * 0.15);
    float dirn = mod(uBeats, 2.0) * 2.0 - 1.0;
    float sx = dirn * (uBeatPhase * 2.4 - 1.2) * uRes.x / uRes.y * N * 0.5;
    float sxp = (rot(0.12 * (uVariant.x - 0.5)) * uv * N).x;
    float behind = dirn * (sx - sxp);
    float shuttle = step(abs(j - row), 0.5) * cv * (exp(-behind * behind * 0.08) * 0.9 + step(0.0, behind) * 0.25 * exp(-behind * 0.05)) * uBeat;
    col += mix(weftC, uPal[4], 0.6) * shuttle * 2.5;

    // Embroidery: cross-stitched diamonds over the cloth, sewn in by the highs.
    vec2 m = mod(vec2(i, j) + floor(uSeed * 8.0), 10.0) - 4.5;
    float ring = abs(abs(m.x) + abs(m.y) - 3.0);
    float cross = min(abs(f.x - f.y), abs(f.x + f.y));
    float stitch = step(ring, 0.6) * (1.0 - smoothstep(0.05, 0.05 + aa, cross)) * step(max(abs(f.x), abs(f.y)), 0.34);
    float sew = smoothstep(0.1, 0.6, uHigh) * (0.5 + 0.5 * sin(uFlow * 0.5 + (i + j) * 0.4));
    col = mix(col, mix(uPal[4], vec3(1.0), 0.15) * (0.7 + 0.6 * uHigh), stitch * sew);
    col += uPal[4] * step(0.985, hash12(vec2(i, j) + floor(uTime * 6.0))) * uHigh * tubeX * 0.6;  // glints along the threads

    col *= 1.0 - 0.25 * smoothstep(0.5, 1.1, length(uv));
    col *= uIntensity * 0.6 + 0.4;
    fragColor = vec4(col, 1.0);
}
