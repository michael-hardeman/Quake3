/* <<quake3>> = <<types>> <<math>> <<vkrt-fns>> <<vkutil>> <<tga-loader>>
               <<vkinit>> <<bsp-loader>> <<bsp-entities>> <<collision>>
               <<physics>> <<scene>> <<blas>> <<tlas>> <<pipeline>>
               <<descriptors>> <<camera>> <<input>> <<render>>
               <<validate>> <<main>>                                    */

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>

/* <<types>> ============================================================= */

typedef uint8_t  U8;
typedef uint16_t U16;
typedef int16_t  I16;
typedef int32_t  I32;
typedef uint32_t U32;
typedef uint64_t U64;
typedef float    F32;

typedef struct { F32 x,y,z;   } V3;
typedef struct { F32 x,y,z,w; } V4;
typedef struct { F32 m[16];   } M4;

typedef struct { int fwd,back,left,right,jump,fire,crouch; float dx,dy; } Input;

typedef struct { VkBuffer b; VkDeviceMemory m; VkDeviceAddress a; U64 sz; } Buf;
typedef struct { VkImage  i; VkDeviceMemory m; VkImageView     v; VkFormat f; } Img;
typedef struct { VkAccelerationStructureKHR h; Buf b; VkDeviceAddress a; } AS;

typedef struct {
    V3   pos, vel;
    F32  yaw, pitch;
    M4   inv_v, inv_p;
    U32  frame;
} Cam;

typedef struct { F32 pos[3], _p; F32 uv[2], _q[2]; F32 n[3], _r; } Vtx;

typedef struct {
    Vtx  *verts;  U32 nv;
    U32  *idxs;   U32 ni;
    V4   *mats;   U32 nm;
    U32  *tex_ids;           /* tex_ids[tri_idx] = shader index */
    char (*tex_names)[64];   /* shader names from BSP shaders lump */
    U32   tri_count;
    U8   *lm_atlas;          /* lightmap atlas RGBA pixels */
    U32   lm_w, lm_h;       /* atlas dimensions */
} Scene;

typedef struct { V3 origin; F32 angle; } Spawn;

typedef struct {
    PFN_vkCreateAccelerationStructureKHR           CreateAS;
    PFN_vkDestroyAccelerationStructureKHR          DestroyAS;
    PFN_vkGetAccelerationStructureBuildSizesKHR    ASBuildSizes;
    PFN_vkCmdBuildAccelerationStructuresKHR        CmdBuildAS;
    PFN_vkGetAccelerationStructureDeviceAddressKHR ASAddr;
    PFN_vkCreateRayTracingPipelinesKHR             CreateRTPipe;
    PFN_vkGetRayTracingShaderGroupHandlesKHR       RTHandles;
    PFN_vkCmdTraceRaysKHR                          CmdTrace;
} RTFn;

typedef struct {
    SDL_Window  *win;
    int          W, H;
    VkInstance   inst;
    VkSurfaceKHR surf;
    VkPhysicalDevice pd;
    VkDevice     dev;
    VkQueue      q;
    U32          qi;
    VkSwapchainKHR sc;
    VkImage      sc_img[8];
    VkImageView  sc_view[8];
    U32          sc_n;
    VkFormat     sc_fmt;
    VkExtent2D   sc_ext;
    VkCommandPool   pool;
    VkCommandBuffer cmd;
    VkFence      fence;
    VkSemaphore  sem_img, sem_done;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;
    RTFn         rt;
    Img          rt_img;
    Buf          cam_ubo;
    Buf          vbuf, ibuf, mbuf;
    Buf          tex_id_buf;          /* per-tri shader index SSBO (binding 6) */
    VkImage     *tex_imgs;
    VkDeviceMemory *tex_mems;
    VkImageView *tex_views;
    VkSampler    tex_sampler;
    U32          n_tex;
    U32          n_tex_loaded;        /* how many were real TGA files */
    VkImage      lm_img;
    VkDeviceMemory lm_mem;
    VkImageView  lm_view;
    VkSampler    lm_sampler;
    AS           blas, tlas;
    VkPipelineLayout pipe_layout;
    VkPipeline       pipe;
    Buf              sbt;
    VkStridedDeviceAddressRegionKHR sbt_rgen, sbt_miss, sbt_hit, sbt_call;
    VkDescriptorSetLayout dsl;
    VkDescriptorPool      dp;
    VkDescriptorSet       ds;
    int quit;
    F32 dt;
} Ctx;

/* <<math>> ============================================================== */

static V3  v3(F32 x,F32 y,F32 z)   { return (V3){x,y,z}; }
static V3  v3add(V3 a,V3 b)         { return v3(a.x+b.x,a.y+b.y,a.z+b.z); }
static V3  v3sub(V3 a,V3 b)         { return v3(a.x-b.x,a.y-b.y,a.z-b.z); }
static V3  v3scale(V3 v,F32 s)      { return v3(v.x*s,v.y*s,v.z*s); }
static F32 v3dot(V3 a,V3 b)         { return a.x*b.x+a.y*b.y+a.z*b.z; }
static V3  v3cross(V3 a,V3 b)       { return v3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x); }
static V3  v3norm(V3 a)             { F32 l=sqrtf(v3dot(a,a)); return l>1e-6f?v3scale(a,1.f/l):a; }

static M4 m4id(void) {
    M4 m = {0}; m.m[0]=m.m[5]=m.m[10]=m.m[15]=1; return m;
}
static M4 m4persp(F32 fovy_deg, F32 asp, F32 zn, F32 zf) {
    F32 f = 1.f / tanf(fovy_deg * (float)M_PI / 360.f);
    M4 m = {0};
    m.m[0]=f/asp; m.m[5]=-f;
    m.m[10]=zf/(zn-zf); m.m[11]=-1;
    m.m[14]=zn*zf/(zn-zf);
    return m;
}
static M4 m4view(V3 pos, F32 yaw, F32 pitch) {
    F32 cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch);
    V3 fwd = v3(sy*cp, -sp, -cy*cp);
    V3 right = v3norm(v3cross(fwd, v3(0,1,0)));
    V3 up    = v3cross(right, fwd);
    M4 m = {0};
    m.m[0]=right.x; m.m[4]=right.y; m.m[8] =right.z;
    m.m[1]=up.x;    m.m[5]=up.y;    m.m[9] =up.z;
    m.m[2]=-fwd.x;  m.m[6]=-fwd.y;  m.m[10]=-fwd.z;
    m.m[12]=-(m.m[0]*pos.x+m.m[4]*pos.y+m.m[8] *pos.z);
    m.m[13]=-(m.m[1]*pos.x+m.m[5]*pos.y+m.m[9] *pos.z);
    m.m[14]=-(m.m[2]*pos.x+m.m[6]*pos.y+m.m[10]*pos.z);
    m.m[15]=1;
    return m;
}
static M4 m4inv_ortho(M4 m) {
    M4 r = {0};
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) r.m[i*4+j]=m.m[j*4+i];
    r.m[12]=-(r.m[0]*m.m[12]+r.m[4]*m.m[13]+r.m[8] *m.m[14]);
    r.m[13]=-(r.m[1]*m.m[12]+r.m[5]*m.m[13]+r.m[9] *m.m[14]);
    r.m[14]=-(r.m[2]*m.m[12]+r.m[6]*m.m[13]+r.m[10]*m.m[14]);
    r.m[15]=1; return r;
}
static M4 m4inv_proj(M4 p) {
    M4 r = {0};
    r.m[0]  = 1.f/p.m[0];
    r.m[5]  = 1.f/p.m[5];
    r.m[11] = 1.f/p.m[14];
    r.m[14] = 1.f/p.m[11];
    r.m[15] = -p.m[10]/(p.m[11]*p.m[14]);
    return r;
}

/* <<vkrt-fns>> ========================================================== */

#define GDFN(d,T,n) (PFN_##T)vkGetDeviceProcAddr(d,#n)

static RTFn rtfn_load(VkDevice dev) {
    return (RTFn){
        .CreateAS    = GDFN(dev,vkCreateAccelerationStructureKHR,          vkCreateAccelerationStructureKHR),
        .DestroyAS   = GDFN(dev,vkDestroyAccelerationStructureKHR,         vkDestroyAccelerationStructureKHR),
        .ASBuildSizes= GDFN(dev,vkGetAccelerationStructureBuildSizesKHR,   vkGetAccelerationStructureBuildSizesKHR),
        .CmdBuildAS  = GDFN(dev,vkCmdBuildAccelerationStructuresKHR,       vkCmdBuildAccelerationStructuresKHR),
        .ASAddr      = GDFN(dev,vkGetAccelerationStructureDeviceAddressKHR,vkGetAccelerationStructureDeviceAddressKHR),
        .CreateRTPipe= GDFN(dev,vkCreateRayTracingPipelinesKHR,            vkCreateRayTracingPipelinesKHR),
        .RTHandles   = GDFN(dev,vkGetRayTracingShaderGroupHandlesKHR,      vkGetRayTracingShaderGroupHandlesKHR),
        .CmdTrace    = GDFN(dev,vkCmdTraceRaysKHR,                         vkCmdTraceRaysKHR),
    };
}

/* <<vkutil>> ============================================================ */

#define VK(f) do { VkResult _r=(f); if(_r) { fprintf(stderr,"VK %d @%d\n",_r,__LINE__); exit(1); } } while(0)

static U32 vk_memtype(VkPhysicalDevice pd, U32 bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (U32 i=0;i<mp.memoryTypeCount;i++)
        if ((bits>>i&1) && (mp.memoryTypes[i].propertyFlags&want)==want) return i;
    assert(0 && "no memtype"); return 0;
}

static Buf buf_alloc(VkDevice dev, VkPhysicalDevice pd, U64 sz,
                     VkBufferUsageFlags usage, VkMemoryPropertyFlags mf) {
    Buf b = {.sz=sz};
    VK(vkCreateBuffer(dev, &(VkBufferCreateInfo){
        .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=sz, .usage=usage
    }, NULL, &b.b));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, b.b, &mr);
    VkMemoryAllocateFlagsInfo mafi = {
        .sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
    };
    VK(vkAllocateMemory(dev, &(VkMemoryAllocateInfo){
        .sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext=(usage&VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)?&mafi:NULL,
        .allocationSize=mr.size,
        .memoryTypeIndex=vk_memtype(pd, mr.memoryTypeBits, mf)
    }, NULL, &b.m));
    VK(vkBindBufferMemory(dev, b.b, b.m, 0));
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        b.a = vkGetBufferDeviceAddress(dev, &(VkBufferDeviceAddressInfo){
            .sType=VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer=b.b
        });
    return b;
}

static void buf_upload(VkDevice dev, Buf b, const void *data, U64 sz) {
    void *p; VK(vkMapMemory(dev,b.m,0,sz,0,&p));
    memcpy(p,data,sz); vkUnmapMemory(dev,b.m);
}

static Buf buf_stage_upload(VkDevice dev, VkPhysicalDevice pd,
                             VkCommandBuffer cmd, VkQueue q,
                             const void *data, U64 sz, VkBufferUsageFlags usage) {
    Buf stage = buf_alloc(dev, pd, sz,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    buf_upload(dev, stage, data, sz);

    Buf dst = buf_alloc(dev, pd, sz,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK(vkResetCommandBuffer(cmd, 0));
    VK(vkBeginCommandBuffer(cmd, &(VkCommandBufferBeginInfo){
        .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT}));
    vkCmdCopyBuffer(cmd, stage.b, dst.b, 1,
        &(VkBufferCopy){.size=sz});
    VK(vkEndCommandBuffer(cmd));
    VK(vkQueueSubmit(q, 1, &(VkSubmitInfo){
        .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount=1, .pCommandBuffers=&cmd}, VK_NULL_HANDLE));
    VK(vkQueueWaitIdle(q));
    vkDestroyBuffer(dev, stage.b, NULL);
    vkFreeMemory(dev, stage.m, NULL);
    return dst;
}

static Img img_storage(VkDevice dev, VkPhysicalDevice pd, U32 w, U32 h) {
    Img im = {.f=VK_FORMAT_R8G8B8A8_UNORM};
    VK(vkCreateImage(dev, &(VkImageCreateInfo){
        .sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=VK_IMAGE_TYPE_2D, .format=im.f,
        .extent={w,h,1}, .mipLevels=1, .arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT, .tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED
    }, NULL, &im.i));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, im.i, &mr);
    VK(vkAllocateMemory(dev, &(VkMemoryAllocateInfo){
        .sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=mr.size,
        .memoryTypeIndex=vk_memtype(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    }, NULL, &im.m));
    VK(vkBindImageMemory(dev, im.i, im.m, 0));
    VK(vkCreateImageView(dev, &(VkImageViewCreateInfo){
        .sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image=im.i, .viewType=VK_IMAGE_VIEW_TYPE_2D, .format=im.f,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}
    }, NULL, &im.v));
    return im;
}

static void img_barrier(VkCommandBuffer cmd, VkImage img,
                         VkImageLayout from, VkImageLayout to,
                         VkAccessFlags src_acc, VkAccessFlags dst_acc,
                         VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) {
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0,NULL, 0,NULL, 1,
        &(VkImageMemoryBarrier){
            .sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask=src_acc, .dstAccessMask=dst_acc,
            .oldLayout=from, .newLayout=to,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=img, .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}
        });
}

static U32* spv_load(const char *path, U32 *out_n) {
    FILE *f = fopen(path,"rb");
    if (!f) { fprintf(stderr,"Cannot open %s\n",path); exit(1); }
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    U32 *buf = malloc(sz);
    fread(buf,1,sz,f); fclose(f);
    *out_n = (U32)sz;
    return buf;
}

static VkShaderModule shader_load(VkDevice dev, const char *path) {
    U32 sz, *code = spv_load(path, &sz);
    VkShaderModule m;
    VK(vkCreateShaderModule(dev, &(VkShaderModuleCreateInfo){
        .sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=sz, .pCode=code
    }, NULL, &m));
    free(code); return m;
}

