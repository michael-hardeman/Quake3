/* <<quake3>> = <<types>> <<math>> <<vulkan_raytracing_functions>>
               <<vulkan_utilities>> <<tga_loader>> <<md3_loader>>
               <<vulkan_init>> <<bsp_loader>> <<bsp_entities>>
               <<collision>> <<physics>> <<scene>>
               <<bottom_level_acceleration>> <<top_level_acceleration>>
               <<pipeline>> <<descriptors>> <<camera>> <<weapon>>
               <<input>> <<render>> <<validate>> <<main>> <<shaders>>  */

// Language Extensions
#include <iso646.h>

// Media Layer/Graphics
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

// Standard Libraries
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>

/* <<types>> ================================================================================================ */

// Comment here !!!
typedef uint8_t  U8;
typedef uint16_t U16;
typedef int16_t  I16;
typedef int32_t  I32;
typedef uint32_t U32;
typedef uint64_t U64;
typedef float    F32;

// Three-component floating-point vector for positions, directions, and velocities
typedef struct {F32 x, y, z;    } V3;

// Four-component floating-point vector for homogeneous coordinates and RGBA colors
typedef struct {F32 x, y, z, w; } V4;

// A 4x4 column-major matrix for view, projection, and model transforms
typedef struct {F32 Elements[16]; } M4;

// Sampled keyboard and mouse state for a single frame
typedef struct {
  int   Forward, Back, Left, Right, Jump, Fire, Crouch;  // Binary key states: 1 if held, 0 otherwise
  float Delta_X, Delta_Y;                                // Mouse displacement in pixels since last frame
} Input;

// GPU-resident buffer with its backing memory and optional device address
typedef struct {
  VkBuffer        Buffer;   // Vulkan buffer handle
  VkDeviceMemory  Memory;   // Device memory allocation backing the buffer
  VkDeviceAddress Address;  // Buffer device address for shader access (zero if not requested)
  U64             Size;     // Allocation size in bytes
} Gpu_Buffer;

// GPU-resident image with its backing memory, view, and format metadata
typedef struct {
  VkImage        Image;   // Vulkan image handle
  VkDeviceMemory Memory;  // Device memory allocation backing the image
  VkImageView    View;    // Image view used for sampling or storage access
  VkFormat       Format;  // Pixel format of the image
} Gpu_Image;

// Ray tracing acceleration structure with its backing buffer and device address
typedef struct {
  VkAccelerationStructureKHR Handle;  // Opaque acceleration structure handle
  Gpu_Buffer                 Buffer;  // GPU buffer holding the acceleration structure data
  VkDeviceAddress            Address; // Device address for referencing from shaders and TLAS builds
} Acceleration_Structure;

// Per-frame camera state uploaded to the GPU as a uniform buffer
typedef struct {
  V3  Position, Velocity;                   // World-space eye position and movement velocity
  F32 Yaw, Pitch;                           // Euler angles in radians for horizontal and vertical look
  M4  Inverse_View, Inverse_Projection;     // Inverse matrices for reconstructing world rays from screen coordinates
  U32 Frame;                                // Monotonically increasing frame counter for temporal effects
} Camera;

// Interleaved vertex layout matching the GPU shader input (std430, 48 bytes per vertex)
typedef struct {
  F32 Position[3],   Padding_A;      // World-space XYZ position; padding aligns to 16 bytes
  F32 Texture_Uv[2], Lightmap_Uv[2]; // Diffuse texture coordinates and lightmap atlas coordinates
  F32 Normal[3],     Padding_B;      // Surface normal; padding aligns to 16 bytes
} Vertex;

// Aggregate scene geometry and material data loaded from a BSP or generated procedurally
typedef struct {
  Vertex  *Vertices;       U32 Vertex_Count;       // Vertex array and its element count
  U32     *Indices;        U32 Index_Count;         // Index array (triangles) and its element count
  V4      *Materials;      U32 Material_Count;      // Per-surface RGBA material tints and their count
  U32     *Texture_Ids;                             // Per-triangle texture index into the texture array
  char   (*Texture_Names)[64];                      // Shader/texture name strings from the BSP (64-char max each)
  U32      Triangle_Count;                          // Total triangles (Index_Count / 3)
  U8      *Lightmap_Atlas;                          // Packed lightmap atlas in RGBA8 format
  U32      Lightmap_Width, Lightmap_Height;         // Atlas dimensions in pixels
} Scene;

// Single spawn point parsed from the BSP entity lump
typedef struct {V3 Origin; F32 Angle; } Spawn;     // World-space origin and facing angle in degrees

// Resolved function pointers for Vulkan ray tracing extension commands
typedef struct {
  PFN_vkCreateAccelerationStructureKHR               Create_Acceleration_Structure;   // Creates a BLAS or TLAS
  PFN_vkDestroyAccelerationStructureKHR              Destroy_Acceleration_Structure;  // Destroys an acceleration structure
  PFN_vkGetAccelerationStructureBuildSizesKHR        Get_Build_Sizes;                 // Queries scratch and result buffer sizes
  PFN_vkCmdBuildAccelerationStructuresKHR            Command_Build;                   // Records a build or update command
  PFN_vkGetAccelerationStructureDeviceAddressKHR     Get_Device_Address;              // Retrieves the device address of a structure
  PFN_vkCreateRayTracingPipelinesKHR                 Create_Pipeline;                 // Creates a ray tracing pipeline
  PFN_vkGetRayTracingShaderGroupHandlesKHR           Get_Shader_Group_Handles;        // Retrieves shader group handles for the SBT
  PFN_vkCmdTraceRaysKHR                              Command_Trace_Rays;              // Records a ray dispatch command
} Raytracing_Functions;

// Central rendering context holding all Vulkan state, GPU resources, and synchronization objects
typedef struct {
  SDL_Window       *Window;                 // SDL window for presentation and input
  int               Width, Height;          // Window dimensions in pixels
  VkInstance        Instance;               // Vulkan instance with validation layers
  VkSurfaceKHR     Surface;                // Window surface for presentation
  VkPhysicalDevice Physical_Device;         // Selected GPU with ray tracing support
  VkDevice         Device;                  // Logical device created from the physical device
  VkQueue          Queue;                   // Universal queue for graphics, compute, and transfer
  U32              Queue_Family_Index;       // Index of the queue family supporting all operations

  // Swapchain state
  VkSwapchainKHR   Swapchain;              // Presentation swapchain
  VkImage          Swapchain_Images[8];    // Swapchain image handles (up to 8 for triple+ buffering)
  VkImageView      Swapchain_Views[8];     // Image views corresponding to each swapchain image
  U32              Swapchain_Image_Count;   // Actual number of swapchain images acquired
  VkFormat         Swapchain_Format;        // Surface format of the swapchain (e.g. B8G8R8A8_SRGB)
  VkExtent2D       Swapchain_Extent;        // Swapchain resolution in pixels

  // Command recording and CPU-GPU synchronization
  VkCommandPool    Command_Pool;            // Command pool for allocating command buffers
  VkCommandBuffer  Command_Buffer;          // Single reusable command buffer for all GPU work
  VkFence          Fence;                   // CPU-GPU synchronization fence for frame serialization
  VkSemaphore      Semaphore_Image_Available;   // Signals when a swapchain image is ready
  VkSemaphore      Semaphore_Render_Finished;   // Signals when rendering is complete for presentation

  // Ray tracing extension state
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR Raytracing_Properties;  // SBT alignment and handle sizes
  Raytracing_Functions                            Raytracing;             // Resolved ray tracing function pointers

  // GPU storage images and scene data buffers
  Gpu_Image        Raytracing_Storage_Image;    // Storage image written by ray generation shader
  Gpu_Buffer       Camera_Uniform_Buffer;       // Uniform buffer for the Camera struct
  Gpu_Buffer       Vertex_Buffer, Index_Buffer, Material_Buffer;  // Scene geometry and material data on GPU
  Gpu_Buffer       Texture_Id_Buffer;           // Per-triangle texture index buffer

  // Diffuse texture array
  VkImage         *Texture_Images;          // Array of diffuse texture images
  VkDeviceMemory  *Texture_Memories;        // Backing memory for each texture image
  VkImageView     *Texture_Views;           // Image views for shader sampling of each texture
  VkSampler        Texture_Sampler;         // Shared sampler with linear filtering and repeat wrap
  U32              Texture_Count;           // Total number of texture slots allocated
  U32              Textures_Loaded;         // Number of textures successfully loaded from disk

  // Lightmap atlas
  VkImage          Lightmap_Image;          // Packed lightmap atlas image
  VkDeviceMemory   Lightmap_Memory;         // Backing memory for the lightmap image
  VkImageView      Lightmap_View;           // Image view for lightmap sampling
  VkSampler        Lightmap_Sampler;        // Sampler for lightmap lookups (linear, clamp-to-edge)

  // Acceleration structures
  Acceleration_Structure Bottom_Level, Top_Level;   // BLAS for world geometry and TLAS combining all instances

  // Ray tracing pipeline and shader binding table
  VkPipelineLayout Pipeline_Layout;             // Pipeline layout with descriptor set bindings
  VkPipeline       Pipeline;                    // Ray tracing pipeline (rgen, rchit, rmiss, shadow rmiss)
  Gpu_Buffer       Shader_Binding_Table_Buffer; // Buffer holding the shader binding table

  // Shader binding table regions (one per shader stage)
  VkStridedDeviceAddressRegionKHR Shader_Binding_Ray_Generation;  // SBT region for the ray generation shader
  VkStridedDeviceAddressRegionKHR Shader_Binding_Miss;            // SBT region for miss shaders
  VkStridedDeviceAddressRegionKHR Shader_Binding_Hit;             // SBT region for closest-hit shaders
  VkStridedDeviceAddressRegionKHR Shader_Binding_Callable;        // SBT region for callable shaders (unused)

  // Descriptor set
  VkDescriptorSetLayout Descriptor_Set_Layout;  // Layout describing all 12 descriptor bindings
  VkDescriptorPool      Descriptor_Pool;        // Pool from which the single descriptor set is allocated
  VkDescriptorSet       Descriptor_Set;         // Descriptor set binding all resources to the pipeline

  // Application state
  int Quit;        // Non-zero when the application should exit
  F32 Delta_Time;  // Time elapsed since the previous frame in seconds
} Vulkan_Context;

/* <<math>> ================================================================================================= */

V3 V3_Make (F32 x, F32 y, F32 z) {
  return (V3){x, y, z };
}

V3 V3_Add (V3 Left, V3 Right) {
  return V3_Make (Left.x + Right.x, Left.y + Right.y, Left.z + Right.z);
}

V3 V3_Subtract (V3 Left, V3 Right) {
  return V3_Make (Left.x - Right.x, Left.y - Right.y, Left.z - Right.z);
}

V3 V3_Scale (V3 Vector, F32 Scalar) {
  return V3_Make (Vector.x * Scalar, Vector.y * Scalar, Vector.z * Scalar);
}

F32 V3_Dot (V3 Left, V3 Right) {
  return Left.x * Right.x + Left.y * Right.y + Left.z * Right.z;
}

V3 V3_Cross (V3 Left, V3 Right) {
  return V3_Make (/*x =>*/ Left.y * Right.z - Left.z * Right.y,
                  /*y =>*/ Left.z * Right.x - Left.x * Right.z,
                  /*z =>*/ Left.x * Right.y - Left.y * Right.x);
}

V3 V3_Normalize (V3 Vector) {
  F32 Length = sqrtf (V3_Dot (Vector, Vector));
  return Length > 1e-6f ? V3_Scale (Vector, 1.f / Length) : Vector;
}

M4 M4_Identity (void) {
  M4 Result = {0};
  Result.Elements[0]  = 1;
  Result.Elements[5]  = 1;
  Result.Elements[10] = 1;
  Result.Elements[15] = 1;
  return Result;
}

/* Construct a reversed-depth perspective matrix from vertical field-of-view in degrees,
   aspect ratio, and near/far clip distances.  The Y axis is flipped for Vulkan conventions. */

M4 M4_Perspective (F32 Fovy_Degrees, F32 Aspect, F32 Near, F32 Far) {
  F32 Focal_Length = 1.f / tanf (Fovy_Degrees * (float)M_PI / 360.f);

  // Populate the matrix elements for reversed-depth Vulkan projection
  M4 Result = {0};
  Result.Elements[0]  = Focal_Length / Aspect;
  Result.Elements[5]  = -Focal_Length;
  Result.Elements[10] = Far / (Near - Far);
  Result.Elements[11] = -1;
  Result.Elements[14] = Near * Far / (Near - Far);
  return Result;
}

/* Build a view matrix from a world-space position and Euler yaw/pitch angles.
   The forward vector points along yaw with pitch elevation; the up vector is derived
   from the cross product of the right and forward vectors. */

M4 M4_View (V3 Position, F32 Yaw, F32 Pitch) {
  F32 Cosine_Yaw   = cosf (Yaw);
  F32 Sine_Yaw     = sinf (Yaw);
  F32 Cosine_Pitch = cosf (Pitch);
  F32 Sine_Pitch   = sinf (Pitch);

  // Derive the camera's orthonormal basis from yaw and pitch
  V3 Forward = V3_Make (Sine_Yaw * Cosine_Pitch, -Sine_Pitch, -Cosine_Yaw * Cosine_Pitch);
  V3 Right   = V3_Normalize (V3_Cross (Forward, V3_Make (0, 1, 0)));
  V3 Up      = V3_Cross (Right, Forward);

  // Populate the rotation portion of the view matrix (transposed basis)
  M4 Result = {0};
  Result.Elements[0]  = Right.x;
  Result.Elements[4]  = Right.y;
  Result.Elements[8]  = Right.z;
  Result.Elements[1]  = Up.x;
  Result.Elements[5]  = Up.y;
  Result.Elements[9]  = Up.z;
  Result.Elements[2]  = -Forward.x;
  Result.Elements[6]  = -Forward.y;
  Result.Elements[10] = -Forward.z;

  // Compute the translation component as the negated dot of each basis vector with the position
  Result.Elements[12] = -(Result.Elements[0] * Position.x + Result.Elements[4] * Position.y + Result.Elements[8]  * Position.z);
  Result.Elements[13] = -(Result.Elements[1] * Position.x + Result.Elements[5] * Position.y + Result.Elements[9]  * Position.z);
  Result.Elements[14] = -(Result.Elements[2] * Position.x + Result.Elements[6] * Position.y + Result.Elements[10] * Position.z);
  Result.Elements[15] = 1;
  return Result;
}

/* Invert an orthogonal matrix (rotation + translation only) by transposing the 3x3
   rotation block and recomputing the translation as the negated rotated original translation. */

M4 M4_Inverse_Orthogonal (M4 Source) {
  M4 Result = {0};

  // Transpose the upper-left 3x3 rotation block
  for (int Row = 0; Row < 3; Row++)
    for (int Column = 0; Column < 3; Column++)
      Result.Elements[Row * 4 + Column] = Source.Elements[Column * 4 + Row];

  // Recompute translation as the negated product of the transposed rotation and the original translation
  Result.Elements[12] = -(Result.Elements[0]  * Source.Elements[12] + Result.Elements[4]  * Source.Elements[13] + Result.Elements[8]  * Source.Elements[14]);
  Result.Elements[13] = -(Result.Elements[1]  * Source.Elements[12] + Result.Elements[5]  * Source.Elements[13] + Result.Elements[9]  * Source.Elements[14]);
  Result.Elements[14] = -(Result.Elements[2]  * Source.Elements[12] + Result.Elements[6]  * Source.Elements[13] + Result.Elements[10] * Source.Elements[14]);
  Result.Elements[15] = 1;
  return Result;
}

/* Analytically invert a perspective projection matrix by exploiting its known sparse structure.
   Only the non-zero elements are inverted; all others remain zero. */

M4 M4_Inverse_Projection (M4 Projection) {
  M4 Result = {0};
  Result.Elements[0]  = 1.f / Projection.Elements[0];
  Result.Elements[5]  = 1.f / Projection.Elements[5];
  Result.Elements[11] = 1.f / Projection.Elements[14];
  Result.Elements[14] = 1.f / Projection.Elements[11];
  Result.Elements[15] = -Projection.Elements[10] / (Projection.Elements[11] * Projection.Elements[14]);
  return Result;
}

/* <<vulkan_raytracing_functions>> ========================================================================== */

#define DEVICE_PROC(Device, Type, Name) (PFN_##Type)vkGetDeviceProcAddr (Device, #Name)

/* Load all ray tracing extension function pointers from the logical device.  These are not part of
   the Vulkan core and must be resolved at runtime via vkGetDeviceProcAddr. */

Raytracing_Functions Raytracing_Functions_Load (VkDevice Device) {
  return (Raytracing_Functions){
    .Create_Acceleration_Structure  = DEVICE_PROC (Device, vkCreateAccelerationStructureKHR,              vkCreateAccelerationStructureKHR),
    .Destroy_Acceleration_Structure = DEVICE_PROC (Device, vkDestroyAccelerationStructureKHR,             vkDestroyAccelerationStructureKHR),
    .Get_Build_Sizes                = DEVICE_PROC (Device, vkGetAccelerationStructureBuildSizesKHR,       vkGetAccelerationStructureBuildSizesKHR),
    .Command_Build                  = DEVICE_PROC (Device, vkCmdBuildAccelerationStructuresKHR,           vkCmdBuildAccelerationStructuresKHR),
    .Get_Device_Address             = DEVICE_PROC (Device, vkGetAccelerationStructureDeviceAddressKHR,    vkGetAccelerationStructureDeviceAddressKHR),
    .Create_Pipeline                = DEVICE_PROC (Device, vkCreateRayTracingPipelinesKHR,                vkCreateRayTracingPipelinesKHR),
    .Get_Shader_Group_Handles       = DEVICE_PROC (Device, vkGetRayTracingShaderGroupHandlesKHR,         vkGetRayTracingShaderGroupHandlesKHR),
    .Command_Trace_Rays             = DEVICE_PROC (Device, vkCmdTraceRaysKHR,                            vkCmdTraceRaysKHR),
  };
}

/* <<vulkan_utilities>> ===================================================================================== */

#define VK_CHECK(Function_Call)                                                                               \
  do {                                                                                                       \
    VkResult Vulkan_Result = (Function_Call);                                                                  \
    if (Vulkan_Result) {                                                                                     \
      fprintf (stderr, "VK %d @%d\n", Vulkan_Result, __LINE__);                                               \
      exit (1);                                                                                               \
    }                                                                                                         \
  } while (0)

/* Search the physical device's memory heaps for a memory type index that satisfies both the
   type bitmask (from memory requirements) and the desired property flags (host-visible, device-local, etc). */

U32 Find_Memory_Type (VkPhysicalDevice Physical_Device, U32 Type_Bits, VkMemoryPropertyFlags Desired_Properties) {
  VkPhysicalDeviceMemoryProperties Memory_Properties;
  vkGetPhysicalDeviceMemoryProperties (Physical_Device, &Memory_Properties);

  // Test each memory type against the required bits and desired property flags
  for (U32 Index = 0; Index < Memory_Properties.memoryTypeCount; Index++) {
    if ((Type_Bits >> Index & 1) and (Memory_Properties.memoryTypes[Index].propertyFlags & Desired_Properties) == Desired_Properties)
      return Index;
  }

  // No matching memory type found (should be unreachable on a conformant driver)
  assert (0 and "no matching memory type");
  return 0;
}

/* Allocate a GPU buffer with the given size, usage flags, and memory properties.  If the usage includes
   shader device address, the allocation is flagged accordingly and the device address is queried. */

