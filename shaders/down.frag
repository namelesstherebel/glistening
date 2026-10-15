// Quarter-res 4x4 box downsample of the mixed scene (bloom + panel backdrop source).
out vec4 o;
void main() {
    vec2 t = 1.0 / uRes;
    vec3 c = scene(vUv + t * vec2(-1, -1)) + scene(vUv + t * vec2(1, -1)) + scene(vUv + t * vec2(-1, 1)) + scene(vUv + t * vec2(1, 1));
    o = vec4(min(c * 0.25, vec3(6.0)), 1.0);
}