static void tex_upload_fmt(VkDevice dev, VkPhysicalDevice pd,
                           VkCommandBuffer cmd, VkQueue q,
                           const U8 *rgba, U32 w, U32 h, VkFormat fmt,
                           VkImage *out_img, VkDeviceMemory *out_mem,
                           VkImageView *out_view) {
    VkImage img;
    VK(vkCreateImage(dev, &(VkImageCreateInfo){
        .sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=VK_IMAGE_TYPE_2D,
        .format=fmt,
        .extent={w,h,1}, .mipLevels=1, .arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT, .tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED
    }, NULL, &img));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, img, &mr);
    VkDeviceMemory mem;
    VK(vkAllocateMemory(dev, &(VkMemoryAllocateInfo){
        .sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=mr.size,
        .memoryTypeIndex=vk_memtype(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    }, NULL, &mem));
    VK(vkBindImageMemory(dev, img, mem, 0));

    U64 sz = (U64)w * h * 4;
    Buf stage = buf_alloc(dev, pd, sz,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    buf_upload(dev, stage, rgba, sz);

    VK(vkResetCommandBuffer(cmd, 0));
    VK(vkBeginCommandBuffer(cmd, &(VkCommandBufferBeginInfo){
        .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT}));
    img_barrier(cmd, img,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyBufferToImage(cmd, stage.b, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
        &(VkBufferImageCopy){
            .imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
            .imageExtent={w,h,1}
        });
    img_barrier(cmd, img,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
    VK(vkEndCommandBuffer(cmd));
    VK(vkQueueSubmit(q, 1, &(VkSubmitInfo){
        .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount=1, .pCommandBuffers=&cmd}, VK_NULL_HANDLE));
    VK(vkQueueWaitIdle(q));
    vkDestroyBuffer(dev, stage.b, NULL);
    vkFreeMemory(dev, stage.m, NULL);

    VkImageView view;
    VK(vkCreateImageView(dev, &(VkImageViewCreateInfo){
        .sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image=img, .viewType=VK_IMAGE_VIEW_TYPE_2D,
        .format=fmt,
        .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}
    }, NULL, &view));

    *out_img = img; *out_mem = mem; *out_view = view;
}

static void tex_upload(VkDevice dev, VkPhysicalDevice pd,
                       VkCommandBuffer cmd, VkQueue q,
                       const U8 *rgba, U32 w, U32 h,
                       VkImage *out_img, VkDeviceMemory *out_mem,
                       VkImageView *out_view) {
    tex_upload_fmt(dev, pd, cmd, q, rgba, w, h,
                   VK_FORMAT_R8G8B8A8_SRGB, out_img, out_mem, out_view);
}

static VkSampler sampler_create(VkDevice dev) {
    VkSampler s;
    VK(vkCreateSampler(dev, &(VkSamplerCreateInfo){
        .sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter=VK_FILTER_LINEAR, .minFilter=VK_FILTER_LINEAR,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .maxLod=1.f
    }, NULL, &s));
    return s;
}

static VkSampler sampler_clamp_create(VkDevice dev) {
    VkSampler s;
    VK(vkCreateSampler(dev, &(VkSamplerCreateInfo){
        .sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter=VK_FILTER_LINEAR, .minFilter=VK_FILTER_LINEAR,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .maxLod=1.f
    }, NULL, &s));
    return s;
}

/* <<tga-loader>> ======================================================== */

static U8* tga_load(const char *path, U32 *w, U32 *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f,0,SEEK_END); long len=ftell(f); rewind(f);
    if (len < 18) { fclose(f); return NULL; }
    U8 *raw = malloc(len); fread(raw,1,len,f); fclose(f);
    U8 *p = raw, *end = raw + len;

    U8 id_len = p[0], cmap_type = p[1], img_type = p[2];
    U16 width, height; memcpy(&width, p+12, 2); memcpy(&height, p+14, 2);
    U8 bpp = p[16];
    (void)cmap_type;
    p += 18 + id_len;

    if (img_type != 2 && img_type != 3 && img_type != 10) {
        free(raw); return NULL;
    }
    U32 cols = width, rows = height;
    *w = cols; *h = rows;
    U8 *out = malloc(cols * rows * 4);

    if (img_type == 2 || img_type == 3) {
        for (U32 row = rows; row-- > 0; ) {
            U8 *dst = out + row * cols * 4;
            for (U32 col = 0; col < cols; col++) {
                if (p >= end) break;
                U8 r,g,b,a=255;
                if (bpp == 8)       { b=*p++; r=g=b; }
                else if (bpp == 24) { b=*p++; g=*p++; r=*p++; }
                else                { b=*p++; g=*p++; r=*p++; a=*p++; }
                *dst++=r; *dst++=g; *dst++=b; *dst++=a;
            }
        }
    } else { /* RLE (type 10) */
        U32 row = rows - 1, col = 0;
        U8 *dst = out + row * cols * 4;
        while (p < end && row < rows) {
            U8 hdr = *p++, cnt = (hdr & 0x7F) + 1;
            if (hdr & 0x80) { /* run-length */
                U8 b=0,g=0,r=0,a=255;
                if (bpp==24) { b=*p++; g=*p++; r=*p++; }
                else         { b=*p++; g=*p++; r=*p++; a=*p++; }
                for (U8 j=0;j<cnt;j++) {
                    *dst++=r; *dst++=g; *dst++=b; *dst++=a;
                    if (++col == cols) {
                        col = 0;
                        if (row == 0) goto done;
                        row--; dst = out + row * cols * 4;
                    }
                }
            } else { /* raw */
                for (U8 j=0;j<cnt;j++) {
                    U8 b=0,g=0,r=0,a=255;
                    if (bpp==24) { b=*p++; g=*p++; r=*p++; }
                    else         { b=*p++; g=*p++; r=*p++; a=*p++; }
                    *dst++=r; *dst++=g; *dst++=b; *dst++=a;
                    if (++col == cols) {
                        col = 0;
                        if (row == 0) goto done;
                        row--; dst = out + row * cols * 4;
                    }
                }
            }
        }
        done:;
    }
    free(raw);
    return out;
}

/* <<md3-loader>> ======================================================== */

#define MD3_MAGIC 0x33504449u  /* "IDP3" */

typedef struct { I32 magic; char name[64]; I32 flags;
                 I32 n_frames, n_shaders, n_verts, n_tris;
                 I32 ofs_tris, ofs_shaders, ofs_st, ofs_verts, ofs_end; } MD3Surf;
typedef struct { char name[64]; F32 origin[3]; F32 axis[9]; } MD3Tag;

typedef struct {
    Vtx *verts; U32 nv; U32 *idxs; U32 ni; U32 *tex_ids; U32 ntri;
    F32  tag_barrel[12]; /* origin(3)+axis(9) from body */
    F32  tag_wpn[30][12]; U32 n_anim_frames; /* tag_weapon from hand */
    char tex_name[3][64]; U32 n_surfaces;
} Weapon;

typedef struct {
    Weapon mdl; Vtx *xverts;
    int firing; F32 fire_t, bob_t;
    Buf vbuf, ibuf, tid_buf; AS blas;
    Buf blas_scratch;
    U32 wpn_tex_base;
} Wpn;

static void md3_parse_surface(const U8 *sdata, Vtx **pv, U32 *pnv,
                               U32 **pi, U32 *pni, U32 **ptid, U32 *pntri,
                               U32 tex_idx, const F32 *xform) {
    const MD3Surf *s = (const MD3Surf*)sdata;
    U32 base_v = *pnv;
    /* indices */
    const I32 *tris = (const I32*)(sdata + s->ofs_tris);
    *pi = realloc(*pi, sizeof(U32)*(*pni + s->n_tris*3));
    for (I32 i = 0; i < s->n_tris*3; i++)
        (*pi)[*pni + i] = base_v + (U32)tris[i];
    /* tex_ids */
    *ptid = realloc(*ptid, sizeof(U32)*(*pntri + s->n_tris));
    for (I32 i = 0; i < s->n_tris; i++)
        (*ptid)[*pntri + i] = tex_idx;
    /* vertices: packed as I16 xyz + U8 normal(lat/lng) */
    const U8 *vdata = sdata + s->ofs_verts; /* frame 0 */
    const F32 *stdata = (const F32*)(sdata + s->ofs_st);
    *pv = realloc(*pv, sizeof(Vtx)*(*pnv + s->n_verts));
    for (I32 i = 0; i < s->n_verts; i++) {
        const I16 *xyz = (const I16*)(vdata + i*8);
        F32 px = xyz[0] / 64.f, py = xyz[1] / 64.f, pz = xyz[2] / 64.f;
        /* decode packed normal (lat/lng in 2 bytes) */
        U8 lat = vdata[i*8+6], lng = vdata[i*8+7];
        F32 la = lat * (2.f*(F32)M_PI/255.f), lo = lng * (2.f*(F32)M_PI/255.f);
        F32 nx = cosf(la)*sinf(lo), ny = sinf(la)*sinf(lo), nz = cosf(lo);
        /* optional transform (tag_barrel pre-transform) */
        if (xform) {
            F32 ox=xform[0],oy=xform[1],oz=xform[2];
            F32 tx = xform[3]*px + xform[6]*py + xform[9]*pz  + ox;
            F32 ty = xform[4]*px + xform[7]*py + xform[10]*pz + oy;
            F32 tz = xform[5]*px + xform[8]*py + xform[11]*pz + oz;
            F32 tnx = xform[3]*nx + xform[6]*ny + xform[9]*nz;
            F32 tny = xform[4]*nx + xform[7]*ny + xform[10]*nz;
            F32 tnz = xform[5]*nx + xform[8]*ny + xform[11]*nz;
            px=tx; py=ty; pz=tz; nx=tnx; ny=tny; nz=tnz;
        }
        /* Q3 swizzle: (x,y,z) → (x,z,-y) */
        F32 su = stdata[i*2], sv = stdata[i*2+1];
        (*pv)[*pnv + i] = (Vtx){
            .pos={px, pz, -py}, .uv={su, sv}, .n={nx, nz, -ny}
        };
    }
    *pnv += s->n_verts;
    *pni += s->n_tris * 3;
    *pntri += s->n_tris;
}

static Weapon weapon_load(void) {
    Weapon w = {0};
    /* load body (machinegun.md3) */
    /* MD3 header: 0=magic(4) 4=ver(4) 8=name(64) 72=flags(4) 76=nframes(4)
       80=ntags(4) 84=nsurfaces(4) 88=nskins(4) 92=ofs_frames(4) 96=ofs_tags(4)
       100=ofs_surfaces(4) 104=ofs_eof(4) = 108 bytes total */
    FILE *f = fopen("assets/models/weapons2/machinegun/machinegun.md3","rb");
    if (!f) { printf("[wpn] machinegun.md3 not found\n"); return w; }
    fseek(f,0,SEEK_END); long sz=ftell(f); rewind(f);
    U8 *body = malloc(sz); fread(body,1,sz,f); fclose(f);
    assert(*(U32*)body == MD3_MAGIC);
    I32 body_n_surf    = *(I32*)(body+84);
    I32 body_n_tags    = *(I32*)(body+80);
    I32 body_ofs_tags  = *(I32*)(body+96);
    I32 body_ofs_surf  = *(I32*)(body+100);

    /* extract tag_barrel from body */
    memset(w.tag_barrel, 0, sizeof(w.tag_barrel));
    const MD3Tag *btags = (const MD3Tag*)(body + body_ofs_tags);
    for (I32 i = 0; i < body_n_tags; i++) {
        if (strncmp(btags[i].name, "tag_barrel", 64)==0) {
            memcpy(w.tag_barrel, btags[i].origin, 3*sizeof(F32));
            memcpy(w.tag_barrel+3, btags[i].axis, 9*sizeof(F32));
            break;
        }
    }

    /* parse body surfaces */
    const U8 *sp = body + body_ofs_surf;
    for (I32 i = 0; i < body_n_surf; i++) {
        const MD3Surf *s = (const MD3Surf*)sp;
        /* shader name from surface's shader list */
        const char *shd_name = (const char*)(sp + s->ofs_shaders);
        U32 tidx = w.n_surfaces;
        if (tidx < 3) {
            snprintf(w.tex_name[tidx], 64, "%s", shd_name);
            w.n_surfaces++;
        }
        md3_parse_surface(sp, &w.verts, &w.nv, &w.idxs, &w.ni,
                          &w.tex_ids, &w.ntri, tidx, NULL);
        sp += s->ofs_end;
    }
    free(body);

    /* load barrel (machinegun_barrel.md3) — pre-transform by tag_barrel */
    f = fopen("assets/models/weapons2/machinegun/machinegun_barrel.md3","rb");
    if (f) {
        fseek(f,0,SEEK_END); sz=ftell(f); rewind(f);
        U8 *brl = malloc(sz); fread(brl,1,sz,f); fclose(f);
        assert(*(U32*)brl == MD3_MAGIC);
        I32 brl_n_surf = *(I32*)(brl+84);
        I32 brl_ofs_surf = *(I32*)(brl+100);
        sp = brl + brl_ofs_surf;
        for (I32 i = 0; i < brl_n_surf; i++) {
            const MD3Surf *s = (const MD3Surf*)sp;
            U32 tidx = 0; /* barrel uses same texture as body (mgun.tga) */
            md3_parse_surface(sp, &w.verts, &w.nv, &w.idxs, &w.ni,
                              &w.tex_ids, &w.ntri, tidx, w.tag_barrel);
            (void)s;
            sp += ((const MD3Surf*)sp)->ofs_end;
        }
        free(brl);
        printf("[wpn] barrel merged, tag_barrel=(%.1f,%.1f,%.1f)\n",
               w.tag_barrel[0],w.tag_barrel[1],w.tag_barrel[2]);
    }

    /* load hand (machinegun_hand.md3) — extract tag_weapon animation */
    f = fopen("assets/models/weapons2/machinegun/machinegun_hand.md3","rb");
    if (f) {
        fseek(f,0,SEEK_END); sz=ftell(f); rewind(f);
        U8 *hand = malloc(sz); fread(hand,1,sz,f); fclose(f);
        assert(*(U32*)hand == MD3_MAGIC);
        I32 hand_n_frames = *(I32*)(hand+76);
        I32 hand_n_tags = *(I32*)(hand+80);
        I32 hand_ofs_tags = *(I32*)(hand+96);
        w.n_anim_frames = hand_n_frames < 30 ? hand_n_frames : 30;
        for (U32 fr = 0; fr < w.n_anim_frames; fr++) {
            const MD3Tag *tags = (const MD3Tag*)(hand + hand_ofs_tags +
                                  fr * hand_n_tags * sizeof(MD3Tag));
            for (I32 t = 0; t < hand_n_tags; t++) {
                if (strncmp(tags[t].name, "tag_weapon", 64)==0) {
                    memcpy(w.tag_wpn[fr], tags[t].origin, 3*sizeof(F32));
                    memcpy(w.tag_wpn[fr]+3, tags[t].axis, 9*sizeof(F32));
                    break;
                }
            }
        }
        free(hand);
        printf("[wpn] hand: %u animation frames\n", w.n_anim_frames);
    }

    printf("[wpn] loaded: %u verts, %u tris, %u surfaces\n", w.nv, w.ntri, w.n_surfaces);
    return w;
}

/* <<vkinit>> ============================================================ */

static void vk_create_instance(Ctx *C) {
    U32 n_ext; SDL_Vulkan_GetInstanceExtensions(C->win, &n_ext, NULL);
    const char **exts = malloc(sizeof(char*)*(n_ext+1));
    SDL_Vulkan_GetInstanceExtensions(C->win, &n_ext, exts);
    exts[n_ext++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

    const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
    VK(vkCreateInstance(&(VkInstanceCreateInfo){
        .sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo=&(VkApplicationInfo){
            .sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName="quake3rt",
            .apiVersion=VK_API_VERSION_1_3
        },
        .enabledLayerCount=1, .ppEnabledLayerNames=layers,
        .enabledExtensionCount=n_ext, .ppEnabledExtensionNames=exts
    }, NULL, &C->inst));
    free(exts);
    SDL_Vulkan_CreateSurface(C->win, C->inst, &C->surf);
}

static void vk_pick_device(Ctx *C) {
    U32 n; vkEnumeratePhysicalDevices(C->inst,&n,NULL);
    VkPhysicalDevice *pds = malloc(sizeof(VkPhysicalDevice)*n);
    vkEnumeratePhysicalDevices(C->inst,&n,pds);
    C->pd = pds[0];
    free(pds);

    U32 nq; vkGetPhysicalDeviceQueueFamilyProperties(C->pd,&nq,NULL);
    VkQueueFamilyProperties *qf = malloc(sizeof(*qf)*nq);
    vkGetPhysicalDeviceQueueFamilyProperties(C->pd,&nq,qf);
    C->qi = 0;
    for (U32 i=0;i<nq;i++) {
        VkBool32 present;
        vkGetPhysicalDeviceSurfaceSupportKHR(C->pd,i,C->surf,&present);
        if ((qf[i].queueFlags&VK_QUEUE_GRAPHICS_BIT) && present) { C->qi=i; break; }
    }
    free(qf);
}

static void vk_create_device(Ctx *C) {
    VkPhysicalDeviceAccelerationStructureFeaturesKHR f_as = {
        .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .accelerationStructure=VK_TRUE
    };
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR f_rt = {
        .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .pNext=&f_as, .rayTracingPipeline=VK_TRUE
    };
    VkPhysicalDeviceVulkan12Features f12 = {
        .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext=&f_rt,
        .bufferDeviceAddress=VK_TRUE,
        .descriptorIndexing=VK_TRUE,
        .runtimeDescriptorArray=VK_TRUE,
        .shaderSampledImageArrayNonUniformIndexing=VK_TRUE,
        .descriptorBindingPartiallyBound=VK_TRUE,
        .descriptorBindingVariableDescriptorCount=VK_TRUE
    };
    VkPhysicalDeviceVulkan13Features f13 = {
        .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext=&f12, .synchronization2=VK_TRUE, .dynamicRendering=VK_TRUE
    };

    const char *dev_exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    };
    F32 prio = 1.f;
    VK(vkCreateDevice(C->pd, &(VkDeviceCreateInfo){
        .sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext=&f13,
        .queueCreateInfoCount=1,
        .pQueueCreateInfos=&(VkDeviceQueueCreateInfo){
            .sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex=C->qi, .queueCount=1, .pQueuePriorities=&prio
        },
        .enabledExtensionCount=4, .ppEnabledExtensionNames=dev_exts
    }, NULL, &C->dev));
    vkGetDeviceQueue(C->dev, C->qi, 0, &C->q);

    C->rt_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 p2 = {
        .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext=&C->rt_props
    };
    vkGetPhysicalDeviceProperties2(C->pd, &p2);
    C->rt = rtfn_load(C->dev);
}

static void vk_create_swapchain(Ctx *C) {
    VkSurfaceCapabilitiesKHR cap;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(C->pd,C->surf,&cap);
    C->sc_ext = cap.currentExtent;
    C->sc_fmt = VK_FORMAT_B8G8R8A8_SRGB;

    U32 nimgs = cap.minImageCount + 1;
    if (cap.maxImageCount && nimgs > cap.maxImageCount) nimgs = cap.maxImageCount;

    VK(vkCreateSwapchainKHR(C->dev, &(VkSwapchainCreateInfoKHR){
        .sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface=C->surf, .minImageCount=nimgs,
        .imageFormat=C->sc_fmt, .imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent=C->sc_ext, .imageArrayLayers=1,
        .imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode=VK_SHARING_MODE_EXCLUSIVE,
        .preTransform=cap.currentTransform,
        .compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode=VK_PRESENT_MODE_FIFO_KHR,
        .clipped=VK_TRUE
    }, NULL, &C->sc));

    vkGetSwapchainImagesKHR(C->dev,C->sc,&C->sc_n,NULL);
    vkGetSwapchainImagesKHR(C->dev,C->sc,&C->sc_n,C->sc_img);
}

static void vk_create_sync(Ctx *C) {
    VK(vkCreateCommandPool(C->dev, &(VkCommandPoolCreateInfo){
        .sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex=C->qi
    }, NULL, &C->pool));
    VK(vkAllocateCommandBuffers(C->dev, &(VkCommandBufferAllocateInfo){
        .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=C->pool, .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount=1
    }, &C->cmd));
    VK(vkCreateFence(C->dev, &(VkFenceCreateInfo){
        .sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags=VK_FENCE_CREATE_SIGNALED_BIT
    }, NULL, &C->fence));
    VkSemaphoreCreateInfo si = {.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK(vkCreateSemaphore(C->dev,&si,NULL,&C->sem_img));
    VK(vkCreateSemaphore(C->dev,&si,NULL,&C->sem_done));
}

static void vk_transition_storage_image(Ctx *C) {
    VK(vkResetCommandBuffer(C->cmd, 0));
    VK(vkBeginCommandBuffer(C->cmd, &(VkCommandBufferBeginInfo){
        .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT}));
    img_barrier(C->cmd, C->rt_img.i,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        0, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
    VK(vkEndCommandBuffer(C->cmd));
    VK(vkQueueSubmit(C->q, 1, &(VkSubmitInfo){
        .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount=1, .pCommandBuffers=&C->cmd
    }, VK_NULL_HANDLE));
    VK(vkQueueWaitIdle(C->q));
}

/* <<bsp-loader>> ========================================================= */

#define BSP_MAGIC     0x50534249u
#define BSP_VER       46
#define BSP_ENTITIES  0
#define BSP_SHADERS   1
#define BSP_VERTS     10
#define BSP_IDXS      11
#define BSP_FACES     13
#define BSP_PLANES      2
#define BSP_NODES       3
#define BSP_LEAFS       4
#define BSP_LEAFSURFS   5
#define BSP_LEAFBRUSHES 6
#define BSP_BRUSHES     8
#define BSP_BRUSHSIDES  9
#define BSP_LIGHTMAPS 14
#define CONTENTS_SOLID  1
#define SURF_CLIP_EPS   0.125f
#define MST_PLANAR    1
#define MST_PATCH     2
#define MST_MESH      3
#define TESS_LOD      5
#define LM_SIZE       128

typedef struct { I32 ofs,len; }                        BLump;
typedef struct { U32 magic,ver; BLump lumps[17]; }     BHdr;
typedef struct { F32 xyz[3],st[2],lm[2],n[3]; U8 c[4];} BVtx;
typedef struct { I32 shd,fog,type,fv,nv,fi,ni,
                     lm,lmx,lmy,lmw,lmh;
                 F32 lmorg[3],lmvec[9]; I32 pw,ph; }   BFace;
typedef struct { char name[64]; I32 flags,contents; }  BShd;

typedef struct { F32 normal[3]; F32 dist; U8 type, signbits, pad[2]; } CPlane;
typedef struct { I32 plane; I32 children[2]; } CNode;
typedef struct { I32 cluster, area;
                 I32 first_surf, n_surfs; I32 first_brush, n_brushes; } CLeaf;
typedef struct { I32 first_side, n_sides, shader; } CBrush;
typedef struct { I32 plane, shader; } CBrushSide;

typedef struct {
    CPlane     *planes;     U32 n_planes;
    CNode      *nodes;      U32 n_nodes;
    CLeaf      *leafs;      U32 n_leafs;
    CBrush     *brushes;    U32 n_brushes;
    CBrushSide *sides;      U32 n_sides;
    I32        *leaf_brushes; U32 n_leaf_brushes;
    I32        *shd_contents;
    U32         check;
    U32        *brush_check;
} ColMap;

static Vtx bvtx_conv(const BVtx *b) {
    /* Q3 Z-up (x,y,z) → Y-up (x,z,-y); _q = raw lightmap UVs */
    return (Vtx){.pos={b->xyz[0], b->xyz[2], -b->xyz[1]},
                 .uv ={b->st[0],  b->st[1]},
                 ._q ={b->lm[0],  b->lm[1]},
                 .n  ={b->n[0],   b->n[2],   -b->n[1]}};
}

static V3 bez3v(V3 a, V3 b, V3 c, F32 t) {
    F32 s=1-t;
    return v3add(v3add(v3scale(a,s*s),v3scale(b,2*s*t)),v3scale(c,t*t));
}

static U32 bsp_tess_patch(const BVtx *cg, int pw, int ph,
                           Vtx **pv, U32 *pnv, U32 **pi, U32 *pni) {
    int nx=(pw-1)/2, ny=(ph-1)/2, L=TESS_LOD, S=L+1;
    U32 addv=(U32)(nx*ny*S*S), addi=(U32)(nx*ny*L*L*6);
    *pv = realloc(*pv, sizeof(Vtx)*(*pnv+addv));
    *pi = realloc(*pi, sizeof(U32)*(*pni+addi));
    U32 vbase=*pnv, ii=*pni;
    for (int py=0;py<ny;py++) for (int px=0;px<nx;px++) {
        V3 cp[3][3], cn[3][3], ct[3][3], cl[3][3];
        for (int r=0;r<3;r++) for (int c=0;c<3;c++) {
            const BVtx *v=&cg[(py*2+r)*pw+(px*2+c)];
            /* Q3 Z-up → Y-up swizzle */
            cp[r][c]=v3(v->xyz[0], v->xyz[2], -v->xyz[1]);
            cn[r][c]=v3(v->n[0],   v->n[2],   -v->n[1]);
            ct[r][c]=v3(v->st[0],  v->st[1],  0);
            cl[r][c]=v3(v->lm[0],  v->lm[1],  0);
        }
        U32 pb=vbase+(U32)((py*nx+px)*S*S);
        for (int j=0;j<=L;j++) {
            F32 t=(F32)j/L; V3 qp[3],qn[3],qt[3],ql[3];
            for (int r=0;r<3;r++) {
                qp[r]=bez3v(cp[r][0],cp[r][1],cp[r][2],t);
                qn[r]=bez3v(cn[r][0],cn[r][1],cn[r][2],t);
                qt[r]=bez3v(ct[r][0],ct[r][1],ct[r][2],t);
                ql[r]=bez3v(cl[r][0],cl[r][1],cl[r][2],t);
            }
            for (int i=0;i<=L;i++) {
                F32 s=(F32)i/L;
                V3 pos=bez3v(qp[0],qp[1],qp[2],s);
                V3 nor=v3norm(bez3v(qn[0],qn[1],qn[2],s));
                V3 tex=bez3v(qt[0],qt[1],qt[2],s);
                V3 lmv=bez3v(ql[0],ql[1],ql[2],s);
                (*pv)[pb+j*S+i]=(Vtx){.pos={pos.x,pos.y,pos.z},
                                       .uv={tex.x,tex.y},
                                       ._q={lmv.x,lmv.y},
                                       .n={nor.x,nor.y,nor.z}};
            }
        }
        for (int j=0;j<L;j++) for (int i=0;i<L;i++) {
            U32 a=pb+j*S+i,b=a+1,c=pb+(j+1)*S+i,d=c+1;
            (*pi)[ii++]=a;(*pi)[ii++]=c;(*pi)[ii++]=b;
            (*pi)[ii++]=b;(*pi)[ii++]=c;(*pi)[ii++]=d;
        }
    }
    *pnv+=addv; *pni+=addi;
    return addi/3;
}

static Spawn bsp_find_spawn(const U8 *data, const BHdr *hdr);

static Scene scene_load_bsp(const char *path, Spawn *out_spawn, ColMap *out_col) {
    FILE *f=fopen(path,"rb");
    if (!f) { fprintf(stderr,"Cannot open %s\n",path); exit(1); }
    fseek(f,0,SEEK_END); long fsz=ftell(f); rewind(f);
    U8 *data=malloc(fsz); fread(data,1,fsz,f); fclose(f);

    BHdr   *hdr   =(BHdr*)  (data+0);
    assert(hdr->magic==BSP_MAGIC && hdr->ver==BSP_VER);
    BVtx   *bv    =(BVtx*)  (data+hdr->lumps[BSP_VERTS].ofs);
    U32     nbv   =(U32)    (hdr->lumps[BSP_VERTS].len/sizeof(BVtx));
    I32    *bi    =(I32*)   (data+hdr->lumps[BSP_IDXS].ofs);
    BFace  *bf    =(BFace*) (data+hdr->lumps[BSP_FACES].ofs);
    U32     nbf   =(U32)    (hdr->lumps[BSP_FACES].len/sizeof(BFace));
    BShd   *bs    =(BShd*)  (data+hdr->lumps[BSP_SHADERS].ofs);
    U32     nbs   =(U32)    (hdr->lumps[BSP_SHADERS].len/sizeof(BShd));

    /* build lightmap atlas */
    U32 lm_lump_sz = (U32)hdr->lumps[BSP_LIGHTMAPS].len;
    U32 n_lm = lm_lump_sz / (LM_SIZE * LM_SIZE * 3);
    U32 n_lm_total = n_lm + 1; /* +1 for white fallback page */
    U32 lm_cols = 1, lm_rows = 1;
    U8 *lm_atlas = NULL;
    U32 lm_aw = 0, lm_ah = 0;
    F32 white_u = 0.5f, white_v = 0.5f;

    if (n_lm > 0) {
        while (lm_cols * lm_cols < n_lm_total) lm_cols++;
        lm_rows = (n_lm_total + lm_cols - 1) / lm_cols;
        lm_aw = lm_cols * LM_SIZE; lm_ah = lm_rows * LM_SIZE;
        lm_atlas = calloc(lm_aw * lm_ah * 4, 1);
        const U8 *lmd = data + hdr->lumps[BSP_LIGHTMAPS].ofs;
        for (U32 pg = 0; pg < n_lm; pg++) {
            U32 cx = pg % lm_cols, cy = pg / lm_cols;
            const U8 *src = lmd + pg * LM_SIZE * LM_SIZE * 3;
            for (U32 y = 0; y < LM_SIZE; y++)
                for (U32 x = 0; x < LM_SIZE; x++) {
                    U32 d = ((cy*LM_SIZE+y)*lm_aw + cx*LM_SIZE+x)*4;
                    U32 s = (y*LM_SIZE+x)*3;
                    lm_atlas[d]=src[s]; lm_atlas[d+1]=src[s+1];
                    lm_atlas[d+2]=src[s+2]; lm_atlas[d+3]=255;
                }
        }
        /* white fallback page */
        U32 wcx = n_lm % lm_cols, wcy = n_lm / lm_cols;
        for (U32 y = 0; y < LM_SIZE; y++)
            for (U32 x = 0; x < LM_SIZE; x++) {
                U32 d = ((wcy*LM_SIZE+y)*lm_aw + wcx*LM_SIZE+x)*4;
                lm_atlas[d]=lm_atlas[d+1]=lm_atlas[d+2]=lm_atlas[d+3]=255;
            }
        white_u = ((F32)wcx + 0.5f) / (F32)lm_cols;
        white_v = ((F32)wcy + 0.5f) / (F32)lm_rows;
        printf("[lm] %u pages → %ux%u atlas (%u cols)\n", n_lm, lm_aw, lm_ah, lm_cols);
    }

    U32 nv=nbv; Vtx *verts=malloc(sizeof(Vtx)*nv);
    for (U32 i=0;i<nv;i++) verts[i]=bvtx_conv(&bv[i]);

    U32 *idxs=NULL, *tex_ids=NULL, ni=0, tc=0;
    for (U32 fi=0;fi<nbf;fi++) {
        const BFace *face=&bf[fi];
        if (face->type==MST_PLANAR||face->type==MST_MESH) {
            U32 ft = (U32)(face->ni / 3);
            idxs=realloc(idxs,sizeof(U32)*(ni+face->ni));
            tex_ids=realloc(tex_ids,sizeof(U32)*(tc+ft));
            for (I32 i=0;i<face->ni;i++)
                idxs[ni+i]=(U32)(face->fv+bi[face->fi+i]);
            for (U32 t=0;t<ft;t++) tex_ids[tc+t]=(U32)face->shd;
            ni+=face->ni; tc+=ft;
            /* transform lightmap UVs → atlas space */
            if (face->lm >= 0 && lm_cols > 0) {
                F32 cx = (F32)((U32)face->lm % lm_cols);
                F32 cy = (F32)((U32)face->lm / lm_cols);
                for (I32 i=0;i<face->nv;i++) {
                    U32 vi=(U32)(face->fv+i);
                    verts[vi]._q[0]=(cx+verts[vi]._q[0])/(F32)lm_cols;
                    verts[vi]._q[1]=(cy+verts[vi]._q[1])/(F32)lm_rows;
                }
            } else {
                for (I32 i=0;i<face->nv;i++) {
                    U32 vi=(U32)(face->fv+i);
                    verts[vi]._q[0]=white_u; verts[vi]._q[1]=white_v;
                }
            }
        } else if (face->type==MST_PATCH) {
            U32 old_nv=nv, old_tc=tc;
            tc+=bsp_tess_patch(&bv[face->fv],face->pw,face->ph,
                               &verts,&nv,&idxs,&ni);
            U32 patch_tris=tc-old_tc;
            tex_ids=realloc(tex_ids,sizeof(U32)*tc);
            for (U32 t=0;t<patch_tris;t++) tex_ids[old_tc+t]=(U32)face->shd;
            /* transform patch lightmap UVs → atlas space */
            if (face->lm >= 0 && lm_cols > 0) {
                F32 cx = (F32)((U32)face->lm % lm_cols);
                F32 cy = (F32)((U32)face->lm / lm_cols);
                for (U32 vi=old_nv;vi<nv;vi++) {
                    verts[vi]._q[0]=(cx+verts[vi]._q[0])/(F32)lm_cols;
                    verts[vi]._q[1]=(cy+verts[vi]._q[1])/(F32)lm_rows;
                }
            } else {
                for (U32 vi=old_nv;vi<nv;vi++) {
                    verts[vi]._q[0]=white_u; verts[vi]._q[1]=white_v;
                }
            }
        }
    }

    U32 nm=nbs;
    V4 *mats=malloc(sizeof(V4)*nm);
    char (*tnames)[64]=malloc(sizeof(char[64])*nm);
    for (U32 i=0;i<nm;i++) {
        U32 h=5381; for (int c=0;bs[i].name[c];c++) h=h*31+(U8)bs[i].name[c];
        mats[i]=(V4){0.4f+0.35f*((h>>0&0xFF)/255.f),
                     0.4f+0.35f*((h>>8&0xFF)/255.f),
                     0.4f+0.35f*((h>>16&0xFF)/255.f),1};
        memcpy(tnames[i], bs[i].name, 64);
    }

    if (out_spawn) *out_spawn = bsp_find_spawn(data, hdr);

    /* load collision data (planes, nodes, leafs, brushes, brush sides) */
    if (out_col) {
        ColMap *cm = out_col;
        memset(cm, 0, sizeof(*cm));
        /* planes: {float normal[3], float dist} = 16 bytes */
        cm->n_planes = (U32)hdr->lumps[BSP_PLANES].len / 16;
        cm->planes = malloc(sizeof(CPlane) * cm->n_planes);
        for (U32 i = 0; i < cm->n_planes; i++) {
            const F32 *p = (const F32*)(data + hdr->lumps[BSP_PLANES].ofs + i*16);
            cm->planes[i].normal[0] = p[0];
            cm->planes[i].normal[1] = p[2];
            cm->planes[i].normal[2] = -p[1];
            cm->planes[i].dist = p[3];
            F32 *n = cm->planes[i].normal;
            /* only positive axials use the shortcut (t1=p1[type]-dist) —
               after Y-up swizzle, negative axials must use the general path */
            cm->planes[i].type = (n[0]==1.f) ? 0 : (n[1]==1.f) ? 1 :
                                  (n[2]==1.f) ? 2 : 3;
            cm->planes[i].signbits = (U8)((n[0]<0) | ((n[1]<0)<<1) | ((n[2]<0)<<2));
        }
        /* nodes: {int plane, children[2], mins[3], maxs[3]} = 36 bytes */
        cm->n_nodes = (U32)hdr->lumps[BSP_NODES].len / 36;
        cm->nodes = malloc(sizeof(CNode) * cm->n_nodes);
        for (U32 i = 0; i < cm->n_nodes; i++) {
            const I32 *nd = (const I32*)(data + hdr->lumps[BSP_NODES].ofs + i*36);
            cm->nodes[i].plane = nd[0];
            cm->nodes[i].children[0] = nd[1];
            cm->nodes[i].children[1] = nd[2];
        }
        /* leafs: 48 bytes each */
        cm->n_leafs = (U32)hdr->lumps[BSP_LEAFS].len / 48;
        cm->leafs = malloc(sizeof(CLeaf) * cm->n_leafs);
        for (U32 i = 0; i < cm->n_leafs; i++) {
            const I32 *lf = (const I32*)(data + hdr->lumps[BSP_LEAFS].ofs + i*48);
            cm->leafs[i].cluster = lf[0];
            cm->leafs[i].area = lf[1];
            cm->leafs[i].first_surf = lf[8];
            cm->leafs[i].n_surfs = lf[9];
            cm->leafs[i].first_brush = lf[10];
            cm->leafs[i].n_brushes = lf[11];
        }
        /* leaf brushes: int[] */
        cm->n_leaf_brushes = (U32)hdr->lumps[BSP_LEAFBRUSHES].len / 4;
        cm->leaf_brushes = malloc(sizeof(I32) * cm->n_leaf_brushes);
        memcpy(cm->leaf_brushes, data + hdr->lumps[BSP_LEAFBRUSHES].ofs,
               sizeof(I32) * cm->n_leaf_brushes);
        /* brushes: {int first_side, n_sides, shader} = 12 bytes */
        cm->n_brushes = (U32)hdr->lumps[BSP_BRUSHES].len / 12;
        cm->brushes = malloc(sizeof(CBrush) * cm->n_brushes);
        for (U32 i = 0; i < cm->n_brushes; i++) {
            const I32 *br = (const I32*)(data + hdr->lumps[BSP_BRUSHES].ofs + i*12);
            cm->brushes[i].first_side = br[0];
            cm->brushes[i].n_sides = br[1];
            cm->brushes[i].shader = br[2];
        }
        /* brush sides: {int plane, shader} = 8 bytes */
        cm->n_sides = (U32)hdr->lumps[BSP_BRUSHSIDES].len / 8;
        cm->sides = malloc(sizeof(CBrushSide) * cm->n_sides);
        for (U32 i = 0; i < cm->n_sides; i++) {
            const I32 *bs2 = (const I32*)(data + hdr->lumps[BSP_BRUSHSIDES].ofs + i*8);
            cm->sides[i].plane = bs2[0];
            cm->sides[i].shader = bs2[1];
        }
        /* shader contents flags */
        cm->shd_contents = malloc(sizeof(I32) * nbs);
        for (U32 i = 0; i < nbs; i++) cm->shd_contents[i] = bs[i].contents;
        /* per-brush checkcount for trace dedup */
        cm->brush_check = calloc(cm->n_brushes, sizeof(U32));
        cm->check = 0;
        printf("[col] %u planes, %u nodes, %u leafs, %u brushes, %u sides\n",
               cm->n_planes, cm->n_nodes, cm->n_leafs, cm->n_brushes, cm->n_sides);
    }

    free(data);
    printf("[bsp] %s: %u verts, %u tris, %u shaders\n",path,nv,tc,nbs);
    return (Scene){.verts=verts,.nv=nv,.idxs=idxs,.ni=ni,
                   .mats=mats,.nm=nm,.tex_ids=tex_ids,
                   .tex_names=tnames,.tri_count=tc,
                   .lm_atlas=lm_atlas,.lm_w=lm_aw,.lm_h=lm_ah};
}

/* <<bsp-entities>> ====================================================== */

static Spawn bsp_find_spawn(const U8 *data, const BHdr *hdr) {
    const char *ents = (const char*)(data + hdr->lumps[BSP_ENTITIES].ofs);
    I32 len = hdr->lumps[BSP_ENTITIES].len;
    Spawn sp = {.origin={0,0,0}, .angle=0};

    const char *p = ents, *end = ents + len;
    while (p < end) {
        /* find opening brace */
        while (p < end && *p != '{') p++;
        if (p >= end) break;
        p++;

        int is_spawn = 0;
        V3 org = {0,0,0}; F32 ang = 0;
        int has_origin = 0;

        while (p < end && *p != '}') {
            /* skip whitespace */
            while (p < end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r')) p++;
            if (p >= end || *p == '}') break;

            /* read key */
            if (*p != '"') { p++; continue; }
            p++;
            const char *key = p;
            while (p < end && *p != '"') p++;
            int klen = (int)(p - key);
            if (p < end) p++;

            /* skip whitespace */
            while (p < end && (*p==' '||*p=='\t')) p++;

            /* read value */
            if (p >= end || *p != '"') continue;
            p++;
            const char *val = p;
            while (p < end && *p != '"') p++;
            int vlen = (int)(p - val);
            if (p < end) p++;

            if (klen==9 && memcmp(key,"classname",9)==0 &&
                vlen==22 && memcmp(val,"info_player_deathmatch",22)==0)
                is_spawn = 1;
            if (klen==6 && memcmp(key,"origin",6)==0) {
                char buf[64]; int n=vlen<63?vlen:63;
                memcpy(buf,val,n); buf[n]=0;
                sscanf(buf,"%f %f %f",&org.x,&org.y,&org.z);
                has_origin = 1;
            }
            if (klen==5 && memcmp(key,"angle",5)==0) {
                char buf[32]; int n=vlen<31?vlen:31;
                memcpy(buf,val,n); buf[n]=0;
                sscanf(buf,"%f",&ang);
            }
        }
        if (is_spawn && has_origin) {
            /* Q3 Z-up → Y-up: (x,y,z) → (x,z,-y) — player origin */
            sp.origin = v3(org.x, org.z, -org.y);
            sp.angle = ang;
            printf("[bsp] spawn: %.0f %.0f %.0f angle %.0f\n",
                   sp.origin.x, sp.origin.y, sp.origin.z, ang);
            return sp;
        }
        if (p < end) p++; /* skip '}' */
    }
    printf("[bsp] no spawn found, using origin\n");
    return sp;
}

/* <<collision>> ========================================================= */

typedef struct {
    F32 fraction;
    V3  endpos;
    V3  normal;
    int startsolid, allsolid;
} Trace;

typedef struct {
    int use;       /* 0=AABB, 1=capsule */
    F32 radius;    /* hemisphere radius */
    F32 halfheight;
    V3  offset;    /* (0, halfheight-radius, 0) in Y-up */
} Sphere;

typedef struct {
    V3    start, end;
    V3    extents;
    V3    offsets[8];
    Trace trace;
    int   isPoint;
    Sphere sphere;
} TraceWork;

static void cm_trace_brush(TraceWork *tw, const CBrush *brush, const ColMap *cm) {
    if (brush->n_sides <= 0) return;

    F32 enterFrac = -1.f, leaveFrac = 1.f;
    const CPlane *clipplane = NULL;
    int getout = 0, startout = 0;

    for (I32 i = 0; i < brush->n_sides; i++) {
        const CBrushSide *side = &cm->sides[brush->first_side + i];
        const CPlane *plane = &cm->planes[side->plane];

        F32 dist, d1, d2;
        if (tw->sphere.use) {
            /* capsule: expand plane by radius, trace from nearest sphere center */
            dist = plane->dist + tw->sphere.radius;
            F32 t = plane->normal[0] * tw->sphere.offset.x +
                    plane->normal[1] * tw->sphere.offset.y +
                    plane->normal[2] * tw->sphere.offset.z;
            V3 sp, ep;
            if (t > 0) {
                sp = v3sub(tw->start, tw->sphere.offset);
                ep = v3sub(tw->end,   tw->sphere.offset);
            } else {
                sp = v3add(tw->start, tw->sphere.offset);
                ep = v3add(tw->end,   tw->sphere.offset);
            }
            d1 = sp.x*plane->normal[0] + sp.y*plane->normal[1] +
                 sp.z*plane->normal[2] - dist;
            d2 = ep.x*plane->normal[0] + ep.y*plane->normal[1] +
                 ep.z*plane->normal[2] - dist;
        } else if (tw->isPoint) {
            dist = plane->dist;
            d1 = tw->start.x*plane->normal[0] + tw->start.y*plane->normal[1] +
                 tw->start.z*plane->normal[2] - dist;
            d2 = tw->end.x*plane->normal[0] + tw->end.y*plane->normal[1] +
                 tw->end.z*plane->normal[2] - dist;
        } else {
            const V3 *ofs = &tw->offsets[plane->signbits];
            dist = plane->dist - (((F32*)ofs)[0] * plane->normal[0] +
                                   ((F32*)ofs)[1] * plane->normal[1] +
                                   ((F32*)ofs)[2] * plane->normal[2]);
            d1 = tw->start.x*plane->normal[0] + tw->start.y*plane->normal[1] +
                 tw->start.z*plane->normal[2] - dist;
            d2 = tw->end.x*plane->normal[0] + tw->end.y*plane->normal[1] +
                 tw->end.z*plane->normal[2] - dist;
        }

        if (d2 > 0) getout = 1;
        if (d1 > 0) startout = 1;

        if (d1 > 0 && (d2 >= SURF_CLIP_EPS || d2 >= d1)) continue;
        if (d1 <= 0 && d2 <= 0) continue;

        if (d1 > d2) { /* entering */
            F32 f = (d1 - SURF_CLIP_EPS) / (d1 - d2);
            if (f < 0) f = 0;
            if (f > enterFrac) { enterFrac = f; clipplane = plane; }
        } else { /* leaving */
            F32 f = (d1 + SURF_CLIP_EPS) / (d1 - d2);
            if (f > 1) f = 1;
            if (f < leaveFrac) leaveFrac = f;
        }
    }

    if (!startout) {
        tw->trace.startsolid = 1;
        if (!getout) tw->trace.allsolid = 1;
        tw->trace.fraction = 0;
        return;
    }

    if (enterFrac < leaveFrac && enterFrac > -1 && enterFrac < tw->trace.fraction) {
        if (enterFrac < 0) enterFrac = 0;
        tw->trace.fraction = enterFrac;
        tw->trace.normal = v3(clipplane->normal[0], clipplane->normal[1],
                              clipplane->normal[2]);
    }
}

static void cm_trace_leaf(TraceWork *tw, const CLeaf *leaf, ColMap *cm) {
    for (I32 k = 0; k < leaf->n_brushes; k++) {
        I32 bi = cm->leaf_brushes[leaf->first_brush + k];
        if ((U32)bi >= cm->n_brushes) continue;
        if (cm->brush_check[bi] == cm->check) continue;
        cm->brush_check[bi] = cm->check;
        const CBrush *b = &cm->brushes[bi];
        if (!(cm->shd_contents[b->shader] & CONTENTS_SOLID)) continue;
        cm_trace_brush(tw, b, cm);
        if (tw->trace.fraction == 0.f) return;
    }
}

static void cm_trace_tree(TraceWork *tw, int num, F32 p1f, F32 p2f,
                           V3 p1, V3 p2, ColMap *cm) {
    if (tw->trace.fraction <= p1f) return;

    if (num < 0) {
        cm_trace_leaf(tw, &cm->leafs[-(num + 1)], cm);
        return;
    }

    const CNode *node = &cm->nodes[num];
    const CPlane *plane = &cm->planes[node->plane];

    F32 t1, t2, offset;

    if (plane->type < 3) {
        t1 = ((F32*)&p1)[plane->type] - plane->dist;
        t2 = ((F32*)&p2)[plane->type] - plane->dist;
        offset = ((F32*)&tw->extents)[plane->type];
    } else {
        V3 pn = v3(plane->normal[0], plane->normal[1], plane->normal[2]);
        t1 = v3dot(p1, pn) - plane->dist;
        t2 = v3dot(p2, pn) - plane->dist;
        if (tw->isPoint) {
            offset = 0;
        } else {
            offset = fabsf(tw->extents.x * plane->normal[0]) +
                     fabsf(tw->extents.y * plane->normal[1]) +
                     fabsf(tw->extents.z * plane->normal[2]);
        }
    }

    if (t1 >= offset + 1 && t2 >= offset + 1) {
        cm_trace_tree(tw, node->children[0], p1f, p2f, p1, p2, cm);
        return;
    }
    if (t1 < -offset - 1 && t2 < -offset - 1) {
        cm_trace_tree(tw, node->children[1], p1f, p2f, p1, p2, cm);
        return;
    }

    int side;
    F32 frac1, frac2;

    if (t1 < t2) {
        F32 idist = 1.f / (t1 - t2);
        side = 1;
        frac2 = (t1 + offset + SURF_CLIP_EPS) * idist;
        frac1 = (t1 - offset + SURF_CLIP_EPS) * idist;
    } else if (t1 > t2) {
        F32 idist = 1.f / (t1 - t2);
        side = 0;
        frac2 = (t1 - offset - SURF_CLIP_EPS) * idist;
        frac1 = (t1 + offset + SURF_CLIP_EPS) * idist;
    } else {
        side = 0;
        frac1 = 1.f;
        frac2 = 0.f;
    }

    if (frac1 < 0) frac1 = 0;
    if (frac1 > 1) frac1 = 1;
    if (frac2 < 0) frac2 = 0;
    if (frac2 > 1) frac2 = 1;

    {
        F32 midf = p1f + (p2f - p1f) * frac1;
        V3 mid = v3add(p1, v3scale(v3sub(p2, p1), frac1));
        cm_trace_tree(tw, node->children[side], p1f, midf, p1, mid, cm);
    }
    {
        F32 midf = p1f + (p2f - p1f) * frac2;
        V3 mid = v3add(p1, v3scale(v3sub(p2, p1), frac2));
        cm_trace_tree(tw, node->children[side ^ 1], midf, p2f, mid, p2, cm);
    }
}

/* sphere-ray intersection: |start + t*dir - origin|² = radius² */
static void cm_trace_sphere(TraceWork *tw, V3 origin, F32 radius,
                             V3 start, V3 end) {
    V3 dir = v3sub(end, start);
    F32 len = sqrtf(v3dot(dir, dir));
    if (len < 1e-6f) {
        /* zero-length trace: just test containment */
        V3 d = v3sub(start, origin);
        if (v3dot(d, d) <= radius * radius) {
            tw->trace.startsolid = 1;
            tw->trace.allsolid = 1;
            tw->trace.fraction = 0;
        }
        return;
    }
    dir = v3scale(dir, 1.f / len);
    V3 v1 = v3sub(start, origin);
    F32 b = 2.f * v3dot(dir, v1);
    F32 c = v3dot(v1, v1) - radius * radius;
    F32 d = b * b - 4.f * c;
    if (d <= 0) return;
    F32 sqrtd = sqrtf(d);
    F32 frac = (-b - sqrtd) * 0.5f;
    if (frac < 0) {
        /* start is inside sphere */
        tw->trace.startsolid = 1;
        frac = (-b + sqrtd) * 0.5f;
        if (frac < 0) { tw->trace.allsolid = 1; tw->trace.fraction = 0; return; }
        return;
    }
    frac /= len; /* convert from ray-param to fraction of original segment */
    if (frac < tw->trace.fraction) {
        tw->trace.fraction = frac < 0 ? 0 : frac;
        V3 hit = v3add(start, v3scale(v3sub(end, start), frac));
        tw->trace.normal = v3norm(v3sub(hit, origin));
    }
}

/* vertical cylinder intersection (2D circle test, clamped to height range) */
static void cm_trace_vert_cylinder(TraceWork *tw, V3 origin, F32 radius,
                                    F32 halfheight, V3 start, V3 end) {
    /* project to XZ plane (Y-up) */
    F32 dx = end.x - start.x, dz = end.z - start.z;
    F32 sx = start.x - origin.x, sz = start.z - origin.z;
    F32 a = dx*dx + dz*dz;
    if (a < 1e-12f) return; /* no horizontal movement */
    F32 b = 2.f * (sx*dx + sz*dz);
    F32 c = sx*sx + sz*sz - radius * radius;
    F32 disc = b*b - 4.f*a*c;
    if (disc <= 0) return;
    F32 sqrtd = sqrtf(disc);
    F32 t = (-b - sqrtd) / (2.f * a);
    if (t < 0 || t >= tw->trace.fraction) return;
    /* check Y is within cylinder height at intersection */
    F32 iy = start.y + t * (end.y - start.y);
    if (iy < origin.y - halfheight || iy > origin.y + halfheight) return;
    tw->trace.fraction = t;
    /* normal is horizontal, pointing outward from cylinder axis */
    F32 hx = start.x + t * dx - origin.x;
    F32 hz = start.z + t * dz - origin.z;
    F32 hlen = sqrtf(hx*hx + hz*hz);
    if (hlen > 1e-6f) tw->trace.normal = v3(hx/hlen, 0, hz/hlen);
}

/* capsule-vs-capsule: cylinder + two sphere tests (for entity-entity collision) */
static void __attribute__((unused)) cm_trace_capsule_capsule(TraceWork *tw,
    V3 origin, F32 radius, F32 halfheight, F32 offs,
    V3 start, V3 end) {
    /* expand radius by moving capsule's radius */
    F32 r = radius + tw->sphere.radius;
    /* cylinder test for the horizontal contact region */
    F32 h = halfheight + tw->sphere.halfheight - r;
    if (h > 0)
        cm_trace_vert_cylinder(tw, origin, r, h, start, end);
    /* top sphere of static capsule vs bottom sphere of moving capsule */
    V3 top = v3add(origin, v3(0, offs, 0));
    cm_trace_sphere(tw, top, r,
                    v3sub(start, tw->sphere.offset),
                    v3sub(end, tw->sphere.offset));
    /* bottom sphere of static capsule vs top sphere of moving capsule */
    V3 bot = v3sub(origin, v3(0, offs, 0));
    cm_trace_sphere(tw, bot, r,
                    v3add(start, tw->sphere.offset),
                    v3add(end, tw->sphere.offset));
}

static Trace cm_trace(V3 start, V3 end, V3 mins, V3 maxs, ColMap *cm, int capsule) {
    TraceWork tw;
    memset(&tw, 0, sizeof(tw));
    tw.start = start;
    tw.end = end;
    tw.trace.fraction = 1.f;

    tw.extents = v3((-mins.x > maxs.x ? -mins.x : maxs.x),
                    (-mins.y > maxs.y ? -mins.y : maxs.y),
                    (-mins.z > maxs.z ? -mins.z : maxs.z));

    tw.isPoint = (mins.x == 0 && mins.y == 0 && mins.z == 0 &&
                  maxs.x == 0 && maxs.y == 0 && maxs.z == 0);

    tw.sphere.use = capsule && !tw.isPoint;
    if (tw.sphere.use) {
        F32 hw = maxs.x, hh = maxs.y;
        tw.sphere.radius = hw < hh ? hw : hh;
        tw.sphere.halfheight = hh;
        tw.sphere.offset = v3(0, hh - tw.sphere.radius, 0);
    }

    for (int i = 0; i < 8; i++) {
        tw.offsets[i] = v3((i & 1) ? maxs.x : mins.x,
                           (i & 2) ? maxs.y : mins.y,
                           (i & 4) ? maxs.z : mins.z);
    }

    cm->check++;
    cm_trace_tree(&tw, 0, 0.f, 1.f, tw.start, tw.end, cm);

    if (tw.trace.fraction == 1.f) {
        tw.trace.endpos = end;
    } else {
        tw.trace.endpos = v3add(start, v3scale(v3sub(end, start), tw.trace.fraction));
    }

    return tw.trace;
}



/* <<physics>> =========================================================== */

#define PM_GRAVITY       800.f
#define PM_FRICTION      6.f
#define PM_STOPSPEED     100.f
#define PM_ACCELERATE    10.f
#define PM_AIRACCELERATE 1.f
#define PM_MAXSPEED      320.f
#define JUMP_VELOCITY    270.f
#define STEPSIZE         18.f
#define MIN_WALK_NORMAL  0.7f
#define OVERCLIP         1.001f
#define MAX_CLIP_PLANES  5
#define DEFAULT_VIEWHEIGHT 26.f
#define CROUCH_VIEWHEIGHT  12.f

static const V3 PM_MINS = {-15, -24, -15};

typedef struct {
    V3  pos;
    V3  vel;
    F32 yaw, pitch;
    int on_ground;     /* walking: on walkable ground */
    int jump_held;
    V3  ground_normal;
    int ground_plane;  /* have a ground plane (may be too steep) */
    int ducked;        /* crouching */
    F32 viewheight;    /* current eye height above pos */
} Player;

static V3 pm_maxs(const Player *p) { return v3(15, p->ducked ? 16 : 32, 15); }

static Trace pm_trace(V3 start, V3 end, const Player *p, ColMap *cm) {
    return cm_trace(start, end, PM_MINS, pm_maxs(p), cm, 1);
}

static V3 pm_clip_velocity(V3 in, V3 normal, F32 overbounce) {
    F32 backoff = v3dot(in, normal);
    if (backoff < 0) backoff *= overbounce;
    else backoff /= overbounce;
    return v3sub(in, v3scale(normal, backoff));
}

/* Q3 PM_CorrectAllSolid: jitter ±1 in each axis to escape solid */
static int pm_correct_allsolid(Player *p, ColMap *cm) {
    for (int i = -1; i <= 1; i++)
    for (int j = -1; j <= 1; j++)
    for (int k = -1; k <= 1; k++) {
        V3 pt = v3(p->pos.x + (F32)i, p->pos.y + (F32)j, p->pos.z + (F32)k);
        Trace tr = pm_trace(pt, pt, p, cm);
        if (!tr.allsolid) {
            /* found non-solid spot; redo ground trace from original pos */
            V3 down = v3add(p->pos, v3(0, -0.25f, 0));
            tr = pm_trace(p->pos, down, p, cm);
            p->ground_normal = tr.normal;
            p->ground_plane = (tr.fraction < 1.f);
            p->on_ground = (tr.fraction < 1.f && tr.normal.y >= MIN_WALK_NORMAL);
            return 1;
        }
    }
    p->on_ground = 0;
    p->ground_plane = 0;
    return 0;
}

/* Q3 PM_GroundTrace: trace 0.25 units down to detect ground */
static void pm_ground_trace(Player *p, ColMap *cm) {
    V3 point = v3add(p->pos, v3(0, -0.25f, 0));
    Trace tr = pm_trace(p->pos, point, p, cm);

    if (tr.allsolid) {
        if (!pm_correct_allsolid(p, cm)) return;
        return; /* pm_correct_allsolid set ground state */
    }

    if (tr.fraction == 1.f) {
        /* free fall — no ground */
        p->ground_plane = 0;
        p->on_ground = 0;
        return;
    }

    /* check if getting thrown off ground */
    if (p->vel.y > 0 && v3dot(p->vel, tr.normal) > 10.f) {
        p->ground_plane = 0;
        p->on_ground = 0;
        return;
    }

    /* steep slope: have ground plane but can't walk on it */
    if (tr.normal.y < MIN_WALK_NORMAL) {
        p->ground_plane = 1;
        p->ground_normal = tr.normal;
        p->on_ground = 0;
        return;
    }

    /* valid walkable ground */
    p->ground_plane = 1;
    p->ground_normal = tr.normal;
    p->on_ground = 1;
}

/* Q3 PM_Friction: ground friction when walking, zero in air */
static void pm_friction(Player *p, F32 dt) {
    V3 vec = p->vel;
    if (p->on_ground) vec.y = 0; /* ignore vertical for speed calc when walking */
    F32 speed = sqrtf(v3dot(vec, vec));
    if (speed < 1.f) {
        p->vel.x = 0;
        p->vel.z = 0;
        /* keep vel.y — allow sinking/falling */
        return;
    }

    F32 drop = 0;
    if (p->on_ground) {
        F32 control = speed < PM_STOPSPEED ? PM_STOPSPEED : speed;
        drop += control * PM_FRICTION * dt;
    }
    /* no air friction in Q3 (drop stays 0 in air) */

    F32 newspeed = speed - drop;
    if (newspeed < 0) newspeed = 0;
    newspeed /= speed;

    p->vel.x *= newspeed;
    p->vel.y *= newspeed;
    p->vel.z *= newspeed;
}

/* Q3 PM_Accelerate: q2-style acceleration (enables strafe-jumping) */
static void pm_accelerate(Player *p, V3 wishdir, F32 wishspeed,
                           F32 accel, F32 dt) {
    F32 currentspeed = v3dot(p->vel, wishdir);
    F32 addspeed = wishspeed - currentspeed;
    if (addspeed <= 0) return;
    F32 accelspeed = accel * dt * wishspeed;
    if (accelspeed > addspeed) accelspeed = addspeed;
    p->vel = v3add(p->vel, v3scale(wishdir, accelspeed));
}

/* Q3 PM_SlideMove: collision response with up to 4 bumps */
static int pm_slide_move(Player *p, ColMap *cm, F32 dt, int gravity) {
    V3 endVelocity = p->vel;

    if (gravity) {
        endVelocity.y -= PM_GRAVITY * dt;
        p->vel.y = (p->vel.y + endVelocity.y) * 0.5f;
        if (p->ground_plane)
            p->vel = pm_clip_velocity(p->vel, p->ground_normal, OVERCLIP);
    }

    F32 time_left = dt;
    V3 planes[MAX_CLIP_PLANES];
    int numplanes = 0;

    if (p->ground_plane)
        planes[numplanes++] = p->ground_normal;

    /* never turn against original velocity */
    { F32 s = sqrtf(v3dot(p->vel, p->vel));
      planes[numplanes++] = s > 0.001f ? v3scale(p->vel, 1.f/s) : v3(0,1,0); }

    for (int bumpcount = 0; bumpcount < 4; bumpcount++) {
        V3 end = v3add(p->pos, v3scale(p->vel, time_left));
        Trace tr = pm_trace(p->pos, end, p, cm);

        if (tr.allsolid) { p->vel.y = 0; return 1; }
        if (tr.fraction > 0) p->pos = tr.endpos;
        if (tr.fraction == 1.f) break;

        time_left -= time_left * tr.fraction;

        if (numplanes >= MAX_CLIP_PLANES) { p->vel = v3(0,0,0); return 1; }

        /* Q3: if same plane as before, nudge velocity along it.
           Relaxed from 0.99 to 0.85: capsule traces can produce varied
           normals at brush seams (bevel planes are designed for AABB). */
        int samePlane = 0;
        for (int i = 0; i < numplanes; i++) {
            if (v3dot(tr.normal, planes[i]) > 0.85f) {
                p->vel = v3add(tr.normal, p->vel);
                samePlane = 1;
                break;
            }
        }
        if (samePlane) continue;

        planes[numplanes++] = tr.normal;

        /* Q3: clip velocity to parallel all clip planes */
        int i;
        for (i = 0; i < numplanes; i++) {
            if (v3dot(p->vel, planes[i]) >= 0.1f) continue;

            V3 cv = pm_clip_velocity(p->vel, planes[i], OVERCLIP);
            V3 ecv = pm_clip_velocity(endVelocity, planes[i], OVERCLIP);

            int j;
            for (j = 0; j < numplanes; j++) {
                if (j == i) continue;
                if (v3dot(cv, planes[j]) >= 0.1f) continue;

                cv = pm_clip_velocity(cv, planes[j], OVERCLIP);
                ecv = pm_clip_velocity(ecv, planes[j], OVERCLIP);

                if (v3dot(cv, planes[i]) >= 0) continue;

                /* slide along crease between planes[i] and planes[j] */
                V3 dv = v3cross(planes[i], planes[j]);
                F32 dlen2 = v3dot(dv, dv);
                if (dlen2 < 0.001f) {
                    /* nearly parallel planes: degenerate crease.
                       Nudge velocity out along latest normal. */
                    cv = v3add(p->vel, tr.normal);
                    ecv = v3add(endVelocity, tr.normal);
                } else {
                    V3 dir = v3scale(dv, 1.f / sqrtf(dlen2));
                    F32 d = v3dot(dir, p->vel);
                    cv = v3scale(dir, d);
                    d = v3dot(dir, endVelocity);
                    ecv = v3scale(dir, d);

                    /* check for triple plane interaction → stop dead */
                    int k;
                    for (k = 0; k < numplanes; k++) {
                        if (k == i || k == j) continue;
                        if (v3dot(cv, planes[k]) >= 0.1f) continue;
                        p->vel = v3(0,0,0); return 1;
                    }
                }
            }

            p->vel = cv;
            endVelocity = ecv;
            break;
        }
    }

    if (gravity) p->vel = endVelocity;

    return 1; /* Q3 returns bumpcount != 0; conservative */
}

/* Q3 PM_StepSlideMove: try slide, then try stepping up */
static void pm_step_slide_move(Player *p, ColMap *cm, F32 dt, int gravity) {
    V3 start_o = p->pos;
    V3 start_v = p->vel;

    if (!pm_slide_move(p, cm, dt, gravity)) return;

    /* check if we should try stepping up */
    V3 down = v3add(start_o, v3(0, -STEPSIZE, 0));
    Trace tr = pm_trace(start_o, down, p, cm);
    /* never step up when you still have up velocity */
    if (p->vel.y > 0 && (tr.fraction == 1.f || tr.normal.y < MIN_WALK_NORMAL))
        return;

    /* try stepping up */
    V3 up = v3add(start_o, v3(0, STEPSIZE, 0));
    tr = pm_trace(start_o, up, p, cm);
    if (tr.allsolid) return;

    F32 stepSize = tr.endpos.y - start_o.y;

    /* from raised position, try slidemove */
    p->pos = tr.endpos;
    p->vel = start_v;
    pm_slide_move(p, cm, dt, gravity);

    /* push back down */
    V3 step_down = v3add(p->pos, v3(0, -stepSize, 0));
    tr = pm_trace(p->pos, step_down, p, cm);
    if (!tr.allsolid) p->pos = tr.endpos;
    if (tr.fraction < 1.f)
        p->vel = pm_clip_velocity(p->vel, tr.normal, OVERCLIP);
}

/* Q3 PM_WalkMove: on-ground movement */
static void pm_walk_move(Player *p, ColMap *cm, Input in, F32 dt) {
    pm_friction(p, dt);

    F32 cy = cosf(p->yaw), sy = sinf(p->yaw);
    V3 fwd = v3(sy, 0, -cy);
    V3 right = v3(cy, 0, sy);

    /* Q3: project forward/right onto ground plane, THEN compute wishvel */
    fwd = v3norm(pm_clip_velocity(fwd, p->ground_normal, OVERCLIP));
    right = v3norm(pm_clip_velocity(right, p->ground_normal, OVERCLIP));

    V3 wish = v3(0, 0, 0);
    if (in.fwd)   wish = v3add(wish, fwd);
    if (in.back)  wish = v3sub(wish, fwd);
    if (in.right) wish = v3add(wish, right);
    if (in.left)  wish = v3sub(wish, right);

    F32 wishspeed = sqrtf(v3dot(wish, wish));
    if (wishspeed > 0.001f) {
        wish = v3scale(wish, 1.f / wishspeed);
        wishspeed = PM_MAXSPEED;
    }
    if (p->ducked) wishspeed *= 0.25f;

    pm_accelerate(p, wish, wishspeed, PM_ACCELERATE, dt);

    /* preserve speed magnitude through ground clip */
    F32 vel = sqrtf(v3dot(p->vel, p->vel));
    p->vel = pm_clip_velocity(p->vel, p->ground_normal, OVERCLIP);
    { F32 nv = sqrtf(v3dot(p->vel, p->vel));
      if (nv > 0.001f) p->vel = v3scale(p->vel, vel / nv); }

    if (p->vel.x == 0 && p->vel.z == 0) return;

    pm_step_slide_move(p, cm, dt, 0);
}

/* Q3 PM_AirMove: airborne movement */
static void pm_air_move(Player *p, ColMap *cm, Input in, F32 dt) {
    pm_friction(p, dt);

    F32 cy = cosf(p->yaw), sy = sinf(p->yaw);
    V3 fwd = v3(sy, 0, -cy);
    V3 right = v3(cy, 0, sy);

    V3 wish = v3(0, 0, 0);
    if (in.fwd)   wish = v3add(wish, fwd);
    if (in.back)  wish = v3sub(wish, fwd);
    if (in.right) wish = v3add(wish, right);
    if (in.left)  wish = v3sub(wish, right);
    wish.y = 0; /* no vertical wish in air */

    F32 wishspeed = sqrtf(v3dot(wish, wish));
    if (wishspeed > 0.001f) {
        wish = v3scale(wish, 1.f / wishspeed);
        wishspeed = PM_MAXSPEED;
    }
    if (p->ducked) wishspeed *= 0.25f;

    pm_accelerate(p, wish, wishspeed, PM_AIRACCELERATE, dt);

    /* slide along steep slopes */
    if (p->ground_plane)
        p->vel = pm_clip_velocity(p->vel, p->ground_normal, OVERCLIP);

    pm_step_slide_move(p, cm, dt, 1);
}

/* Q3 PM_CheckDuck: handle crouch state transitions.
   Capsule bottoms differ by stance: standing = pos.y-32, crouched = pos.y-16.
   Adjust pos.y on transitions to keep feet at the same world height. */
static void pm_check_duck(Player *p, ColMap *cm, Input in) {
    /* height difference between standing/crouched capsule bottoms */
    const F32 dh = 16.f; /* (32-15) - (16-15) = 16 */
    if (in.crouch) {
        if (!p->ducked) {
            p->ducked = 1;
            p->pos.y -= dh; /* lower pos so capsule bottom stays on floor */
        }
    } else if (p->ducked) {
        /* try to stand: test at raised position so standing capsule
           bottom matches our current crouched capsule bottom */
        V3 test = v3add(p->pos, v3(0, dh, 0));
        V3 stand = v3(15, 32, 15);
        Trace tr = cm_trace(test, test, PM_MINS, stand, cm, 1);
        if (!tr.allsolid) {
            p->ducked = 0;
            p->pos = test;
        }
    }
    p->viewheight = p->ducked ? CROUCH_VIEWHEIGHT : DEFAULT_VIEWHEIGHT;
}

/* Top-level per-frame player movement */
static void player_move(Player *p, ColMap *cm, Input in, F32 dt) {
    /* mouse look */
    p->yaw += in.dx * 0.003f;
    p->pitch += in.dy * 0.003f;
    if (p->pitch >  1.4f) p->pitch =  1.4f;
    if (p->pitch < -1.4f) p->pitch = -1.4f;

    /* noclip fallback if no collision data */
    if (!cm || !cm->n_nodes) {
        F32 cy = cosf(p->yaw), sy = sinf(p->yaw);
        V3 fwd = v3(sy, 0, -cy);
        V3 right = v3(cy, 0, sy);
        V3 move = v3(0, 0, 0);
        F32 spd = 320.f;
        if (in.fwd)   move = v3add(move, v3scale(fwd, spd));
        if (in.back)  move = v3add(move, v3scale(fwd, -spd));
        if (in.right) move = v3add(move, v3scale(right, spd));
        if (in.left)  move = v3add(move, v3scale(right, -spd));
        if (in.jump)  move.y += spd;
        p->pos = v3add(p->pos, v3scale(move, dt));
        return;
    }

    /* crouch state */
    pm_check_duck(p, cm, in);

    /* pre-move ground trace */
    pm_ground_trace(p, cm);

    if (p->on_ground) {
        /* Q3: check jump BEFORE friction/movement */
        if (in.jump && !p->jump_held) {
            p->vel.y = JUMP_VELOCITY;
            p->on_ground = 0;
            p->ground_plane = 0;
            pm_air_move(p, cm, in, dt);
        } else {
            pm_walk_move(p, cm, in, dt);
        }
    } else {
        pm_air_move(p, cm, in, dt);
    }

    /* post-move ground trace (Q3 does this) */
    pm_ground_trace(p, cm);

    p->jump_held = in.jump;
}

/* <<scene>> ============================================================= */

static Scene scene_build_test(void) {
#define VX(px,py,pz,u,v,nx,ny,nz) {.pos={px,py,pz},.uv={u,v},.n={nx,ny,nz}}
    static Vtx V[] = {
        VX(-10,0,-10, 0,1, 0,1,0), VX(10,0,-10, 1,1, 0,1,0),
        VX( 10,0, 10, 1,0, 0,1,0), VX(-10,0,10, 0,0, 0,1,0),
        VX(  0,0,  0,.5,.5, 0,0,-1), VX(-2,0,-4, 0,1, 0,0,-1),
        VX(  2,0, -4, 1,1, 0,0,-1), VX( 0,4,-4,.5, 0, 0,0,-1),
        VX(  0,4, -4,.5,.5,-1,0,0), VX(-2,0,-4, 0,1,-1,0,0),
        VX( -2,0,  0, 1,1,-1,0,0),
        VX(  0,4, -4,.5,.5, 1,0,0), VX( 2,0,-4, 0,1, 1,0,0),
        VX(  2,0,  0, 1,1, 1,0,0),
    };
    static U32 I[] = {
        0,1,2,  0,2,3,
        4,5,6,  4,6,7,
        8,9,10,
        11,12,13
    };
    static V4 M[] = {
        {0.55f,0.52f,0.48f,1},
        {0.85f,0.42f,0.15f,1},
        {0.25f,0.60f,0.85f,1},
    };
    static U32 TI[] = {0,0,0,0,0,0}; /* 6 tris, all shader 0 */
    (void)V[0]; (void)I[0]; (void)M[0]; (void)TI[0];
    return (Scene){.verts=V,.nv=14,.idxs=I,.ni=18,.mats=M,.nm=3,
                   .tex_ids=TI,.tex_names=NULL,.tri_count=6};
}

static void textures_load(Ctx *C, const Scene *sc) {
    C->tex_sampler = sampler_create(C->dev);
    C->n_tex = sc->nm;
    C->n_tex_loaded = 0;
    C->tex_imgs  = calloc(C->n_tex, sizeof(VkImage));
    C->tex_mems  = calloc(C->n_tex, sizeof(VkDeviceMemory));
    C->tex_views = calloc(C->n_tex, sizeof(VkImageView));

    for (U32 i = 0; i < C->n_tex; i++) {
        U32 w=0, h=0; U8 *rgba = NULL;
        if (sc->tex_names) {
            char path[256];
            snprintf(path, sizeof(path), "assets/%s.tga", sc->tex_names[i]);
            rgba = tga_load(path, &w, &h);
        }
        if (rgba && w && h) {
            tex_upload(C->dev, C->pd, C->cmd, C->q, rgba, w, h,
                       &C->tex_imgs[i], &C->tex_mems[i], &C->tex_views[i]);
            free(rgba);
            C->n_tex_loaded++;
        } else {
            /* 1x1 fallback from hash color */
            V4 c = sc->mats[i];
            U8 fb[4] = {(U8)(c.x*255),(U8)(c.y*255),(U8)(c.z*255),255};
            tex_upload(C->dev, C->pd, C->cmd, C->q, fb, 1, 1,
                       &C->tex_imgs[i], &C->tex_mems[i], &C->tex_views[i]);
        }
    }

    C->tex_id_buf = buf_stage_upload(C->dev, C->pd, C->cmd, C->q,
        sc->tex_ids, sizeof(U32) * sc->tri_count,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    printf("[tex] loaded %u/%u textures, %u fallbacks\n",
           C->n_tex_loaded, C->n_tex, C->n_tex - C->n_tex_loaded);

    /* upload lightmap atlas (UNORM, clamp sampler) or 1x1 white fallback */
    C->lm_sampler = sampler_clamp_create(C->dev);
    if (sc->lm_atlas && sc->lm_w && sc->lm_h) {
        tex_upload_fmt(C->dev, C->pd, C->cmd, C->q,
                       sc->lm_atlas, sc->lm_w, sc->lm_h,
                       VK_FORMAT_R8G8B8A8_UNORM,
                       &C->lm_img, &C->lm_mem, &C->lm_view);
        printf("[lm] uploaded %ux%u atlas (UNORM)\n", sc->lm_w, sc->lm_h);
    } else {
        U8 white[4] = {255,255,255,255};
        tex_upload_fmt(C->dev, C->pd, C->cmd, C->q, white, 1, 1,
                       VK_FORMAT_R8G8B8A8_UNORM,
                       &C->lm_img, &C->lm_mem, &C->lm_view);
    }
}

static void wpn_textures_load(Ctx *C, Wpn *wpn) {
    /* weapon texture names: body surfaces + barrel fallback to surface 0 */
    const char *wpn_tex_paths[] = {
        "assets/models/weapons2/machinegun/mgun.tga",
        "assets/models/weapons2/machinegun/sight.tga",
    };
    U32 n_wpn_tex = 2;
    wpn->wpn_tex_base = C->n_tex;

    /* grow texture arrays */
    U32 new_total = C->n_tex + n_wpn_tex;
    C->tex_imgs  = realloc(C->tex_imgs,  sizeof(VkImage)        * new_total);
    C->tex_mems  = realloc(C->tex_mems,  sizeof(VkDeviceMemory) * new_total);
    C->tex_views = realloc(C->tex_views, sizeof(VkImageView)    * new_total);

    for (U32 i = 0; i < n_wpn_tex; i++) {
        U32 w=0, h=0;
        U8 *rgba = tga_load(wpn_tex_paths[i], &w, &h);
        if (rgba && w && h) {
            tex_upload(C->dev, C->pd, C->cmd, C->q, rgba, w, h,
                       &C->tex_imgs[C->n_tex], &C->tex_mems[C->n_tex],
                       &C->tex_views[C->n_tex]);
            free(rgba);
            printf("[wpn] loaded tex %s (%ux%u)\n", wpn_tex_paths[i], w, h);
        } else {
            U8 fb[4] = {180, 180, 180, 255};
            tex_upload(C->dev, C->pd, C->cmd, C->q, fb, 1, 1,
                       &C->tex_imgs[C->n_tex], &C->tex_mems[C->n_tex],
                       &C->tex_views[C->n_tex]);
            printf("[wpn] fallback tex for %s\n", wpn_tex_paths[i]);
        }
        C->n_tex++;
    }
    printf("[wpn] textures: base=%u, count=%u\n", wpn->wpn_tex_base, n_wpn_tex);
}

/* <<blas>> ============================================================== */

static AS blas_build(Ctx *C, const Scene *S) {
    C->vbuf = buf_stage_upload(C->dev, C->pd, C->cmd, C->q,
        S->verts, sizeof(Vtx)*S->nv,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
    C->ibuf = buf_stage_upload(C->dev, C->pd, C->cmd, C->q,
        S->idxs, sizeof(U32)*S->ni,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
    C->mbuf = buf_alloc(C->dev, C->pd, sizeof(V4)*S->nm,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    buf_upload(C->dev, C->mbuf, S->mats, sizeof(V4)*S->nm);

    VkAccelerationStructureGeometryKHR geom = {
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .flags=VK_GEOMETRY_OPAQUE_BIT_KHR,
        .geometry.triangles={
            .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat=VK_FORMAT_R32G32B32_SFLOAT,
            .vertexData.deviceAddress=C->vbuf.a,
            .vertexStride=sizeof(Vtx),
            .maxVertex=S->nv-1,
            .indexType=VK_INDEX_TYPE_UINT32,
            .indexData.deviceAddress=C->ibuf.a,
        }
    };
    VkAccelerationStructureBuildGeometryInfoKHR bgi = {
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .geometryCount=1, .pGeometries=&geom
    };
    U32 prim_count = S->tri_count;
    VkAccelerationStructureBuildSizesInfoKHR bsi = {
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    C->rt.ASBuildSizes(C->dev,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bgi, &prim_count, &bsi);

    AS as = {0};
    as.b = buf_alloc(C->dev, C->pd, bsi.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(C->rt.CreateAS(C->dev, &(VkAccelerationStructureCreateInfoKHR){
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer=as.b.b, .size=bsi.accelerationStructureSize,
        .type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
    }, NULL, &as.h));

    Buf scratch = buf_alloc(C->dev, C->pd, bsi.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    bgi.dstAccelerationStructure  = as.h;
    bgi.scratchData.deviceAddress = scratch.a;

    VkAccelerationStructureBuildRangeInfoKHR range = {.primitiveCount=prim_count};
    const VkAccelerationStructureBuildRangeInfoKHR *p_range = &range;

    VK(vkResetCommandBuffer(C->cmd, 0));
    VK(vkBeginCommandBuffer(C->cmd, &(VkCommandBufferBeginInfo){
        .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT}));
    C->rt.CmdBuildAS(C->cmd, 1, &bgi, &p_range);
    VK(vkEndCommandBuffer(C->cmd));
    VK(vkQueueSubmit(C->q, 1, &(VkSubmitInfo){
        .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount=1, .pCommandBuffers=&C->cmd}, VK_NULL_HANDLE));
    VK(vkQueueWaitIdle(C->q));

    as.a = C->rt.ASAddr(C->dev, &(VkAccelerationStructureDeviceAddressInfoKHR){
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure=as.h
    });

    vkDestroyBuffer(C->dev,scratch.b,NULL);
    vkFreeMemory(C->dev,scratch.m,NULL);
    return as;
}

static void wpn_blas_init(Ctx *C, Wpn *w) {
    if (!w->mdl.nv) return;
    w->xverts = malloc(sizeof(Vtx) * w->mdl.nv);
    memcpy(w->xverts, w->mdl.verts, sizeof(Vtx) * w->mdl.nv);

    /* host-visible vertex buffer (updated each frame) */
    w->vbuf = buf_alloc(C->dev, C->pd, sizeof(Vtx) * w->mdl.nv,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    buf_upload(C->dev, w->vbuf, w->xverts, sizeof(Vtx) * w->mdl.nv);

    /* device-local index buffer (uploaded once) */
    w->ibuf = buf_stage_upload(C->dev, C->pd, C->cmd, C->q,
        w->mdl.idxs, sizeof(U32) * w->mdl.ni,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

    /* tex_id buffer */
    w->tid_buf = buf_stage_upload(C->dev, C->pd, C->cmd, C->q,
        w->mdl.tex_ids, sizeof(U32) * w->mdl.ntri,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    /* build initial BLAS with PREFER_FAST_BUILD */
    VkAccelerationStructureGeometryKHR geom = {
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .flags=VK_GEOMETRY_OPAQUE_BIT_KHR,
        .geometry.triangles={
            .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat=VK_FORMAT_R32G32B32_SFLOAT,
            .vertexData.deviceAddress=w->vbuf.a,
            .vertexStride=sizeof(Vtx),
            .maxVertex=w->mdl.nv-1,
            .indexType=VK_INDEX_TYPE_UINT32,
            .indexData.deviceAddress=w->ibuf.a,
        }
    };
    VkAccelerationStructureBuildGeometryInfoKHR bgi = {
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
               VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
        .geometryCount=1, .pGeometries=&geom
    };
    U32 prim_count = w->mdl.ntri;
    VkAccelerationStructureBuildSizesInfoKHR bsi = {
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    C->rt.ASBuildSizes(C->dev,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bgi, &prim_count, &bsi);

    w->blas.b = buf_alloc(C->dev, C->pd, bsi.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(C->rt.CreateAS(C->dev, &(VkAccelerationStructureCreateInfoKHR){
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer=w->blas.b.b, .size=bsi.accelerationStructureSize,
        .type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
    }, NULL, &w->blas.h));

    /* persistent scratch buffer for rebuilds */
    w->blas_scratch = buf_alloc(C->dev, C->pd, bsi.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    bgi.dstAccelerationStructure  = w->blas.h;
    bgi.scratchData.deviceAddress = w->blas_scratch.a;

    VkAccelerationStructureBuildRangeInfoKHR range = {.primitiveCount=prim_count};
    const VkAccelerationStructureBuildRangeInfoKHR *p_range = &range;
    VK(vkResetCommandBuffer(C->cmd, 0));
    VK(vkBeginCommandBuffer(C->cmd, &(VkCommandBufferBeginInfo){
        .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT}));
    C->rt.CmdBuildAS(C->cmd, 1, &bgi, &p_range);
    VK(vkEndCommandBuffer(C->cmd));
    VK(vkQueueSubmit(C->q, 1, &(VkSubmitInfo){
        .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount=1, .pCommandBuffers=&C->cmd}, VK_NULL_HANDLE));
    VK(vkQueueWaitIdle(C->q));

    w->blas.a = C->rt.ASAddr(C->dev, &(VkAccelerationStructureDeviceAddressInfoKHR){
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure=w->blas.h
    });
    printf("[wpn] BLAS built: %u tris\n", prim_count);
}

static void wpn_blas_rebuild(Ctx *C, Wpn *w) {
    if (!w->mdl.nv) return;
    buf_upload(C->dev, w->vbuf, w->xverts, sizeof(Vtx) * w->mdl.nv);

    VkAccelerationStructureGeometryKHR geom = {
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .flags=VK_GEOMETRY_OPAQUE_BIT_KHR,
        .geometry.triangles={
            .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .vertexFormat=VK_FORMAT_R32G32B32_SFLOAT,
            .vertexData.deviceAddress=w->vbuf.a,
            .vertexStride=sizeof(Vtx),
            .maxVertex=w->mdl.nv-1,
            .indexType=VK_INDEX_TYPE_UINT32,
            .indexData.deviceAddress=w->ibuf.a,
        }
    };
    VkAccelerationStructureBuildGeometryInfoKHR bgi = {
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
               VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
        .mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .srcAccelerationStructure=VK_NULL_HANDLE,
        .dstAccelerationStructure=w->blas.h,
        .scratchData.deviceAddress=w->blas_scratch.a,
        .geometryCount=1, .pGeometries=&geom
    };
    VkAccelerationStructureBuildRangeInfoKHR range = {.primitiveCount=w->mdl.ntri};
    const VkAccelerationStructureBuildRangeInfoKHR *p_range = &range;
    VK(vkResetCommandBuffer(C->cmd, 0));
    VK(vkBeginCommandBuffer(C->cmd, &(VkCommandBufferBeginInfo){
        .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT}));
    C->rt.CmdBuildAS(C->cmd, 1, &bgi, &p_range);
    VK(vkEndCommandBuffer(C->cmd));
    VK(vkQueueSubmit(C->q, 1, &(VkSubmitInfo){
        .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount=1, .pCommandBuffers=&C->cmd}, VK_NULL_HANDLE));
    VK(vkQueueWaitIdle(C->q));
}

/* <<tlas>> ============================================================== */

static Buf  tlas_inst_buf;
static Buf  tlas_scratch;

static void tlas_init_prealloc(Ctx *C, U32 max_inst) {
    /* preallocate instance buffer, scratch, and TLAS for per-frame rebuild */
    tlas_inst_buf = buf_alloc(C->dev, C->pd,
        sizeof(VkAccelerationStructureInstanceKHR) * max_inst,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkAccelerationStructureGeometryKHR geom = {
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .flags=VK_GEOMETRY_OPAQUE_BIT_KHR,
        .geometry.instances={
            .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .arrayOfPointers=VK_FALSE,
            .data.deviceAddress=tlas_inst_buf.a
        }
    };
    VkAccelerationStructureBuildGeometryInfoKHR bgi = {
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
        .geometryCount=1, .pGeometries=&geom
    };
    VkAccelerationStructureBuildSizesInfoKHR bsi = {
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    C->rt.ASBuildSizes(C->dev,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bgi, &max_inst, &bsi);

    C->tlas.b = buf_alloc(C->dev, C->pd, bsi.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(C->rt.CreateAS(C->dev, &(VkAccelerationStructureCreateInfoKHR){
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer=C->tlas.b.b, .size=bsi.accelerationStructureSize,
        .type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
    }, NULL, &C->tlas.h));

    tlas_scratch = buf_alloc(C->dev, C->pd, bsi.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    C->tlas.a = C->rt.ASAddr(C->dev, &(VkAccelerationStructureDeviceAddressInfoKHR){
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure=C->tlas.h
    });
}

static void tlas_rebuild(Ctx *C, AS *world_blas, AS *wpn_blas) {
    VkAccelerationStructureInstanceKHR insts[2];
    memset(insts, 0, sizeof(insts));
    /* instance 0: world (mask=0xFF, customIdx=0) */
    insts[0].transform.matrix[0][0] = insts[0].transform.matrix[1][1] =
    insts[0].transform.matrix[2][2] = 1.f;
    insts[0].mask = 0xFF;
    insts[0].instanceCustomIndex = 0;
    insts[0].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    insts[0].accelerationStructureReference = world_blas->a;

    U32 n_inst = 1;
    if (wpn_blas && wpn_blas->h) {
        /* instance 1: weapon (mask=0x01, customIdx=1) — excluded from shadow rays */
        insts[1].transform.matrix[0][0] = insts[1].transform.matrix[1][1] =
        insts[1].transform.matrix[2][2] = 1.f;
        insts[1].mask = 0x01;
        insts[1].instanceCustomIndex = 1;
        insts[1].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        insts[1].accelerationStructureReference = wpn_blas->a;
        n_inst = 2;
    }
    buf_upload(C->dev, tlas_inst_buf, insts,
               sizeof(VkAccelerationStructureInstanceKHR) * n_inst);

    VkAccelerationStructureGeometryKHR geom = {
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .flags=VK_GEOMETRY_OPAQUE_BIT_KHR,
        .geometry.instances={
            .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .arrayOfPointers=VK_FALSE,
            .data.deviceAddress=tlas_inst_buf.a
        }
    };
    VkAccelerationStructureBuildGeometryInfoKHR bgi = {
        .sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type=VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
        .mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .dstAccelerationStructure=C->tlas.h,
        .scratchData.deviceAddress=tlas_scratch.a,
        .geometryCount=1, .pGeometries=&geom
    };
    VkAccelerationStructureBuildRangeInfoKHR range = {.primitiveCount=n_inst};
    const VkAccelerationStructureBuildRangeInfoKHR *p_range = &range;
    VK(vkResetCommandBuffer(C->cmd, 0));
    VK(vkBeginCommandBuffer(C->cmd, &(VkCommandBufferBeginInfo){
        .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT}));
    C->rt.CmdBuildAS(C->cmd, 1, &bgi, &p_range);
    VK(vkEndCommandBuffer(C->cmd));
    VK(vkQueueSubmit(C->q, 1, &(VkSubmitInfo){
        .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount=1, .pCommandBuffers=&C->cmd}, VK_NULL_HANDLE));
    VK(vkQueueWaitIdle(C->q));
}

/* <<pipeline>> ========================================================== */

static void pipeline_rt_create(Ctx *C) {
    VkDescriptorSetLayoutBinding bindings[] = {
        {0,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_RAYGEN_BIT_KHR|VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {2,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_RAYGEN_BIT_KHR|VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {3,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {4,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {5,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {6,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {7,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {8,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},  /* wpn verts */
        {9,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},  /* wpn idxs */
        {10,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR}, /* wpn tex_ids */
        {11,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,256,VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
    };
    VkDescriptorBindingFlags bflags[] = {0,0,0,0,0,0,0,0,0,0,0,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT|VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT};
    VkDescriptorSetLayoutBindingFlagsCreateInfo bfci = {
        .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount=12, .pBindingFlags=bflags
    };
    VK(vkCreateDescriptorSetLayout(C->dev, &(VkDescriptorSetLayoutCreateInfo){
        .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext=&bfci,
        .bindingCount=12, .pBindings=bindings
    }, NULL, &C->dsl));
    VK(vkCreatePipelineLayout(C->dev, &(VkPipelineLayoutCreateInfo){
        .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1, .pSetLayouts=&C->dsl
    }, NULL, &C->pipe_layout));

    VkShaderModule rgen  = shader_load(C->dev, "build/shaders/rgen.spv");
    VkShaderModule rchit = shader_load(C->dev, "build/shaders/rchit.spv");
    VkShaderModule rmiss = shader_load(C->dev, "build/shaders/rmiss.spv");
    VkShaderModule smiss = shader_load(C->dev, "build/shaders/shadow.rmiss.spv");

    VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,NULL,0,VK_SHADER_STAGE_RAYGEN_BIT_KHR,     rgen, "main",NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,NULL,0,VK_SHADER_STAGE_MISS_BIT_KHR,       rmiss,"main",NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,NULL,0,VK_SHADER_STAGE_MISS_BIT_KHR,       smiss,"main",NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,NULL,0,VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,rchit,"main",NULL},
    };
    VkRayTracingShaderGroupCreateInfoKHR groups[] = {
        {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,NULL,
         VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, 0,VK_SHADER_UNUSED_KHR,VK_SHADER_UNUSED_KHR,VK_SHADER_UNUSED_KHR},
        {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,NULL,
         VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, 1,VK_SHADER_UNUSED_KHR,VK_SHADER_UNUSED_KHR,VK_SHADER_UNUSED_KHR},
        {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,NULL,
         VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR, 2,VK_SHADER_UNUSED_KHR,VK_SHADER_UNUSED_KHR,VK_SHADER_UNUSED_KHR},
        {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,NULL,
         VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR, VK_SHADER_UNUSED_KHR,3,VK_SHADER_UNUSED_KHR,VK_SHADER_UNUSED_KHR},
    };
    VK(C->rt.CreateRTPipe(C->dev, VK_NULL_HANDLE, VK_NULL_HANDLE, 1,
        &(VkRayTracingPipelineCreateInfoKHR){
            .sType=VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
            .stageCount=4, .pStages=stages,
            .groupCount=4, .pGroups=groups,
            .maxPipelineRayRecursionDepth=2,
            .layout=C->pipe_layout
        }, NULL, &C->pipe));

    vkDestroyShaderModule(C->dev,rgen,NULL);
    vkDestroyShaderModule(C->dev,rchit,NULL);
    vkDestroyShaderModule(C->dev,rmiss,NULL);
    vkDestroyShaderModule(C->dev,smiss,NULL);
}

static void sbt_create(Ctx *C) {
    U32 handle_sz    = C->rt_props.shaderGroupHandleSize;
    U32 handle_align = C->rt_props.shaderGroupHandleAlignment;
    U32 base_align   = C->rt_props.shaderGroupBaseAlignment;
    U32 stride       = (handle_sz + handle_align - 1) & ~(handle_align - 1);
    if (stride < base_align) stride = base_align;

    U32 n_groups = 4;
    U8 *handles = malloc(handle_sz * n_groups);
    VK(C->rt.RTHandles(C->dev, C->pipe, 0, n_groups, handle_sz*n_groups, handles));

    U32 sbt_sz = stride * n_groups;
    C->sbt = buf_alloc(C->dev, C->pd, sbt_sz,
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    U8 *dst; vkMapMemory(C->dev, C->sbt.m, 0, sbt_sz, 0, (void**)&dst);
    for (U32 i=0;i<n_groups;i++)
        memcpy(dst + i*stride, handles + i*handle_sz, handle_sz);
    vkUnmapMemory(C->dev, C->sbt.m);
    free(handles);

    VkDeviceAddress base = C->sbt.a;
    C->sbt_rgen = (VkStridedDeviceAddressRegionKHR){base+0*stride, stride, stride};
    C->sbt_miss = (VkStridedDeviceAddressRegionKHR){base+1*stride, stride, stride*2};
    C->sbt_hit  = (VkStridedDeviceAddressRegionKHR){base+3*stride, stride, stride};
    C->sbt_call = (VkStridedDeviceAddressRegionKHR){0};
}

/* <<descriptors>> ======================================================= */

static void desc_create(Ctx *C, Wpn *wpn) {
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,7}, /* 4 world + 3 weapon */
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,257}, /* 256 textures + 1 lightmap */
    };
    VK(vkCreateDescriptorPool(C->dev, &(VkDescriptorPoolCreateInfo){
        .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=1, .poolSizeCount=5, .pPoolSizes=sizes
    }, NULL, &C->dp));
    U32 var_count = C->n_tex;
    VkDescriptorSetVariableDescriptorCountAllocateInfo var_alloc = {
        .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount=1, .pDescriptorCounts=&var_count
    };
    VK(vkAllocateDescriptorSets(C->dev, &(VkDescriptorSetAllocateInfo){
        .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext=&var_alloc,
        .descriptorPool=C->dp, .descriptorSetCount=1, .pSetLayouts=&C->dsl
    }, &C->ds));

    VkWriteDescriptorSetAccelerationStructureKHR tlas_write = {
        .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount=1, .pAccelerationStructures=&C->tlas.h
    };
    VkDescriptorImageInfo img_info = {
        .imageView=C->rt_img.v, .imageLayout=VK_IMAGE_LAYOUT_GENERAL
    };
    VkDescriptorBufferInfo cam_info  = {C->cam_ubo.b, 0, C->cam_ubo.sz};
    VkDescriptorBufferInfo vtx_info  = {C->vbuf.b,    0, C->vbuf.sz};
    VkDescriptorBufferInfo idx_info  = {C->ibuf.b,    0, C->ibuf.sz};
    VkDescriptorBufferInfo mat_info  = {C->mbuf.b,    0, C->mbuf.sz};
    VkDescriptorBufferInfo tid_info  = {C->tex_id_buf.b, 0, C->tex_id_buf.sz};
    VkDescriptorBufferInfo wvtx_info = {wpn->vbuf.b,    0, wpn->vbuf.sz};
    VkDescriptorBufferInfo widx_info = {wpn->ibuf.b,    0, wpn->ibuf.sz};
    VkDescriptorBufferInfo wtid_info = {wpn->tid_buf.b,  0, wpn->tid_buf.sz};

    VkDescriptorImageInfo *tex_infos = calloc(C->n_tex, sizeof(VkDescriptorImageInfo));
    for (U32 i = 0; i < C->n_tex; i++) {
        tex_infos[i] = (VkDescriptorImageInfo){
            .sampler=C->tex_sampler,
            .imageView=C->tex_views[i],
            .imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    }

    VkDescriptorImageInfo lm_info = {
        .sampler=C->lm_sampler,
        .imageView=C->lm_view,
        .imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    VkWriteDescriptorSet writes[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,&tlas_write,C->ds,0,0,1,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,C->ds,1,0,1,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,   &img_info},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,C->ds,2,0,1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,  NULL,&cam_info},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,C->ds,3,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  NULL,&vtx_info},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,C->ds,4,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  NULL,&idx_info},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,C->ds,5,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  NULL,&mat_info},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,C->ds,6,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  NULL,&tid_info},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,C->ds,7,0,1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,&lm_info},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,C->ds,8,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  NULL,&wvtx_info},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,C->ds,9,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,  NULL,&widx_info},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,C->ds,10,0,1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL,&wtid_info},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,C->ds,11,0,C->n_tex,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,tex_infos},
    };
    vkUpdateDescriptorSets(C->dev, 12, writes, 0, NULL);
    free(tex_infos);
}

/* <<camera>> ============================================================ */

static void cam_upload(Ctx *C, Cam *cam, F32 fov, U32 wpn_tex_base) {
    M4 view = m4view(cam->pos, cam->yaw, cam->pitch);
    M4 proj = m4persp(fov, (F32)C->W / C->H, 0.1f, 10000.f);
    struct { M4 inv_v, inv_p; U32 frame; U32 wpn_tex_base; F32 pad[2]; } ubo;
    ubo.inv_v = m4inv_ortho(view);
    ubo.inv_p = m4inv_proj(proj);
    ubo.frame = cam->frame;
    ubo.wpn_tex_base = wpn_tex_base;
    buf_upload(C->dev, C->cam_ubo, &ubo, sizeof(ubo));
}

/* <<weapon>> ============================================================ */

static void wpn_update(Wpn *w, const Cam *cam, F32 dt, int fire) {
    if (!w->mdl.nv) return;

    /* fire state machine */
    if (fire && !w->firing) { w->firing = 1; w->fire_t = 0; }
    if (w->firing) {
        w->fire_t += dt * 10.f; /* ~10 fps animation */
        if (w->fire_t >= 6.f) { w->firing = 0; w->fire_t = 0; }
    }
    w->bob_t += dt;

    /* camera basis */
    F32 cy=cosf(cam->yaw), sy=sinf(cam->yaw);
    F32 cp=cosf(cam->pitch), sp=sinf(cam->pitch);
    V3 fwd   = v3(sy*cp, -sp, -cy*cp);
    V3 right = v3norm(v3cross(fwd, v3(0,1,0)));
    V3 up    = v3cross(right, fwd);

    /* viewmodel offset from camera */
    F32 bob_v = sinf(w->bob_t * 3.5f) * 0.4f;
    F32 bob_h = cosf(w->bob_t * 1.7f) * 0.2f;
    F32 recoil = w->firing ? -1.2f * expf(-w->fire_t * 5.f) : 0.f;

    V3 offset = v3add(cam->pos,
                v3add(v3scale(fwd, 8.f + recoil),
                v3add(v3scale(right, 5.f + bob_h),
                      v3scale(up, -5.f + bob_v))));

    /* interpolate tag_weapon animation frame */
    U32 fr = 0;
    if (w->mdl.n_anim_frames > 1) {
        if (w->firing) {
            fr = (U32)w->fire_t;
            if (fr >= w->mdl.n_anim_frames) fr = w->mdl.n_anim_frames - 1;
        }
    }
    const F32 *tag = w->mdl.tag_wpn[fr]; /* origin(3) + axis(9) */

    /* build weapon-to-world rotation: camera_basis * tag_rot_yup * model_vert
       Model verts are Y-up swizzled: X=barrel, Y=up, Z=right.
       Tag axis vectors are Q3 Z-up, need swizzle (x,y,z)→(x,z,-y).
       Camera basis maps model axes to world: X→fwd, Y→up, Z→right.
       Tag rotation in Y-up: T_yup = S * T_q3 * S^(-1) where S is the swizzle.
       Combined: R = CamBasis * T_yup, both row-major. */
    F32 R[9];

    /* swizzle each tag axis to Y-up: (x,y,z)→(x,z,-y) */
    V3 a0 = {tag[3], tag[5], -tag[4]};   /* tag forward (swizzled) */
    V3 a1 = {tag[6], tag[8], -tag[7]};   /* tag left    (swizzled) */
    V3 a2 = {tag[9], tag[11], -tag[10]}; /* tag up      (swizzled) */

    /* T_yup maps Y-up model coords to Y-up tag space:
       v_tag = a0*v_q3.x + a1*v_q3.y + a2*v_q3.z
       where v_q3.x = v_yup.x, v_q3.y = -v_yup.z, v_q3.z = v_yup.y
       → v_tag = a0*v_yup.x + a2*v_yup.y + (-a1)*v_yup.z
       So T_yup columns = [a0 | a2 | -a1] */
    F32 TY[9] = { a0.x, a2.x, -a1.x,
                   a0.y, a2.y, -a1.y,
                   a0.z, a2.z, -a1.z };

    /* camera basis (row-major): columns = fwd, up, right
       maps Y-up model space (X=fwd, Y=up, Z=right) to world */
    F32 CB[9] = { fwd.x, up.x, right.x,
                   fwd.y, up.y, right.y,
                   fwd.z, up.z, right.z };

    /* R = CB * TY */
    for (int i=0;i<3;i++) for (int j=0;j<3;j++) {
        R[i*3+j] = CB[i*3+0]*TY[0*3+j] + CB[i*3+1]*TY[1*3+j] + CB[i*3+2]*TY[2*3+j];
    }

    /* scale viewmodel down slightly for better FPS feel */
    F32 sc = 0.7f;

    /* transform each vertex */
    for (U32 i = 0; i < w->mdl.nv; i++) {
        F32 px = w->mdl.verts[i].pos[0] * sc;
        F32 py = w->mdl.verts[i].pos[1] * sc;
        F32 pz = w->mdl.verts[i].pos[2] * sc;
        w->xverts[i].pos[0] = R[0]*px + R[1]*py + R[2]*pz + offset.x;
        w->xverts[i].pos[1] = R[3]*px + R[4]*py + R[5]*pz + offset.y;
        w->xverts[i].pos[2] = R[6]*px + R[7]*py + R[8]*pz + offset.z;
        /* transform normals */
        F32 nx = w->mdl.verts[i].n[0], ny = w->mdl.verts[i].n[1], nz = w->mdl.verts[i].n[2];
        w->xverts[i].n[0] = R[0]*nx + R[1]*ny + R[2]*nz;
        w->xverts[i].n[1] = R[3]*nx + R[4]*ny + R[5]*nz;
        w->xverts[i].n[2] = R[6]*nx + R[7]*ny + R[8]*nz;
        /* copy UVs through */
        w->xverts[i].uv[0] = w->mdl.verts[i].uv[0];
        w->xverts[i].uv[1] = w->mdl.verts[i].uv[1];
    }
}

/* <<input>> ============================================================= */

static Input poll_input(Ctx *C) {
    Input in = {0};
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type==SDL_QUIT) C->quit=1;
        if (e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE) C->quit=1;
        if (e.type==SDL_MOUSEMOTION) {
            in.dx += e.motion.xrel;
            in.dy += e.motion.yrel;
        }
        if (e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT)
            in.fire = 1;
    }
    const U8 *k = SDL_GetKeyboardState(NULL);
    in.fwd   = k[SDL_SCANCODE_W]||k[SDL_SCANCODE_UP];
    in.back  = k[SDL_SCANCODE_S]||k[SDL_SCANCODE_DOWN];
    in.left  = k[SDL_SCANCODE_A]||k[SDL_SCANCODE_LEFT];
    in.right = k[SDL_SCANCODE_D]||k[SDL_SCANCODE_RIGHT];
    in.jump  = k[SDL_SCANCODE_SPACE];
    in.crouch = k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_C];
    return in;
}

/* <<render>> ============================================================ */

static void rt_frame(Ctx *C) {
    VK(vkWaitForFences(C->dev,1,&C->fence,VK_TRUE,UINT64_MAX));
    U32 img_idx;
    VK(vkAcquireNextImageKHR(C->dev,C->sc,UINT64_MAX,C->sem_img,VK_NULL_HANDLE,&img_idx));
    VK(vkResetFences(C->dev,1,&C->fence));
    VK(vkResetCommandBuffer(C->cmd,0));
    VK(vkBeginCommandBuffer(C->cmd,&(VkCommandBufferBeginInfo){
        .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}));

    vkCmdBindPipeline(C->cmd,VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,C->pipe);
    vkCmdBindDescriptorSets(C->cmd,VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        C->pipe_layout,0,1,&C->ds,0,NULL);

    C->rt.CmdTrace(C->cmd,
        &C->sbt_rgen, &C->sbt_miss, &C->sbt_hit, &C->sbt_call,
        C->W, C->H, 1);

    img_barrier(C->cmd, C->rt_img.i,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT);
    img_barrier(C->cmd, C->sc_img[img_idx],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    vkCmdBlitImage(C->cmd,
        C->rt_img.i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        C->sc_img[img_idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &(VkImageBlit){
            .srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
            .srcOffsets[1]={C->W,C->H,1},
            .dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
            .dstOffsets[1]={(int)C->sc_ext.width,(int)C->sc_ext.height,1}
        }, VK_FILTER_LINEAR);

    img_barrier(C->cmd, C->rt_img.i,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);
    img_barrier(C->cmd, C->sc_img[img_idx],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_TRANSFER_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    VK(vkEndCommandBuffer(C->cmd));

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    VK(vkQueueSubmit(C->q, 1, &(VkSubmitInfo){
        .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount=1, .pWaitSemaphores=&C->sem_img,
        .pWaitDstStageMask=&wait_stage,
        .commandBufferCount=1, .pCommandBuffers=&C->cmd,
        .signalSemaphoreCount=1, .pSignalSemaphores=&C->sem_done
    }, C->fence));

    VK(vkQueuePresentKHR(C->q, &(VkPresentInfoKHR){
        .sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount=1, .pWaitSemaphores=&C->sem_done,
        .swapchainCount=1, .pSwapchains=&C->sc, .pImageIndices=&img_idx
    }));
    vkQueueWaitIdle(C->q);
}

/* <<validate>> ========================================================== */

static void export_validate_json(const Ctx *C, const Scene *S, const char *map) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(C->pd, &props);

    FILE *f = fopen("build/validate.json","w");
    fprintf(f,"{\n");
    fprintf(f,"  \"stage\": 3,\n");
    fprintf(f,"  \"map\": \"%s\",\n", map ? map : "test");
    fprintf(f,"  \"device\": \"%s\",\n", props.deviceName);
    fprintf(f,"  \"rt_handle_size\": %u,\n", C->rt_props.shaderGroupHandleSize);
    fprintf(f,"  \"rt_max_recursion\": %u,\n", C->rt_props.maxRayRecursionDepth);
    fprintf(f,"  \"scene\": {\n");
    fprintf(f,"    \"verts\": %u,\n", S->nv);
    fprintf(f,"    \"tris\": %u,\n",  S->tri_count);
    fprintf(f,"    \"mats\": %u,\n",  S->nm);
    fprintf(f,"    \"textures_loaded\": %u,\n", C->n_tex_loaded);
    fprintf(f,"    \"textures_fallback\": %u\n", C->n_tex - C->n_tex_loaded);
    fprintf(f,"  },\n");
    fprintf(f,"  \"render\": {\n");
    fprintf(f,"    \"width\": %d,\n", C->W);
    fprintf(f,"    \"height\": %d,\n", C->H);
    fprintf(f,"    \"sc_images\": %u\n", C->sc_n);
    fprintf(f,"  }\n");
    fprintf(f,"}\n");
    fclose(f);
    printf("[validate] build/validate.json written\n");
}

/* <<main>> ============================================================== */

int main(int argc, char **argv) {
    const char *map = argc > 1 ? argv[1] : NULL;
    const int W=1280, H=720;

    SDL_Init(SDL_INIT_VIDEO);
    Ctx C = {.W=W,.H=H};
    C.win = SDL_CreateWindow("Quake3 RT",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
        SDL_WINDOW_VULKAN|SDL_WINDOW_SHOWN);

    vk_create_instance(&C);
    vk_pick_device(&C);
    vk_create_device(&C);
    vk_create_swapchain(&C);
    vk_create_sync(&C);

    C.rt_img  = img_storage(C.dev, C.pd, W, H);
    C.cam_ubo = buf_alloc(C.dev, C.pd, 144,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vk_transition_storage_image(&C);

    Spawn spawn = {.origin={0,2,8}, .angle=0};
    ColMap col = {0};
    Scene sc  = map ? scene_load_bsp(map, &spawn, &col) : scene_build_test();
    textures_load(&C, &sc);

    /* load weapon model + textures */
    Wpn wpn = {0};
    wpn.mdl = weapon_load();
    if (wpn.mdl.nv) {
        wpn_textures_load(&C, &wpn);
        wpn_blas_init(&C, &wpn);
    } else {
        /* create tiny dummy buffers so descriptor bindings 8-10 are valid */
        Vtx dummy_v = {0}; U32 dummy_i[3] = {0,0,0}; U32 dummy_t = 0;
        wpn.vbuf = buf_alloc(C.dev, C.pd, sizeof(Vtx),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        buf_upload(C.dev, wpn.vbuf, &dummy_v, sizeof(dummy_v));
        wpn.ibuf = buf_stage_upload(C.dev, C.pd, C.cmd, C.q,
            dummy_i, sizeof(dummy_i), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        wpn.tid_buf = buf_stage_upload(C.dev, C.pd, C.cmd, C.q,
            &dummy_t, sizeof(dummy_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    }

    C.blas = blas_build(&C, &sc);

    /* preallocate TLAS for 2 instances and do initial build */
    tlas_init_prealloc(&C, 2);
    tlas_rebuild(&C, &C.blas, wpn.mdl.nv ? &wpn.blas : NULL);

    pipeline_rt_create(&C);
    sbt_create(&C);
    desc_create(&C, &wpn);

    export_validate_json(&C, &sc, map);

    Player player = {0};
    player.viewheight = DEFAULT_VIEWHEIGHT;
    F32 smooth_cam_y = 0; /* smoothed camera Y — initialized after spawn */
    int cam_y_init = 0;
    if (map) {
        player.yaw = (90.f - spawn.angle) * (F32)M_PI / 180.f;

        /* place player at spawn, nudge up if inside solid, then drop to floor */
        {
            V3 stand = v3(15, 32, 15);
            player.pos = spawn.origin;
            /* nudge upward out of solid (capsule may clip into floor at spawn) */
            for (int i = 0; i < 128; i++) {
                Trace t = cm_trace(player.pos, player.pos, PM_MINS, stand, &col, 1);
                if (!t.allsolid) break;
                player.pos.y += 1.f;
            }
            /* drop to floor from current valid position */
            V3 down = v3add(player.pos, v3(0, -256, 0));
            Trace drop = cm_trace(player.pos, down, PM_MINS, stand, &col, 1);
            if (drop.fraction < 1.f && !drop.allsolid)
                player.pos = drop.endpos;
            printf("[spawn] placed at (%.1f, %.1f, %.1f)\n",
                   player.pos.x, player.pos.y, player.pos.z);
        }
    } else {
        player.pos = v3(0, 2 - DEFAULT_VIEWHEIGHT, 8);
    }

    SDL_SetRelativeMouseMode(SDL_TRUE);
    U64 t0    = SDL_GetTicks64();
    U32 frame = 0;

    while (!C.quit) {
        U64 t1 = SDL_GetTicks64();
        C.dt   = (t1 - t0) * 0.001f;
        t0     = t1;

        Input in = poll_input(&C);
        player_move(&player, map ? &col : NULL, in, C.dt);

        /* smooth camera Y: absorbs stair steps, crouch transitions, etc. */
        {
            F32 target = player.pos.y + player.viewheight;
            if (!cam_y_init) { smooth_cam_y = target; cam_y_init = 1; }
            F32 diff = target - smooth_cam_y;
            if (diff < -48.f)
                smooth_cam_y = target; /* big fall — snap, don't float */
            else
                smooth_cam_y += diff * fminf(12.f * C.dt, 1.f);
        }

        Cam cam = { .pos = v3(player.pos.x, smooth_cam_y, player.pos.z),
                    .yaw = player.yaw, .pitch = player.pitch,
                    .frame = frame++ };

        if (wpn.mdl.nv) {
            wpn_update(&wpn, &cam, C.dt, in.fire);
            wpn_blas_rebuild(&C, &wpn);
            tlas_rebuild(&C, &C.blas, &wpn.blas);
        }

        cam_upload(&C, &cam, 90.f, wpn.wpn_tex_base);
        rt_frame(&C);
    }

    vkDeviceWaitIdle(C.dev);
    SDL_DestroyWindow(C.win);
    SDL_Quit();
    return 0;
}
