#version 460
#extension GL_EXT_ray_tracing : require

layout(set=0,binding=0) uniform accelerationStructureEXT TLAS;
layout(set=0,binding=1,rgba8) uniform image2D RT;
layout(set=0,binding=2) uniform CamUBO {
    mat4 InvV;
    mat4 InvP;
    uint Frame;
    uint WpnTexBase;
    float pad[2];
} Cam;

layout(location=0) rayPayloadEXT vec4 Pay;

void main() {
    vec2 px  = vec2(gl_LaunchIDEXT.xy) + 0.5;
    vec2 ndc = px / vec2(gl_LaunchSizeEXT.xy) * 2.0 - 1.0;

    vec4 O = Cam.InvV * vec4(0,0,0,1);
    vec4 T = Cam.InvP * vec4(ndc, 1, 1);
    vec4 D = Cam.InvV * vec4(normalize(T.xyz / T.w), 0);

    Pay = vec4(0);
    traceRayEXT(TLAS, gl_RayFlagsOpaqueEXT, 0xFF,
                0, 1, 0,
                O.xyz, 1e-3, D.xyz, 1e4, 0);

    imageStore(RT, ivec2(gl_LaunchIDEXT.xy), vec4(Pay.rgb, 1.0));
}
