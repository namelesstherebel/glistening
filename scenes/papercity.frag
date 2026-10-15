// @name Paper City
// @desc folded paper buildings rise from a dark plane, each district a band; beats run light down the streets, breakdowns fold the skyline flat
// @busy 0.65
// @bloom 0.6
// @palettes paper,dusk,indigo,silver
out vec4 fragColor;

const float STREET = 0.2;  // half street width (cells are 1 x 1)
const float RADIUS = 8.5;  // city extent in cells
const float HMAX = 3.2;    // tallest building + roof

// Building in cell id: .x wall height, .y roof rise (0: flat), .z 1 turns the ridge to run along z, .w band it follows.
vec4 block(vec2 id) {
    vec3 h = hash33(vec3(id, floor(uSeed * 64.0)));
    float r = length(id + 0.5) / RADIUS;
    if (r > 1.0 || h.x < 0.12) return vec4(0.0);  // outskirts and squares stay empty
    float band = 0.04 + 0.8 * r;                   // bass downtown, highs at the edge
    float level = 0.5 * (hist(band, 0.0) + hist(band, 0.05));
    // Breakdowns fold the city flat, block by block; the music unfolds it again.
    float rise = clamp((smoothstep(0.12, 0.45, uEnergySlow) + uDrop) * 1.5 - h.y * 0.5, 0.03, 1.0);
    float wall = (0.25 + 1.5 * h.y * h.y * (1.0 - 0.6 * r)) * (0.45 + 0.9 * level) * rise;
    float roof = step(0.35, h.z) * (0.18 + 0.2 * h.x) * rise;
    return vec4(wall, roof, step(0.65, h.z), band);
}

// Ray against the convex block (four walls, two roof planes, the ground): returns t (or -1) and the face normal.
float hitBlock(vec3 ro, vec3 rd, vec2 id, vec4 b, out vec3 nor, out vec3 local) {
    vec2 c = id + 0.5;
    float hw = 0.5 - STREET;
    vec3 o = ro - vec3(c.x, 0.0, c.y), d = rd;
    if (b.z > 0.5) o = o.zyx, d = d.zyx;  // the planes below put the ridge along x; swapping turns it
    float s = b.y / hw, top = b.x + b.y;
    vec4 pl[7] = vec4[](vec4(1, 0, 0, hw), vec4(-1, 0, 0, hw), vec4(0, 0, 1, hw), vec4(0, 0, -1, hw),
                        vec4(0, 1, s, top), vec4(0, 1, -s, top), vec4(0, -1, 0, 0));  // ground: no wall hits below y = 0
    float tin = -1e9, tout = 1e9;
    vec3 n = vec3(0.0);
    for (int k = 0; k < 7; k++) {
        float den = dot(pl[k].xyz, d), dist = pl[k].w - dot(pl[k].xyz, o);
        if (abs(den) < 1e-6) {
            if (dist < 0.0) return -1.0;
            continue;
        }
        float t = dist / den;
        if (den < 0.0) {
            if (t > tin) tin = t, n = pl[k].xyz;
        } else tout = min(tout, t);
    }
    if (tin > tout || tout < 0.0) return -1.0;
    local = b.z > 0.5 ? (o + d * tin).zyx : o + d * tin;  // hit point relative to the block centre, world axes
    nor = normalize(b.z > 0.5 ? n.zyx : n);
    return tin;
}

