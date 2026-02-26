#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

layout(location=0) rayPayloadInEXT vec4 Pay;
layout(location=1) rayPayloadEXT   float Shadow;
hitAttributeEXT vec2 Bary;

layout(set=0,binding=0) uniform accelerationStructureEXT TLAS;

layout(set=0,binding=2) uniform CamUBO {
    mat4 InvV;
    mat4 InvP;
    uint Frame;
    uint WpnTexBase;
    float pad[2];
} Cam;

struct Vtx { vec3 pos; float _p0; vec2 uv; vec2 lmuv; vec3 n; float _p2; };
layout(set=0,binding=3,std430) readonly buffer VtxBuf  { Vtx  V[]; };
layout(set=0,binding=4,std430) readonly buffer IdxBuf  { uint I[]; };
layout(set=0,binding=5,std430) readonly buffer MatBuf  { vec4 M[]; };
layout(set=0,binding=6,std430) readonly buffer TexIdBuf{ uint TI[];};
layout(set=0,binding=7) uniform sampler2D Lightmap;

/* weapon SSBOs */
layout(set=0,binding=8,std430)  readonly buffer WpnVtxBuf  { Vtx  WV[]; };
layout(set=0,binding=9,std430)  readonly buffer WpnIdxBuf  { uint WI[]; };
layout(set=0,binding=10,std430) readonly buffer WpnTidBuf  { uint WTI[];};

layout(set=0,binding=11) uniform sampler2D Textures[];

vec3 bary3(vec2 b) { return vec3(1.0-b.x-b.y, b.x, b.y); }

void main() {
    vec3 bc = bary3(Bary);
    bool wpn = (gl_InstanceCustomIndexEXT == 1u);

    vec3 P, N;
    vec2 UV, lmUV;

    if (wpn) {
        uint b0  = gl_PrimitiveID * 3u;
        uvec3 tri = uvec3(WI[b0], WI[b0+1u], WI[b0+2u]);
        P    = bc.x*WV[tri.x].pos  + bc.y*WV[tri.y].pos  + bc.z*WV[tri.z].pos;
        N    = normalize(bc.x*WV[tri.x].n + bc.y*WV[tri.y].n + bc.z*WV[tri.z].n);
        UV   = bc.x*WV[tri.x].uv  + bc.y*WV[tri.y].uv  + bc.z*WV[tri.z].uv;
        lmUV = vec2(0);
    } else {
        uint b0  = gl_PrimitiveID * 3u;
        uvec3 tri = uvec3(I[b0], I[b0+1u], I[b0+2u]);
        P    = bc.x*V[tri.x].pos  + bc.y*V[tri.y].pos  + bc.z*V[tri.z].pos;
        N    = normalize(bc.x*V[tri.x].n + bc.y*V[tri.y].n + bc.z*V[tri.z].n);
        UV   = bc.x*V[tri.x].uv  + bc.y*V[tri.y].uv  + bc.z*V[tri.z].uv;
        lmUV = bc.x*V[tri.x].lmuv + bc.y*V[tri.y].lmuv + bc.z*V[tri.z].lmuv;
    }

    /* albedo from bindless texture */
    uint texIdx;
    if (wpn) {
        texIdx = Cam.WpnTexBase + WTI[gl_PrimitiveID];
    } else {
        texIdx = TI[gl_PrimitiveID];
    }
    vec3 Alb = texture(Textures[nonuniformEXT(texIdx)], UV).rgb;

    vec3 col;
    if (wpn) {
        /* weapon: simple ambient + directional, no lightmap */
        vec3 Sun = normalize(vec3(0.6, 0.9, 0.3));
        float sun = max(0.0, dot(N, Sun)) * 0.4;
        col = Alb * (vec3(0.6) + vec3(sun));
    } else {
        /* BSP lightmap (baked lighting) — 2.5x overbright to match Q3 levels */
        vec3 LM = texture(Lightmap, lmUV).rgb * 2.5;

        /* RT shadow ray toward sun for dynamic shadows */
        vec3 Sun = normalize(vec3(0.6, 0.9, 0.3));
        Shadow = 0.0;
        vec3 orig = P + N * 0.1;
        /* cull mask 0xFE excludes weapon (mask 0x01) from shadow rays */
        traceRayEXT(TLAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
                    0xFE, 0, 1, 1, orig, 1e-3, Sun, 1e4, 1);
        float sun = max(0.0, dot(N, Sun)) * Shadow * 0.15;

        /* combine: lightmap * albedo + subtle sun contribution */
        col = Alb * (LM + vec3(sun));

        /* subtle distance fog */
        float Fog = 1.0 - exp(-gl_HitTEXT * 0.00012);
        col = mix(col, vec3(0.45, 0.52, 0.65), Fog);
    }

    Pay.rgb = col;
    Pay.a   = 1.0;
}