Gpu_Buffer Buffer_Allocate (VkDevice Device, VkPhysicalDevice Physical_Device, U64 Size,
                                   VkBufferUsageFlags Usage, VkMemoryPropertyFlags Memory_Flags) {
  Gpu_Buffer Result = {.Size = Size };

  // Create the buffer object with the requested size and usage
  VK_CHECK (vkCreateBuffer (/*device      =>*/ Device,
                            /*pCreateInfo =>*/ &(VkBufferCreateInfo){
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size  = Size,
      .usage = Usage,
    },
                            /*pAllocator  =>*/ NULL,
                            /*pBuffer     =>*/ &Result.Buffer));

  // Query how much memory this buffer actually requires and which memory types are compatible
  VkMemoryRequirements Memory_Requirements;
  vkGetBufferMemoryRequirements (Device, Result.Buffer, &Memory_Requirements);

  // If the buffer needs a device address, pass the device-address allocation flag
  VkMemoryAllocateFlagsInfo Allocate_Flags = {
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
    .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
  };

  // Allocate device memory from the appropriate heap
  VK_CHECK (vkAllocateMemory (/*device        =>*/ Device,
                              /*pAllocateInfo =>*/ &(VkMemoryAllocateInfo){
      .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext           = (Usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? &Allocate_Flags : NULL,
      .allocationSize  = Memory_Requirements.size,
      .memoryTypeIndex = Find_Memory_Type (Physical_Device, Memory_Requirements.memoryTypeBits, Memory_Flags),
    },
                              /*pAllocator    =>*/ NULL,
                              /*pMemory       =>*/ &Result.Memory));

  // Bind the allocated memory to the buffer
  VK_CHECK (vkBindBufferMemory (Device, Result.Buffer, Result.Memory, 0));

  // Retrieve the 64-bit device address if this buffer will be referenced from shaders
  if (Usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    Result.Address = vkGetBufferDeviceAddress (/*device =>*/ Device,
                                               /*pInfo  =>*/ &(VkBufferDeviceAddressInfo){
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = Result.Buffer,
      });

  return Result;
}

/* Map the buffer's device memory into host address space, copy the source data, then unmap. */

void Buffer_Upload (VkDevice Device, Gpu_Buffer Destination, const void *Data, U64 Size) {
  void *Mapped;
  VK_CHECK (vkMapMemory (Device, Destination.Memory, 0, Size, 0, &Mapped));
  memcpy (Mapped, Data, Size);
  vkUnmapMemory (Device, Destination.Memory);
}

/* Upload data to device-local memory via a host-visible staging buffer.  A one-shot command buffer
   performs the copy, then the staging buffer is freed.  The resulting buffer has the requested usage
   flags plus transfer-destination and shader-device-address. */

Gpu_Buffer Buffer_Stage_Upload (VkDevice Device, VkPhysicalDevice Physical_Device,
                                       VkCommandBuffer Command_Buffer, VkQueue Queue,
                                       const void *Data, U64 Size, VkBufferUsageFlags Usage) {

  // Allocate a host-visible staging buffer and fill it with the source data
  Gpu_Buffer Staging = Buffer_Allocate (/*Device          =>*/ Device,
                                        /*Physical_Device =>*/ Physical_Device,
                                        /*Size            =>*/ Size,
                                        /*Usage           =>*/ VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  Buffer_Upload (Device, Staging, Data, Size);

  // Allocate the final device-local buffer that shaders will access
  Gpu_Buffer Destination = Buffer_Allocate (/*Device          =>*/ Device,
                                            /*Physical_Device =>*/ Physical_Device,
                                            /*Size            =>*/ Size,
                                            /*Usage           =>*/ Usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                            /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // Record and submit a one-shot command buffer to copy staging to destination
  VK_CHECK (vkResetCommandBuffer (Command_Buffer, 0));
  VK_CHECK (vkBeginCommandBuffer (/*commandBuffer =>*/ Command_Buffer,
                                  /*pBeginInfo    =>*/ &(VkCommandBufferBeginInfo){
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    }));

  // Record the copy command from staging to destination
  vkCmdCopyBuffer (Command_Buffer, Staging.Buffer, Destination.Buffer, 1, &(VkBufferCopy){.size = Size });

  // End recording, submit the command buffer, and wait for the transfer to finish
  VK_CHECK (vkEndCommandBuffer (Command_Buffer));
  VK_CHECK (vkQueueSubmit (/*queue       =>*/ Queue,
                           /*submitCount =>*/ 1,
                           /*pSubmits    =>*/ &(VkSubmitInfo){
      .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers    = &Command_Buffer,
    },
                           /*fence       =>*/ VK_NULL_HANDLE));
  VK_CHECK (vkQueueWaitIdle (Queue));

  // Release the temporary staging buffer now that the transfer is complete
  vkDestroyBuffer (Device, Staging.Buffer, NULL);
  vkFreeMemory (Device, Staging.Memory, NULL);
  return Destination;
}

/* Create a device-local 2D image suitable for use as a ray tracing storage target.
   The image is RGBA8 UNORM with storage and transfer-source usage bits. */

Gpu_Image Image_Storage_Create (VkDevice Device, VkPhysicalDevice Physical_Device, U32 Width, U32 Height) {
  Gpu_Image Result = {.Format = VK_FORMAT_R8G8B8A8_UNORM };

  // Create the image object with storage and transfer-source usage
  VK_CHECK (vkCreateImage (/*device      =>*/ Device,
                           /*pCreateInfo =>*/ &(VkImageCreateInfo){
      .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType     = VK_IMAGE_TYPE_2D,
      .format        = Result.Format,
      .extent        = {Width, Height, 1 },
      .mipLevels     = 1,
      .arrayLayers   = 1,
      .samples       = VK_SAMPLE_COUNT_1_BIT,
      .tiling        = VK_IMAGE_TILING_OPTIMAL,
      .usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    },
                           /*pAllocator  =>*/ NULL,
                           /*pImage      =>*/ &Result.Image));

  // Query memory requirements and allocate device-local memory for the image
  VkMemoryRequirements Memory_Requirements;
  vkGetImageMemoryRequirements (Device, Result.Image, &Memory_Requirements);

  // Allocate device-local memory and bind it to the image
  VK_CHECK (vkAllocateMemory (/*device        =>*/ Device,
                              /*pAllocateInfo =>*/ &(VkMemoryAllocateInfo){
      .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize  = Memory_Requirements.size,
      .memoryTypeIndex = Find_Memory_Type (Physical_Device, Memory_Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    },
                              /*pAllocator    =>*/ NULL,
                              /*pMemory       =>*/ &Result.Memory));

  VK_CHECK (vkBindImageMemory (Device, Result.Image, Result.Memory, 0));

  // Create an image view so shaders can reference this image
  VK_CHECK (vkCreateImageView (/*device      =>*/ Device,
                               /*pCreateInfo =>*/ &(VkImageViewCreateInfo){
      .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image            = Result.Image,
      .viewType         = VK_IMAGE_VIEW_TYPE_2D,
      .format           = Result.Format,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    },
                               /*pAllocator  =>*/ NULL,
                               /*pView       =>*/ &Result.View));

  return Result;
}

/* Insert a pipeline barrier that transitions an image between layouts, specifying the
   source and destination access masks and pipeline stages for proper synchronization. */

void Image_Layout_Barrier (VkCommandBuffer Command_Buffer, VkImage Image,
                                  VkImageLayout Old_Layout, VkImageLayout New_Layout,
                                  VkAccessFlags Source_Access, VkAccessFlags Destination_Access,
                                  VkPipelineStageFlags Source_Stage, VkPipelineStageFlags Destination_Stage) {
  vkCmdPipelineBarrier (/*commandBuffer            =>*/ Command_Buffer,
                        /*srcStageMask             =>*/ Source_Stage,
                        /*dstStageMask             =>*/ Destination_Stage,
                        /*dependencyFlags          =>*/ 0,
                        /*memoryBarrierCount       =>*/ 0,
                        /*pMemoryBarriers          =>*/ NULL,
                        /*bufferMemoryBarrierCount =>*/ 0,
                        /*pBufferMemoryBarriers    =>*/ NULL,
                        /*imageMemoryBarrierCount  =>*/ 1,
                        /*pImageMemoryBarriers     =>*/ &(VkImageMemoryBarrier){
                            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                            .srcAccessMask       = Source_Access,
                            .dstAccessMask       = Destination_Access,
                            .oldLayout           = Old_Layout,
                            .newLayout           = New_Layout,
                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .image               = Image,
                            .subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
                          });
}

/* Read a SPIR-V binary file from disk into a heap-allocated buffer.  Returns the raw U32
   code pointer and writes the byte size to Out_Size. */

U32 *Spirv_Load (const char *Path, U32 *Out_Size) {
  FILE *File = fopen (Path, "rb");
  if (not File) {fprintf (stderr, "Cannot open %s\n", Path); exit (1); }

  // Read the file contents into a heap-allocated buffer
  fseek (File, 0, SEEK_END);
  long Size = ftell (File);
  rewind (File);

  // Allocate a buffer and read the SPIR-V bytecode
  U32 *Code = malloc (Size);
  fread (Code, 1, Size, File);
  fclose (File);

  // Return the bytecode pointer and its size
  *Out_Size = (U32)Size;
  return Code;
}

/* Load a SPIR-V file and wrap it in a Vulkan shader module. */

VkShaderModule Shader_Module_Load (VkDevice Device, const char *Path) {
  U32 Size;
  U32 *Code = Spirv_Load (Path, &Size);

  // Wrap the raw SPIR-V code in a Vulkan shader module
  VkShaderModule Module;
  VK_CHECK (vkCreateShaderModule (/*device        =>*/ Device,
                                  /*pCreateInfo   =>*/ &(VkShaderModuleCreateInfo){
                                      .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                      .codeSize = Size,
                                      .pCode    = Code,
                                    },
                                  /*pAllocator    =>*/ NULL,
                                  /*pShaderModule =>*/ &Module));

  free (Code);
  return Module;
}

/* Upload raw RGBA pixel data to a device-local texture image via staging buffer,
   transitioning the image layout from undefined through transfer-destination to shader-read-only.
   The caller specifies the Vulkan format (SRGB vs UNORM). */

void Texture_Upload_With_Format (VkDevice Device, VkPhysicalDevice Physical_Device,
                                        VkCommandBuffer Command_Buffer, VkQueue Queue,
                                        const U8 *Pixels, U32 Width, U32 Height, VkFormat Format,
                                        VkImage *Out_Image, VkDeviceMemory *Out_Memory, VkImageView *Out_View) {

  // Create the texture image with sampled and transfer-destination usage
  VkImage Image;
  VK_CHECK (vkCreateImage (/*device      =>*/ Device,
                           /*pCreateInfo =>*/ &(VkImageCreateInfo){
      .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType     = VK_IMAGE_TYPE_2D,
      .format        = Format,
      .extent        = {Width, Height, 1 },
      .mipLevels     = 1,
      .arrayLayers   = 1,
      .samples       = VK_SAMPLE_COUNT_1_BIT,
      .tiling        = VK_IMAGE_TILING_OPTIMAL,
      .usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    },
                           /*pAllocator  =>*/ NULL,
                           /*pImage      =>*/ &Image));

  // Allocate and bind device-local memory for the texture
  VkMemoryRequirements Memory_Requirements;
  vkGetImageMemoryRequirements (Device, Image, &Memory_Requirements);

  // Allocate device-local memory for the texture
  VkDeviceMemory Memory;
  VK_CHECK (vkAllocateMemory (/*device        =>*/ Device,
                              /*pAllocateInfo =>*/ &(VkMemoryAllocateInfo){
      .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize  = Memory_Requirements.size,
      .memoryTypeIndex = Find_Memory_Type (Physical_Device, Memory_Requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    },
                              /*pAllocator    =>*/ NULL,
                              /*pMemory       =>*/ &Memory));
  VK_CHECK (vkBindImageMemory (Device, Image, Memory, 0));

  // Stage the pixel data through a host-visible buffer
  U64 Byte_Size = (U64)Width * Height * 4;
  Gpu_Buffer Staging = Buffer_Allocate (/*Device          =>*/ Device,
                                        /*Physical_Device =>*/ Physical_Device,
                                        /*Size            =>*/ Byte_Size,
                                        /*Usage           =>*/ VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  Buffer_Upload (Device, Staging, Pixels, Byte_Size);

  // Record a command buffer that transitions the image and copies the staging data into it
  VK_CHECK (vkResetCommandBuffer (Command_Buffer, 0));
  VK_CHECK (vkBeginCommandBuffer (/*commandBuffer =>*/ Command_Buffer,
                                  /*pBeginInfo    =>*/ &(VkCommandBufferBeginInfo){
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    }));

  // Transition from undefined to transfer-destination for the copy
  Image_Layout_Barrier (/*Command_Buffer     =>*/ Command_Buffer,
                        /*Image              =>*/ Image,
                        /*Old_Layout         =>*/ VK_IMAGE_LAYOUT_UNDEFINED,
                        /*New_Layout         =>*/ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        /*Source_Access      =>*/ 0,
                        /*Destination_Access =>*/ VK_ACCESS_TRANSFER_WRITE_BIT,
                        /*Source_Stage       =>*/ VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        /*Destination_Stage  =>*/ VK_PIPELINE_STAGE_TRANSFER_BIT);

  // Copy the staging buffer contents into the image
  vkCmdCopyBufferToImage (/*commandBuffer  =>*/ Command_Buffer,
                          /*srcBuffer      =>*/ Staging.Buffer,
                          /*dstImage       =>*/ Image,
                          /*dstImageLayout =>*/ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          /*regionCount    =>*/ 1,
                          /*pRegions       =>*/ &(VkBufferImageCopy){
      .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
      .imageExtent      = {Width, Height, 1 },
    });

  // Transition from transfer-destination to shader-read-only for sampling in ray tracing shaders
  Image_Layout_Barrier (/*Command_Buffer     =>*/ Command_Buffer,
                        /*Image              =>*/ Image,
                        /*Old_Layout         =>*/ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        /*New_Layout         =>*/ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        /*Source_Access      =>*/ VK_ACCESS_TRANSFER_WRITE_BIT,
                        /*Destination_Access =>*/ VK_ACCESS_SHADER_READ_BIT,
                        /*Source_Stage       =>*/ VK_PIPELINE_STAGE_TRANSFER_BIT,
                        /*Destination_Stage  =>*/ VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

  // Submit the command buffer and wait for the transfer to complete
  VK_CHECK (vkEndCommandBuffer (Command_Buffer));
  VK_CHECK (vkQueueSubmit (/*queue       =>*/ Queue,
                           /*submitCount =>*/ 1,
                           /*pSubmits    =>*/ &(VkSubmitInfo){
      .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers    = &Command_Buffer,
    },
                           /*fence       =>*/ VK_NULL_HANDLE));
  VK_CHECK (vkQueueWaitIdle (Queue));

  // Release the staging buffer
  vkDestroyBuffer (Device, Staging.Buffer, NULL);
  vkFreeMemory (Device, Staging.Memory, NULL);

  // Create an image view for shader access
  VkImageView View;
  VK_CHECK (vkCreateImageView (/*device      =>*/ Device,
                               /*pCreateInfo =>*/ &(VkImageViewCreateInfo){
      .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image            = Image,
      .viewType         = VK_IMAGE_VIEW_TYPE_2D,
      .format           = Format,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    },
                               /*pAllocator  =>*/ NULL,
                               /*pView       =>*/ &View));

  *Out_Image  = Image;
  *Out_Memory = Memory;
  *Out_View   = View;
}

/* Convenience wrapper that uploads a texture as SRGB. */

void Texture_Upload (VkDevice Device, VkPhysicalDevice Physical_Device,
                            VkCommandBuffer Command_Buffer, VkQueue Queue,
                            const U8 *Pixels, U32 Width, U32 Height,
                            VkImage *Out_Image, VkDeviceMemory *Out_Memory, VkImageView *Out_View) {
  Texture_Upload_With_Format (/*Device          =>*/ Device,
                              /*Physical_Device =>*/ Physical_Device,
                              /*Command_Buffer  =>*/ Command_Buffer,
                              /*Queue           =>*/ Queue,
                              /*Pixels          =>*/ Pixels,
                              /*Width           =>*/ Width,
                              /*Height          =>*/ Height,
                              /*Format          =>*/ VK_FORMAT_R8G8B8A8_SRGB,
                              /*Out_Image       =>*/ Out_Image,
                              /*Out_Memory      =>*/ Out_Memory,
                              /*Out_View        =>*/ Out_View);
}

/* Create a sampler with linear filtering and repeating address mode on all axes. */

VkSampler Sampler_Create_Repeating (VkDevice Device) {
  VkSampler Sampler;
  VK_CHECK (vkCreateSampler (/*device      =>*/ Device,
                             /*pCreateInfo =>*/ &(VkSamplerCreateInfo){
      .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter    = VK_FILTER_LINEAR,
      .minFilter    = VK_FILTER_LINEAR,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .maxLod       = 1.f,
    },
                             /*pAllocator  =>*/ NULL,
                             /*pSampler    =>*/ &Sampler));
  return Sampler;
}

/* Create a sampler with linear filtering and clamp-to-edge on all axes. */

VkSampler Sampler_Create_Clamping (VkDevice Device) {
  VkSampler Sampler;
  VK_CHECK (vkCreateSampler (/*device      =>*/ Device,
                             /*pCreateInfo =>*/ &(VkSamplerCreateInfo){
      .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter    = VK_FILTER_LINEAR,
      .minFilter    = VK_FILTER_LINEAR,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .maxLod       = 1.f,
    },
                             /*pAllocator  =>*/ NULL,
                             /*pSampler    =>*/ &Sampler));
  return Sampler;
}

/* <<tga_loader>> ======================================================================================= */

/* Load a TGA image file and decode it into RGBA8 pixel data.  Supports uncompressed
   8/24/32-bit images (type 2/3) and RLE-compressed true-color images (type 10).
   The output is always bottom-to-top, RGBA, 8 bits per channel. */

U8 *Tga_Load (const char *Path, U32 *Out_Width, U32 *Out_Height) {
  FILE *File = fopen (Path, "rb");
  if (not File) return NULL;

  // Read the entire file into memory
  fseek (File, 0, SEEK_END);
  long Length = ftell (File);
  rewind (File);
  if (Length < 18) {fclose (File); return NULL; }

  // Allocate a buffer and read the entire file into memory
  U8 *Raw = malloc (Length);
  fread (Raw, 1, Length, File);
  fclose (File);

  // Parse the 18-byte TGA header fields
  U8 *Cursor     = Raw;
  U8 *End_Cursor = Raw + Length;

  // Extract the 18-byte TGA header fields
  U8  Id_Length     = Cursor[0];
  U8  Colormap_Type = Cursor[1];
  U8  Image_Type    = Cursor[2];
  U16 Image_Width, Image_Height;
  memcpy (&Image_Width,  Cursor + 12, 2);
  memcpy (&Image_Height, Cursor + 14, 2);
  U8  Bits_Per_Pixel = Cursor[16];
  (void)Colormap_Type;
  Cursor += 18 + Id_Length;

  // Reject unsupported image types (only uncompressed and RLE true-color are handled)
  if (Image_Type != 2 and Image_Type != 3 and Image_Type != 10) {
    free (Raw);
    return NULL;
  }

  // Allocate the output RGBA pixel buffer
  U32 Columns = Image_Width;
  U32 Rows    = Image_Height;
  *Out_Width  = Columns;
  *Out_Height = Rows;
  U8 *Output  = malloc (Columns * Rows * 4);

  // Decode uncompressed image data (types 2 and 3), reading rows bottom-to-top
  if (Image_Type == 2 or Image_Type == 3) {
    for (U32 Row = Rows; Row-- > 0; ) {
      U8 *Destination = Output + Row * Columns * 4;
      for (U32 Column = 0; Column < Columns; Column++) {
        if (Cursor >= End_Cursor) break;
        U8 Red, Green, Blue, Alpha = 255;
        if (Bits_Per_Pixel == 8) {
          Blue = *Cursor++;
          Red = Green = Blue;
        } else if (Bits_Per_Pixel == 24) {
          Blue  = *Cursor++;
          Green = *Cursor++;
          Red   = *Cursor++;
        } else {
          Blue  = *Cursor++;
          Green = *Cursor++;
          Red   = *Cursor++;
          Alpha = *Cursor++;
        }
        *Destination++ = Red;
        *Destination++ = Green;
        *Destination++ = Blue;
        *Destination++ = Alpha;
      }
    }

  // Decode RLE-compressed image data (type 10)
  } else {
    U32 Row = Rows - 1, Column = 0;
    U8 *Destination = Output + Row * Columns * 4;
    while (Cursor < End_Cursor and Row < Rows) {

      // Read the RLE packet header: high bit indicates a run-length packet
      U8 Header      = *Cursor++;
      U8 Pixel_Count = (Header & 0x7F) + 1;

      // Dispatch on packet type: run-length or raw
      if (Header & 0x80) {

        // Run-length packet: one pixel value repeated Pixel_Count times
        U8 Blue = 0, Green = 0, Red = 0, Alpha = 255;
        if (Bits_Per_Pixel == 24) {
          Blue = *Cursor++; Green = *Cursor++; Red = *Cursor++;
        } else {
          Blue = *Cursor++; Green = *Cursor++; Red = *Cursor++; Alpha = *Cursor++;
        }
        for (U8 Pixel = 0; Pixel < Pixel_Count; Pixel++) {
          *Destination++ = Red;
          *Destination++ = Green;
          *Destination++ = Blue;
          *Destination++ = Alpha;
          if (++Column == Columns) {
            Column = 0;
            if (Row == 0) goto Tga_Done;
            Row--;
            Destination = Output + Row * Columns * 4;
          }
        }

      // Raw packet: Pixel_Count distinct pixel values follow
      } else {
        for (U8 Pixel = 0; Pixel < Pixel_Count; Pixel++) {
          U8 Blue = 0, Green = 0, Red = 0, Alpha = 255;
          if (Bits_Per_Pixel == 24) {
            Blue = *Cursor++; Green = *Cursor++; Red = *Cursor++;
          } else {
            Blue = *Cursor++; Green = *Cursor++; Red = *Cursor++; Alpha = *Cursor++;
          }
          *Destination++ = Red;
          *Destination++ = Green;
          *Destination++ = Blue;
          *Destination++ = Alpha;
          if (++Column == Columns) {
            Column = 0;
            if (Row == 0) goto Tga_Done;
            Row--;
            Destination = Output + Row * Columns * 4;
          }
        }
      }
    }
    Tga_Done:;
  }

  free (Raw);
  return Output;
}

/* <<md3_loader>> ======================================================================================= */

#define MD3_MAGIC 0x33504449u

// MD3 surface header: describes one mesh within an MD3 model file
typedef struct {
  I32 Magic;                              // Surface magic identifier (always IDP3)
  char Name[64];                          // Null-terminated surface name
  I32 Flags;                              // Surface flags (unused in Quake 3)
  I32 Number_Of_Frames, Number_Of_Shaders;    // Animation frame count and attached shader count
  I32 Number_Of_Vertices, Number_Of_Triangles; // Per-frame vertex count and triangle count
  I32 Triangles_Offset, Shaders_Offset;   // Byte offsets from surface start to triangle and shader data
  I32 Texture_Coordinates_Offset;         // Byte offset to the per-vertex texture coordinate array
  I32 Vertices_Offset, End_Offset;        // Byte offset to compressed vertex frames and to the next surface
} Md3_Surface;

// MD3 tag: a named attachment point with position and orientation for linking model parts
typedef struct {
  char Name[64];    // Null-terminated tag name (e.g. "tag_barrel", "tag_weapon")
  F32  Origin[3];   // World-space position of the attachment point
  F32  Axis[9];     // 3x3 rotation matrix (row-major) defining the tag's local coordinate frame
} Md3_Tag;

// Parsed weapon model assembled from multiple MD3 surfaces (body, barrel, hand)
typedef struct {
  Vertex *Vertices;       U32 Vertex_Count;     // Merged vertex array from all surfaces
  U32    *Indices;        U32 Index_Count;       // Merged index array from all surfaces
  U32    *Texture_Ids;    U32 Triangle_Count;    // Per-triangle texture index and total triangle count
  F32     Tag_Barrel[12];                        // Barrel attachment transform: origin[3] + axis[9]
  F32     Tag_Weapon[30][12];                    // Per-frame weapon tag transforms (up to 30 animation frames)
  U32     Animation_Frame_Count;                 // Number of valid frames in the Tag_Weapon array
  char    Texture_Names[3][64];                  // Texture path for each surface (body, barrel, hand)
  U32     Surface_Count;                         // Number of surfaces composing this weapon (typically 3)
} Weapon_Model;

// Runtime weapon state combining the model data with per-frame animation and GPU resources
typedef struct {
  Weapon_Model           Model;                 // Parsed model geometry and attachment tags
  Vertex                *Transformed_Vertices;  // Scratch buffer for CPU-side per-frame vertex transformation
  int                    Is_Firing;             // Non-zero while the fire button is held
  F32                    Fire_Time, Bob_Time;   // Recoil decay timer and idle bob phase accumulator
  Gpu_Buffer             Vertex_Buffer, Index_Buffer, Texture_Id_Buffer;  // GPU buffers for weapon geometry
  Acceleration_Structure Bottom_Level;          // BLAS for the weapon (rebuilt each frame)
  Gpu_Buffer             Bottom_Level_Scratch;  // Scratch buffer reused across BLAS rebuilds
  U32                    Texture_Base_Index;     // Starting index into the global texture array for weapon textures
} Weapon_Instance;

/* Parse a single MD3 surface's geometry (vertices, indices, texture coordinates) into the
   growing output arrays.  An optional 12-float transform (origin + 3x3 axis matrix) can
   pre-transform vertices and normals.  Quake 3 coordinate swizzle (x,y,z)->(x,z,-y) is applied. */

void Md3_Parse_Surface (const U8 *Surface_Data, Vertex **Inout_Vertices, U32 *Inout_Vertex_Count,
                               U32 **Inout_Indices, U32 *Inout_Index_Count,
                               U32 **Inout_Texture_Ids, U32 *Inout_Triangle_Count,
                               U32 Assigned_Texture_Index, const F32 *Transform) {
  const Md3_Surface *Surface = (const Md3_Surface *)Surface_Data;
  U32 Base_Vertex = *Inout_Vertex_Count;

  // Copy triangle indices, offsetting each by the current vertex base
  const I32 *Triangles = (const I32 *)(Surface_Data + Surface->Triangles_Offset);
  *Inout_Indices = realloc (*Inout_Indices, sizeof (U32) * (*Inout_Index_Count + Surface->Number_Of_Triangles * 3));
  for (I32 Index = 0; Index < Surface->Number_Of_Triangles * 3; Index++) {
    (*Inout_Indices)[*Inout_Index_Count + Index] = Base_Vertex + (U32)Triangles[Index];
  }

  // Assign the same texture index to every triangle in this surface
  *Inout_Texture_Ids = realloc (*Inout_Texture_Ids, sizeof (U32) * (*Inout_Triangle_Count + Surface->Number_Of_Triangles));
  for (I32 Triangle = 0; Triangle < Surface->Number_Of_Triangles; Triangle++) {
    (*Inout_Texture_Ids)[*Inout_Triangle_Count + Triangle] = Assigned_Texture_Index;
  }

  // Decode packed MD3 vertices: I16 xyz at 1/64 scale, plus spherical normal encoding
  const U8  *Vertex_Data              = Surface_Data + Surface->Vertices_Offset;
  const F32 *Texture_Coordinate_Data  = (const F32 *)(Surface_Data + Surface->Texture_Coordinates_Offset);

  // Grow the vertex array and decode each compressed MD3 vertex position and normal
  *Inout_Vertices = realloc (*Inout_Vertices, sizeof (Vertex) * (*Inout_Vertex_Count + Surface->Number_Of_Vertices));

  // Decode each vertex's packed position, spherical normal, and texture coordinates
  for (I32 Vertex_Index = 0; Vertex_Index < Surface->Number_Of_Vertices; Vertex_Index++) {

    // Unpack the 16-bit position coordinates and scale from MD3's fixed-point representation
    const I16 *Coordinates = (const I16 *)(Vertex_Data + Vertex_Index * 8);
    F32 Position_X = Coordinates[0] / 64.f;
    F32 Position_Y = Coordinates[1] / 64.f;
    F32 Position_Z = Coordinates[2] / 64.f;

    // Decode the spherical normal from latitude/longitude byte pair
    U8  Latitude  = Vertex_Data[Vertex_Index * 8 + 6];
    U8  Longitude = Vertex_Data[Vertex_Index * 8 + 7];
    F32 Latitude_Angle  = Latitude  * (2.f * (F32)M_PI / 255.f);
    F32 Longitude_Angle = Longitude * (2.f * (F32)M_PI / 255.f);
    F32 Normal_X = cosf (Latitude_Angle) * sinf (Longitude_Angle);
    F32 Normal_Y = sinf (Latitude_Angle) * sinf (Longitude_Angle);
    F32 Normal_Z = cosf (Longitude_Angle);

    // Optionally apply the tag transform (origin + rotation matrix) to position and normal
    if (Transform) {
      F32 Origin_X = Transform[0];
      F32 Origin_Y = Transform[1];
      F32 Origin_Z = Transform[2];
      F32 Transformed_X = Transform[3]  * Position_X + Transform[6]  * Position_Y + Transform[9]  * Position_Z + Origin_X;
      F32 Transformed_Y = Transform[4]  * Position_X + Transform[7]  * Position_Y + Transform[10] * Position_Z + Origin_Y;
      F32 Transformed_Z = Transform[5]  * Position_X + Transform[8]  * Position_Y + Transform[11] * Position_Z + Origin_Z;
      F32 Transformed_Normal_X = Transform[3] * Normal_X + Transform[6] * Normal_Y + Transform[9]  * Normal_Z;
      F32 Transformed_Normal_Y = Transform[4] * Normal_X + Transform[7] * Normal_Y + Transform[10] * Normal_Z;
      F32 Transformed_Normal_Z = Transform[5] * Normal_X + Transform[8] * Normal_Y + Transform[11] * Normal_Z;
      Position_X = Transformed_X;
      Position_Y = Transformed_Y;
      Position_Z = Transformed_Z;
      Normal_X   = Transformed_Normal_X;
      Normal_Y   = Transformed_Normal_Y;
      Normal_Z   = Transformed_Normal_Z;
    }

    // Read texture coordinates and store the final vertex with Quake3 swizzle applied
    F32 Texture_U = Texture_Coordinate_Data[Vertex_Index * 2];
    F32 Texture_V = Texture_Coordinate_Data[Vertex_Index * 2 + 1];

      // Assemble the final vertex with position, texture coordinates, and normal
    (*Inout_Vertices)[*Inout_Vertex_Count + Vertex_Index] = (Vertex){
      .Position   = {Position_X, Position_Z, -Position_Y },
      .Texture_Uv = {Texture_U, Texture_V },
      .Normal     = {Normal_X, Normal_Z, -Normal_Y },
    };
  }

  // Advance the running totals
  *Inout_Vertex_Count   += Surface->Number_Of_Vertices;
  *Inout_Index_Count    += Surface->Number_Of_Triangles * 3;
  *Inout_Triangle_Count += Surface->Number_Of_Triangles;
}

/* Load the three-part machinegun weapon model (body, barrel, hand) from MD3 files.
   The barrel is pre-transformed by tag_barrel; animation frames are extracted from tag_weapon in the hand model. */

Weapon_Model Weapon_Model_Load (void) {
  Weapon_Model Result = {0};

  // Open the main weapon body mesh
  FILE *File = fopen ("assets/models/weapons2/machinegun/machinegun.md3", "rb");
  if (not File) {printf ("[weapon] machinegun.md3 not found\n"); return Result; }

  // Read the body file into memory and validate the MD3 magic number
  fseek (File, 0, SEEK_END);
  long File_Size = ftell (File);
  rewind (File);
  U8 *Body_Data = malloc (File_Size);
  fread (Body_Data, 1, File_Size, File);
  fclose (File);
  assert (*(U32 *)Body_Data == MD3_MAGIC);

  // Read body header fields: surface count, tag count, and their offsets
  I32 Body_Surface_Count   = *(I32 *)(Body_Data + 84);
  I32 Body_Tag_Count       = *(I32 *)(Body_Data + 80);
  I32 Body_Tags_Offset     = *(I32 *)(Body_Data + 96);
  I32 Body_Surfaces_Offset = *(I32 *)(Body_Data + 100);

  // Search for the "tag_barrel" attachment point in the body's tag list
  memset (Result.Tag_Barrel, 0, sizeof (Result.Tag_Barrel));
  const Md3_Tag *Body_Tags = (const Md3_Tag *)(Body_Data + Body_Tags_Offset);
  for (I32 Tag = 0; Tag < Body_Tag_Count; Tag++) {
    if (strncmp (Body_Tags[Tag].Name, "tag_barrel", 64) == 0) {
      memcpy (Result.Tag_Barrel, Body_Tags[Tag].Origin, 3 * sizeof (F32));
      memcpy (Result.Tag_Barrel + 3, Body_Tags[Tag].Axis, 9 * sizeof (F32));
      break;
    }
  }

  // Iterate over each surface in the body and parse its geometry
  const U8 *Surface_Cursor = Body_Data + Body_Surfaces_Offset;
  for (I32 Surface = 0; Surface < Body_Surface_Count; Surface++) {
    const Md3_Surface *Header = (const Md3_Surface *)Surface_Cursor;
    const char *Shader_Name   = (const char *)(Surface_Cursor + Header->Shaders_Offset);

    // Record the shader name as this surface's texture and parse the geometry
    U32 Texture_Index = Result.Surface_Count;
    if (Texture_Index < 3) {
      snprintf (Result.Texture_Names[Texture_Index], 64, "%s", Shader_Name);
      Result.Surface_Count++;
    }

    // Parse the surface geometry into the shared vertex/index/texture arrays
    Md3_Parse_Surface (/*Surface_Data           =>*/ Surface_Cursor,
                       /*Inout_Vertices         =>*/ &Result.Vertices,
                       /*Inout_Vertex_Count     =>*/ &Result.Vertex_Count,
                       /*Inout_Indices          =>*/ &Result.Indices,
                       /*Inout_Index_Count      =>*/ &Result.Index_Count,
                       /*Inout_Texture_Ids      =>*/ &Result.Texture_Ids,
                       /*Inout_Triangle_Count   =>*/ &Result.Triangle_Count,
                       /*Assigned_Texture_Index =>*/ Texture_Index,
                       /*Transform              =>*/ NULL);
    Surface_Cursor += Header->End_Offset;
  }
  free (Body_Data);

  // Load the barrel mesh and pre-transform it by the tag_barrel attachment transform
  File = fopen ("assets/models/weapons2/machinegun/machinegun_barrel.md3", "rb");
  if (File) {
    fseek (File, 0, SEEK_END);
    File_Size = ftell (File);
    rewind (File);
    U8 *Barrel_Data = malloc (File_Size);
    fread (Barrel_Data, 1, File_Size, File);
    fclose (File);
    assert (*(U32 *)Barrel_Data == MD3_MAGIC);

    // Read the barrel model's surface count and offset
    I32 Barrel_Surface_Count   = *(I32 *)(Barrel_Data + 84);
    I32 Barrel_Surfaces_Offset = *(I32 *)(Barrel_Data + 100);

    // Parse each barrel surface, applying the tag_barrel attachment transform
    Surface_Cursor = Barrel_Data + Barrel_Surfaces_Offset;
    for (I32 Surface = 0; Surface < Barrel_Surface_Count; Surface++) {
      Md3_Parse_Surface (/*Surface_Data           =>*/ Surface_Cursor,
                         /*Inout_Vertices         =>*/ &Result.Vertices,
                         /*Inout_Vertex_Count     =>*/ &Result.Vertex_Count,
                         /*Inout_Indices          =>*/ &Result.Indices,
                         /*Inout_Index_Count      =>*/ &Result.Index_Count,
                         /*Inout_Texture_Ids      =>*/ &Result.Texture_Ids,
                         /*Inout_Triangle_Count   =>*/ &Result.Triangle_Count,
                         /*Assigned_Texture_Index =>*/ 0,
                         /*Transform              =>*/ Result.Tag_Barrel);
      Surface_Cursor += ((const Md3_Surface *)Surface_Cursor)->End_Offset;
    }
    free (Barrel_Data);
    printf (/*format =>*/ "[weapon] barrel merged, "
            "tag_barrel=(%.1f,%.1f,%.1f)\n",
            Result.Tag_Barrel[0],
            Result.Tag_Barrel[1],
            Result.Tag_Barrel[2]);
  }

  // Load the hand mesh to extract the tag_weapon animation frames for recoil
  File = fopen ("assets/models/weapons2/machinegun/machinegun_hand.md3", "rb");
  if (File) {
    fseek (File, 0, SEEK_END);
    File_Size = ftell (File);
    rewind (File);
    U8 *Hand_Data = malloc (File_Size);
    fread (Hand_Data, 1, File_Size, File);
    fclose (File);
    assert (*(U32 *)Hand_Data == MD3_MAGIC);

    // Extract the hand model's frame count and per-frame tag_weapon transforms
    I32 Hand_Frame_Count = *(I32 *)(Hand_Data + 76);
    I32 Hand_Tag_Count   = *(I32 *)(Hand_Data + 80);
    I32 Hand_Tags_Offset = *(I32 *)(Hand_Data + 96);
    Result.Animation_Frame_Count = Hand_Frame_Count < 30 ? Hand_Frame_Count : 30;

    // Extract the origin and axis for tag_weapon at each animation frame
    for (U32 Frame = 0; Frame < Result.Animation_Frame_Count; Frame++) {
      const Md3_Tag *Tags = (const Md3_Tag *)(Hand_Data + Hand_Tags_Offset + Frame * Hand_Tag_Count * sizeof (Md3_Tag));
      for (I32 Tag = 0; Tag < Hand_Tag_Count; Tag++) {
        if (strncmp (Tags[Tag].Name, "tag_weapon", 64) == 0) {
          memcpy (Result.Tag_Weapon[Frame], Tags[Tag].Origin, 3 * sizeof (F32));
          memcpy (Result.Tag_Weapon[Frame] + 3, Tags[Tag].Axis, 9 * sizeof (F32));
          break;
        }
      }
    }
    free (Hand_Data);
    printf ("[weapon] hand: %u animation frames\n", Result.Animation_Frame_Count);
  }

  // Report the loaded weapon geometry statistics
  printf (/*format =>*/ "[weapon] loaded: %u vertices, "
          "%u triangles, "
          "%u surfaces\n",
          Result.Vertex_Count,
          Result.Triangle_Count,
          Result.Surface_Count);
  return Result;
}

/* <<vulkan_init>> ====================================================================================== */

/* Create a Vulkan instance with the SDL-required surface extensions plus the debug utils extension,
   and the Khronos validation layer.  Then create a surface from the SDL window. */

void Vulkan_Create_Instance (Vulkan_Context *Context) {

  // Gather the instance extensions required by SDL for Vulkan surface presentation
  U32 Extension_Count;
  SDL_Vulkan_GetInstanceExtensions (Context->Window, &Extension_Count, NULL);
  const char **Extensions = malloc (sizeof (char *) * (Extension_Count + 1));
  SDL_Vulkan_GetInstanceExtensions (Context->Window, &Extension_Count, Extensions);
  Extensions[Extension_Count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

  // Enable the Khronos validation layer for debug builds
  const char *Layers[] = {"VK_LAYER_KHRONOS_validation" };

  // Create the Vulkan instance targeting API version 1.3
  VK_CHECK (vkCreateInstance (/*pCreateInfo =>*/ &(VkInstanceCreateInfo){
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &(VkApplicationInfo){
        .sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "quake3rt",
        .apiVersion       = VK_API_VERSION_1_3,
      },
      .enabledLayerCount       = 1,
      .ppEnabledLayerNames     = Layers,
      .enabledExtensionCount   = Extension_Count,
      .ppEnabledExtensionNames = Extensions,
    },
                              /*pAllocator  =>*/ NULL,
                              /*pInstance   =>*/ &Context->Instance));

  free (Extensions);

  // Create the platform window surface
  SDL_Vulkan_CreateSurface (Context->Window, Context->Instance, &Context->Surface);
}

/* Enumerate physical devices and select the first one.  Then find a queue family
   that supports both graphics operations and presentation to the window surface. */

void Vulkan_Pick_Physical_Device (Vulkan_Context *Context) {

  // Pick the first available physical device
  U32 Device_Count;
  vkEnumeratePhysicalDevices (Context->Instance, &Device_Count, NULL);
  VkPhysicalDevice *Devices = malloc (sizeof (VkPhysicalDevice) * Device_Count);
  vkEnumeratePhysicalDevices (Context->Instance, &Device_Count, Devices);
  Context->Physical_Device = Devices[0];
  free (Devices);

  // Search for a queue family that supports both graphics and surface presentation
  U32 Family_Count;
  vkGetPhysicalDeviceQueueFamilyProperties (Context->Physical_Device, &Family_Count, NULL);
  VkQueueFamilyProperties *Families = malloc (sizeof (*Families) * Family_Count);
  vkGetPhysicalDeviceQueueFamilyProperties (Context->Physical_Device, &Family_Count, Families);

  // Select the first queue family that supports both graphics and presentation
  Context->Queue_Family_Index = 0;
  for (U32 Index = 0; Index < Family_Count; Index++) {
    VkBool32 Supports_Present;
    vkGetPhysicalDeviceSurfaceSupportKHR (Context->Physical_Device, Index, Context->Surface, &Supports_Present);
    if ((Families[Index].queueFlags & VK_QUEUE_GRAPHICS_BIT) and Supports_Present) {
      Context->Queue_Family_Index = Index;
      break;
    }
  }
  free (Families);
}

/* Create the logical device with all required features enabled: Vulkan 1.2 buffer device address
   and descriptor indexing, Vulkan 1.3 synchronization2 and dynamic rendering, plus the KHR
   acceleration structure and ray tracing pipeline extensions. */

void Vulkan_Create_Logical_Device (Vulkan_Context *Context) {

  // Chain together the feature structures for acceleration structure and ray tracing pipeline
  VkPhysicalDeviceAccelerationStructureFeaturesKHR Acceleration_Structure_Features = {
    .sType                  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
    .accelerationStructure  = VK_TRUE,
  };

  // Enable ray tracing pipeline features chained to the acceleration structure features
  VkPhysicalDeviceRayTracingPipelineFeaturesKHR Raytracing_Pipeline_Features = {
    .sType              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
    .pNext              = &Acceleration_Structure_Features,
    .rayTracingPipeline = VK_TRUE,
  };

  // Enable Vulkan 1.2 features: buffer device address, descriptor indexing with runtime arrays
  VkPhysicalDeviceVulkan12Features Vulkan_12_Features = {
    .sType                                         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    .pNext                                         = &Raytracing_Pipeline_Features,
    .bufferDeviceAddress                           = VK_TRUE,
    .descriptorIndexing                            = VK_TRUE,
    .runtimeDescriptorArray                        = VK_TRUE,
    .shaderSampledImageArrayNonUniformIndexing     = VK_TRUE,
    .descriptorBindingPartiallyBound               = VK_TRUE,
    .descriptorBindingVariableDescriptorCount      = VK_TRUE,
  };

  // Enable Vulkan 1.3 features: synchronization2 and dynamic rendering
  VkPhysicalDeviceVulkan13Features Vulkan_13_Features = {
    .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
    .pNext            = &Vulkan_12_Features,
    .synchronization2 = VK_TRUE,
    .dynamicRendering = VK_TRUE,
  };

  // Specify the required device extensions: swapchain, acceleration structure, ray tracing, deferred ops
  const char *Device_Extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
  };

  // Set queue priority to maximum (1.0) for the single graphics queue
  F32 Priority = 1.f;

  // Create the logical device with a single graphics queue and all chained features
  VK_CHECK (vkCreateDevice (/*physicalDevice =>*/ Context->Physical_Device,
                            /*pCreateInfo    =>*/ &(VkDeviceCreateInfo){
      .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext                   = &Vulkan_13_Features,
      .queueCreateInfoCount    = 1,
      .pQueueCreateInfos       = &(VkDeviceQueueCreateInfo){
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = Context->Queue_Family_Index,
        .queueCount       = 1,
        .pQueuePriorities = &Priority,
      },
      .enabledExtensionCount   = 4,
      .ppEnabledExtensionNames = Device_Extensions,
    },
                            /*pAllocator     =>*/ NULL,
                            /*pDevice        =>*/ &Context->Device));

  // Retrieve the queue handle and ray tracing pipeline properties
  vkGetDeviceQueue (Context->Device, Context->Queue_Family_Index, 0, &Context->Queue);

  // Query the physical device's ray tracing pipeline properties for SBT layout
  Context->Raytracing_Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
  VkPhysicalDeviceProperties2 Device_Properties = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
    .pNext = &Context->Raytracing_Properties,
  };
  vkGetPhysicalDeviceProperties2 (Context->Physical_Device, &Device_Properties);

  // Load the ray tracing extension function pointers
  Context->Raytracing = Raytracing_Functions_Load (Context->Device);
}

/* Create the swapchain using the surface's current extent, BGRA8 SRGB format,
   and FIFO (v-sync) present mode. */

void Vulkan_Create_Swapchain (Vulkan_Context *Context) {
  VkSurfaceCapabilitiesKHR Capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR (Context->Physical_Device, Context->Surface, &Capabilities);
  Context->Swapchain_Extent = Capabilities.currentExtent;
  Context->Swapchain_Format = VK_FORMAT_B8G8R8A8_SRGB;

  // Request one more image than the minimum to avoid stalling on the driver
  U32 Image_Count = Capabilities.minImageCount + 1;
  if (Capabilities.maxImageCount and Image_Count > Capabilities.maxImageCount)
    Image_Count = Capabilities.maxImageCount;

  // Create the swapchain with the determined format and present mode
  VK_CHECK (vkCreateSwapchainKHR (/*device      =>*/ Context->Device,
                                  /*pCreateInfo =>*/ &(VkSwapchainCreateInfoKHR){
      .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface          = Context->Surface,
      .minImageCount    = Image_Count,
      .imageFormat      = Context->Swapchain_Format,
      .imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
      .imageExtent      = Context->Swapchain_Extent,
      .imageArrayLayers = 1,
      .imageUsage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform     = Capabilities.currentTransform,
      .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode      = VK_PRESENT_MODE_FIFO_KHR,
      .clipped          = VK_TRUE,
    },
                                  /*pAllocator  =>*/ NULL,
                                  /*pSwapchain  =>*/ &Context->Swapchain));

  // Retrieve the swapchain image handles
  vkGetSwapchainImagesKHR (Context->Device, Context->Swapchain, &Context->Swapchain_Image_Count, NULL);
  vkGetSwapchainImagesKHR (Context->Device, Context->Swapchain, &Context->Swapchain_Image_Count, Context->Swapchain_Images);
}

/* Create the command pool, a primary command buffer, a fence for frame synchronization,
   and two semaphores for image-available and render-finished signaling. */

void Vulkan_Create_Synchronization (Vulkan_Context *Context) {
  VK_CHECK (vkCreateCommandPool (/*device       =>*/ Context->Device,
                                 /*pCreateInfo  =>*/ &(VkCommandPoolCreateInfo){
      .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = Context->Queue_Family_Index,
    },
                                 /*pAllocator   =>*/ NULL,
                                 /*pCommandPool =>*/ &Context->Command_Pool));

  VK_CHECK (vkAllocateCommandBuffers (/*device          =>*/ Context->Device,
                                      /*pAllocateInfo   =>*/ &(VkCommandBufferAllocateInfo){
      .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool        = Context->Command_Pool,
      .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
    },
                                      /*pCommandBuffers =>*/ &Context->Command_Buffer));

  VK_CHECK (vkCreateFence (/*device      =>*/ Context->Device,
                           /*pCreateInfo =>*/ &(VkFenceCreateInfo){
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    },
                           /*pAllocator  =>*/ NULL,
                           /*pFence      =>*/ &Context->Fence));

  VkSemaphoreCreateInfo Semaphore_Info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
  VK_CHECK (vkCreateSemaphore (Context->Device, &Semaphore_Info, NULL, &Context->Semaphore_Image_Available));
  VK_CHECK (vkCreateSemaphore (Context->Device, &Semaphore_Info, NULL, &Context->Semaphore_Render_Finished));
}

/* Transition the ray tracing storage image from undefined to general layout via a one-shot
   command buffer so it is ready for shader writes on the first frame. */

void Vulkan_Transition_Storage_Image (Vulkan_Context *Context) {
  VK_CHECK (vkResetCommandBuffer (Context->Command_Buffer, 0));
  VK_CHECK (vkBeginCommandBuffer (/*commandBuffer =>*/ Context->Command_Buffer,
                                  /*pBeginInfo    =>*/ &(VkCommandBufferBeginInfo){
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    }));

  // Record and submit a layout transition from undefined to general for storage writes
  Image_Layout_Barrier (/*Command_Buffer     =>*/ Context->Command_Buffer,
                        /*Image              =>*/ Context->Raytracing_Storage_Image.Image,
                        /*Old_Layout         =>*/ VK_IMAGE_LAYOUT_UNDEFINED,
                        /*New_Layout         =>*/ VK_IMAGE_LAYOUT_GENERAL,
                        /*Source_Access      =>*/ 0,
                        /*Destination_Access =>*/ VK_ACCESS_SHADER_WRITE_BIT,
                        /*Source_Stage       =>*/ VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        /*Destination_Stage  =>*/ VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

  VK_CHECK (vkEndCommandBuffer (Context->Command_Buffer));
  VK_CHECK (vkQueueSubmit (/*queue       =>*/ Context->Queue,
                           /*submitCount =>*/ 1,
                           /*pSubmits    =>*/ &(VkSubmitInfo){
      .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers    = &Context->Command_Buffer,
    },
                           /*fence       =>*/ VK_NULL_HANDLE));
  VK_CHECK (vkQueueWaitIdle (Context->Queue));
}

/* <<bsp_loader>> ======================================================================================= */

#define BSP_MAGIC               0x50534249u
#define BSP_VERSION             46
#define BSP_ENTITIES            0
#define BSP_SHADERS             1
#define BSP_VERTICES            10
#define BSP_INDICES             11
#define BSP_FACES               13
#define BSP_PLANES              2
#define BSP_NODES               3
#define BSP_LEAFS               4
#define BSP_LEAF_SURFACES       5
#define BSP_LEAF_BRUSHES        6
#define BSP_BRUSHES             8
#define BSP_BRUSH_SIDES         9
#define BSP_LIGHTMAPS           14
#define CONTENTS_SOLID          1
#define SURFACE_CLIP_EPSILON    0.125f
#define SURFACE_TYPE_PLANAR     1
#define SURFACE_TYPE_PATCH      2
#define SURFACE_TYPE_MESH       3
#define TESSELLATION_LEVEL      5
#define LIGHTMAP_PAGE_SIZE      128

// BSP lump directory entry: byte offset and length of a data lump within the file
typedef struct {I32 Offset, Length;} Bsp_Lump;

// BSP file header: magic number, version, and the 17-entry lump directory
typedef struct {
  U32      Magic, Version; // Magic (0x50534249 = "IBSP") and format version (46 for Quake 3)
  Bsp_Lump Lumps[17];      // Directory of data lumps indexed by BSP_ENTITIES..BSP_LIGHTMAPS
} Bsp_Header;

// BSP vertex as stored on disk: position, two UV sets, normal, and vertex color
typedef struct {
  F32 Position[3], Texture_Coords[2], Lightmap_Coords[2]; // World XYZ, diffuse UVs, lightmap UVs
  F32 Normal[3];                                          // Unit surface normal
  U8  Color[4];                                           // RGBA vertex color (0-255 per channel)
} Bsp_Vertex;

// BSP face/surface descriptor: references into vertex, index, and lightmap data
typedef struct {
  I32 Shader_Index;                            // Material shader
  I32 Fog_Volume;                              // Fog reference
  I32 Type;                                    // Surface type (planar/patch/mesh)
  I32 First_Vertex,       Vertex_Count;        // Starting vertex index and count in the global vertex array
  I32 First_Index,        Index_Count;         // Starting element index and count in the global index array
  I32 Lightmap_Index;                          // Lightmap page index (-1 if none)
  I32 Lightmap_X,         Lightmap_Y;          // Top-left corner of this face's lightmap within its page
  I32 Lightmap_Width,     Lightmap_Height;     // Dimensions of the lightmap region in texels
  F32 Lightmap_Origin[3], Lightmap_Vectors[9]; // World-space lightmap placement (origin + 2 basis vectors + normal)
  I32 Patch_Width,        Patch_Height;        // Control point grid dimensions (only valid for patch surfaces)
} Bsp_Face;

// BSP shader entry: maps a surface material name to content and surface flags
typedef struct {
  char Name[64];        // Shader path (e.g. "textures/gothic_wall/wall01")
  I32  Flags, Contents; // Surface flags (e.g. translucent) and content flags (e.g. solid, water)
} Bsp_Shader;

// Axis-aligned splitting plane for BSP tree traversal and brush collision
typedef struct {
  F32 Normal[3];                   // Plane normal (axis-aligned planes use 1,0,0 / 0,1,0 / 0,0,1)
  F32 Distance;                    // Signed distance from origin along the normal
  U8  Type, Sign_Bits, Padding[2]; // Axis type for fast-path tests and sign bits for AABB checks
} Collision_Plane;

// BSP tree interior node: splits space by a plane into front and back children
typedef struct {
  I32 Plane_Index; // Index of the splitting plane
  I32 Children[2]; // Front and back child indices (negative values encode leaf indices as ~Child)
} Collision_Node;

// BSP tree leaf node: references a range of surfaces and brushes for collision testing
typedef struct {
  I32 Cluster, Area;                // PVS cluster and area portal indices (unused for collision)
  I32 First_Surface, Surface_Count; // Range of surfaces in this leaf
  I32 First_Brush, Brush_Count;     // Range of brush indices in the leaf brush array
} Collision_Leaf;

// Convex brush: a solid volume defined as the intersection of half-spaces (brush sides)
typedef struct {
  I32 First_Side, Side_Count, Shader_Index; // Range of bounding planes and the material shader
} Collision_Brush;

// Single face of a convex brush, referencing a bounding plane
typedef struct {
  I32 Plane_Index, Shader_Index; // Plane defining this face and its material shader
} Collision_Brush_Side;

// Complete collision map loaded from the BSP: all spatial data structures for trace queries
typedef struct {
  Collision_Plane      *Planes;       U32 Plane_Count;      // Splitting and brush planes
  Collision_Node       *Nodes;        U32 Node_Count;       // BSP tree interior nodes
  Collision_Leaf       *Leafs;        U32 Leaf_Count;       // BSP tree leaf nodes
  Collision_Brush      *Brushes;      U32 Brush_Count;      // Convex solid brushes
  Collision_Brush_Side *Sides;        U32 Side_Count;       // Brush face planes
  I32                  *Leaf_Brushes; U32 Leaf_Brush_Count; // Indirection array: leaf brush ranges index into this
  I32                  *Shader_Contents;                    // Content flags per shader index (for solidity tests)
  U32                   Check_Counter;                      // Monotonic counter to avoid testing the same brush twice
  U32                  *Brush_Checks;                       // Per-brush last-checked counter (compared to Check_Counter)
} Collision_Map;

/* Convert a BSP vertex from Quake 3's Z-up coordinate system to our Y-up system: (x,y,z) becomes (x,z,-y). */

Vertex Convert_Bsp_Vertex (const Bsp_Vertex *Source) {
  return (Vertex){
    .Position    = {Source->Position[0],        Source->Position[2], -Source->Position[1]},
    .Texture_Uv  = {Source->Texture_Coords[0],  Source->Texture_Coords[1]},
    .Lightmap_Uv = {Source->Lightmap_Coords[0], Source->Lightmap_Coords[1]},
    .Normal      = {Source->Normal[0],          Source->Normal[2],   -Source->Normal[1]},
  };
}

/* Evaluate a quadratic Bezier curve at parameter t given three control points. */

V3 Bezier_Evaluate (V3 Control_A, V3 Control_B, V3 Control_C, F32 Parameter) {
  F32 Complement = 1 - Parameter;
  return V3_Add (/*Left  =>*/ V3_Add (V3_Scale (Control_A,     Complement * Complement),
                                      V3_Scale (Control_B, 2 * Complement * Parameter)),
                 /*Right =>*/ V3_Scale (Control_C, Parameter * Parameter));
}

/* Tessellate a Bezier patch surface from its control grid into triangles.  The patch is subdivided
   into a grid of sub-patches (each defined by a 3x3 control point block), and each sub-patch is
   evaluated at TESSELLATION_LEVEL intervals to produce a smooth triangle mesh. */

U32 Bsp_Tessellate_Patch (const Bsp_Vertex *Control_Grid, int Patch_Width, int Patch_Height,
                                 Vertex **Inout_Vertices, U32 *Inout_Vertex_Count,
                                 U32 **Inout_Indices, U32 *Inout_Index_Count) {
  int Grid_Columns = (Patch_Width - 1) / 2;
  int Grid_Rows    = (Patch_Height - 1) / 2;
  int Level        = TESSELLATION_LEVEL;
  int Stride       = Level + 1;

  // Compute the total vertices and indices this patch will produce and grow the output arrays
  U32 Added_Vertices = (U32)(Grid_Columns * Grid_Rows * Stride * Stride);
  U32 Added_Indices  = (U32)(Grid_Columns * Grid_Rows * Level * Level * 6);

  // Grow the output vertex and index arrays to hold the tessellated patch geometry
  *Inout_Vertices = realloc (*Inout_Vertices, sizeof (Vertex) * (*Inout_Vertex_Count + Added_Vertices));
  *Inout_Indices  = realloc (*Inout_Indices,  sizeof (U32)    * (*Inout_Index_Count  + Added_Indices));

  // Track the insertion points for new vertices and indices
  U32 Vertex_Base  = *Inout_Vertex_Count;
  U32 Index_Cursor = *Inout_Index_Count;

  // Iterate over each 3x3 sub-patch in the control grid
  for (int Patch_Y = 0; Patch_Y < Grid_Rows; Patch_Y++) for (int Patch_X = 0; Patch_X < Grid_Columns; Patch_X++) {
    V3 Control_Position[3][3];
    V3 Control_Normal[3][3];
    V3 Control_Texture[3][3];
    V3 Control_Lightmap[3][3];

    // Extract the 3x3 control points for this sub-patch, applying coordinate swizzle
    for (int Row = 0; Row < 3; Row++)
      for (int Column = 0; Column < 3; Column++) {
        const Bsp_Vertex *Vertex_Source = &Control_Grid[(Patch_Y * 2 + Row) * Patch_Width + (Patch_X * 2 + Column)];
        Control_Position[Row][Column]   = V3_Make (Vertex_Source->Position[0],        Vertex_Source->Position[2], -Vertex_Source->Position[1]);
        Control_Normal[Row][Column]     = V3_Make (Vertex_Source->Normal[0],          Vertex_Source->Normal[2],   -Vertex_Source->Normal[1]);
        Control_Texture[Row][Column]    = V3_Make (Vertex_Source->Texture_Coords[0],  Vertex_Source->Texture_Coords[1], 0);
        Control_Lightmap[Row][Column]   = V3_Make (Vertex_Source->Lightmap_Coords[0], Vertex_Source->Lightmap_Coords[1], 0);
      }

      // Compute the base vertex offset for this sub-patch in the output array
    U32 Patch_Base = Vertex_Base + (U32)((Patch_Y * Grid_Columns + Patch_X) * Stride * Stride);

    // Evaluate the bi-quadratic Bezier surface at each tessellation grid point
    for (int Vertical = 0; Vertical <= Level; Vertical++) {
      F32 Parameter_V = (F32)Vertical / Level;
      V3 Row_Position[3], Row_Normal[3], Row_Texture[3], Row_Lightmap[3];

      // First pass: evaluate each row of control points along the V parameter
      for (int Row = 0; Row < 3; Row++) {
        Row_Position[Row] = Bezier_Evaluate (Control_Position[Row][0], Control_Position[Row][1], Control_Position[Row][2], Parameter_V);
        Row_Normal[Row]   = Bezier_Evaluate (Control_Normal[Row][0],   Control_Normal[Row][1],   Control_Normal[Row][2],   Parameter_V);
        Row_Texture[Row]  = Bezier_Evaluate (Control_Texture[Row][0],  Control_Texture[Row][1],  Control_Texture[Row][2],  Parameter_V);
        Row_Lightmap[Row] = Bezier_Evaluate (Control_Lightmap[Row][0], Control_Lightmap[Row][1], Control_Lightmap[Row][2], Parameter_V);
      }

      // Second pass: evaluate the intermediate row results along the U parameter
      for (int Horizontal = 0; Horizontal <= Level; Horizontal++) {
        F32 Parameter_U = (F32)Horizontal / Level;
        V3 Position = Bezier_Evaluate (Row_Position[0], Row_Position[1], Row_Position[2], Parameter_U);
        V3 Normal   = V3_Normalize (Bezier_Evaluate (Row_Normal[0], Row_Normal[1], Row_Normal[2], Parameter_U));
        V3 Texture  = Bezier_Evaluate (Row_Texture[0],  Row_Texture[1],  Row_Texture[2],  Parameter_U);
        V3 Lightmap = Bezier_Evaluate (Row_Lightmap[0], Row_Lightmap[1], Row_Lightmap[2], Parameter_U);

        // Evaluate the bi-quadratic Bezier surface and store the interpolated vertex
        (*Inout_Vertices)[Patch_Base + Vertical * Stride + Horizontal] = (Vertex){
          .Position    = {Position.x, Position.y, Position.z },
          .Texture_Uv  = {Texture.x, Texture.y },
          .Lightmap_Uv = {Lightmap.x, Lightmap.y },
          .Normal      = {Normal.x, Normal.y, Normal.z },
        };
      }
    }

    // Generate two triangles for each quad in the tessellated grid
    for (int Vertical = 0; Vertical < Level; Vertical++) for (int Horizontal = 0; Horizontal < Level; Horizontal++) {
      U32 Index_A = Patch_Base + Vertical * Stride + Horizontal;
      U32 Index_B = Index_A + 1;
      U32 Index_C = Patch_Base + (Vertical + 1) * Stride + Horizontal;
      U32 Index_D = Index_C + 1;
      (*Inout_Indices)[Index_Cursor++] = Index_A;
      (*Inout_Indices)[Index_Cursor++] = Index_C;
      (*Inout_Indices)[Index_Cursor++] = Index_B;
      (*Inout_Indices)[Index_Cursor++] = Index_B;
      (*Inout_Indices)[Index_Cursor++] = Index_C;
      (*Inout_Indices)[Index_Cursor++] = Index_D;
    }
  }

  // Advance the output counts by the number of vertices and indices added
  *Inout_Vertex_Count += Added_Vertices;
  *Inout_Index_Count  += Added_Indices;
  return Added_Indices / 3;
}

/* Forward declaration: entity parsing needs the BSP header. */
Spawn Bsp_Find_Spawn (const U8 *File_Data, const Bsp_Header *Header);

/* Load a complete scene from a Quake 3 BSP file.  This parses vertices, indices, faces (planar,
   mesh, and patch types), shader references, lightmap pages (packed into a single atlas), and
   optionally populates a collision map and spawn point. */

Scene Scene_Load_From_Bsp (const char *Path, Spawn *Out_Spawn, Collision_Map *Out_Collision) {

  // Read the entire BSP file into memory
  FILE *File = fopen (Path, "rb");
  if (not File) {fprintf (stderr, "Cannot open %s\n", Path); exit (1); }
  fseek (File, 0, SEEK_END);
  long File_Size = ftell (File);
  rewind (File);
  U8 *File_Data = malloc (File_Size);
  fread (File_Data, 1, File_Size, File);
  fclose (File);

  // Validate the BSP magic number and version
  Bsp_Header *Header = (Bsp_Header *)(File_Data);
  assert (Header->Magic == BSP_MAGIC and Header->Version == BSP_VERSION);

  // Locate the raw lump data for vertices, indices, faces, and shaders
  Bsp_Vertex *Raw_Vertices   = (Bsp_Vertex *)(File_Data + Header->Lumps[BSP_VERTICES].Offset);
  U32 Raw_Vertex_Count       = (U32)(Header->Lumps[BSP_VERTICES].Length / sizeof (Bsp_Vertex));
  I32 *Raw_Indices           = (I32 *)(File_Data + Header->Lumps[BSP_INDICES].Offset);
  Bsp_Face *Raw_Faces        = (Bsp_Face *)(File_Data + Header->Lumps[BSP_FACES].Offset);
  U32 Raw_Face_Count         = (U32)(Header->Lumps[BSP_FACES].Length / sizeof (Bsp_Face));
  Bsp_Shader *Raw_Shaders    = (Bsp_Shader *)(File_Data + Header->Lumps[BSP_SHADERS].Offset);
  U32 Raw_Shader_Count       = (U32)(Header->Lumps[BSP_SHADERS].Length / sizeof (Bsp_Shader));

  // Build the lightmap atlas by packing all 128x128 lightmap pages into a single texture
  U32 Lightmap_Lump_Size  = (U32)Header->Lumps[BSP_LIGHTMAPS].Length;
  U32 Lightmap_Page_Count = Lightmap_Lump_Size / (LIGHTMAP_PAGE_SIZE * LIGHTMAP_PAGE_SIZE * 3);
  U32 Total_Pages         = Lightmap_Page_Count + 1;
  U32 Atlas_Columns = 1, Atlas_Rows = 1;
  U8 *Lightmap_Atlas = NULL;
  U32 Atlas_Width = 0, Atlas_Height = 0;
  F32 White_Fallback_U = 0.5f, White_Fallback_V = 0.5f;

  // Pack lightmap pages into a grid atlas if any lightmaps exist
  if (Lightmap_Page_Count > 0) {

    // Determine the atlas grid dimensions (square-ish layout)
    while (Atlas_Columns * Atlas_Columns < Total_Pages) Atlas_Columns++;
    Atlas_Rows   = (Total_Pages + Atlas_Columns - 1) / Atlas_Columns;
    Atlas_Width  = Atlas_Columns * LIGHTMAP_PAGE_SIZE;
    Atlas_Height = Atlas_Rows * LIGHTMAP_PAGE_SIZE;
    Lightmap_Atlas = calloc (Atlas_Width * Atlas_Height * 4, 1);

    // Locate the raw lightmap data in the BSP file
    const U8 *Lightmap_Data = File_Data + Header->Lumps[BSP_LIGHTMAPS].Offset;

    // Copy each RGB lightmap page into its grid cell, converting RGB to RGBA
    for (U32 Page = 0; Page < Lightmap_Page_Count; Page++) {
      U32 Column     = Page % Atlas_Columns;
      U32 Row        = Page / Atlas_Columns;
      const U8 *Source = Lightmap_Data + Page * LIGHTMAP_PAGE_SIZE * LIGHTMAP_PAGE_SIZE * 3;
      for (U32 y = 0; y < LIGHTMAP_PAGE_SIZE; y++)
      for (U32 x = 0; x < LIGHTMAP_PAGE_SIZE; x++) {
        U32 Destination  = ((Row * LIGHTMAP_PAGE_SIZE + y) * Atlas_Width + Column * LIGHTMAP_PAGE_SIZE + x) * 4;
        U32 Source_Index = (y * LIGHTMAP_PAGE_SIZE + x) * 3;
        Lightmap_Atlas[Destination]     = Source[Source_Index];
        Lightmap_Atlas[Destination + 1] = Source[Source_Index + 1];
        Lightmap_Atlas[Destination + 2] = Source[Source_Index + 2];
        Lightmap_Atlas[Destination + 3] = 255;
      }
    }

    // Fill an extra all-white page as a fallback for faces with no lightmap
    U32 White_Column = Lightmap_Page_Count % Atlas_Columns;
    U32 White_Row    = Lightmap_Page_Count / Atlas_Columns;
    for (U32 y = 0; y < LIGHTMAP_PAGE_SIZE; y++)
    for (U32 x = 0; x < LIGHTMAP_PAGE_SIZE; x++) {
      U32 Destination = ((White_Row * LIGHTMAP_PAGE_SIZE + y) * Atlas_Width + White_Column * LIGHTMAP_PAGE_SIZE + x) * 4;
      Lightmap_Atlas[Destination] = Lightmap_Atlas[Destination + 1] = Lightmap_Atlas[Destination + 2] = Lightmap_Atlas[Destination + 3] = 255;
    }
    White_Fallback_U = ((F32)White_Column + 0.5f) / (F32)Atlas_Columns;
    White_Fallback_V = ((F32)White_Row    + 0.5f) / (F32)Atlas_Rows;
    printf ("[lightmap] %u pages -> %ux%u atlas (%u columns)\n", Lightmap_Page_Count, Atlas_Width, Atlas_Height, Atlas_Columns);
  }

  // Convert all BSP vertices from Z-up to Y-up coordinate system
  U32 Vertex_Count = Raw_Vertex_Count;
  Vertex *Vertices = malloc (sizeof (Vertex) * Vertex_Count);
  for (U32 Index = 0; Index < Vertex_Count; Index++)
    Vertices[Index] = Convert_Bsp_Vertex (&Raw_Vertices[Index]);

  // Allocate growing arrays for assembled indices and per-triangle texture IDs
  U32 *Indices = NULL, *Texture_Ids = NULL;
  U32  Index_Count = 0, Triangle_Count = 0;

  // Process each face: planar and mesh faces copy indices directly; patch faces are tessellated
  for (U32 Face_Index = 0; Face_Index < Raw_Face_Count; Face_Index++) {
    const Bsp_Face *Face = &Raw_Faces[Face_Index];

    // Handle each face type: copy indices for planar/mesh, tessellate for patches
    if (Face->Type == SURFACE_TYPE_PLANAR or Face->Type == SURFACE_TYPE_MESH) {
      U32 Face_Triangles = (U32)(Face->Index_Count / 3);

      // Grow the index and texture-id arrays to accommodate this face
      Indices     = realloc (Indices,     sizeof (U32) * (Index_Count    + Face->Index_Count));
      Texture_Ids = realloc (Texture_Ids, sizeof (U32) * (Triangle_Count + Face_Triangles));

      // Copy face indices, offsetting by the face's first vertex
      for (I32 Loop = 0; Loop < Face->Index_Count; Loop++)
        Indices[Index_Count + Loop] = (U32)(Face->First_Vertex + Raw_Indices[Face->First_Index + Loop]);

      // Assign the face's shader index to each triangle
      for (U32 Triangle = 0; Triangle < Face_Triangles; Triangle++)
        Texture_Ids[Triangle_Count + Triangle] = (U32)Face->Shader_Index;

      // Advance the running index and triangle counts
      Index_Count    += Face->Index_Count;
      Triangle_Count += Face_Triangles;

      // Remap lightmap UVs from per-page to atlas space, or use the white fallback
      if (Face->Lightmap_Index >= 0 and Atlas_Columns > 0) {
        F32 Column_Offset = (F32)((U32)Face->Lightmap_Index % Atlas_Columns);
        F32 Row_Offset    = (F32)((U32)Face->Lightmap_Index / Atlas_Columns);
        for (I32 Vertex_Loop = 0; Vertex_Loop < Face->Vertex_Count; Vertex_Loop++) {
          U32 Vertex_Index = (U32)(Face->First_Vertex + Vertex_Loop);
          Vertices[Vertex_Index].Lightmap_Uv[0] = (Column_Offset + Vertices[Vertex_Index].Lightmap_Uv[0]) / (F32)Atlas_Columns;
          Vertices[Vertex_Index].Lightmap_Uv[1] = (Row_Offset    + Vertices[Vertex_Index].Lightmap_Uv[1]) / (F32)Atlas_Rows;
        }
      } else {
        for (I32 Vertex_Loop = 0; Vertex_Loop < Face->Vertex_Count; Vertex_Loop++) {
          U32 Vertex_Index = (U32)(Face->First_Vertex + Vertex_Loop);
          Vertices[Vertex_Index].Lightmap_Uv[0] = White_Fallback_U;
          Vertices[Vertex_Index].Lightmap_Uv[1] = White_Fallback_V;
        }
      }

    // Tessellate the Bezier patch into triangles
    } else if (Face->Type == SURFACE_TYPE_PATCH) {
      U32 Previous_Vertex_Count   = Vertex_Count;
      U32 Previous_Triangle_Count = Triangle_Count;

      // Tessellate the Bezier patch and assign shader indices to the new triangles
      Triangle_Count += Bsp_Tessellate_Patch (/*Control_Grid       =>*/ &Raw_Vertices[Face->First_Vertex],
                                              /*Patch_Width        =>*/ Face->Patch_Width,
                                              /*Patch_Height       =>*/ Face->Patch_Height,
                                              /*Inout_Vertices     =>*/ &Vertices,
                                              /*Inout_Vertex_Count =>*/ &Vertex_Count,
                                              /*Inout_Indices      =>*/ &Indices,
                                              /*Inout_Index_Count  =>*/ &Index_Count);

      // Assign shader indices to the newly tessellated triangles
      U32 Patch_Triangles = Triangle_Count - Previous_Triangle_Count;
      Texture_Ids = realloc (Texture_Ids, sizeof (U32) * Triangle_Count);
      for (U32 Triangle = 0; Triangle < Patch_Triangles; Triangle++)
        Texture_Ids[Previous_Triangle_Count + Triangle] = (U32)Face->Shader_Index;

      // Remap lightmap UVs for the tessellated vertices
      if (Face->Lightmap_Index >= 0 and Atlas_Columns > 0) {
        F32 Column_Offset = (F32)((U32)Face->Lightmap_Index % Atlas_Columns);
        F32 Row_Offset    = (F32)((U32)Face->Lightmap_Index / Atlas_Columns);
        for (U32 Vertex_Index = Previous_Vertex_Count; Vertex_Index < Vertex_Count; Vertex_Index++) {
          Vertices[Vertex_Index].Lightmap_Uv[0] = (Column_Offset + Vertices[Vertex_Index].Lightmap_Uv[0]) / (F32)Atlas_Columns;
          Vertices[Vertex_Index].Lightmap_Uv[1] = (Row_Offset    + Vertices[Vertex_Index].Lightmap_Uv[1]) / (F32)Atlas_Rows;
        }
      } else {
        for (U32 Vertex_Index = Previous_Vertex_Count; Vertex_Index < Vertex_Count; Vertex_Index++) {
          Vertices[Vertex_Index].Lightmap_Uv[0] = White_Fallback_U;
          Vertices[Vertex_Index].Lightmap_Uv[1] = White_Fallback_V;
        }
      }
    }
  }

  // Build per-material fallback colors by hashing the shader name to a deterministic RGB value
  U32 Material_Count = Raw_Shader_Count;
  V4 *Materials = malloc (sizeof (V4) * Material_Count);
  char (*Texture_Names)[64] = malloc (sizeof (char[64]) * Material_Count);

    // Hash each shader name to generate a deterministic fallback color
  for (U32 Material = 0; Material < Material_Count; Material++) {
    U32 Hash = 5381;
    for (int Character = 0; Raw_Shaders[Material].Name[Character]; Character++)
      Hash = Hash * 31 + (U8)Raw_Shaders[Material].Name[Character];
    Materials[Material] = (V4){
      0.4f + 0.35f * ((Hash >> 0  & 0xFF) / 255.f),
      0.4f + 0.35f * ((Hash >> 8  & 0xFF) / 255.f),
      0.4f + 0.35f * ((Hash >> 16 & 0xFF) / 255.f),
      1,
    };
    memcpy (Texture_Names[Material], Raw_Shaders[Material].Name, 64);
  }

  // Parse the spawn point from the entity lump
  if (Out_Spawn)
    *Out_Spawn = Bsp_Find_Spawn (File_Data, Header);

  // Load collision data from planes, nodes, leafs, brushes, and brush sides lumps
  if (Out_Collision) {
    Collision_Map *Collision = Out_Collision;
    memset (Collision, 0, sizeof (*Collision));

    // Parse planes (16 bytes each: float normal[3], float distance) with coordinate swizzle
    Collision->Plane_Count = (U32)Header->Lumps[BSP_PLANES].Length / 16;
    Collision->Planes      = malloc (sizeof (Collision_Plane) * Collision->Plane_Count);
    for (U32 Index = 0; Index < Collision->Plane_Count; Index++) {
      const F32 *Source = (const F32 *)(File_Data + Header->Lumps[BSP_PLANES].Offset + Index * 16);
      Collision->Planes[Index].Normal[0] =  Source[0];
      Collision->Planes[Index].Normal[1] =  Source[2];
      Collision->Planes[Index].Normal[2] = -Source[1];
      Collision->Planes[Index].Distance  =  Source[3];
      F32 *Normal = Collision->Planes[Index].Normal;
      Collision->Planes[Index].Type      = (Normal[0] == 1.f) ? 0 : (Normal[1] == 1.f) ? 1 : (Normal[2] == 1.f) ? 2 : 3;
      Collision->Planes[Index].Sign_Bits = (U8)((Normal[0] < 0) | ((Normal[1] < 0) << 1) | ((Normal[2] < 0) << 2));
    }

    // Parse BSP nodes (36 bytes each: int plane, children[2], mins[3], maxs[3])
    Collision->Node_Count = (U32)Header->Lumps[BSP_NODES].Length / 36;
    Collision->Nodes      = malloc (sizeof (Collision_Node) * Collision->Node_Count);
    for (U32 Index = 0; Index < Collision->Node_Count; Index++) {
      const I32 *Source = (const I32 *)(File_Data + Header->Lumps[BSP_NODES].Offset + Index * 36);
      Collision->Nodes[Index].Plane_Index = Source[0];
      Collision->Nodes[Index].Children[0] = Source[1];
      Collision->Nodes[Index].Children[1] = Source[2];
    }

    // Parse BSP leafs (48 bytes each)
    Collision->Leaf_Count = (U32)Header->Lumps[BSP_LEAFS].Length / 48;
    Collision->Leafs      = malloc (sizeof (Collision_Leaf) * Collision->Leaf_Count);
    for (U32 Index = 0; Index < Collision->Leaf_Count; Index++) {
      const I32 *Source = (const I32 *)(File_Data + Header->Lumps[BSP_LEAFS].Offset + Index * 48);
      Collision->Leafs[Index].Cluster       = Source[0];
      Collision->Leafs[Index].Area          = Source[1];
      Collision->Leafs[Index].First_Surface = Source[8];
      Collision->Leafs[Index].Surface_Count = Source[9];
      Collision->Leafs[Index].First_Brush   = Source[10];
      Collision->Leafs[Index].Brush_Count   = Source[11];
    }

    // Copy leaf brush index array verbatim
    Collision->Leaf_Brush_Count = (U32)Header->Lumps[BSP_LEAF_BRUSHES].Length / 4;
    Collision->Leaf_Brushes     = malloc (sizeof (I32) * Collision->Leaf_Brush_Count);
    memcpy (Collision->Leaf_Brushes, File_Data + Header->Lumps[BSP_LEAF_BRUSHES].Offset, sizeof (I32) * Collision->Leaf_Brush_Count);

    // Parse brushes (12 bytes each: int first_side, side_count, shader)
    Collision->Brush_Count = (U32)Header->Lumps[BSP_BRUSHES].Length / 12;
    Collision->Brushes     = malloc (sizeof (Collision_Brush) * Collision->Brush_Count);
    for (U32 Index = 0; Index < Collision->Brush_Count; Index++) {
      const I32 *Source = (const I32 *)(File_Data + Header->Lumps[BSP_BRUSHES].Offset + Index * 12);
      Collision->Brushes[Index].First_Side   = Source[0];
      Collision->Brushes[Index].Side_Count   = Source[1];
      Collision->Brushes[Index].Shader_Index = Source[2];
    }

    // Parse brush sides (8 bytes each: int plane, shader)
    Collision->Side_Count = (U32)Header->Lumps[BSP_BRUSH_SIDES].Length / 8;
    Collision->Sides      = malloc (sizeof (Collision_Brush_Side) * Collision->Side_Count);
    for (U32 Index = 0; Index < Collision->Side_Count; Index++) {
      const I32 *Source = (const I32 *)(File_Data + Header->Lumps[BSP_BRUSH_SIDES].Offset + Index * 8);
      Collision->Sides[Index].Plane_Index  = Source[0];
      Collision->Sides[Index].Shader_Index = Source[1];
    }

    // Extract the contents flags from each shader for solid-brush filtering
    Collision->Shader_Contents = malloc (sizeof (I32) * Raw_Shader_Count);
    for (U32 Shader = 0; Shader < Raw_Shader_Count; Shader++)
      Collision->Shader_Contents[Shader] = Raw_Shaders[Shader].Contents;

    // Allocate the per-brush deduplication check array
    Collision->Brush_Checks  = calloc (Collision->Brush_Count, sizeof (U32));
    Collision->Check_Counter = 0;

    // Report the collision map statistics
    printf (/*format =>*/ "[collision] %u planes, "
            "%u nodes, "
            "%u leafs, "
            "%u brushes, "
            "%u sides\n",
            Collision->Plane_Count,
            Collision->Node_Count,
            Collision->Leaf_Count,
            Collision->Brush_Count,
            Collision->Side_Count);
  }

  // Release the raw BSP file buffer and return the assembled scene
  free (File_Data);
  printf ("[bsp] %s: %u vertices, %u triangles, %u shaders\n", Path, Vertex_Count, Triangle_Count, Raw_Shader_Count);

  return (Scene){
    .Vertices        = Vertices,       .Vertex_Count    = Vertex_Count,
    .Indices         = Indices,        .Index_Count     = Index_Count,
    .Materials       = Materials,      .Material_Count  = Material_Count,
    .Texture_Ids     = Texture_Ids,    .Texture_Names   = Texture_Names,
    .Triangle_Count  = Triangle_Count,
    .Lightmap_Atlas  = Lightmap_Atlas, .Lightmap_Width  = Atlas_Width,    .Lightmap_Height = Atlas_Height,
  };
}

/* <<bsp_entities>> ===================================================================================== */

/* Parse the BSP entity lump to find the first info_player_deathmatch spawn point.
   Returns the origin (swizzled to Y-up) and facing angle. */

Spawn Bsp_Find_Spawn (const U8 *File_Data, const Bsp_Header *Header) {
  const char *Entities = (const char *)(File_Data + Header->Lumps[BSP_ENTITIES].Offset);
  I32 Length = Header->Lumps[BSP_ENTITIES].Length;
  Spawn Result = {.Origin = {0, 0, 0}, .Angle = 0 };

  // Set up cursor to walk through the entity lump text
  const char *Cursor = Entities;
  const char *End    = Entities + Length;

  // Walk through each entity block delimited by curly braces
  while (Cursor < End) {
    while (Cursor < End and *Cursor != '{') Cursor++;
    if (Cursor >= End) break;
    Cursor++;

    // Initialize per-entity state for tracking spawn candidates
    int  Is_Spawn   = 0;
    V3   Origin     = {0, 0, 0};
    F32  Angle      = 0;
    int  Has_Origin = 0;

    // Parse key-value pairs within this entity
    while (Cursor < End and *Cursor != '}') {
      while (Cursor < End and (*Cursor == ' ' or *Cursor == '\t' or *Cursor == '\n' or *Cursor == '\r'))
        Cursor++;
      if (Cursor >= End or *Cursor == '}') break;

      // Read the quoted key string
      if (*Cursor != '"') {Cursor++; continue; }
      Cursor++;
      const char *Key = Cursor;
      while (Cursor < End and *Cursor != '"') Cursor++;
      int Key_Length = (int)(Cursor - Key);
      if (Cursor < End) Cursor++;

      // Skip leading whitespace before the value string
      while (Cursor < End and (*Cursor == ' ' or *Cursor == '\t')) Cursor++;

      // Read the quoted value string
      if (Cursor >= End or *Cursor != '"') continue;
      Cursor++;
      const char *Value = Cursor;
      while (Cursor < End and *Cursor != '"') Cursor++;
      int Value_Length = (int)(Cursor - Value);
      if (Cursor < End) Cursor++;

      // Check if this entity is a deathmatch spawn point
      if (Key_Length == 9 and memcmp (Key, "classname", 9) == 0
          and Value_Length == 22 and memcmp (Value, "info_player_deathmatch", 22) == 0)
        Is_Spawn = 1;

      // Parse the "origin" key into three floats
      if (Key_Length == 6 and memcmp (Key, "origin", 6) == 0) {
        char Temporary[64];
        int Limit = Value_Length < 63 ? Value_Length : 63;
        memcpy (Temporary, Value, Limit);
        Temporary[Limit] = 0;
        sscanf (Temporary, "%f %f %f", &Origin.x, &Origin.y, &Origin.z);
        Has_Origin = 1;
      }

      // Parse the "angle" key into a facing direction
      if (Key_Length == 5 and memcmp (Key, "angle", 5) == 0) {
        char Temporary[32];
        int Limit = Value_Length < 31 ? Value_Length : 31;
        memcpy (Temporary, Value, Limit);
        Temporary[Limit] = 0;
        sscanf (Temporary, "%f", &Angle);
      }
    }

    // If this entity is a spawn with a valid origin, swizzle and return it
    if (Is_Spawn and Has_Origin) {
      Result.Origin = V3_Make (Origin.x, Origin.z, -Origin.y);
      Result.Angle  = Angle;
      printf ("[bsp] spawn: %.0f %.0f %.0f angle %.0f\n", Result.Origin.x, Result.Origin.y, Result.Origin.z, Angle);
      return Result;
    }
    if (Cursor < End) Cursor++;
  }

  printf ("[bsp] no spawn found, using origin\n");
  return Result;
}

/* <<collision>> ======================================================================================== */

// Result of a collision trace: records the nearest contact point and surface normal
typedef struct {
  F32 Fraction;          // Parametric distance along the trace (0.0 = start, 1.0 = no collision)
  V3  End_Position;      // World-space position where the trace was stopped
  V3  Normal;            // Surface normal at the contact point
  int Start_Solid, All_Solid;  // Start_Solid: trace began inside a brush; All_Solid: trace is entirely embedded
} Trace_Result;

// Capsule collision shape: a sphere swept along a vertical segment
typedef struct {
  int Use_Capsule;   // Non-zero to use capsule collision instead of AABB
  F32 Radius;        // Radius of the capsule's hemispheres and cylinder
  F32 Half_Height;   // Half the height of the cylindrical body (center to hemisphere center)
  V3  Offset;        // Offset from the AABB center to the capsule's midpoint
} Trace_Sphere;

// Working state for a single collision trace through the BSP tree
typedef struct {
  V3           Start, End;      // World-space start and end points of the trace
  V3           Extents;         // Half-extents of the bounding box for broad-phase testing
  V3           Offsets[8];      // Eight AABB corner offsets for plane-side testing
  Trace_Result Result;          // Accumulated trace result (nearest hit so far)
  int          Is_Point;        // Non-zero if the trace is a zero-volume point trace
  Trace_Sphere Sphere;          // Capsule shape parameters (when Use_Capsule is set)
} Trace_Work;

/* Test a moving AABB or capsule against a single convex brush (intersection of half-spaces).
   Uses the separating-axis theorem: track the latest entry and earliest exit across all planes.
   If the brush starts solid, mark the result accordingly. */

void Trace_Against_Brush (Trace_Work *Work, const Collision_Brush *Brush, const Collision_Map *Collision) {
  if (Brush->Side_Count <= 0) return;

  F32 Enter_Fraction = -1.f;
  F32 Leave_Fraction =  1.f;
  const Collision_Plane *Clip_Plane = NULL;
  int Gets_Out   = 0;
  int Starts_Out = 0;

  // Test the trace segment against each bounding plane of the brush
  for (I32 Side = 0; Side < Brush->Side_Count; Side++) {
    const Collision_Brush_Side *Brush_Side = &Collision->Sides[Brush->First_Side + Side];
    const Collision_Plane *Plane           = &Collision->Planes[Brush_Side->Plane_Index];

    // Compute the signed distance from the trace start and end to the offset plane
    F32 Distance, Distance_Start, Distance_End;

    // Dispatch on the trace shape to compute plane distances
    if (Work->Sphere.Use_Capsule) {

      // For capsule traces, offset the plane by the sphere radius and project the sphere center
      Distance = Plane->Distance + Work->Sphere.Radius;
      F32 Offset_Projection = Plane->Normal[0] * Work->Sphere.Offset.x
                            + Plane->Normal[1] * Work->Sphere.Offset.y
                            + Plane->Normal[2] * Work->Sphere.Offset.z;
      V3 Sphere_Start, Sphere_End;
      if (Offset_Projection > 0) {
        Sphere_Start = V3_Subtract (Work->Start, Work->Sphere.Offset);
        Sphere_End   = V3_Subtract (Work->End,   Work->Sphere.Offset);
      } else {
        Sphere_Start = V3_Add (Work->Start, Work->Sphere.Offset);
        Sphere_End   = V3_Add (Work->End,   Work->Sphere.Offset);
      }
      Distance_Start = Sphere_Start.x * Plane->Normal[0] + Sphere_Start.y * Plane->Normal[1] + Sphere_Start.z * Plane->Normal[2] - Distance;
      Distance_End   = Sphere_End.x   * Plane->Normal[0] + Sphere_End.y   * Plane->Normal[1] + Sphere_End.z   * Plane->Normal[2] - Distance;

    // Point trace: no extent offset needed
    } else if (Work->Is_Point) {
      Distance       = Plane->Distance;
      Distance_Start = Work->Start.x * Plane->Normal[0] + Work->Start.y * Plane->Normal[1] + Work->Start.z * Plane->Normal[2] - Distance;
      Distance_End   = Work->End.x   * Plane->Normal[0] + Work->End.y   * Plane->Normal[1] + Work->End.z   * Plane->Normal[2] - Distance;

    // AABB trace: use the sign-bits-indexed corner to maximize the offset
    } else {
      const V3 *Corner = &Work->Offsets[Plane->Sign_Bits];
      Distance       = Plane->Distance - (((F32 *)Corner)[0] * Plane->Normal[0] + ((F32 *)Corner)[1] * Plane->Normal[1] + ((F32 *)Corner)[2] * Plane->Normal[2]);
      Distance_Start = Work->Start.x * Plane->Normal[0] + Work->Start.y * Plane->Normal[1] + Work->Start.z * Plane->Normal[2] - Distance;
      Distance_End   = Work->End.x   * Plane->Normal[0] + Work->End.y   * Plane->Normal[1] + Work->End.z   * Plane->Normal[2] - Distance;
    }

    // Update the entry and exit fractions based on the signed distances
    if (Distance_End > 0)   Gets_Out   = 1;
    if (Distance_Start > 0) Starts_Out = 1;

    // If the start is in front and the end is also in front (or on the same side), this plane does not clip
    if (Distance_Start > 0 and (Distance_End >= SURFACE_CLIP_EPSILON or Distance_End >= Distance_Start))
      continue;

    // If both start and end are behind this plane, it does not contribute to clipping
    if (Distance_Start <= 0 and Distance_End <= 0)
      continue;

    // Compute the entry or exit fraction with an epsilon nudge for numerical stability
    if (Distance_Start > Distance_End) {
      F32 Fraction = (Distance_Start - SURFACE_CLIP_EPSILON) / (Distance_Start - Distance_End);
      if (Fraction < 0) Fraction = 0;
      if (Fraction > Enter_Fraction) {
        Enter_Fraction = Fraction;
        Clip_Plane     = Plane;
      }
    } else {
      F32 Fraction = (Distance_Start + SURFACE_CLIP_EPSILON) / (Distance_Start - Distance_End);
      if (Fraction > 1) Fraction = 1;
      if (Fraction < Leave_Fraction)
        Leave_Fraction = Fraction;
    }
  }

  // If the trace started inside all planes, the start position is embedded in solid
  if (not Starts_Out) {
    Work->Result.Start_Solid = 1;
    if (not Gets_Out) Work->Result.All_Solid = 1;
    Work->Result.Fraction = 0;
    return;
  }

  // If the entry fraction is before the exit and closer than any previous hit, record this collision
  if (Enter_Fraction < Leave_Fraction and Enter_Fraction > -1 and Enter_Fraction < Work->Result.Fraction) {
    if (Enter_Fraction < 0) Enter_Fraction = 0;
    Work->Result.Fraction = Enter_Fraction;
    Work->Result.Normal   = V3_Make (Clip_Plane->Normal[0], Clip_Plane->Normal[1], Clip_Plane->Normal[2]);
  }
}

/* Test the trace against all solid brushes referenced by a BSP leaf, skipping brushes
   that have already been tested during this trace (via the check counter). */

void Trace_Against_Leaf (Trace_Work *Work, const Collision_Leaf *Leaf, Collision_Map *Collision) {
  for (I32 Loop = 0; Loop < Leaf->Brush_Count; Loop++) {
    I32 Brush_Index = Collision->Leaf_Brushes[Leaf->First_Brush + Loop];
    if ((U32)Brush_Index >= Collision->Brush_Count) continue;

    // Skip brushes already tested in this trace pass
    if (Collision->Brush_Checks[Brush_Index] == Collision->Check_Counter) continue;
    Collision->Brush_Checks[Brush_Index] = Collision->Check_Counter;

    // Only test brushes whose shader has the CONTENTS_SOLID flag
    const Collision_Brush *Brush = &Collision->Brushes[Brush_Index];
    if (not (Collision->Shader_Contents[Brush->Shader_Index] & CONTENTS_SOLID)) continue;

    // Test the trace against this brush if it hasn't been checked this frame
    Trace_Against_Brush (Work, Brush, Collision);
    if (Work->Result.Fraction == 0.f) return;
  }
}

/* Recursively traverse the BSP tree, splitting the trace segment at each node plane.
   When a leaf is reached, test against its brushes.  Early-out if a previous hit
   is closer than the current node's range. */

void Trace_Through_Tree (Trace_Work *Work, int Node_Index, F32 Start_Fraction, F32 End_Fraction,
                                V3 Point_Start, V3 Point_End, Collision_Map *Collision) {

  // Early-out: a closer hit has already been found
  if (Work->Result.Fraction <= Start_Fraction) return;

  // Leaf node: test against the leaf's brushes
  if (Node_Index < 0) {
    Trace_Against_Leaf (Work, &Collision->Leafs[-(Node_Index + 1)], Collision);
    return;
  }

  const Collision_Node  *Node  = &Collision->Nodes[Node_Index];
  const Collision_Plane *Plane = &Collision->Planes[Node->Plane_Index];

  F32 Distance_Start, Distance_End, Offset;

  // For axis-aligned planes, use direct component access for speed
  if (Plane->Type < 3) {
    Distance_Start = ((F32 *)&Point_Start)[Plane->Type] - Plane->Distance;
    Distance_End   = ((F32 *)&Point_End)[Plane->Type]   - Plane->Distance;
    Offset         = ((F32 *)&Work->Extents)[Plane->Type];
  } else {
    V3 Plane_Normal = V3_Make (Plane->Normal[0], Plane->Normal[1], Plane->Normal[2]);
    Distance_Start  = V3_Dot (Point_Start, Plane_Normal) - Plane->Distance;
    Distance_End    = V3_Dot (Point_End,   Plane_Normal) - Plane->Distance;
    if (Work->Is_Point) {
      Offset = 0;
    } else {
      Offset = fabsf (Work->Extents.x * Plane->Normal[0])
             + fabsf (Work->Extents.y * Plane->Normal[1])
             + fabsf (Work->Extents.z * Plane->Normal[2]);
    }
  }

  // If the entire segment is on the front side of the plane, recurse into the front child only
  if (Distance_Start >= Offset + 1 and Distance_End >= Offset + 1) {
    Trace_Through_Tree (Work, Node->Children[0], Start_Fraction, End_Fraction, Point_Start, Point_End, Collision);
    return;
  }

  // If the entire segment is on the back side, recurse into the back child only
  if (Distance_Start < -Offset - 1 and Distance_End < -Offset - 1) {
    Trace_Through_Tree (Work, Node->Children[1], Start_Fraction, End_Fraction, Point_Start, Point_End, Collision);
    return;
  }

  // The segment straddles the plane; compute the near and far split fractions
  int Side_Index;
  F32 Fraction_Near, Fraction_Far;

  if (Distance_Start < Distance_End) {
    F32 Inverse   = 1.f / (Distance_Start - Distance_End);
    Side_Index    = 1;
    Fraction_Far  = (Distance_Start + Offset + SURFACE_CLIP_EPSILON) * Inverse;
    Fraction_Near = (Distance_Start - Offset + SURFACE_CLIP_EPSILON) * Inverse;
  } else if (Distance_Start > Distance_End) {
    F32 Inverse   = 1.f / (Distance_Start - Distance_End);
    Side_Index    = 0;
    Fraction_Far  = (Distance_Start - Offset - SURFACE_CLIP_EPSILON) * Inverse;
    Fraction_Near = (Distance_Start + Offset + SURFACE_CLIP_EPSILON) * Inverse;
  } else {
    Side_Index    = 0;
    Fraction_Near = 1.f;
    Fraction_Far  = 0.f;
  }

  // Clamp fractions to [0, 1]
  if (Fraction_Near < 0) Fraction_Near = 0;
  if (Fraction_Near > 1) Fraction_Near = 1;
  if (Fraction_Far  < 0) Fraction_Far  = 0;
  if (Fraction_Far  > 1) Fraction_Far  = 1;

  // Recurse into the near child first (the side the start point is on)
  {
    F32 Mid_Fraction = Start_Fraction + (End_Fraction - Start_Fraction) * Fraction_Near;
    V3 Mid_Point     = V3_Add (Point_Start, V3_Scale (V3_Subtract (Point_End, Point_Start), Fraction_Near));
    Trace_Through_Tree (Work, Node->Children[Side_Index], Start_Fraction, Mid_Fraction, Point_Start, Mid_Point, Collision);
  }

  // Then recurse into the far child
  {
    F32 Mid_Fraction = Start_Fraction + (End_Fraction - Start_Fraction) * Fraction_Far;
    V3 Mid_Point     = V3_Add (Point_Start, V3_Scale (V3_Subtract (Point_End, Point_Start), Fraction_Far));
    Trace_Through_Tree (Work, Node->Children[Side_Index ^ 1], Mid_Fraction, End_Fraction, Mid_Point, Point_End, Collision);
  }
}

/* Ray-sphere intersection test for capsule collision endpoints. */

void Trace_Against_Sphere (Trace_Work *Work, V3 Center, F32 Radius, V3 Start, V3 End) {
  V3  Direction = V3_Subtract (End, Start);
  F32 Length    = sqrtf (V3_Dot (Direction, Direction));

  // Degenerate case: zero-length trace, just test containment
  if (Length < 1e-6f) {
    V3 Delta = V3_Subtract (Start, Center);
    if (V3_Dot (Delta, Delta) <= Radius * Radius) {
      Work->Result.Start_Solid = 1;
      Work->Result.All_Solid   = 1;
      Work->Result.Fraction    = 0;
    }
    return;
  }

  // Solve the quadratic equation for ray-sphere intersection
  Direction        = V3_Scale (Direction, 1.f / Length);
  V3  Offset       = V3_Subtract (Start, Center);
  F32 Quadratic_B  = 2.f * V3_Dot (Direction, Offset);
  F32 Quadratic_C  = V3_Dot (Offset, Offset) - Radius * Radius;
  F32 Discriminant = Quadratic_B * Quadratic_B - 4.f * Quadratic_C;
  if (Discriminant <= 0) return;

  // Check the nearest intersection
  F32 Square_Root  = sqrtf (Discriminant);
  F32 Intersection = (-Quadratic_B - Square_Root) * 0.5f;
  if (Intersection < 0) {
    Work->Result.Start_Solid = 1;
    Intersection = (-Quadratic_B + Square_Root) * 0.5f;
    if (Intersection < 0) {
      Work->Result.All_Solid = 1;
      Work->Result.Fraction  = 0;
      return;
    }
    return;
  }

  // Normalize the intersection parameter to [0,1] and record if closer than previous hits
  Intersection /= Length;
  if (Intersection < Work->Result.Fraction) {
    Work->Result.Fraction = Intersection < 0 ? 0 : Intersection;
    V3 Hit = V3_Add (Start, V3_Scale (V3_Subtract (End, Start), Intersection));
    Work->Result.Normal = V3_Normalize (V3_Subtract (Hit, Center));
  }
}

/* Ray-cylinder intersection for the middle section of a capsule (vertical axis only). */

void Trace_Against_Vertical_Cylinder (Trace_Work *Work, V3 Center, F32 Radius, F32 Half_Height, V3 Start, V3 End) {

  // Project the trace onto the XZ plane for 2D circle intersection
  F32 Delta_X  = End.x - Start.x;
  F32 Delta_Z  = End.z - Start.z;
  F32 Offset_X = Start.x - Center.x;
  F32 Offset_Z = Start.z - Center.z;
  F32 Quadratic_A = Delta_X * Delta_X + Delta_Z * Delta_Z;
  if (Quadratic_A < 1e-12f) return;

  F32 Quadratic_B = 2.f * (Offset_X * Delta_X + Offset_Z * Delta_Z);
  F32 Quadratic_C = Offset_X * Offset_X + Offset_Z * Offset_Z - Radius * Radius;
  F32 Discriminant = Quadratic_B * Quadratic_B - 4.f * Quadratic_A * Quadratic_C;
  if (Discriminant <= 0) return;

  // Compute the nearest intersection parameter
  F32 Square_Root = sqrtf (Discriminant);
  F32 Parameter   = (-Quadratic_B - Square_Root) / (2.f * Quadratic_A);
  if (Parameter < 0 or Parameter >= Work->Result.Fraction) return;

  // Check that the intersection is within the cylinder's vertical extent
  F32 Intersection_Y = Start.y + Parameter * (End.y - Start.y);
  if (Intersection_Y < Center.y - Half_Height or Intersection_Y > Center.y + Half_Height) return;

  // Record the hit with a horizontal normal (no vertical component)
  Work->Result.Fraction = Parameter;
  F32 Hit_X  = Start.x + Parameter * Delta_X - Center.x;
  F32 Hit_Z  = Start.z + Parameter * Delta_Z - Center.z;
  F32 Hit_Length = sqrtf (Hit_X * Hit_X + Hit_Z * Hit_Z);
  if (Hit_Length > 1e-6f)
    Work->Result.Normal = V3_Make (Hit_X / Hit_Length, 0, Hit_Z / Hit_Length);
}

/* Capsule-vs-capsule swept intersection test: tests the cylinder body and both
   hemispherical end-caps of the combined Minkowski sum capsule. */

void 
Trace_Capsule_Against_Capsule (Trace_Work *Work,
                               V3          Center,
                               F32         Radius,
                               F32         Half_Height,
                               F32         Sphere_Offset,
                               V3          Start,
                               V3          End)
{
  F32 Combined_Radius  = Radius + Work->Sphere.Radius;
  F32 Cylinder_Height  = Half_Height + Work->Sphere.Half_Height - Combined_Radius;

  // Test against the cylindrical body of the combined capsule
  if (Cylinder_Height > 0)
    Trace_Against_Vertical_Cylinder (Work, Center, Combined_Radius, Cylinder_Height, Start, End);

  // Test against the top hemisphere
  V3 Top = V3_Add (Center, V3_Make (0, Sphere_Offset, 0));
  Trace_Against_Sphere (/*Work        =>*/ Work,
                        /*Center      =>*/ Top,
                        /*Radius      =>*/ Combined_Radius,
                        /*Start       =>*/ V3_Subtract (Start, Work->Sphere.Offset),
                        /*End         =>*/ V3_Subtract (End, Work->Sphere.Offset));

  // Test against the bottom hemisphere
  V3 Bottom = V3_Subtract (Center, V3_Make (0, Sphere_Offset, 0));
  Trace_Against_Sphere (/*Work        =>*/ Work,
                        /*Center      =>*/ Bottom,
                        /*Radius      =>*/ Combined_Radius,
                        /*Start       =>*/ V3_Add (Start, Work->Sphere.Offset),
                        /*End         =>*/ V3_Add (End, Work->Sphere.Offset));
}

/* Perform a complete collision trace from Start to End with the given AABB extents.
   Traverses the BSP tree, tests against all relevant solid brushes, and returns the
   closest hit fraction, position, and surface normal. */

Trace_Result Collision_Trace (V3 Start, V3 End, V3 Minimums, V3 Maximums,
                                     Collision_Map *Collision, int Use_Capsule) {
  Trace_Work Work;
  memset (&Work, 0, sizeof (Work));
  Work.Start           = Start;
  Work.End             = End;
  Work.Result.Fraction = 1.f;

  // Compute the half-extents from the asymmetric bounding box
  Work.Extents = V3_Make (/*x =>*/ (-Minimums.x > Maximums.x) ? -Minimums.x : Maximums.x,
                          /*y =>*/ (-Minimums.y > Maximums.y) ? -Minimums.y : Maximums.y,
                          /*z =>*/ (-Minimums.z > Maximums.z) ? -Minimums.z : Maximums.z);

  // Determine whether this is a point trace (zero-size bounding box)
  Work.Is_Point = (Minimums.x == 0 and Minimums.y == 0 and Minimums.z == 0
               and Maximums.x == 0 and Maximums.y == 0 and Maximums.z == 0);

  // Configure capsule parameters if capsule mode is requested and the trace is not a point
  Work.Sphere.Use_Capsule = Use_Capsule and not Work.Is_Point;
  if (Work.Sphere.Use_Capsule) {
    F32 Half_Width          = Maximums.x;
    F32 Half_Height         = Maximums.y;
    Work.Sphere.Radius      = Half_Width < Half_Height ? Half_Width : Half_Height;
    Work.Sphere.Half_Height = Half_Height;
    Work.Sphere.Offset      = V3_Make (0, Half_Height - Work.Sphere.Radius, 0);
  }

  // Pre-compute the eight corners of the bounding box for sign-bit-indexed plane testing
  for (int Index = 0; Index < 8; Index++) {
    Work.Offsets[Index] = V3_Make (/*x =>*/ (Index & 1) ? Maximums.x : Minimums.x,
                                   /*y =>*/ (Index & 2) ? Maximums.y : Minimums.y,
                                   /*z =>*/ (Index & 4) ? Maximums.z : Minimums.z);
  }

  // Increment the check counter to invalidate previous brush-test marks
  Collision->Check_Counter++;
  Trace_Through_Tree (&Work, 0, 0.f, 1.f, Work.Start, Work.End, Collision);

  // Compute the final end position from the hit fraction
  if (Work.Result.Fraction == 1.f) {
    Work.Result.End_Position = End;
  } else {
    Work.Result.End_Position = V3_Add (Start, V3_Scale (V3_Subtract (End, Start), Work.Result.Fraction));
  }

  return Work.Result;
}

/* <<physics>> ========================================================================================== */

#define GRAVITY             800.f
#define GROUND_FRICTION     6.f
#define STOP_SPEED          100.f
#define GROUND_ACCELERATE   10.f
#define AIR_ACCELERATE      1.f
#define MAXIMUM_SPEED       320.f
#define JUMP_VELOCITY       270.f
#define STEP_SIZE           18.f
#define MINIMUM_WALK_NORMAL 0.7f
#define OVERBOUNCE          1.001f
#define MAXIMUM_CLIP_PLANES 5
#define DEFAULT_VIEW_HEIGHT 26.f
#define CROUCH_VIEW_HEIGHT  12.f

const V3 PLAYER_MINIMUMS = {-15, -24, -15 };

// Player movement state: position, velocity, orientation, and ground contact information
typedef struct {
  V3  Position;        // World-space position of the player's bounding box origin
  V3  Velocity;        // Current velocity in units per second
  F32 Yaw, Pitch;      // Look direction: yaw (horizontal) and pitch (vertical) in radians
  int On_Ground;        // Non-zero if the player is standing on a walkable surface
  int Jump_Held;        // Non-zero if the jump key was held last frame (prevents auto-bunny-hopping)
  V3  Ground_Normal;    // Surface normal of the ground plane the player is standing on
  int Ground_Plane;     // Non-zero if the ground trace hit a valid plane (not an edge or brush start)
  int Ducked;           // Non-zero if the player is crouching
  F32 View_Height;      // Camera height offset from Position.y (smoothly interpolated)
} Player;

V3 Player_Maximums (const Player *State) {
  return V3_Make (15, State->Ducked ? 16 : 32, 15);
}

Trace_Result Player_Trace (V3 Start, V3 End, const Player *State, Collision_Map *Collision) {
  return Collision_Trace (Start, End, PLAYER_MINIMUMS, Player_Maximums (State), Collision, 1);
}

/* Remove the component of velocity along a surface normal, with a slight overbounce
   factor to prevent the player from sinking into surfaces. */

V3 Clip_Velocity (V3 Velocity, V3 Normal, F32 Overbounce_Factor) {
  F32 Backoff = V3_Dot (Velocity, Normal);
  if (Backoff < 0) Backoff *= Overbounce_Factor;
  else             Backoff /= Overbounce_Factor;
  return V3_Subtract (Velocity, V3_Scale (Normal, Backoff));
}

/* Attempt to nudge the player out of solid geometry by testing small offsets in all
   directions.  Returns true if a non-solid position was found. */

int Player_Correct_All_Solid (Player *State, Collision_Map *Collision) {
  for (int Offset_X = -1; Offset_X <= 1; Offset_X++)
  for (int Offset_Y = -1; Offset_Y <= 1; Offset_Y++)
  for (int Offset_Z = -1; Offset_Z <= 1; Offset_Z++) {
    V3 Adjusted = V3_Make (State->Position.x + (F32)Offset_X, State->Position.y + (F32)Offset_Y, State->Position.z + (F32)Offset_Z);
    Trace_Result Trace = Player_Trace (Adjusted, Adjusted, State, Collision);

      // Found a valid non-solid position; probe downward for ground contact
    if (not Trace.All_Solid) {

      // Found a valid position; update ground state from a short downward probe
      V3 Down = V3_Add (State->Position, V3_Make (0, -0.25f, 0));
      Trace = Player_Trace (State->Position, Down, State, Collision);
      State->Ground_Normal = Trace.Normal;
      State->Ground_Plane  = (Trace.Fraction < 1.f);
      State->On_Ground     = (Trace.Fraction < 1.f and Trace.Normal.y >= MINIMUM_WALK_NORMAL);
      return 1;
    }
  }
  State->On_Ground    = 0;
  State->Ground_Plane = 0;
  return 0;
}

/* Cast a short ray downward to determine whether the player is standing on solid ground.
   Updates the ground plane normal and the on-ground flag based on the surface slope. */

void Player_Ground_Trace (Player *State, Collision_Map *Collision) {
  V3 Down = V3_Add (State->Position, V3_Make (0, -0.25f, 0));
  Trace_Result Trace = Player_Trace (State->Position, Down, State, Collision);

  // If the trace starts inside solid, attempt to correct the position
  if (Trace.All_Solid) {
    if (not Player_Correct_All_Solid (State, Collision)) return;
    return;
  }

  // No ground contact if the trace reaches full distance
  if (Trace.Fraction == 1.f) {
    State->Ground_Plane = 0;
    State->On_Ground    = 0;
    return;
  }

  // Moving upward and away from the surface: not grounded
  if (State->Velocity.y > 0 and V3_Dot (State->Velocity, Trace.Normal) > 10.f) {
    State->Ground_Plane = 0;
    State->On_Ground    = 0;
    return;
  }

  // Surface is too steep to walk on: touching a wall or steep slope
  if (Trace.Normal.y < MINIMUM_WALK_NORMAL) {
    State->Ground_Plane  = 1;
    State->Ground_Normal = Trace.Normal;
    State->On_Ground     = 0;
    return;
  }

  // Solidly on walkable ground
  State->Ground_Plane  = 1;
  State->Ground_Normal = Trace.Normal;
  State->On_Ground     = 1;
}

/* Apply ground friction to the player's velocity.  The friction model uses a control
   speed (clamped to STOP_SPEED) to ensure low-speed movement decays quickly. */

void Player_Apply_Friction (Player *State, F32 Delta_Time) {
  V3 Horizontal_Velocity = State->Velocity;
  if (State->On_Ground) Horizontal_Velocity.y = 0;
  F32 Speed = sqrtf (V3_Dot (Horizontal_Velocity, Horizontal_Velocity));

  // Kill velocity entirely below the minimum threshold
  if (Speed < 1.f) {
    State->Velocity.x = 0;
    State->Velocity.z = 0;
    return;
  }
  F32 Drop = 0;
  if (State->On_Ground) {
    F32 Control = Speed < STOP_SPEED ? STOP_SPEED : Speed;
    Drop += Control * GROUND_FRICTION * Delta_Time;
  }

  // Scale velocity by the remaining fraction after friction
  F32 New_Speed = Speed - Drop;
  if (New_Speed < 0) New_Speed = 0;
  New_Speed /= Speed;
  State->Velocity.x *= New_Speed;
  State->Velocity.y *= New_Speed;
  State->Velocity.z *= New_Speed;
}

/* Accelerate the player along a wish direction up to a maximum wish speed.
   The acceleration is capped so that the player cannot exceed the desired speed. */

void Player_Accelerate (Player *State, V3 Wish_Direction, F32 Wish_Speed, F32 Acceleration, F32 Delta_Time) {
  F32 Current_Speed = V3_Dot (State->Velocity, Wish_Direction);
  F32 Add_Speed     = Wish_Speed - Current_Speed;

  // Comment here !!!
  if (Add_Speed <= 0) return;

  // Comment here !!!
  F32 Acceleration_Speed = Acceleration * Delta_Time * Wish_Speed;
  if (Acceleration_Speed > Add_Speed) Acceleration_Speed = Add_Speed;

  // Comment here !!!
  State->Velocity = V3_Add (State->Velocity, V3_Scale (Wish_Direction, Acceleration_Speed));
}

/* Iterative clipping slide-move: advance the player along their velocity, clipping against
   surfaces encountered.  Handles up to MAXIMUM_CLIP_PLANES simultaneous contacts by computing
   a velocity that satisfies all contact constraints (crease resolution). */

int Player_Slide_Move (Player *State, Collision_Map *Collision, F32 Delta_Time, int Apply_Gravity) {
  int Plane_Count  = 0;
  F32 Time_Left    = Delta_Time;
  V3  End_Velocity = State->Velocity;
  V3  Planes[MAXIMUM_CLIP_PLANES];

  // Apply gravity: average the start and end vertical velocity for a trapezoidal integration
  if (Apply_Gravity) {
    End_Velocity.y -= GRAVITY * Delta_Time;
    State->Velocity.y = (State->Velocity.y + End_Velocity.y) * 0.5f;
    if (State->Ground_Plane)
      State->Velocity = Clip_Velocity (State->Velocity, State->Ground_Normal, OVERBOUNCE);
  }


  // Seed the plane list with the ground normal and the velocity direction
  if (State->Ground_Plane)
    Planes[Plane_Count++] = State->Ground_Normal;

  // Comment here !!!
  {
    F32 Magnitude = sqrtf (V3_Dot (State->Velocity, State->Velocity));
    Planes[Plane_Count++] = Magnitude > 0.001f ? V3_Scale (State->Velocity, 1.f / Magnitude) : V3_Make (0, 1, 0);
  }

  // Iteratively advance the player, clipping against each surface encountered
  for (int Bump = 0; Bump < 4; Bump++) {
    V3 End_Position = V3_Add (State->Position, V3_Scale (State->Velocity, Time_Left));
    Trace_Result Trace = Player_Trace (State->Position, End_Position, State, Collision);

    // If stuck in solid, zero vertical velocity and bail
    if (Trace.All_Solid) {State->Velocity.y = 0; return 1; }

    // Advance the position by the fraction of movement that was unobstructed
    if (Trace.Fraction > 0) State->Position = Trace.End_Position;
    if (Trace.Fraction == 1.f) break;

    // Reduce the remaining time by the consumed fraction
    Time_Left -= Time_Left * Trace.Fraction;

    // Too many clip planes encountered; stop all movement
    if (Plane_Count >= MAXIMUM_CLIP_PLANES) {State->Velocity = V3_Make (0, 0, 0); return 1; }

    // Check if this normal is nearly parallel to an existing clip plane
    int Same_Plane = 0;
    for (int Index = 0; Index < Plane_Count; Index++) {
      if (V3_Dot (Trace.Normal, Planes[Index]) > 0.85f) {
        State->Velocity = V3_Add (Trace.Normal, State->Velocity);
        Same_Plane = 1;
        break;
      }
    }
    if (Same_Plane) continue;

    // Add this new contact plane and resolve velocity against all accumulated planes
    Planes[Plane_Count++] = Trace.Normal;

    // Find the first clip plane that the current velocity still enters
    int Plane_Index;
    for (Plane_Index = 0; Plane_Index < Plane_Count; Plane_Index++) {
      if (V3_Dot (State->Velocity, Planes[Plane_Index]) >= 0.1f) continue;

      // Clip the velocity against the blocking plane
      V3 Clipped     = Clip_Velocity (State->Velocity, Planes[Plane_Index], OVERBOUNCE);
      V3 End_Clipped = Clip_Velocity (End_Velocity,     Planes[Plane_Index], OVERBOUNCE);

      // Check if the clipped velocity violates any other accumulated plane
      int Other_Index;
      for (Other_Index = 0; Other_Index < Plane_Count; Other_Index++) {

        // Comment here !!!
        if (Other_Index == Plane_Index) continue;

        // Comment here !!!
        if (V3_Dot (Clipped, Planes[Other_Index]) >= 0.1f) continue;

        // Clip against the second plane
        Clipped     = Clip_Velocity (Clipped,     Planes[Other_Index], OVERBOUNCE);
        End_Clipped = Clip_Velocity (End_Clipped, Planes[Other_Index], OVERBOUNCE);

        // If the clipped velocity no longer enters any other plane, accept it
        if (V3_Dot (Clipped, Planes[Plane_Index]) >= 0) continue;

        // Two planes form a crease: project velocity along the crease direction
        V3  Crease = V3_Cross (Planes[Plane_Index], Planes[Other_Index]);
        F32 Crease_Length_Squared = V3_Dot (Crease, Crease);

        // Two planes form a crease; project velocity along the crease direction
        if (Crease_Length_Squared < 0.001f) {
          Clipped     = V3_Add (State->Velocity, Trace.Normal);
          End_Clipped = V3_Add (End_Velocity,     Trace.Normal);

        // Comment here !!!
        } else {
          V3 Direction   = V3_Scale (Crease, 1.f / sqrtf (Crease_Length_Squared));
          F32 Dot_Product = V3_Dot (Direction, State->Velocity);
          Clipped         = V3_Scale (Direction, Dot_Product);
          Dot_Product     = V3_Dot (Direction, End_Velocity);
          End_Clipped     = V3_Scale (Direction, Dot_Product);

          // If a third plane also blocks the crease direction, zero velocity
          int Third;
          for (Third = 0; Third < Plane_Count; Third++) {
            if (Third == Plane_Index or Third == Other_Index) continue;
            if (V3_Dot (Clipped, Planes[Third]) >= 0.1f) continue;
            State->Velocity = V3_Make (0, 0, 0);
            return 1;
          }
        }
      }

      // Apply the clipped velocity to the player position
      State->Velocity = Clipped;
      End_Velocity    = End_Clipped;
      break;
    }
  }

  // Restore the gravity-adjusted end velocity
  if (Apply_Gravity) State->Velocity = End_Velocity;
  return 1;
}

/* Step-slide move: first attempt a normal slide move.  If the player hit a wall or step,
   try stepping up by STEP_SIZE, sliding along the raised plane, then stepping back down.
   This allows the player to smoothly walk up stairs. */

void Player_Step_Slide_Move (Player *State, Collision_Map *Collision, F32 Delta_Time, int Apply_Gravity) {
  V3 Start_Position = State->Position;
  V3 Start_Velocity = State->Velocity;

  // Attempt a normal slide move first
  if (not Player_Slide_Move (State, Collision, Delta_Time, Apply_Gravity)) return;

  // Check if there is ground below the starting position (needed for step detection)
  V3 Down = V3_Add (Start_Position, V3_Make (0, -STEP_SIZE, 0));
  Trace_Result Trace = Player_Trace (Start_Position, Down, State, Collision);

  // Do not step if the player is moving upward and either in the air or on a steep slope
  if (State->Velocity.y > 0 and (Trace.Fraction == 1.f or Trace.Normal.y < MINIMUM_WALK_NORMAL)) return;

  // Try stepping up: trace upward by STEP_SIZE from the starting position
  V3 Up = V3_Add (Start_Position, V3_Make (0, STEP_SIZE, 0));
  Trace = Player_Trace (Start_Position, Up, State, Collision);
  if (Trace.All_Solid) return;

  // Slide move from the elevated position
  F32 Actual_Step = Trace.End_Position.y - Start_Position.y;
  State->Position = Trace.End_Position;
  State->Velocity = Start_Velocity;
  Player_Slide_Move (State, Collision, Delta_Time, Apply_Gravity);

  // Step back down to the ground after sliding
  V3 Step_Down = V3_Add (State->Position, V3_Make (0, -Actual_Step, 0));
  Trace = Player_Trace (State->Position, Step_Down, State, Collision);
  if (not Trace.All_Solid) State->Position = Trace.End_Position;

  // Clip velocity against the surface we landed on
  if (Trace.Fraction < 1.f)
    State->Velocity = Clip_Velocity (State->Velocity, Trace.Normal, OVERBOUNCE);
}

/* Ground movement: apply friction, compute the wish direction from input relative to the
   ground plane, accelerate, then perform a step-slide move. */

void Player_Walk_Move (Player *State, Collision_Map *Collision, Input Input_Data, F32 Delta_Time) {
  Player_Apply_Friction (State, Delta_Time);

  // Compute the forward and right vectors projected onto the ground plane
  F32 Cosine_Yaw = cosf (State->Yaw);
  F32 Sine_Yaw   = sinf (State->Yaw);
  V3 Forward = V3_Normalize (Clip_Velocity (V3_Make (Sine_Yaw, 0, -Cosine_Yaw), State->Ground_Normal, OVERBOUNCE));
  V3 Right   = V3_Normalize (Clip_Velocity (V3_Make (Cosine_Yaw, 0, Sine_Yaw),  State->Ground_Normal, OVERBOUNCE));

  // Sum the input directions into a wish velocity
  V3 Wish_Velocity = V3_Make (0, 0, 0);
  if (Input_Data.Forward) Wish_Velocity = V3_Add      (Wish_Velocity, Forward);
  if (Input_Data.Back)    Wish_Velocity = V3_Subtract  (Wish_Velocity, Forward);
  if (Input_Data.Right)   Wish_Velocity = V3_Add      (Wish_Velocity, Right);
  if (Input_Data.Left)    Wish_Velocity = V3_Subtract  (Wish_Velocity, Right);

  // Comment here !!!
  F32 Wish_Speed = sqrtf (V3_Dot (Wish_Velocity, Wish_Velocity));
  if (Wish_Speed > 0.001f) {
    Wish_Velocity = V3_Scale (Wish_Velocity, 1.f / Wish_Speed);
    Wish_Speed = MAXIMUM_SPEED;
  }
  if (State->Ducked) Wish_Speed *= 0.25f;

  // Comment here !!!
  Player_Accelerate (State, Wish_Velocity, Wish_Speed, GROUND_ACCELERATE, Delta_Time);

  // Preserve speed magnitude through the ground-plane clip to avoid speed loss on slopes
  F32 Speed_Magnitude = sqrtf (V3_Dot (State->Velocity, State->Velocity));
  State->Velocity = Clip_Velocity (State->Velocity, State->Ground_Normal, OVERBOUNCE);
  {
    F32 New_Speed = sqrtf (V3_Dot (State->Velocity, State->Velocity));
    if (New_Speed > 0.001f)
      State->Velocity = V3_Scale (State->Velocity, Speed_Magnitude / New_Speed);
  }

  // Only move if there is horizontal velocity
  if (State->Velocity.x == 0 and State->Velocity.z == 0) return;

  // Comment here !!!
  Player_Step_Slide_Move (State, Collision, Delta_Time, 0);
}

/* Air movement: apply friction, accelerate along the wish direction (with reduced air
   acceleration), then perform a step-slide move with gravity applied. */

void Player_Air_Move (Player *State, Collision_Map *Collision, Input Input_Data, F32 Delta_Time) {
  Player_Apply_Friction (State, Delta_Time);

  // Compute horizontal-only wish direction from input
  F32 Cosine_Yaw = cosf (State->Yaw);
  F32 Sine_Yaw   = sinf (State->Yaw);
  V3 Forward = V3_Make (Sine_Yaw, 0, -Cosine_Yaw);
  V3 Right   = V3_Make (Cosine_Yaw, 0, Sine_Yaw);

  V3 Wish_Direction = V3_Make (0, 0, 0);
  if (Input_Data.Forward) Wish_Direction = V3_Add      (Wish_Direction, Forward);
  if (Input_Data.Back)    Wish_Direction = V3_Subtract  (Wish_Direction, Forward);
  if (Input_Data.Right)   Wish_Direction = V3_Add      (Wish_Direction, Right);
  if (Input_Data.Left)    Wish_Direction = V3_Subtract  (Wish_Direction, Right);
  Wish_Direction.y = 0;

  F32 Wish_Speed = sqrtf (V3_Dot (Wish_Direction, Wish_Direction));
  if (Wish_Speed > 0.001f) {
    Wish_Direction = V3_Scale (Wish_Direction, 1.f / Wish_Speed);
    Wish_Speed = MAXIMUM_SPEED;
  }
  if (State->Ducked) Wish_Speed *= 0.25f;

  Player_Accelerate (State, Wish_Direction, Wish_Speed, AIR_ACCELERATE, Delta_Time);

  // Clip velocity against the ground plane if touching a slope
  if (State->Ground_Plane)
    State->Velocity = Clip_Velocity (State->Velocity, State->Ground_Normal, OVERBOUNCE);

  Player_Step_Slide_Move (State, Collision, Delta_Time, 1);
}

/* Handle crouch state transitions: duck down when the crouch key is pressed, and attempt
   to stand up when released (blocked if there is solid overhead). */

void Player_Check_Crouch (Player *State, Collision_Map *Collision, Input Input_Data) {
  const F32 Height_Delta = 16.f;

  if (Input_Data.Crouch) {

    // Transition to crouched state by lowering the player's position
    if (not State->Ducked) {
      State->Ducked = 1;
      State->Position.y -= Height_Delta;
    }

  // Attempt to stand: check if there is room above
  } else if (State->Ducked) {
    V3 Test_Position     = V3_Add (State->Position, V3_Make (0, Height_Delta, 0));
    V3 Standing_Maximums = V3_Make (15, 32, 15);
    Trace_Result Trace   = Collision_Trace (Test_Position, Test_Position, PLAYER_MINIMUMS, Standing_Maximums, Collision, 1);
    if (not Trace.All_Solid) {
      State->Ducked   = 0;
      State->Position = Test_Position;
    }
  }

  State->View_Height = State->Ducked ? CROUCH_VIEW_HEIGHT : DEFAULT_VIEW_HEIGHT;
}

/* Top-level player movement entry point.  Updates yaw/pitch from mouse input, handles
   crouching, determines ground contact, and dispatches to walk or air movement. */

void Player_Move (Player *State, Collision_Map *Collision, Input Input_Data, F32 Delta_Time) {

  // Apply mouse look: accumulate yaw and pitch from mouse deltas
  State->Yaw   += Input_Data.Delta_X * 0.003f;
  State->Pitch += Input_Data.Delta_Y * 0.003f;
  if (State->Pitch >  1.4f) State->Pitch =  1.4f;
  if (State->Pitch < -1.4f) State->Pitch = -1.4f;

  // No-clip fallback movement when no collision data is loaded
  if (not Collision or not Collision->Node_Count) {
    F32 Cosine_Yaw = cosf (State->Yaw);
    F32 Sine_Yaw   = sinf (State->Yaw);
    V3 Forward  = V3_Make (Sine_Yaw, 0, -Cosine_Yaw);
    V3 Right    = V3_Make (Cosine_Yaw, 0, Sine_Yaw);
    V3 Movement = V3_Make (0, 0, 0);
    F32 Speed   = 320.f;
    if (Input_Data.Forward) Movement = V3_Add (Movement, V3_Scale (Forward,  Speed));
    if (Input_Data.Back)    Movement = V3_Add (Movement, V3_Scale (Forward, -Speed));
    if (Input_Data.Right)   Movement = V3_Add (Movement, V3_Scale (Right,    Speed));
    if (Input_Data.Left)    Movement = V3_Add (Movement, V3_Scale (Right,   -Speed));
    if (Input_Data.Jump)    Movement.y += Speed;
    State->Position = V3_Add (State->Position, V3_Scale (Movement, Delta_Time));
    return;
  }

  // Update crouch state before checking ground contact
  Player_Check_Crouch (State, Collision, Input_Data);
  Player_Ground_Trace (State, Collision);

  // Dispatch to grounded or airborne movement
  if (State->On_Ground) {
    if (Input_Data.Jump and not State->Jump_Held) {

      // Launch the player upward and switch to air movement for this frame
      State->Velocity.y    = JUMP_VELOCITY;
      State->On_Ground     = 0;
      State->Ground_Plane  = 0;
      Player_Air_Move (State, Collision, Input_Data, Delta_Time);
    } else {
      Player_Walk_Move (State, Collision, Input_Data, Delta_Time);
    }
  } else {
    Player_Air_Move (State, Collision, Input_Data, Delta_Time);
  }

  // Re-evaluate ground contact after movement
  Player_Ground_Trace (State, Collision);
  State->Jump_Held = Input_Data.Jump;
}

/* <<scene>> ============================================================================================ */

/* Build a minimal test scene with a ground plane and a small triangular prism for development. */

Scene Scene_Build_Test (void) {
  static Vertex Test_Vertices[] = {
    {.Position = {-10, 0, -10}, .Texture_Uv = { 0,  1}, .Normal = { 0, 1,  0}},
    {.Position = { 10, 0, -10}, .Texture_Uv = { 1,  1}, .Normal = { 0, 1,  0}},
    {.Position = { 10, 0,  10}, .Texture_Uv = { 1,  0}, .Normal = { 0, 1,  0}},
    {.Position = {-10, 0,  10}, .Texture_Uv = { 0,  0}, .Normal = { 0, 1,  0}},
    {.Position = {  0, 0,   0}, .Texture_Uv = {.5, .5}, .Normal = { 0, 0, -1}},
    {.Position = { -2, 0,  -4}, .Texture_Uv = { 0,  1}, .Normal = { 0, 0, -1}},
    {.Position = {  2, 0,  -4}, .Texture_Uv = { 1,  1}, .Normal = { 0, 0, -1}},
    {.Position = {  0, 4,  -4}, .Texture_Uv = {.5,  0}, .Normal = { 0, 0, -1}},
    {.Position = {  0, 4,  -4}, .Texture_Uv = {.5, .5}, .Normal = {-1, 0,  0}},
    {.Position = { -2, 0,  -4}, .Texture_Uv = { 0,  1}, .Normal = {-1, 0,  0}},
    {.Position = { -2, 0,   0}, .Texture_Uv = { 1,  1}, .Normal = {-1, 0,  0}},
    {.Position = {  0, 4,  -4}, .Texture_Uv = {.5, .5}, .Normal = { 1, 0,  0}},
    {.Position = {  2, 0,  -4}, .Texture_Uv = { 0,  1}, .Normal = { 1, 0,  0}},
    {.Position = {  2, 0,   0}, .Texture_Uv = { 1,  1}, .Normal = { 1, 0,  0}},
  };
  static U32 Test_Indices[]     = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 11, 12, 13 };
  static V4  Test_Materials[]   = {{0.55f, 0.52f, 0.48f, 1}, {0.85f, 0.42f, 0.15f, 1}, {0.25f, 0.60f, 0.85f, 1}};
  static U32 Test_Texture_Ids[] = {0, 0, 0, 0, 0, 0 };

  return (Scene){
    .Vertices    = Test_Vertices,    .Vertex_Count   = 14,
    .Indices     = Test_Indices,     .Index_Count    = 18,
    .Materials   = Test_Materials,   .Material_Count = 3,
    .Texture_Ids = Test_Texture_Ids, .Triangle_Count = 6, .Texture_Names = NULL
  };
}

/* Load textures for every material in the scene.  Attempts to load TGA files from the assets
   directory; materials without a texture file fall back to a 1x1 solid-color pixel derived
   from the hashed shader name.  Also uploads the lightmap atlas and per-triangle texture IDs. */

void Scene_Load_Textures (Vulkan_Context *Context, const Scene *Scene_Data) {
  Context->Texture_Sampler  = Sampler_Create_Repeating (Context->Device);
  Context->Texture_Count    = Scene_Data->Material_Count;
  Context->Textures_Loaded  = 0;
  Context->Texture_Images   = calloc (Context->Texture_Count, sizeof (VkImage));
  Context->Texture_Memories = calloc (Context->Texture_Count, sizeof (VkDeviceMemory));
  Context->Texture_Views    = calloc (Context->Texture_Count, sizeof (VkImageView));

  // Load each material's TGA texture, or generate a fallback solid-color pixel
  for (U32 Index = 0; Index < Context->Texture_Count; Index++) {
    U32 Width = 0, Height = 0;
    U8 *Pixels = NULL;
    if (Scene_Data->Texture_Names) {
      char Path[256];
      snprintf (Path, sizeof (Path), "assets/%s.tga", Scene_Data->Texture_Names[Index]);
      Pixels = Tga_Load (Path, &Width, &Height);
    }
    if (Pixels and Width and Height) {
      Texture_Upload (/*Device          =>*/ Context->Device,
                      /*Physical_Device =>*/ Context->Physical_Device,
                      /*Command_Buffer  =>*/ Context->Command_Buffer,
                      /*Queue           =>*/ Context->Queue,
                      /*Pixels          =>*/ Pixels,
                      /*Width           =>*/ Width,
                      /*Height          =>*/ Height,
                      /*Out_Image       =>*/ &Context->Texture_Images[Index],
                      /*Out_Memory      =>*/ &Context->Texture_Memories[Index],
                      /*Out_View        =>*/ &Context->Texture_Views[Index]);
      free (Pixels);
      Context->Textures_Loaded++;
    } else {
      V4 Color = Scene_Data->Materials[Index];
      U8 Fallback[4] = {(U8)(Color.x * 255), (U8)(Color.y * 255), (U8)(Color.z * 255), 255 };
      Texture_Upload (/*Device          =>*/ Context->Device,
                      /*Physical_Device =>*/ Context->Physical_Device,
                      /*Command_Buffer  =>*/ Context->Command_Buffer,
                      /*Queue           =>*/ Context->Queue,
                      /*Pixels          =>*/ Fallback,
                      /*Width           =>*/ 1,
                      /*Height          =>*/ 1,
                      /*Out_Image       =>*/ &Context->Texture_Images[Index],
                      /*Out_Memory      =>*/ &Context->Texture_Memories[Index],
                      /*Out_View        =>*/ &Context->Texture_Views[Index]);
    }
  }

  // Upload per-triangle texture IDs as a storage buffer
  Context->Texture_Id_Buffer = Buffer_Stage_Upload (/*Device          =>*/ Context->Device,
                                                    /*Physical_Device =>*/ Context->Physical_Device,
                                                    /*Command_Buffer  =>*/ Context->Command_Buffer,
                                                    /*Queue           =>*/ Context->Queue,
                                                    /*Data            =>*/ Scene_Data->Texture_Ids,
                                                    /*Size            =>*/ sizeof (U32) * Scene_Data->Triangle_Count,
                                                    /*Usage           =>*/ VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

  printf ("[textures] loaded %u/%u textures, "
          "%u fallbacks\n",
          Context->Textures_Loaded,
          Context->Texture_Count,
          Context->Texture_Count - Context->Textures_Loaded);

  // Upload the lightmap atlas (or a 1x1 white fallback if no lightmaps exist)
  Context->Lightmap_Sampler = Sampler_Create_Clamping (Context->Device);
  if (Scene_Data->Lightmap_Atlas and Scene_Data->Lightmap_Width and Scene_Data->Lightmap_Height) {
    Texture_Upload_With_Format (/*Device          =>*/ Context->Device,
                                /*Physical_Device =>*/ Context->Physical_Device,
                                /*Command_Buffer  =>*/ Context->Command_Buffer,
                                /*Queue           =>*/ Context->Queue,
                                /*Pixels          =>*/ Scene_Data->Lightmap_Atlas,
                                /*Width           =>*/ Scene_Data->Lightmap_Width,
                                /*Height          =>*/ Scene_Data->Lightmap_Height,
                                /*Format          =>*/ VK_FORMAT_R8G8B8A8_UNORM,
                                /*Out_Image       =>*/ &Context->Lightmap_Image,
                                /*Out_Memory      =>*/ &Context->Lightmap_Memory,
                                /*Out_View        =>*/ &Context->Lightmap_View);
    printf ("[lightmap] uploaded %ux%u atlas (UNORM)\n", Scene_Data->Lightmap_Width, Scene_Data->Lightmap_Height);
  } else {
    U8 White[4] = {255, 255, 255, 255 };
    Texture_Upload_With_Format (/*Device          =>*/ Context->Device,
                                /*Physical_Device =>*/ Context->Physical_Device,
                                /*Command_Buffer  =>*/ Context->Command_Buffer,
                                /*Queue           =>*/ Context->Queue,
                                /*Pixels          =>*/ White,
                                /*Width           =>*/ 1,
                                /*Height          =>*/ 1,
                                /*Format          =>*/ VK_FORMAT_R8G8B8A8_UNORM,
                                /*Out_Image       =>*/ &Context->Lightmap_Image,
                                /*Out_Memory      =>*/ &Context->Lightmap_Memory,
                                /*Out_View        =>*/ &Context->Lightmap_View);
  }
}

/* Load the weapon model's TGA textures and append them to the global texture array. */

void Weapon_Load_Textures (Vulkan_Context *Context, Weapon_Instance *Weapon) {

  // Comment here !!!
  const char *Weapon_Texture_Paths[] = {
    "assets/models/weapons2/machinegun/mgun.tga",
    "assets/models/weapons2/machinegun/sight.tga",
  };
  U32 Weapon_Texture_Count   = 2;
  Weapon->Texture_Base_Index = Context->Texture_Count;

  // Grow the global texture arrays to accommodate the weapon textures
  U32 New_Total = Context->Texture_Count + Weapon_Texture_Count;
  Context->Texture_Images   = realloc (Context->Texture_Images,   sizeof (VkImage)        * New_Total);
  Context->Texture_Memories = realloc (Context->Texture_Memories, sizeof (VkDeviceMemory)  * New_Total);
  Context->Texture_Views    = realloc (Context->Texture_Views,    sizeof (VkImageView)     * New_Total);

  // Comment here !!!
  for (U32 Index = 0; Index < Weapon_Texture_Count; Index++) {
    U32 Width = 0, Height = 0;
    U8 *Pixels = Tga_Load (Weapon_Texture_Paths[Index], &Width, &Height);
    if (Pixels and Width and Height) {
      Texture_Upload (/*Device          =>*/ Context->Device,
                      /*Physical_Device =>*/ Context->Physical_Device,
                      /*Command_Buffer  =>*/ Context->Command_Buffer,
                      /*Queue           =>*/ Context->Queue,
                      /*Pixels          =>*/ Pixels,
                      /*Width           =>*/ Width,
                      /*Height          =>*/ Height,
                      /*Out_Image       =>*/ &Context->Texture_Images[Context->Texture_Count],
                      /*Out_Memory      =>*/ &Context->Texture_Memories[Context->Texture_Count],
                      /*Out_View        =>*/ &Context->Texture_Views[Context->Texture_Count]);
      free (Pixels);
      printf ("[weapon] loaded texture %s (%ux%u)\n", Weapon_Texture_Paths[Index], Width, Height);

    // Comment here !!!
    } else {
      U8 Fallback[4] = {180, 180, 180, 255 };
      Texture_Upload (/*Device          =>*/ Context->Device,
                      /*Physical_Device =>*/ Context->Physical_Device,
                      /*Command_Buffer  =>*/ Context->Command_Buffer,
                      /*Queue           =>*/ Context->Queue,
                      /*Pixels          =>*/ Fallback,
                      /*Width           =>*/ 1,
                      /*Height          =>*/ 1,
                      /*Out_Image       =>*/ &Context->Texture_Images[Context->Texture_Count],
                      /*Out_Memory      =>*/ &Context->Texture_Memories[Context->Texture_Count],
                      /*Out_View        =>*/ &Context->Texture_Views[Context->Texture_Count]);
      printf ("[weapon] fallback texture for %s\n", Weapon_Texture_Paths[Index]);
    }
    Context->Texture_Count++;
  }
  printf ("[weapon] textures: base=%u, count=%u\n", Weapon->Texture_Base_Index, Weapon_Texture_Count);
}

/* <<bottom_level_acceleration>> ======================================================================== */

/* Build the world geometry's bottom-level acceleration structure (BLAS).  Uploads the scene
   vertex, index, and material buffers to the GPU, then constructs a single BLAS geometry
   entry covering all triangles.  Uses PREFER_FAST_TRACE since the world is static. */

Acceleration_Structure Build_World_Bottom_Level (Vulkan_Context *Context, const Scene *Scene_Data) {

  // Upload scene vertex, index, and material data to device-local GPU buffers
  Context->Vertex_Buffer = Buffer_Stage_Upload (/*Device          =>*/ Context->Device,
                                                /*Physical_Device =>*/ Context->Physical_Device,
                                                /*Command_Buffer  =>*/ Context->Command_Buffer,
                                                /*Queue           =>*/ Context->Queue,
                                                /*Data            =>*/ Scene_Data->Vertices,
                                                /*Size            =>*/ sizeof (Vertex) * Scene_Data->Vertex_Count,
                                                /*Usage           =>*/ VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

  // Comment here !!!
  Context->Index_Buffer = Buffer_Stage_Upload (/*Device          =>*/ Context->Device,
                                               /*Physical_Device =>*/ Context->Physical_Device,
                                               /*Command_Buffer  =>*/ Context->Command_Buffer,
                                               /*Queue           =>*/ Context->Queue,
                                               /*Data            =>*/ Scene_Data->Indices,
                                               /*Size            =>*/ sizeof (U32) * Scene_Data->Index_Count,
                                               /*Usage           =>*/ VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

  // Comment here !!!
  Context->Material_Buffer = Buffer_Allocate (/*Device          =>*/ Context->Device,
                                              /*Physical_Device =>*/ Context->Physical_Device,
                                              /*Size            =>*/ sizeof (V4) * Scene_Data->Material_Count,
                                              /*Usage           =>*/ VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                              /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  // Comment here !!!
  Buffer_Upload (Context->Device, Context->Material_Buffer, Scene_Data->Materials, sizeof (V4) * Scene_Data->Material_Count);

  // Define the triangle geometry referencing the uploaded vertex and index buffers
  VkAccelerationStructureGeometryKHR Geometry = {
    .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
    .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
    .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR,
    .geometry.triangles = {
      .sType                       = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
      .vertexFormat                = VK_FORMAT_R32G32B32_SFLOAT,
      .vertexData.deviceAddress    = Context->Vertex_Buffer.Address,
      .vertexStride                = sizeof (Vertex),
      .maxVertex                   = Scene_Data->Vertex_Count - 1,
      .indexType                   = VK_INDEX_TYPE_UINT32,
      .indexData.deviceAddress     = Context->Index_Buffer.Address,
    },
  };

  // Query the required buffer sizes for the acceleration structure and scratch memory
  VkAccelerationStructureBuildGeometryInfoKHR Build_Info = {
    .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
    .type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
    .flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
    .geometryCount = 1,
    .pGeometries   = &Geometry,
  };

  // Comment here !!!
  U32 Primitive_Count = Scene_Data->Triangle_Count;
  VkAccelerationStructureBuildSizesInfoKHR Build_Sizes = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
  Context->Raytracing.Get_Build_Sizes (Context->Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &Build_Info, &Primitive_Count, &Build_Sizes);

  // Allocate the acceleration structure buffer and create the BLAS object
  Acceleration_Structure Result = {0};
  Result.Buffer = Buffer_Allocate (/*Device          =>*/ Context->Device,
                                   /*Physical_Device =>*/ Context->Physical_Device,
                                   /*Size            =>*/ Build_Sizes.accelerationStructureSize,
                                   /*Usage           =>*/ VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                   /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // Comment here !!!
  VK_CHECK (Context->Raytracing.Create_Acceleration_Structure (/*device                 =>*/ Context->Device,
                                                               /*pCreateInfo            =>*/ &(VkAccelerationStructureCreateInfoKHR){
                                                                 .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
                                                                 .buffer = Result.Buffer.Buffer,
                                                                 .size   = Build_Sizes.accelerationStructureSize,
                                                                 .type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                                                               },
                                                               /*pAllocator             =>*/ NULL,
                                                               /*pAccelerationStructure =>*/ &Result.Handle));

  // Allocate scratch memory for the build operation
  Gpu_Buffer Scratch = Buffer_Allocate (/*Device          =>*/ Context->Device,
                                        /*Physical_Device =>*/ Context->Physical_Device,
                                        /*Size            =>*/ Build_Sizes.buildScratchSize,
                                        /*Usage           =>*/ VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                        /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // Build the BLAS via a one-shot command buffer
  Build_Info.dstAccelerationStructure  = Result.Handle;
  Build_Info.scratchData.deviceAddress = Scratch.Address;

  // Comment here !!!
  VkAccelerationStructureBuildRangeInfoKHR Range = {.primitiveCount = Primitive_Count };
  const VkAccelerationStructureBuildRangeInfoKHR *Range_Pointer = &Range;

  // Comment here !!!
  VK_CHECK (vkResetCommandBuffer (Context->Command_Buffer, 0));
  VK_CHECK (vkBeginCommandBuffer (/*commandBuffer =>*/ Context->Command_Buffer,
                                  /*pBeginInfo    =>*/ &(VkCommandBufferBeginInfo){
                                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
                                  }));

  // Comment here !!!
  Context->Raytracing.Command_Build (Context->Command_Buffer, 1, &Build_Info, &Range_Pointer);
  VK_CHECK (vkEndCommandBuffer (Context->Command_Buffer));
  VK_CHECK (vkQueueSubmit (/*queue       =>*/ Context->Queue,
                           /*submitCount =>*/ 1,
                           /*pSubmits    =>*/ &(VkSubmitInfo){
                             .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                             .commandBufferCount = 1,
                             .pCommandBuffers    = &Context->Command_Buffer
                           },
                           /*fence       =>*/ VK_NULL_HANDLE));

  // Comment here !!!
  VK_CHECK (vkQueueWaitIdle (Context->Queue));

  // Query the device address of the built BLAS for referencing from the TLAS
  Result.Address = Context->Raytracing.Get_Device_Address (/*device =>*/ Context->Device,
                                                           /*pInfo  =>*/ &(VkAccelerationStructureDeviceAddressInfoKHR){
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
      .accelerationStructure = Result.Handle,
    });

  // Free the scratch buffer (no longer needed after the build)
  vkDestroyBuffer (Context->Device, Scratch.Buffer, NULL);
  vkFreeMemory (Context->Device, Scratch.Memory, NULL);
  return Result;
}

/* Initialize the weapon's BLAS with host-visible vertex buffer (for per-frame updates)
   and ALLOW_UPDATE flag for fast rebuilds.  Scratch memory is kept alive for reuse. */

void Weapon_Bottom_Level_Initialize (Vulkan_Context *Context, Weapon_Instance *Weapon) {
  if (not Weapon->Model.Vertex_Count) return;

  // Allocate a host-visible copy of the weapon vertices for per-frame CPU transformation
  Weapon->Transformed_Vertices = malloc (sizeof (Vertex) * Weapon->Model.Vertex_Count);
  memcpy (Weapon->Transformed_Vertices, Weapon->Model.Vertices, sizeof (Vertex) * Weapon->Model.Vertex_Count);

  // Create host-visible vertex buffer for direct CPU writes each frame
  Weapon->Vertex_Buffer = Buffer_Allocate (/*Device          =>*/ Context->Device,
                                           /*Physical_Device =>*/ Context->Physical_Device,
                                           /*Size            =>*/ sizeof (Vertex) * Weapon->Model.Vertex_Count,
                                           /*Usage           =>*/ VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                                                | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                                                | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                                                                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                           /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                                                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  // Comment here !!!
  Buffer_Upload (Context->Device, Weapon->Vertex_Buffer, Weapon->Transformed_Vertices, sizeof (Vertex) * Weapon->Model.Vertex_Count);

  // Upload index and texture-id data (static, device-local)
  Weapon->Index_Buffer = Buffer_Stage_Upload (/*Device          =>*/ Context->Device,
                                              /*Physical_Device =>*/ Context->Physical_Device,
                                              /*Command_Buffer  =>*/ Context->Command_Buffer,
                                              /*Queue           =>*/ Context->Queue,
                                              /*Data            =>*/ Weapon->Model.Indices,
                                              /*Size            =>*/ sizeof (U32) * Weapon->Model.Index_Count,
                                              /*Usage           =>*/ VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                                                                   | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                                                   | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

  Weapon->Texture_Id_Buffer = Buffer_Stage_Upload (/*Device          =>*/ Context->Device,
                                                   /*Physical_Device =>*/ Context->Physical_Device,
                                                   /*Command_Buffer  =>*/ Context->Command_Buffer,
                                                   /*Queue           =>*/ Context->Queue,
                                                   /*Data            =>*/ Weapon->Model.Texture_Ids,
                                                   /*Size            =>*/ sizeof (U32) * Weapon->Model.Triangle_Count,
                                                   /*Usage           =>*/ VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

  // Configure the BLAS for fast builds with update capability
  VkAccelerationStructureGeometryKHR Geometry = {
    .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
    .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
    .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR,
    .geometry.triangles = {
      .sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
      .vertexFormat             = VK_FORMAT_R32G32B32_SFLOAT,
      .vertexData.deviceAddress = Weapon->Vertex_Buffer.Address,
      .vertexStride             = sizeof (Vertex),
      .maxVertex                = Weapon->Model.Vertex_Count - 1,
      .indexType                = VK_INDEX_TYPE_UINT32,
      .indexData.deviceAddress  = Weapon->Index_Buffer.Address,
    },
  };

  // Comment here !!!
  VkAccelerationStructureBuildGeometryInfoKHR Build_Info = {
    .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
    .type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
    .flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR
                   | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
    .geometryCount = 1,
    .pGeometries   = &Geometry,
  };

  // Comment here !!!
  U32 Primitive_Count = Weapon->Model.Triangle_Count;
  VkAccelerationStructureBuildSizesInfoKHR Build_Sizes = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
  Context->Raytracing.Get_Build_Sizes (Context->Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &Build_Info, &Primitive_Count, &Build_Sizes);

  // Allocate the BLAS buffer and persistent scratch buffer
  Weapon->Bottom_Level.Buffer = Buffer_Allocate (/*Device          =>*/ Context->Device,
                                                 /*Physical_Device =>*/ Context->Physical_Device,
                                                 /*Size            =>*/ Build_Sizes.accelerationStructureSize,
                                                 /*Usage           =>*/ VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                                                                      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                 /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VK_CHECK (Context->Raytracing.Create_Acceleration_Structure (/*device                 =>*/ Context->Device,
                                                               /*pCreateInfo            =>*/ &(VkAccelerationStructureCreateInfoKHR){
                                                                 .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
                                                                 .buffer = Weapon->Bottom_Level.Buffer.Buffer,
                                                                 .size  = Build_Sizes.accelerationStructureSize,
                                                                 .type  = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                                                               },
                                                               /*pAllocator             =>*/ NULL,
                                                               /*pAccelerationStructure =>*/ &Weapon->Bottom_Level.Handle));
  Weapon->Bottom_Level_Scratch = Buffer_Allocate (/*Device          =>*/ Context->Device,
                                                  /*Physical_Device =>*/ Context->Physical_Device,
                                                  /*Size            =>*/ Build_Sizes.buildScratchSize,
                                                  /*Usage           =>*/ VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                                                       | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                  /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // Perform the initial BLAS build
  Build_Info.dstAccelerationStructure  = Weapon->Bottom_Level.Handle;
  Build_Info.scratchData.deviceAddress = Weapon->Bottom_Level_Scratch.Address;

  VkAccelerationStructureBuildRangeInfoKHR Range = {.primitiveCount = Primitive_Count };
  const VkAccelerationStructureBuildRangeInfoKHR *Range_Pointer = &Range;

  VK_CHECK (vkResetCommandBuffer (Context->Command_Buffer, 0));
  VK_CHECK (vkBeginCommandBuffer (/*commandBuffer =>*/ Context->Command_Buffer,
                                  /*pBeginInfo    =>*/ &(VkCommandBufferBeginInfo){.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
  Context->Raytracing.Command_Build (Context->Command_Buffer, 1, &Build_Info, &Range_Pointer);
  VK_CHECK (vkEndCommandBuffer (Context->Command_Buffer));
  VK_CHECK (vkQueueSubmit (/*queue       =>*/ Context->Queue,
                           /*submitCount =>*/ 1,
                           /*pSubmits    =>*/ &(VkSubmitInfo){
                             .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                             .commandBufferCount = 1,
                             .pCommandBuffers    = &Context->Command_Buffer
                           },
                           /*fence       =>*/ VK_NULL_HANDLE));
  VK_CHECK (vkQueueWaitIdle (Context->Queue));

  Weapon->Bottom_Level.Address = Context->Raytracing.Get_Device_Address (/*device =>*/ Context->Device,
                                                                         /*pInfo  =>*/ &(VkAccelerationStructureDeviceAddressInfoKHR){
                                                                           .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
                                                                           .accelerationStructure = Weapon->Bottom_Level.Handle,
                                                                         });

  printf ("[weapon] BLAS built: %u triangles\n", Primitive_Count);
}

/* Rebuild the weapon BLAS from scratch after CPU vertex transformation.
   Re-uploads the vertex buffer and performs a full (non-update) rebuild. */

void Weapon_Bottom_Level_Rebuild (Vulkan_Context *Context, Weapon_Instance *Weapon) {
  if (not Weapon->Model.Vertex_Count) return;

  // Re-upload the transformed vertices to the GPU
  Buffer_Upload (Context->Device, Weapon->Vertex_Buffer, Weapon->Transformed_Vertices, sizeof (Vertex) * Weapon->Model.Vertex_Count);

  // Rebuild the BLAS with the updated vertex positions
  VkAccelerationStructureGeometryKHR Geometry = {
    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
    .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
    .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
    .geometry.triangles = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
      .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
      .vertexData.deviceAddress = Weapon->Vertex_Buffer.Address,
      .vertexStride = sizeof (Vertex),
      .maxVertex = Weapon->Model.Vertex_Count - 1,
      .indexType = VK_INDEX_TYPE_UINT32,
      .indexData.deviceAddress = Weapon->Index_Buffer.Address,
    },
  };

  VkAccelerationStructureBuildGeometryInfoKHR Build_Info = {
    .sType                     = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
    .type                      = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
    .flags                     = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
    .mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
    .srcAccelerationStructure  = VK_NULL_HANDLE,
    .dstAccelerationStructure  = Weapon->Bottom_Level.Handle,
    .scratchData.deviceAddress = Weapon->Bottom_Level_Scratch.Address,
    .geometryCount             = 1,
    .pGeometries               = &Geometry,
  };

  VkAccelerationStructureBuildRangeInfoKHR Range = {.primitiveCount = Weapon->Model.Triangle_Count };
  const VkAccelerationStructureBuildRangeInfoKHR *Range_Pointer = &Range;

  VK_CHECK (vkResetCommandBuffer (Context->Command_Buffer, 0));
  VK_CHECK (vkBeginCommandBuffer (/*commandBuffer =>*/ Context->Command_Buffer,
                                  /*pBeginInfo    =>*/ &(VkCommandBufferBeginInfo){.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
  Context->Raytracing.Command_Build (Context->Command_Buffer, 1, &Build_Info, &Range_Pointer);
  VK_CHECK (vkEndCommandBuffer (Context->Command_Buffer));
  VK_CHECK (vkQueueSubmit (/*queue       =>*/ Context->Queue,
                           /*submitCount =>*/ 1,
                           /*pSubmits    =>*/ &(VkSubmitInfo){.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &Context->Command_Buffer },
                           /*fence       =>*/ VK_NULL_HANDLE));
  VK_CHECK (vkQueueWaitIdle (Context->Queue));
}

/* <<top_level_acceleration>> =========================================================================== */

Gpu_Buffer Top_Level_Instance_Buffer;
Gpu_Buffer Top_Level_Scratch_Buffer;

/* Pre-allocate the top-level acceleration structure (TLAS) for up to Maximum_Instances
   instance entries.  The instance buffer, scratch buffer, and TLAS object are created once
   and reused across frames.  The TLAS is rebuilt (not updated) each frame. */

void Top_Level_Initialize (Vulkan_Context *Context, U32 Maximum_Instances) {

  // Allocate a host-visible instance buffer large enough for the maximum number of instances
  Top_Level_Instance_Buffer = Buffer_Allocate (/*Device          =>*/ Context->Device,
                                               /*Physical_Device =>*/ Context->Physical_Device,
                                               /*Size            =>*/ sizeof (VkAccelerationStructureInstanceKHR) * Maximum_Instances,
                                               /*Usage           =>*/ VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                                                                    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                               /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                                                    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  // Query the required sizes for the TLAS and its scratch buffer
  VkAccelerationStructureGeometryKHR Geometry = {
    .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
    .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
    .flags        = VK_GEOMETRY_OPAQUE_BIT_KHR,
    .geometry.instances = {
      .sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
      .arrayOfPointers    = VK_FALSE,
      .data.deviceAddress = Top_Level_Instance_Buffer.Address,
    },
  };

  VkAccelerationStructureBuildGeometryInfoKHR Build_Info = {
    .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
    .type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
    .flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
    .geometryCount = 1,
    .pGeometries   = &Geometry,
  };

  VkAccelerationStructureBuildSizesInfoKHR Build_Sizes = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  Context->Raytracing.Get_Build_Sizes (Context->Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &Build_Info, &Maximum_Instances, &Build_Sizes);

  // Allocate the TLAS storage and create the acceleration structure object
  Context->Top_Level.Buffer = Buffer_Allocate (/*Device          =>*/ Context->Device,
                                               /*Physical_Device =>*/ Context->Physical_Device,
                                               /*Size            =>*/ Build_Sizes.accelerationStructureSize,
                                               /*Usage           =>*/ VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                                                                    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                               /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // Comment here !!!
  VK_CHECK (Context->Raytracing.Create_Acceleration_Structure (/*device                 =>*/ Context->Device,
                                                               /*pCreateInfo            =>*/ &(VkAccelerationStructureCreateInfoKHR){
                                                                 .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
                                                                 .buffer = Context->Top_Level.Buffer.Buffer,
                                                                 .size = Build_Sizes.accelerationStructureSize,
                                                                 .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
                                                               },
                                                               /*pAllocator             =>*/ NULL,
                                                               /*pAccelerationStructure =>*/ &Context->Top_Level.Handle));

  // Allocate persistent scratch memory for per-frame rebuilds
  Top_Level_Scratch_Buffer = Buffer_Allocate (/*Device          =>*/ Context->Device,
                                              /*Physical_Device =>*/ Context->Physical_Device,
                                              /*Size            =>*/ Build_Sizes.buildScratchSize,
                                              /*Usage           =>*/ VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                                                   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                              /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  // Comment here !!!
  Context->Top_Level.Address = Context->Raytracing.Get_Device_Address (/*device =>*/ Context->Device,
                                                                       /*pInfo  =>*/ &(VkAccelerationStructureDeviceAddressInfoKHR){
                                                                         .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
                                                                         .accelerationStructure = Context->Top_Level.Handle,
                                                                       });
}

/* Rebuild the TLAS each frame with the world BLAS as instance 0 (mask 0xFF) and optionally
   the weapon BLAS as instance 1 (mask 0x01, so shadow rays can skip it). */

void Top_Level_Rebuild (Vulkan_Context *Context, Acceleration_Structure *World, Acceleration_Structure *Weapon) {
  VkAccelerationStructureInstanceKHR Instances[2];
  memset (Instances, 0, sizeof (Instances));

  // Instance 0: the world geometry with identity transform
  Instances[0].transform.matrix[0][0]          = 1.f;
  Instances[0].transform.matrix[1][1]          = 1.f;
  Instances[0].transform.matrix[2][2]          = 1.f;
  Instances[0].mask                            = 0xFF;
  Instances[0].instanceCustomIndex             = 0;
  Instances[0].flags                           = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
  Instances[0].accelerationStructureReference  = World->Address;

  U32 Instance_Count = 1;

  // Instance 1 (optional): the weapon viewmodel, excluded from shadow rays via mask
  if (Weapon and Weapon->Handle) {
    Instances[1].transform.matrix[0][0]          = 1.f;
    Instances[1].transform.matrix[1][1]          = 1.f;
    Instances[1].transform.matrix[2][2]          = 1.f;
    Instances[1].mask                            = 0x01;
    Instances[1].instanceCustomIndex             = 1;
    Instances[1].flags                           = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    Instances[1].accelerationStructureReference  = Weapon->Address;
    Instance_Count = 2;
  }

  // Upload the instance data and rebuild the TLAS
  Buffer_Upload (Context->Device, Top_Level_Instance_Buffer, Instances, sizeof (VkAccelerationStructureInstanceKHR) * Instance_Count);

  VkAccelerationStructureGeometryKHR Geometry = {
    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
    .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
    .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
    .geometry.instances = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
      .arrayOfPointers = VK_FALSE,
      .data.deviceAddress = Top_Level_Instance_Buffer.Address,
    },
  };

  VkAccelerationStructureBuildGeometryInfoKHR Build_Info = {
    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
    .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
    .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
    .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
    .dstAccelerationStructure = Context->Top_Level.Handle,
    .scratchData.deviceAddress = Top_Level_Scratch_Buffer.Address,
    .geometryCount = 1,
    .pGeometries = &Geometry,
  };

  VkAccelerationStructureBuildRangeInfoKHR Range = {.primitiveCount = Instance_Count };
  const VkAccelerationStructureBuildRangeInfoKHR *Range_Pointer = &Range;

  VK_CHECK (vkResetCommandBuffer (Context->Command_Buffer, 0));
  VK_CHECK (vkBeginCommandBuffer (/*commandBuffer =>*/ Context->Command_Buffer,
                                  /*pBeginInfo    =>*/ &(VkCommandBufferBeginInfo){.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT }));
  Context->Raytracing.Command_Build (Context->Command_Buffer, 1, &Build_Info, &Range_Pointer);
  VK_CHECK (vkEndCommandBuffer (Context->Command_Buffer));
  VK_CHECK (vkQueueSubmit (/*queue       =>*/ Context->Queue,
                           /*submitCount =>*/ 1,
                           /*pSubmits    =>*/ &(VkSubmitInfo){.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &Context->Command_Buffer },
                           /*fence       =>*/ VK_NULL_HANDLE));
  VK_CHECK (vkQueueWaitIdle (Context->Queue));
}

/* <<pipeline>> ========================================================================================= */

/* Create the ray tracing pipeline with four shader stages: ray generation, primary miss,
   shadow miss, and closest-hit.  The descriptor set layout defines 12 bindings covering
   the TLAS, storage image, camera uniform, vertex/index/material/texture-id buffers,
   lightmap sampler, weapon buffers, and a variable-count texture array. */

void Raytracing_Pipeline_Create (Vulkan_Context *Context) {

  // Define the 12 descriptor bindings for the ray tracing pipeline
  VkDescriptorSetLayoutBinding Bindings[] = {
    {0,  VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1,   VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
    {1,  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              1,   VK_SHADER_STAGE_RAYGEN_BIT_KHR },
    {2,  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,             1,   VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
    {3,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1,   VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
    {4,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1,   VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
    {5,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1,   VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
    {6,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1,   VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
    {7,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,     1,   VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
    {8,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1,   VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
    {9,  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1,   VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
    {10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             1,   VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
    {11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,     256, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
  };

  // The last binding (texture array) uses partially-bound and variable-count flags
  VkDescriptorBindingFlags Binding_Flags[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT,
  };

  VkDescriptorSetLayoutBindingFlagsCreateInfo Binding_Flags_Info = {
    .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
    .bindingCount  = 12,
    .pBindingFlags = Binding_Flags,
  };

  VK_CHECK (vkCreateDescriptorSetLayout (/*device      =>*/ Context->Device,
                                         /*pCreateInfo =>*/ &(VkDescriptorSetLayoutCreateInfo){
      .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext        = &Binding_Flags_Info,
      .bindingCount = 12,
      .pBindings    = Bindings,
    },
                                         /*pAllocator  =>*/ NULL,
                                         /*pSetLayout  =>*/ &Context->Descriptor_Set_Layout));

  VK_CHECK (vkCreatePipelineLayout (/*device          =>*/ Context->Device,
                                    /*pCreateInfo     =>*/ &(VkPipelineLayoutCreateInfo){
      .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts    = &Context->Descriptor_Set_Layout,
    },
                                    /*pAllocator      =>*/ NULL,
                                    /*pPipelineLayout =>*/ &Context->Pipeline_Layout));

  // Load the four SPIR-V shader modules
  VkShaderModule Ray_Generation = Shader_Module_Load (Context->Device, "build/shaders/Ray_Generation.spv");
  VkShaderModule Closest_Hit    = Shader_Module_Load (Context->Device, "build/shaders/Closest_Hit.spv");
  VkShaderModule Primary_Miss   = Shader_Module_Load (Context->Device, "build/shaders/Primary_Miss.spv");
  VkShaderModule Shadow_Miss    = Shader_Module_Load (Context->Device, "build/shaders/Shadow_Miss.spv");

  // Define the pipeline shader stages
  VkPipelineShaderStageCreateInfo Stages[] = {
    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_RAYGEN_BIT_KHR,      Ray_Generation, "main", NULL },
    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_MISS_BIT_KHR,        Primary_Miss,   "main", NULL },
    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_MISS_BIT_KHR,        Shadow_Miss,    "main", NULL },
    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, Closest_Hit,    "main", NULL },
  };

  // Define the shader groups: raygen, two miss shaders, and one hit group
  VkRayTracingShaderGroupCreateInfoKHR Groups[] = {
    {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, NULL, VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,                0, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR },
    {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, NULL, VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,                1, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR },
    {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, NULL, VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,                2, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR },
    {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR, NULL, VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR, VK_SHADER_UNUSED_KHR, 3, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR },
  };

  // Create the ray tracing pipeline with recursion depth of 2 (primary + shadow rays)
  VK_CHECK (Context->Raytracing.Create_Pipeline (/*device            =>*/ Context->Device,
                                                 /*deferredOperation =>*/ VK_NULL_HANDLE,
                                                 /*pipelineCache     =>*/ VK_NULL_HANDLE,
                                                 /*createInfoCount   =>*/ 1,
                                                 /*pCreateInfos      =>*/ &(VkRayTracingPipelineCreateInfoKHR){
                                                   .sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
                                                   .stageCount                   = 4,
                                                   .pStages                      = Stages,
                                                   .groupCount                   = 4,
                                                   .pGroups                      = Groups,
                                                   .maxPipelineRayRecursionDepth = 2,
                                                   .layout                       = Context->Pipeline_Layout,
                                                 },
                                                 /*pAllocator        =>*/ NULL,
                                                 /*pPipelines        =>*/ &Context->Pipeline));

  // Destroy the shader modules now that the pipeline owns the compiled code
  vkDestroyShaderModule (Context->Device, Ray_Generation, NULL);
  vkDestroyShaderModule (Context->Device, Closest_Hit,    NULL);
  vkDestroyShaderModule (Context->Device, Primary_Miss,   NULL);
  vkDestroyShaderModule (Context->Device, Shadow_Miss,    NULL);
}

/* Build the shader binding table (SBT) by querying shader group handles from the pipeline
   and laying them out in an aligned buffer.  Each group gets one entry at the required stride. */

void Shader_Binding_Table_Create (Vulkan_Context *Context) {
  U32 Handle_Size      = Context->Raytracing_Properties.shaderGroupHandleSize;
  U32 Handle_Alignment = Context->Raytracing_Properties.shaderGroupHandleAlignment;
  U32 Base_Alignment   = Context->Raytracing_Properties.shaderGroupBaseAlignment;

  // Compute the stride: aligned handle size, at least as large as the base alignment
  U32 Stride = (Handle_Size + Handle_Alignment - 1) & compl (Handle_Alignment - 1);
  if (Stride < Base_Alignment) Stride = Base_Alignment;

  // Retrieve the raw shader group handles from the pipeline
  U32 Group_Count = 4;
  U8 *Handles = malloc (Handle_Size * Group_Count);
  VK_CHECK (Context->Raytracing.Get_Shader_Group_Handles (Context->Device, Context->Pipeline, 0, Group_Count, Handle_Size * Group_Count, Handles));

  // Allocate the SBT buffer and copy each handle at the proper stride offset
  U32 Table_Size = Stride * Group_Count;
  Context->Shader_Binding_Table_Buffer = Buffer_Allocate (/*Device          =>*/ Context->Device,
                                                          /*Physical_Device =>*/ Context->Physical_Device,
                                                          /*Size            =>*/ Table_Size,
                                                          /*Usage           =>*/ VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                          /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  // Comment here !!!
  U8 *Destination;
  vkMapMemory (Context->Device, Context->Shader_Binding_Table_Buffer.Memory, 0, Table_Size, 0, (void **)&Destination);
  for (U32 Index = 0; Index < Group_Count; Index++)
    memcpy (Destination + Index * Stride, Handles + Index * Handle_Size, Handle_Size);
  vkUnmapMemory (Context->Device, Context->Shader_Binding_Table_Buffer.Memory);
  free (Handles);

  // Set up the strided device address regions for each shader group type
  VkDeviceAddress Base = Context->Shader_Binding_Table_Buffer.Address;
  Context->Shader_Binding_Ray_Generation = (VkStridedDeviceAddressRegionKHR){Base + 0 * Stride, Stride, Stride };
  Context->Shader_Binding_Miss           = (VkStridedDeviceAddressRegionKHR){Base + 1 * Stride, Stride, Stride * 2 };
  Context->Shader_Binding_Hit            = (VkStridedDeviceAddressRegionKHR){Base + 3 * Stride, Stride, Stride };
  Context->Shader_Binding_Callable       = (VkStridedDeviceAddressRegionKHR){0 };
}

/* <<descriptors>> ====================================================================================== */

/* Create the descriptor pool and allocate a single descriptor set, then write all 12 bindings
   covering the TLAS, storage image, camera uniform, scene buffers, lightmap, weapon buffers,
   and the variable-count texture array. */

void Descriptor_Set_Create (Vulkan_Context *Context, Weapon_Instance *Weapon) {
  VkDescriptorPoolSize Pool_Sizes[] = {
    {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              1},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,             1},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             7},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,     257},
  };

  VK_CHECK (vkCreateDescriptorPool (/*device          =>*/ Context->Device,
                                    /*pCreateInfo     =>*/ &(VkDescriptorPoolCreateInfo){
                                      .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                      .maxSets       = 1,
                                      .poolSizeCount = 5,
                                      .pPoolSizes    = Pool_Sizes,
                                    },
                                    /*pAllocator      =>*/ NULL,
                                    /*pDescriptorPool =>*/ &Context->Descriptor_Pool));

  // Allocate the descriptor set with a variable descriptor count for the texture array
  U32 Variable_Count = Context->Texture_Count;
  VkDescriptorSetVariableDescriptorCountAllocateInfo Variable_Allocate = {
    .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
    .descriptorSetCount = 1,
    .pDescriptorCounts  = &Variable_Count,
  };

  VK_CHECK (vkAllocateDescriptorSets (/*device          =>*/ Context->Device,
                                      /*pAllocateInfo   =>*/ &(VkDescriptorSetAllocateInfo){
      .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext              = &Variable_Allocate,
      .descriptorPool     = Context->Descriptor_Pool,
      .descriptorSetCount = 1,
      .pSetLayouts        = &Context->Descriptor_Set_Layout,
    },
                                      /*pDescriptorSets =>*/ &Context->Descriptor_Set));

  // Prepare descriptor info structures for each binding
  VkWriteDescriptorSetAccelerationStructureKHR Acceleration_Write = {
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
    .accelerationStructureCount = 1,
    .pAccelerationStructures    = &Context->Top_Level.Handle,
  };

  VkDescriptorImageInfo  Image_Info             = {.imageView = Context->Raytracing_Storage_Image.View, .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
  VkDescriptorBufferInfo Camera_Info            = {Context->Camera_Uniform_Buffer.Buffer, 0, Context->Camera_Uniform_Buffer.Size };
  VkDescriptorBufferInfo Vertex_Info            = {Context->Vertex_Buffer.Buffer,         0, Context->Vertex_Buffer.Size };
  VkDescriptorBufferInfo Index_Info             = {Context->Index_Buffer.Buffer,          0, Context->Index_Buffer.Size };
  VkDescriptorBufferInfo Material_Info          = {Context->Material_Buffer.Buffer,       0, Context->Material_Buffer.Size };
  VkDescriptorBufferInfo Texture_Id_Info        = {Context->Texture_Id_Buffer.Buffer,     0, Context->Texture_Id_Buffer.Size };
  VkDescriptorBufferInfo Weapon_Vertex_Info     = {Weapon->Vertex_Buffer.Buffer,          0, Weapon->Vertex_Buffer.Size };
  VkDescriptorBufferInfo Weapon_Index_Info      = {Weapon->Index_Buffer.Buffer,           0, Weapon->Index_Buffer.Size };
  VkDescriptorBufferInfo Weapon_Texture_Id_Info = {Weapon->Texture_Id_Buffer.Buffer,      0, Weapon->Texture_Id_Buffer.Size };

  // Build the texture array descriptor info for all loaded textures
  VkDescriptorImageInfo *Texture_Infos = calloc (Context->Texture_Count, sizeof (VkDescriptorImageInfo));
  for (U32 Index = 0; Index < Context->Texture_Count; Index++) {
    Texture_Infos[Index] = (VkDescriptorImageInfo){
      .sampler     = Context->Texture_Sampler,
      .imageView   = Context->Texture_Views[Index],
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
  }

  VkDescriptorImageInfo Lightmap_Info = {
    .sampler = Context->Lightmap_Sampler, .imageView = Context->Lightmap_View, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };

  // Write all 12 descriptor bindings in one batch
  VkWriteDescriptorSet Writes[] = {
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &Acceleration_Write, Context->Descriptor_Set, 0, 0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR },
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, Context->Descriptor_Set, 1,  0, 1,                       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          &Image_Info },
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, Context->Descriptor_Set, 2,  0, 1,                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         NULL, &Camera_Info },
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, Context->Descriptor_Set, 3,  0, 1,                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         NULL, &Vertex_Info },
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, Context->Descriptor_Set, 4,  0, 1,                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         NULL, &Index_Info },
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, Context->Descriptor_Set, 5,  0, 1,                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         NULL, &Material_Info },
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, Context->Descriptor_Set, 6,  0, 1,                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         NULL, &Texture_Id_Info },
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, Context->Descriptor_Set, 7,  0, 1,                       VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &Lightmap_Info },
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, Context->Descriptor_Set, 8,  0, 1,                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         NULL, &Weapon_Vertex_Info },
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, Context->Descriptor_Set, 9,  0, 1,                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         NULL, &Weapon_Index_Info },
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, Context->Descriptor_Set, 10, 0, 1,                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         NULL, &Weapon_Texture_Id_Info },
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, Context->Descriptor_Set, 11, 0, Context->Texture_Count,  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, Texture_Infos },
  };

  vkUpdateDescriptorSets (Context->Device, 12, Writes, 0, NULL);
  free (Texture_Infos);
}

/* <<camera>> =========================================================================================== */

/* Compute inverse view and inverse projection matrices from the camera state, then upload
   the camera uniform buffer for the ray generation and closest-hit shaders. */

void Camera_Upload (Vulkan_Context *Context, Camera *State, F32 Field_Of_View, U32 Weapon_Texture_Base) {
  M4 View       = M4_View (State->Position, State->Yaw, State->Pitch);
  M4 Projection = M4_Perspective (Field_Of_View, (F32)Context->Width / Context->Height, 0.1f, 10000.f);

  struct {
    M4  Inverse_View, Inverse_Projection;
    U32 Frame;
    U32 Weapon_Texture_Base;
    F32 Padding[2];
  } Uniform;

  Uniform.Inverse_View        = M4_Inverse_Orthogonal (View);
  Uniform.Inverse_Projection  = M4_Inverse_Projection (Projection);
  Uniform.Frame               = State->Frame;
  Uniform.Weapon_Texture_Base = Weapon_Texture_Base;

  Buffer_Upload (Context->Device, Context->Camera_Uniform_Buffer, &Uniform, sizeof (Uniform));
}

/* <<weapon>> =========================================================================================== */

/* Update the weapon viewmodel's position, orientation, and animation state.  The weapon follows
   the camera with an offset, applies recoil and bob animation, and transforms all model vertices
   from local space to world space using the camera basis and tag_weapon animation frame. */

void Weapon_Update (Weapon_Instance *Weapon, const Camera *Camera_Data, F32 Delta_Time, int Fire) {
  if (not Weapon->Model.Vertex_Count) return;

  // Advance the fire animation state machine
  if (Fire and not Weapon->Is_Firing) {
    Weapon->Is_Firing = 1;
    Weapon->Fire_Time = 0;
  }
  if (Weapon->Is_Firing) {
    Weapon->Fire_Time += Delta_Time * 10.f;
    if (Weapon->Fire_Time >= 6.f) {
      Weapon->Is_Firing = 0;
      Weapon->Fire_Time = 0;
    }
  }
  Weapon->Bob_Time += Delta_Time;

  // Derive the camera's orthonormal basis from yaw and pitch
  F32 Cosine_Yaw   = cosf (Camera_Data->Yaw);
  F32 Sine_Yaw     = sinf (Camera_Data->Yaw);
  F32 Cosine_Pitch = cosf (Camera_Data->Pitch);
  F32 Sine_Pitch   = sinf (Camera_Data->Pitch);

  V3 Forward = V3_Make (Sine_Yaw * Cosine_Pitch, -Sine_Pitch, -Cosine_Yaw * Cosine_Pitch);
  V3 Right   = V3_Normalize (V3_Cross (Forward, V3_Make (0, 1, 0)));
  V3 Up      = V3_Cross (Right, Forward);

  // Compute the viewmodel offset with idle bob and recoil animations
  F32 Bob_Vertical   = sinf (Weapon->Bob_Time * 3.5f) * 0.4f;
  F32 Bob_Horizontal = cosf (Weapon->Bob_Time * 1.7f) * 0.2f;
  F32 Recoil         = Weapon->Is_Firing ? -1.2f * expf (-Weapon->Fire_Time * 5.f) : 0.f;

  V3 Offset = V3_Add (/*Left  =>*/ Camera_Data->Position,
                      /*Right =>*/ V3_Add (V3_Scale (Forward, 8.f + Recoil),
    V3_Add (V3_Scale (Right,   5.f + Bob_Horizontal),
            V3_Scale (Up,     -5.f + Bob_Vertical))));

  // Select the current animation frame from the hand model's tag_weapon data
  U32 Frame_Index = 0;
  if (Weapon->Model.Animation_Frame_Count > 1) {
    if (Weapon->Is_Firing) {
      Frame_Index = (U32)Weapon->Fire_Time;
      if (Frame_Index >= Weapon->Model.Animation_Frame_Count)
        Frame_Index = Weapon->Model.Animation_Frame_Count - 1;
    }
  }

  // Read the tag transform (origin + 3x3 axis matrix) for the current animation frame
  const F32 *Tag = Weapon->Model.Tag_Weapon[Frame_Index];

  // Swizzle each tag axis from Quake 3 Z-up to Y-up: (x,y,z) becomes (x,z,-y)
  V3 Axis_0 = (V3){Tag[3],  Tag[5],  -Tag[4]  };
  V3 Axis_1 = (V3){Tag[6],  Tag[8],  -Tag[7]  };
  V3 Axis_2 = (V3){Tag[9],  Tag[11], -Tag[10] };

  // Build the Y-up tag rotation matrix: columns = [forward | up | -left]
  F32 Tag_Y_Up[9] = {
    Axis_0.x, Axis_2.x, -Axis_1.x,
    Axis_0.y, Axis_2.y, -Axis_1.y,
    Axis_0.z, Axis_2.z, -Axis_1.z,
  };

  // Camera basis matrix (row-major): columns = forward, up, right
  F32 Camera_Basis[9] = {
    Forward.x, Up.x, Right.x,
    Forward.y, Up.y, Right.y,
    Forward.z, Up.z, Right.z,
  };

  // Combined rotation = Camera_Basis * Tag_Y_Up
  F32 Rotation[9];
  for (int Row = 0; Row < 3; Row++)
    for (int Column = 0; Column < 3; Column++)
      Rotation[Row * 3 + Column] = Camera_Basis[Row * 3 + 0] * Tag_Y_Up[0 * 3 + Column]
                                 + Camera_Basis[Row * 3 + 1] * Tag_Y_Up[1 * 3 + Column]
                                 + Camera_Basis[Row * 3 + 2] * Tag_Y_Up[2 * 3 + Column];

  // Scale the viewmodel down slightly for a better first-person perspective feel
  F32 Scale = 0.7f;

  // Transform each vertex from model space to world space
  for (U32 Index = 0; Index < Weapon->Model.Vertex_Count; Index++) {
    F32 Source_X = Weapon->Model.Vertices[Index].Position[0] * Scale;
    F32 Source_Y = Weapon->Model.Vertices[Index].Position[1] * Scale;
    F32 Source_Z = Weapon->Model.Vertices[Index].Position[2] * Scale;

    // Apply the combined rotation and translate by the camera offset
    Weapon->Transformed_Vertices[Index].Position[0] = Rotation[0] * Source_X + Rotation[1] * Source_Y + Rotation[2] * Source_Z + Offset.x;
    Weapon->Transformed_Vertices[Index].Position[1] = Rotation[3] * Source_X + Rotation[4] * Source_Y + Rotation[5] * Source_Z + Offset.y;
    Weapon->Transformed_Vertices[Index].Position[2] = Rotation[6] * Source_X + Rotation[7] * Source_Y + Rotation[8] * Source_Z + Offset.z;

    // Rotate normals by the same matrix (no translation for normals)
    F32 Normal_X = Weapon->Model.Vertices[Index].Normal[0];
    F32 Normal_Y = Weapon->Model.Vertices[Index].Normal[1];
    F32 Normal_Z = Weapon->Model.Vertices[Index].Normal[2];

      // Rotate the vertex normal by the same 3x3 rotation matrix
    Weapon->Transformed_Vertices[Index].Normal[0] = Rotation[0] * Normal_X + Rotation[1] * Normal_Y + Rotation[2] * Normal_Z;
    Weapon->Transformed_Vertices[Index].Normal[1] = Rotation[3] * Normal_X + Rotation[4] * Normal_Y + Rotation[5] * Normal_Z;
    Weapon->Transformed_Vertices[Index].Normal[2] = Rotation[6] * Normal_X + Rotation[7] * Normal_Y + Rotation[8] * Normal_Z;

    // Pass texture coordinates through unchanged
    Weapon->Transformed_Vertices[Index].Texture_Uv[0] = Weapon->Model.Vertices[Index].Texture_Uv[0];
    Weapon->Transformed_Vertices[Index].Texture_Uv[1] = Weapon->Model.Vertices[Index].Texture_Uv[1];
  }
}

/* <<input>> ============================================================================================ */

/* Poll SDL events and keyboard state to produce a frame's worth of input.
   Mouse motion is accumulated from all events; keyboard state is sampled once. */

Input Poll_Input (Vulkan_Context *Context) {
  Input Input_Data = {0};
  SDL_Event Event;

  // Process all pending SDL events: quit, escape, mouse motion, and mouse clicks
  while (SDL_PollEvent (&Event)) {
    if (Event.type == SDL_QUIT) Context->Quit = 1;
    if (Event.type == SDL_KEYDOWN and Event.key.keysym.sym == SDLK_ESCAPE) Context->Quit = 1;
    if (Event.type == SDL_MOUSEMOTION) {
      Input_Data.Delta_X += Event.motion.xrel;
      Input_Data.Delta_Y += Event.motion.yrel;
    }
    if (Event.type == SDL_MOUSEBUTTONDOWN and Event.button.button == SDL_BUTTON_LEFT)
      Input_Data.Fire = 1;
  }

  // Sample the current keyboard state for movement keys
  const U8 *Keyboard = SDL_GetKeyboardState (NULL);
  Input_Data.Forward = Keyboard[SDL_SCANCODE_W]     or Keyboard[SDL_SCANCODE_UP];
  Input_Data.Back    = Keyboard[SDL_SCANCODE_S]     or Keyboard[SDL_SCANCODE_DOWN];
  Input_Data.Left    = Keyboard[SDL_SCANCODE_A]     or Keyboard[SDL_SCANCODE_LEFT];
  Input_Data.Right   = Keyboard[SDL_SCANCODE_D]     or Keyboard[SDL_SCANCODE_RIGHT];
  Input_Data.Jump    = Keyboard[SDL_SCANCODE_SPACE];
  Input_Data.Crouch  = Keyboard[SDL_SCANCODE_LCTRL] or Keyboard[SDL_SCANCODE_C];
  return Input_Data;
}

/* <<render>> =========================================================================================== */

/* Execute one frame of ray tracing: wait for the previous frame to finish, acquire a swapchain
   image, dispatch rays, blit the storage image to the swapchain image, and present. */

void Raytracing_Frame (Vulkan_Context *Context) {

  // Wait for the previous frame's GPU work to complete
  VK_CHECK (vkWaitForFences (Context->Device, 1, &Context->Fence, VK_TRUE, UINT64_MAX));

  // Acquire the next swapchain image
  U32 Image_Index;
  VK_CHECK (vkAcquireNextImageKHR (/*device      =>*/ Context->Device,
                                   /*swapchain   =>*/ Context->Swapchain,
                                   /*timeout     =>*/ UINT64_MAX,
                                   /*semaphore   =>*/ Context->Semaphore_Image_Available,
                                   /*fence       =>*/ VK_NULL_HANDLE,
                                   /*pImageIndex =>*/ &Image_Index));

  // Reset the fence and begin recording the frame's command buffer
  VK_CHECK (vkResetFences (Context->Device, 1, &Context->Fence));
  VK_CHECK (vkResetCommandBuffer (Context->Command_Buffer, 0));
  VK_CHECK (vkBeginCommandBuffer (/*commandBuffer =>*/ Context->Command_Buffer,
                                  /*pBeginInfo    =>*/ &(VkCommandBufferBeginInfo){.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }));

  // Bind the ray tracing pipeline and descriptor set
  vkCmdBindPipeline (Context->Command_Buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, Context->Pipeline);
  vkCmdBindDescriptorSets (/*commandBuffer      =>*/ Context->Command_Buffer,
                           /*pipelineBindPoint  =>*/ VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                           /*layout             =>*/ Context->Pipeline_Layout,
                           /*firstSet           =>*/ 0,
                           /*descriptorSetCount =>*/ 1,
                           /*pDescriptorSets    =>*/ &Context->Descriptor_Set,
                           /*dynamicOffsetCount =>*/ 0,
                           /*pDynamicOffsets    =>*/ NULL);

  // Dispatch ray tracing for every pixel
  Context->Raytracing.Command_Trace_Rays (/*commandBuffer =>*/ Context->Command_Buffer,
                                          /*pRaygenSBT    =>*/ &Context->Shader_Binding_Ray_Generation,
                                          /*pMissSBT      =>*/ &Context->Shader_Binding_Miss,
                                          /*pHitSBT       =>*/ &Context->Shader_Binding_Hit,
                                          /*pCallableSBT  =>*/ &Context->Shader_Binding_Callable,
                                          /*width         =>*/ Context->Width,
                                          /*height        =>*/ Context->Height,
                                          /*depth         =>*/ 1);

  // Transition the storage image from general to transfer-source for the blit
  Image_Layout_Barrier (/*Command_Buffer     =>*/ Context->Command_Buffer,
                        /*Image              =>*/ Context->Raytracing_Storage_Image.Image,
                        /*Old_Layout         =>*/ VK_IMAGE_LAYOUT_GENERAL,
                        /*New_Layout         =>*/ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        /*Source_Access      =>*/ VK_ACCESS_SHADER_WRITE_BIT,
                        /*Destination_Access =>*/ VK_ACCESS_TRANSFER_READ_BIT,
                        /*Source_Stage       =>*/ VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                        /*Destination_Stage  =>*/ VK_PIPELINE_STAGE_TRANSFER_BIT);

  // Transition the swapchain image from undefined to transfer-destination
  Image_Layout_Barrier (/*Command_Buffer     =>*/ Context->Command_Buffer,
                        /*Image              =>*/ Context->Swapchain_Images[Image_Index],
                        /*Old_Layout         =>*/ VK_IMAGE_LAYOUT_UNDEFINED,
                        /*New_Layout         =>*/ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        /*Source_Access      =>*/ 0,
                        /*Destination_Access =>*/ VK_ACCESS_TRANSFER_WRITE_BIT,
                        /*Source_Stage       =>*/ VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        /*Destination_Stage  =>*/ VK_PIPELINE_STAGE_TRANSFER_BIT);

  // Blit the ray tracing result to the swapchain image (with potential scaling)
  vkCmdBlitImage (/*commandBuffer  =>*/ Context->Command_Buffer,
                  /*srcImage       =>*/ Context->Raytracing_Storage_Image.Image,
                  /*srcImageLayout =>*/ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  /*dstImage       =>*/ Context->Swapchain_Images[Image_Index],
                  /*dstImageLayout =>*/ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  /*regionCount    =>*/ 1,
                  /*pRegions       =>*/ &(VkImageBlit){
                    .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    .srcOffsets[1]  = {Context->Width, Context->Height, 1},
                    .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    .dstOffsets[1]  = {(int)Context->Swapchain_Extent.width, (int)Context->Swapchain_Extent.height, 1},
                  },
                  /*filter         =>*/ VK_FILTER_LINEAR);

  // Transition the storage image back to general for the next frame's writes
  Image_Layout_Barrier (/*Command_Buffer     =>*/ Context->Command_Buffer,
                        /*Image              =>*/ Context->Raytracing_Storage_Image.Image,
                        /*Old_Layout         =>*/ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        /*New_Layout         =>*/ VK_IMAGE_LAYOUT_GENERAL,
                        /*Source_Access      =>*/ VK_ACCESS_TRANSFER_READ_BIT,
                        /*Destination_Access =>*/ VK_ACCESS_SHADER_WRITE_BIT,
                        /*Source_Stage       =>*/ VK_PIPELINE_STAGE_TRANSFER_BIT,
                        /*Destination_Stage  =>*/ VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

  // Transition the swapchain image to present-source for presentation
  Image_Layout_Barrier (/*Command_Buffer     =>*/ Context->Command_Buffer,
                        /*Image              =>*/ Context->Swapchain_Images[Image_Index],
                        /*Old_Layout         =>*/ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        /*New_Layout         =>*/ VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        /*Source_Access      =>*/ VK_ACCESS_TRANSFER_WRITE_BIT,
                        /*Destination_Access =>*/ 0,
                        /*Source_Stage       =>*/ VK_PIPELINE_STAGE_TRANSFER_BIT,
                        /*Destination_Stage  =>*/ VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

  VK_CHECK (vkEndCommandBuffer (Context->Command_Buffer));

  // Submit the command buffer, waiting on image-available and signaling render-finished
  VkPipelineStageFlags Wait_Stage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
  VK_CHECK (vkQueueSubmit (/*queue       =>*/ Context->Queue,
                           /*submitCount =>*/ 1,
                           /*pSubmits    =>*/ &(VkSubmitInfo){
                             .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                             .waitSemaphoreCount   = 1,
                             .pWaitSemaphores      = &Context->Semaphore_Image_Available,
                             .pWaitDstStageMask    = &Wait_Stage,
                             .commandBufferCount   = 1,
                             .pCommandBuffers      = &Context->Command_Buffer,
                             .signalSemaphoreCount = 1,
                             .pSignalSemaphores    = &Context->Semaphore_Render_Finished,
                           },
                           /*fence       =>*/ Context->Fence));

  // Present the rendered image to the display
  VK_CHECK (vkQueuePresentKHR (/*queue        =>*/ Context->Queue,
                               /*pPresentInfo =>*/ &(VkPresentInfoKHR){
      .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores    = &Context->Semaphore_Render_Finished,
      .swapchainCount     = 1,
      .pSwapchains        = &Context->Swapchain,
      .pImageIndices      = &Image_Index,
    }));

  vkQueueWaitIdle (Context->Queue);
}

/* <<validate>> ========================================================================================= */

/* Write a JSON file summarizing the current renderer state for automated validation.
   Includes device info, scene statistics, and render configuration. */

void Export_Validate_JSON (const Vulkan_Context *Context, const Scene *Scene_Data, const char *Map_Name) {
  VkPhysicalDeviceProperties Device_Properties;
  vkGetPhysicalDeviceProperties (Context->Physical_Device, &Device_Properties);

  FILE *File = fopen ("build/validate.json", "w");
  fprintf (File, "{\n");
  fprintf (File, "  \"stage\": 3,\n");
  fprintf (File, "  \"map\": \"%s\",\n",         Map_Name ? Map_Name : "test");
  fprintf (File, "  \"device\": \"%s\",\n",      Device_Properties.deviceName);
  fprintf (File, "  \"rt_handle_size\": %u,\n",  Context->Raytracing_Properties.shaderGroupHandleSize);
  fprintf (File, "  \"rt_max_recursion\": %u,\n", Context->Raytracing_Properties.maxRayRecursionDepth);
  fprintf (File, "  \"scene\": {\n");
  fprintf (File, "    \"verts\": %u,\n",          Scene_Data->Vertex_Count);
  fprintf (File, "    \"tris\": %u,\n",           Scene_Data->Triangle_Count);
  fprintf (File, "    \"mats\": %u,\n",           Scene_Data->Material_Count);
  fprintf (File, "    \"textures_loaded\": %u,\n", Context->Textures_Loaded);
  fprintf (File, "    \"textures_fallback\": %u\n", Context->Texture_Count - Context->Textures_Loaded);
  fprintf (File, "  },\n");
  fprintf (File, "  \"render\": {\n");
  fprintf (File, "    \"width\": %d,\n",          Context->Width);
  fprintf (File, "    \"height\": %d,\n",         Context->Height);
  fprintf (File, "    \"sc_images\": %u\n",       Context->Swapchain_Image_Count);
  fprintf (File, "  }\n");
  fprintf (File, "}\n");
  fclose (File);
  printf ("[validate] build/validate.json written\n");
}

/* <<main>> ============================================================================================= */

int main (int Argument_Count, char **Arguments) {

  // Parse command-line arguments and configure window dimensions
  const char *Map_Path      = Argument_Count > 1 ? Arguments[1] : NULL;
  const int   Window_Width  = 1280;
  const int   Window_Height = 720;

  // Initialize the SDL video subsystem
  SDL_Init (SDL_INIT_VIDEO);

  // Initialize the Vulkan context and create the window
  Vulkan_Context Context = {.Width = Window_Width, .Height = Window_Height };

  Context.Window = SDL_CreateWindow (/*title =>*/ "Quake3 RT",
                                     /*x     =>*/ SDL_WINDOWPOS_CENTERED,
                                     /*y     =>*/ SDL_WINDOWPOS_CENTERED,
                                     /*w     =>*/ Window_Width,
                                     /*h     =>*/ Window_Height,
                                     /*flags =>*/ SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);

  // Set up the Vulkan instance, device, swapchain, and synchronization primitives
  Vulkan_Create_Instance        (&Context);
  Vulkan_Pick_Physical_Device   (&Context);
  Vulkan_Create_Logical_Device  (&Context);
  Vulkan_Create_Swapchain       (&Context);
  Vulkan_Create_Synchronization (&Context);

  // Create the ray tracing storage image and camera uniform buffer
  Context.Raytracing_Storage_Image = Image_Storage_Create (Context.Device, Context.Physical_Device, Window_Width, Window_Height);
  Context.Camera_Uniform_Buffer    = Buffer_Allocate (/*Device          =>*/ Context.Device,
                                                      /*Physical_Device =>*/ Context.Physical_Device,
                                                      /*Size            =>*/ 144,
                                                      /*Usage           =>*/ VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                      /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  // Transition the storage image to general layout for ray tracing writes
  Vulkan_Transition_Storage_Image (&Context);

  // Load the scene from a BSP file or build the test scene
  Spawn Spawn_Point   = {.Origin = {0, 2, 8}, .Angle = 0 };
  Collision_Map Collision = {0};

  // Select the BSP or test scene based on whether a map path was provided
  Scene Scene_Data = Map_Path
    ? Scene_Load_From_Bsp (Map_Path, &Spawn_Point, &Collision)
    : Scene_Build_Test ();

  // Upload all scene textures to the GPU (TGA files or solid-color fallbacks)
  Scene_Load_Textures (&Context, &Scene_Data);

  // Load the weapon model and its textures
  Weapon_Instance Weapon_Data = {0};
  Weapon_Data.Model = Weapon_Model_Load ();

  // If the weapon loaded, upload its textures and build its BLAS
  if (Weapon_Data.Model.Vertex_Count) {
    Weapon_Load_Textures (&Context, &Weapon_Data);
    Weapon_Bottom_Level_Initialize (&Context, &Weapon_Data);

  // Otherwise, create minimal dummy buffers so descriptor bindings 8-10 remain valid
  } else {
    Vertex Dummy_Vertex     = {0};
    U32    Dummy_Indices[3] = {0, 0, 0};
    U32    Dummy_Texture_Id = 0;

    // Create a single-vertex dummy buffer for the weapon vertex binding
    Weapon_Data.Vertex_Buffer = Buffer_Allocate (/*Device          =>*/ Context.Device,
                                                 /*Physical_Device =>*/ Context.Physical_Device,
                                                 /*Size            =>*/ sizeof (Vertex),
                                                 /*Usage           =>*/ VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                 /*Memory_Flags    =>*/ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Buffer_Upload (Context.Device, Weapon_Data.Vertex_Buffer, &Dummy_Vertex, sizeof (Dummy_Vertex));

    // Create dummy index and texture-id buffers with a single degenerate triangle
    Weapon_Data.Index_Buffer = Buffer_Stage_Upload (/*Device          =>*/ Context.Device,
                                                    /*Physical_Device =>*/ Context.Physical_Device,
                                                    /*Command_Buffer  =>*/ Context.Command_Buffer,
                                                    /*Queue           =>*/ Context.Queue,
                                                    /*Data            =>*/ Dummy_Indices,
                                                    /*Size            =>*/ sizeof (Dummy_Indices),
                                                    /*Usage           =>*/ VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    Weapon_Data.Texture_Id_Buffer = Buffer_Stage_Upload (/*Device          =>*/ Context.Device,
                                                         /*Physical_Device =>*/ Context.Physical_Device,
                                                         /*Command_Buffer  =>*/ Context.Command_Buffer,
                                                         /*Queue           =>*/ Context.Queue,
                                                         /*Data            =>*/ &Dummy_Texture_Id,
                                                         /*Size            =>*/ sizeof (Dummy_Texture_Id),
                                                         /*Usage           =>*/ VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  }

  // Build the world BLAS and initialize the TLAS with up to 2 instances (world + weapon)
  Context.Bottom_Level = Build_World_Bottom_Level (&Context, &Scene_Data);
  Top_Level_Initialize (&Context, 2);
  Top_Level_Rebuild (/*Context =>*/ &Context,
                     /*World   =>*/ &Context.Bottom_Level,
                     /*Weapon  =>*/ Weapon_Data.Model.Vertex_Count ? &Weapon_Data.Bottom_Level : NULL);

  // Create the ray tracing pipeline, shader binding table, and descriptor set
  Raytracing_Pipeline_Create    (&Context);
  Shader_Binding_Table_Create   (&Context);
  Descriptor_Set_Create         (&Context, &Weapon_Data);

  // Write validation data for automated testing
  Export_Validate_JSON (&Context, &Scene_Data, Map_Path);

  // Initialize the player state at the default view height
  Player Player_Data     = {0};
  Player_Data.View_Height = DEFAULT_VIEW_HEIGHT;

  // Set up camera-Y smoothing state (absorbs stair steps and crouch transitions)
  F32 Smoothed_Camera_Y    = 0;
  int Camera_Y_Initialized = 0;

  // Place the player at the BSP spawn point if a map was loaded
  if (Map_Path) {

    // Convert the BSP spawn angle to a yaw in radians
    Player_Data.Yaw = (90.f - Spawn_Point.Angle) * (F32)M_PI / 180.f;

    // Place the player at the spawn point, nudging upward if embedded in solid
    V3 Standing_Maximums = V3_Make (15, 32, 15);
    Player_Data.Position = Spawn_Point.Origin;

    // Nudge the player upward until they escape solid geometry (up to 128 attempts)
    for (int Iteration = 0; Iteration < 128; Iteration++) {
      Trace_Result Result = Collision_Trace (/*Start       =>*/ Player_Data.Position,
                                             /*End         =>*/ Player_Data.Position,
                                             /*Minimums    =>*/ PLAYER_MINIMUMS,
                                             /*Maximums    =>*/ Standing_Maximums,
                                             /*Collision   =>*/ &Collision,
                                             /*Use_Capsule =>*/ 1);
      if (not Result.All_Solid) break;
      Player_Data.Position.y += 1.f;
    }

    // Drop the player to the floor from the valid position
    V3 Down_Point = V3_Add (Player_Data.Position, V3_Make (0, -256, 0));
    Trace_Result Drop_Result = Collision_Trace (/*Start       =>*/ Player_Data.Position,
                                                /*End         =>*/ Down_Point,
                                                /*Minimums    =>*/ PLAYER_MINIMUMS,
                                                /*Maximums    =>*/ Standing_Maximums,
                                                /*Collision   =>*/ &Collision,
                                                /*Use_Capsule =>*/ 1);
    if (Drop_Result.Fraction < 1.f and not Drop_Result.All_Solid)
      Player_Data.Position = Drop_Result.End_Position;

    // Report the final spawn position
    printf ("[spawn] placed at (%.1f, %.1f, %.1f)\n", Player_Data.Position.x, Player_Data.Position.y, Player_Data.Position.z);

  // Use a fixed default position for the test scene
  } else {
    Player_Data.Position = V3_Make (0, 2 - DEFAULT_VIEW_HEIGHT, 8);
  }

  // Capture the mouse and enter the main loop
  SDL_SetRelativeMouseMode (SDL_TRUE);
  U64 Time_Start    = SDL_GetTicks64 ();
  U32 Frame_Counter = 0;
  while (not Context.Quit) {

    // Compute the frame delta time
    U64 Time_Current = SDL_GetTicks64 ();
    Context.Delta_Time = (Time_Current - Time_Start) * 0.001f;
    Time_Start = Time_Current;

    // Poll input and advance the player physics
    Input Input_Data = Poll_Input (&Context);
    Player_Move (&Player_Data, Map_Path ? &Collision : NULL, Input_Data, Context.Delta_Time);

    // Smooth the camera Y to absorb stair steps and crouch transitions
    {
      F32 Target_Camera_Y = Player_Data.Position.y + Player_Data.View_Height;
      if (not Camera_Y_Initialized) {
        Smoothed_Camera_Y    = Target_Camera_Y;
        Camera_Y_Initialized = 1;
      }
      F32 Camera_Y_Difference = Target_Camera_Y - Smoothed_Camera_Y;
      if (Camera_Y_Difference < -48.f)
        Smoothed_Camera_Y = Target_Camera_Y;
      else
        Smoothed_Camera_Y += Camera_Y_Difference * fminf (12.f * Context.Delta_Time, 1.f);
    }

    // Build the camera state for this frame
    Camera Camera_Data = {
      .Position = V3_Make (Player_Data.Position.x, Smoothed_Camera_Y, Player_Data.Position.z),
      .Yaw      = Player_Data.Yaw,
      .Pitch    = Player_Data.Pitch,
      .Frame    = Frame_Counter++,
    };

    // Update and rebuild the weapon's geometry if the weapon model is loaded
    if (Weapon_Data.Model.Vertex_Count) {
      Weapon_Update (&Weapon_Data, &Camera_Data, Context.Delta_Time, Input_Data.Fire);
      Weapon_Bottom_Level_Rebuild (&Context, &Weapon_Data);
      Top_Level_Rebuild (&Context, &Context.Bottom_Level, &Weapon_Data.Bottom_Level);
    }

    // Upload camera matrices and render the frame
    Camera_Upload (&Context, &Camera_Data, 90.f, Weapon_Data.Texture_Base_Index);
    Raytracing_Frame (&Context);
  }

  // Clean up
  vkDeviceWaitIdle (Context.Device);
  SDL_DestroyWindow (Context.Window);
  SDL_Quit ();
  return 0;
}

/* <<shaders>> = <<ray_generation>> <<closest_hit>> <<primary_miss>> <<shadow_miss>> */

/* <<ray_generation>> =================================================================================== */

/* Ray generation shader (rgen).  For each pixel, reconstruct a world-space ray from the
   inverse view and projection matrices, trace it against the TLAS, and write the resulting
   color into the storage image. */

glsl shader Ray_Generation rgen {
  #version 460
  #extension GL_EXT_ray_tracing : require

  // Binding 0: top-level acceleration structure containing world and weapon instances
  layout (set = 0, binding = 0)        uniform accelerationStructureEXT TLAS;

  // Binding 1: storage image written by this shader and blitted to the swapchain each frame
  layout (set = 0, binding = 1, rgba8) uniform image2D                  Storage_Image;

  // Binding 2: per-frame camera uniform buffer
  layout (set = 0, binding = 2) uniform Camera_Uniform {
    mat4  Inverse_View;          // Inverse view matrix for reconstructing world-space ray origin
    mat4  Inverse_Projection;    // Inverse projection matrix for reconstructing world-space ray direction
    uint  Frame;                 // Monotonically increasing frame counter for temporal effects
    uint  Weapon_Texture_Base;   // First texture index for weapon model surfaces
    float Padding[2];            // Explicit padding to align the uniform block to 16 bytes
  } Camera;

  // Primary ray color payload (location 0 matches closest-hit and primary miss shaders)
  layout (location = 0) rayPayloadEXT vec4 Payload;

  void main () {

    // Compute normalized device coordinates from the pixel center
    vec2 Pixel = vec2 (gl_LaunchIDEXT.xy) + 0.5;
    vec2 NDC   = Pixel / vec2 (gl_LaunchSizeEXT.xy) * 2.0 - 1.0;

    // Reconstruct the world-space ray origin and direction from the inverse matrices
    vec4 Origin    = Camera.Inverse_View       * vec4 (0, 0, 0, 1);
    vec4 Target    = Camera.Inverse_Projection * vec4 (NDC, 1, 1);
    vec4 Direction = Camera.Inverse_View       * vec4 (normalize (Target.xyz / Target.w), 0);

    // Trace the primary ray against the scene
    Payload = vec4 (0);
    traceRayEXT (/*topLevel        =>*/ TLAS,
                 /*rayFlags        =>*/ gl_RayFlagsOpaqueEXT,
                 /*cullMask        =>*/ 0xFF,
                 /*sbtRecordOffset =>*/ 0,
                 /*sbtRecordStride =>*/ 1,
                 /*missIndex       =>*/ 0,
                 /*origin          =>*/ Origin.xyz,
                 /*tMin            =>*/ 1e-3,
                 /*direction       =>*/ Direction.xyz,
                 /*tMax            =>*/ 1e4,
                 /*payload         =>*/ 0);

    // Write the shaded color to the storage image
    imageStore (Storage_Image, ivec2 (gl_LaunchIDEXT.xy), vec4 (Payload.rgb, 1.0));
  }
}

/* <<closest_hit>> ====================================================================================== */

/* Closest-hit shader (rchit).  Interpolates vertex attributes at the hit point using
   barycentric coordinates, samples the albedo from the bindless texture array, applies
   lightmap-based illumination for BSP geometry or simple directional lighting for the
   weapon model, traces a shadow ray toward the sun, and returns the final color. */

glsl shader Closest_Hit rchit {
  #version 460
  #extension GL_EXT_ray_tracing          : require
  #extension GL_EXT_nonuniform_qualifier : require

  // Primary ray color payload written by this shader and read by the ray generation shader
  layout (location = 0) rayPayloadInEXT vec4  Payload;

  // Shadow ray occlusion factor (1.0 = sunlit, 0.0 = shadowed)
  layout (location = 1) rayPayloadEXT   float Shadow_Factor;

  // Triangle barycentric coordinates at the hit point
  hitAttributeEXT vec2 Barycentric;

  /* ── Descriptor bindings (set 0) ───────────────────────────────────────────── */

  // Binding 0: top-level acceleration structure for secondary (shadow) ray traversal
  layout (set = 0, binding = 0) uniform accelerationStructureEXT TLAS;

  // Binding 2: camera uniform buffer (shared with ray generation shader)
  layout (set = 0, binding = 2) uniform Camera_Uniform {
    mat4  Inverse_View;          // Inverse view matrix (unused here, present for layout compatibility)
    mat4  Inverse_Projection;    // Inverse projection matrix (unused here)
    uint  Frame;                 // Frame counter (unused here)
    uint  Weapon_Texture_Base;   // First texture index for weapon model surfaces
    float Padding[2];            // Explicit padding to 16-byte alignment
  } Camera;

  /* Interleaved vertex layout matching the CPU-side Vertex struct (std430, 48 bytes per vertex) */
  struct Vertex {
    vec3  Position;              // World-space XYZ position
    float Padding_A;             // Padding to align to 16 bytes
    vec2  Texture_Uv;            // Diffuse texture coordinates
    vec2  Lightmap_Uv;           // Lightmap atlas coordinates
    vec3  Normal;                // Surface normal
    float Padding_B;             // Padding to align to 16 bytes
  };

  // Bindings 3–6: world geometry buffers
  layout (set = 0, binding = 3, std430) readonly buffer Vertex_Buffer     { Vertex Vertices[];    };   // Scene vertex array
  layout (set = 0, binding = 4, std430) readonly buffer Index_Buffer      { uint   Indices[];     };   // Scene triangle indices
  layout (set = 0, binding = 5, std430) readonly buffer Material_Buffer   { vec4   Materials[];   };   // Per-surface RGBA material tints
  layout (set = 0, binding = 6, std430) readonly buffer Texture_Id_Buffer { uint   Texture_Ids[]; };   // Per-triangle texture index

  // Binding 7: packed lightmap atlas sampler
  layout (set = 0, binding = 7) uniform sampler2D Lightmap;

  // Bindings 8–10: weapon model buffers (same Vertex layout as world buffers)
  layout (set = 0, binding = 8,  std430) readonly buffer Weapon_Vertex_Buffer     { Vertex Weapon_Vertices[];    };
  layout (set = 0, binding = 9,  std430) readonly buffer Weapon_Index_Buffer      { uint   Weapon_Indices[];     };
  layout (set = 0, binding = 10, std430) readonly buffer Weapon_Texture_Id_Buffer { uint   Weapon_Texture_Ids[]; };

  // Binding 11: bindless texture array for all diffuse textures (world + weapon)
  layout (set = 0, binding = 11) uniform sampler2D Textures[];

  /* ── Helpers ───────────────────────────────────────────────────────────────── */

  /* Expand 2D barycentric coordinates into a 3-component weight vector
     where the weights sum to 1.0 (w0 = 1 - u - v, w1 = u, w2 = v). */

  vec3 Barycentric_Weights (vec2 Bary) {
    return vec3 (1.0 - Bary.x - Bary.y, Bary.x, Bary.y);
  }

  /* ── Entry point ───────────────────────────────────────────────────────────── */

  void main () {
    vec3 Weights   = Barycentric_Weights (Barycentric);
    bool Is_Weapon = (gl_InstanceCustomIndexEXT == 1u);

    vec3 Position;
    vec3 Normal;
    vec2 Texture_Uv;
    vec2 Lightmap_Uv;

    // Weapon geometry: interpolate from the weapon vertex/index buffers
    if (Is_Weapon) {
      uint  Base     = gl_PrimitiveID * 3u;
      uvec3 Triangle = uvec3 (Weapon_Indices[Base], Weapon_Indices[Base + 1u], Weapon_Indices[Base + 2u]);

      Position    = Weights.x * Weapon_Vertices[Triangle.x].Position
                  + Weights.y * Weapon_Vertices[Triangle.y].Position
                  + Weights.z * Weapon_Vertices[Triangle.z].Position;

      Normal      = normalize (Weights.x * Weapon_Vertices[Triangle.x].Normal
                             + Weights.y * Weapon_Vertices[Triangle.y].Normal
                             + Weights.z * Weapon_Vertices[Triangle.z].Normal);

      Texture_Uv  = Weights.x * Weapon_Vertices[Triangle.x].Texture_Uv
                  + Weights.y * Weapon_Vertices[Triangle.y].Texture_Uv
                  + Weights.z * Weapon_Vertices[Triangle.z].Texture_Uv;

      Lightmap_Uv = vec2 (0);

    // World geometry: interpolate from the scene vertex/index buffers
    } else {
      uint  Base     = gl_PrimitiveID * 3u;
      uvec3 Triangle = uvec3 (Indices[Base], Indices[Base + 1u], Indices[Base + 2u]);

      Position    = Weights.x * Vertices[Triangle.x].Position
                  + Weights.y * Vertices[Triangle.y].Position
                  + Weights.z * Vertices[Triangle.z].Position;

      Normal      = normalize (Weights.x * Vertices[Triangle.x].Normal
                             + Weights.y * Vertices[Triangle.y].Normal
                             + Weights.z * Vertices[Triangle.z].Normal);

      Texture_Uv  = Weights.x * Vertices[Triangle.x].Texture_Uv
                  + Weights.y * Vertices[Triangle.y].Texture_Uv
                  + Weights.z * Vertices[Triangle.z].Texture_Uv;

      Lightmap_Uv = Weights.x * Vertices[Triangle.x].Lightmap_Uv
                  + Weights.y * Vertices[Triangle.y].Lightmap_Uv
                  + Weights.z * Vertices[Triangle.z].Lightmap_Uv;
    }

    // Look up the albedo from the bindless texture array using the per-triangle texture index
    uint Texture_Index;
    if (Is_Weapon) {
      Texture_Index = Camera.Weapon_Texture_Base + Weapon_Texture_Ids[gl_PrimitiveID];
    } else {
      Texture_Index = Texture_Ids[gl_PrimitiveID];
    }
    vec3 Albedo = texture (Textures[nonuniformEXT (Texture_Index)], Texture_Uv).rgb;

    // Shade the hit point
    vec3 Color;
    vec3 Sun_Direction = normalize (vec3 (0.6, 0.9, 0.3));

    // Weapon: simple ambient plus directional lighting (no lightmap, no shadows)
    if (Is_Weapon) {
      float Sun_Contribution = max (0.0, dot (Normal, Sun_Direction)) * 0.4;
      Color = Albedo * (vec3 (0.6) + vec3 (Sun_Contribution));

    // World geometry: lightmap-based illumination with real-time shadow rays
    } else {

      // Sample the baked lightmap with 2.5x overbright to match Quake 3 lighting levels
      vec3 Lightmap_Color = texture (Lightmap, Lightmap_Uv).rgb * 2.5;

      // Trace a shadow ray toward the sun (offset along normal to avoid self-intersection)
      Shadow_Factor = 0.0;
      vec3 Shadow_Origin = Position + Normal * 0.1;

      // Cull mask 0xFE excludes the weapon instance (mask bit 0x01) from shadow testing
      traceRayEXT (/*topLevel        =>*/ TLAS,
                   /*rayFlags        =>*/ gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
                   /*cullMask        =>*/ 0xFE,
                   /*sbtRecordOffset =>*/ 0,
                   /*sbtRecordStride =>*/ 1,
                   /*missIndex       =>*/ 1,
                   /*origin          =>*/ Shadow_Origin,
                   /*tMin            =>*/ 1e-3,
                   /*direction       =>*/ Sun_Direction,
                   /*tMax            =>*/ 1e4,
                   /*payload         =>*/ 1);

      // Combine: lightmap × albedo + subtle sun contribution modulated by the shadow result
      float Sun_Contribution = max (0.0, dot (Normal, Sun_Direction)) * Shadow_Factor * 0.15;
      Color = Albedo * (Lightmap_Color + vec3 (Sun_Contribution));

      // Apply subtle distance fog blending toward a blue-grey horizon color
      float Fog = 1.0 - exp (-gl_HitTEXT * 0.00012);
      Color = mix (Color, vec3 (0.45, 0.52, 0.65), Fog);
    }

    Payload.rgb = Color;
    Payload.a   = 1.0;
  }
}

/* <<primary_miss>> ===================================================================================== */

/* Primary miss shader (rmiss).  Called when a ray from the ray generation shader
   misses all geometry.  Returns a procedural sky gradient interpolated from a pale
   horizon color to a deeper blue at the zenith, based on the ray's vertical component. */

glsl shader Primary_Miss rmiss {
  #version 460
  #extension GL_EXT_ray_tracing : require

  // Incoming color payload to fill with the sky color
  layout (location = 0) rayPayloadInEXT vec4 Payload;

  void main () {

    // Compute a 0-to-1 gradient factor from the ray's vertical direction component
    vec3  Ray_Direction = normalize (gl_WorldRayDirectionEXT);
    float Gradient      = Ray_Direction.y * 0.5 + 0.5;

    // Interpolate between pale horizon and deep-blue zenith
    Payload.rgb = mix (vec3 (0.55, 0.62, 0.72),     // Horizon color (pale blue-grey)
                       vec3 (0.18, 0.38, 0.78),      // Zenith color (deeper blue)
                       Gradient);
    Payload.a   = 0.0;
  }
}

/* <<shadow_miss>> ====================================================================================== */

/* Shadow miss shader (rmiss, index 1).  Called when a shadow ray reaches the sun
   without hitting any occluder.  Sets the shadow factor to 1.0 to indicate full
   illumination; if the ray had hit geometry, the closest-hit shader would not be
   invoked (due to SkipClosestHitShader flag) and the factor remains at 0.0. */

glsl shader Shadow_Miss rmiss {
  #version 460
  #extension GL_EXT_ray_tracing : require

  // Shadow factor payload: 1.0 = sunlit, 0.0 = shadowed (initialized to 0.0 by caller)
  layout (location = 1) rayPayloadInEXT float Shadow_Factor;

  void main () {
    Shadow_Factor = 1.0;
  }
}