void main() {
    vec2 uv = (gl_FragCoord.xy - 0.5 * uRes) / uRes.y;
    float az = uVariant.x * 6.28 + uFlow * 0.006, el = 0.62 + 0.12 * uVariant.y;
    vec3 ro = 12.5 * vec3(cos(el) * sin(az), sin(el), cos(el) * cos(az));
    vec3 fw = normalize(-ro), rt = normalize(cross(fw, vec3(0, 1, 0))), up = cross(rt, fw);
    vec3 rd = normalize(fw * 1.9 + uv.x * rt + uv.y * up);
    vec3 L = normalize(vec3(0.6 * cos(uVariant.z * 6.28), 0.75, 0.6 * sin(uVariant.z * 6.28)));

    vec3 col = mix(uPal[0], uPal[1] * 0.6, smoothstep(-0.2, 0.3, rd.y));  // night above the plane
    float tGround = rd.y < 0.0 ? -ro.y / rd.y : 1e9;

    // Walk the grid cells under the ray (2D DDA) until a block is hit or the ray meets the ground.
    float t = rd.y < 0.0 ? max(0.0, (ro.y - HMAX) / -rd.y) : 1e9;
    vec3 p = ro + rd * t;
    vec2 id = floor(p.xz), stepv = sign(rd.xz), inv = 1.0 / max(abs(rd.xz), vec2(1e-5));
    vec2 next = (stepv * (id - p.xz) + stepv * 0.5 + 0.5) * inv + t;  // t at the next x / z cell boundary
    float hitT = -1.0;
    vec3 nor = vec3(0.0), local = vec3(0.0);
    vec4 hb = vec4(0.0);
    int steps = int(24.0 + 16.0 * uQuality);
    for (int i = 0; i < 40; i++) {
        if (i >= steps || t > tGround) break;
        vec4 b = block(id);
        if (b.x > 0.0) {
            float th = hitBlock(ro, rd, id, b, nor, local);
            if (th > 0.0) { hitT = th, hb = b; break; }
        }
        if (next.x < next.y) t = next.x, next.x += inv.x, id.x += stepv.x;
        else t = next.y, next.y += inv.y, id.y += stepv.y;
    }

    if (hitT > 0.0) {
        vec3 hp = ro + rd * hitT;
        // Paper: a soft key light, every wall folded once down the middle, creases catching light.
        float fold = 0.0;
        if (abs(nor.y) < 0.5) fold = sign(dot(local.xz, vec2(-nor.z, nor.x))) * 0.12;
        vec3 n2 = normalize(nor + fold * vec3(-nor.z, 0.0, nor.x));
        float dif = 0.5 + 0.5 * dot(n2, L);
        vec3 paper = mix(uPal[3], uPal[4], 0.55 + 0.35 * hash12(floor(hp.xz) + 7.0));
        vec3 c = paper * (0.06 + 0.5 * dif * dif) + uPal[2] * 0.05;
        c *= 0.94 + 0.06 * gnoise(hp.xz * 30.0 + hp.y * 30.0);  // fibre
        float hw = 0.5 - STREET;
        vec3 e = abs(local) - vec3(hw, 0.0, hw);
        float edge;
        if (abs(nor.y) < 0.5) {
            // A wall lies on one x/z plane: measure along its tangent, never its normal.
            float tangent = abs(nor.x) > 0.5 ? abs(e.z) : abs(e.x);
            float roofAxis = hb.z > 0.5 ? abs(local.x) : abs(local.z);
            float eave = hb.x + hb.y * (1.0 - roofAxis / hw);
            edge = min(tangent, min(abs(local.y), abs(local.y - eave)));
        } else edge = min(abs(e.x), abs(e.z));
        if (abs(nor.y) >= 0.5 && hb.y > 0.0) edge = min(edge, hb.z > 0.5 ? abs(local.x) : abs(local.z));  // roof ridge
        float px = hitT * 1.5 / (1.9 * uRes.y);
        c += uPal[4] * (1.0 - smoothstep(px, 3.0 * px, edge)) * (0.12 + 0.2 * dif) * (0.7 + 0.8 * uBeat);  // creases flare on the beat
        // Windows wake with the mids.
        if (abs(nor.y) < 0.5 && local.y < hb.x - 0.08 && local.y > 0.08) {
            vec2 wq = vec2(dot(local.xz, vec2(-nor.z, nor.x)) * 5.0, local.y * 6.0);
            vec2 wf = abs(fract(wq) - 0.5);
            float win = step(wf.x, 0.22) * step(wf.y, 0.2) * step(hash12(floor(wq) + floor(hp.xz) * 13.0), 0.15 + 0.55 * uMid);
            c += mix(uPal[3], uPal[4], 0.3) * win * (0.15 + 0.6 * uMid);
        }
        col = c;
    } else if (rd.y < 0.0) {
        vec3 gp = ro + rd * tGround;
        vec2 g = gp.xz;
        float inCity = 1.0 - smoothstep(RADIUS - 1.0, RADIUS + 0.5, length(g));
        vec2 dd = abs(g - floor(g + 0.5));
        float street = min(dd.x, dd.y);
        float px = tGround * 1.5 / (1.9 * uRes.y);
        float lane = (1.0 - smoothstep(0.02, 0.02 + px * 2.0, street)) * inCity;
        // Each beat sends a front of light out through the streets from downtown.
        float D = abs(g.x) + abs(g.y);
        float front = exp(-pow(D - uBeatPhase * 14.0, 2.0) * 0.5) * (0.05 + 2.2 * uBeat);
        // Traffic: small lights drifting along the lanes.
        float along = dd.x < dd.y ? g.y : g.x, cell = dd.x < dd.y ? floor(g.x + 0.5) : floor(g.y + 0.5);
        float car = smoothstep(0.85, 1.0, sin(along * 2.0 + uFlow * (0.6 + hash11(cell) * 0.6) * (mod(cell, 2.0) * 2.0 - 1.0)));
        vec3 base = uPal[0] * 1.2 + uPal[1] * 0.25 * inCity;
        // Contact shadow round the block bases.
        vec2 cid = floor(g), cl = g - cid - 0.5;
        vec4 b = block(cid);
        vec2 q = abs(cl) - (0.5 - STREET);
        float ao = b.x > 0.0 ? smoothstep(0.0, 0.18, length(max(q, 0.0))) : 1.0;
        col = base * (0.4 + 0.6 * ao) + mix(uPal[3], uPal[4], 0.4) * lane * (0.05 + front * 0.6 + car * 0.25);
        col = mix(col, uPal[0], smoothstep(14.0, 24.0, tGround));
    }

    col *= uIntensity * 0.6 + 0.4;
    fragColor = vec4(col, 1.0);
}
