#version 460
#extension GL_EXT_ray_tracing : require

layout(location=0) rayPayloadInEXT vec4 Pay;

void main() {
    vec3 D = normalize(gl_WorldRayDirectionEXT);
    float t = D.y * 0.5 + 0.5;
    Pay.rgb = mix(vec3(0.55, 0.62, 0.72), vec3(0.18, 0.38, 0.78), t);
    Pay.a   = 0.0;
}
