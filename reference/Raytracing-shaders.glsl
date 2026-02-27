#if __VERSION__ < 300
# error Sorry, this shader requires WebGL 2.0!
#endif

/***************************************************************
  Quake / Introduction
  A textureless* shader recreating the first room of Quake (1996)
  Andrei Drexler 2018

  For some details on how this shader was made, see this Twitter thread:
  https://twitter.com/andrei_drexler/status/1217945589989748742

  Many thanks to:

- id Software - for creating not only a great game/series, but also
  a thriving modding community around it through the release of
  dev tools, code, and specs, inspiring new generations of game developers:
  https://github.com/id-Software/Quake

- John Romero - for creating such memorable designs in the first place,
  and then releasing the original map files:
  https://rome.ro/news/2016/2/14/quake-map-sources-released

- Inigo Quilez (@iq) - for his many articles/code samples on signed distance fields,
  ray-marching, noise and more:
  https://iquilezles.org/articles/distfunctions
  https://iquilezles.org/articles/smin
  https://iquilezles.org/articles/voronoilines
  https://iquilezles.org/www/index.htm

- Jamie Wong (@jlfwong) - for his article on ray-marching/SDF's (and accompanying samples):
  http://jamie-wong.com/2016/07/15/ray-marching-signed-distance-functions/

- Mercury - for the hg_sdf library:
  http://mercury.sexy/hg_sdf

- Paul Malin (@P_Malin) - for his awesome QTest shader, that prompted me to resume work and replace
  the AO+TSS+negative/capsule lights combo with proper lightmaps, somewhat similar to his solution:
  https://www.shadertoy.com/view/MdGXDK

- Brian Sharpe - for his GPU noise library/blog:
  https://github.com/BrianSharpe/GPU-Noise-Lib
  https://briansharpe.wordpress.com

- Dave Hoskins (@Dave_Hoskins) - for his 'Hash without Sine' functions:
  https://www.shadertoy.com/view/4djSRW

- Marc B. Reynolds (@MBR) - for his 2D Weyl hash code
  https://www.shadertoy.com/view/Xdy3Rc
  http://marc-b-reynolds.github.io/math/2016/03/29/weyl_hash.html

- Morgan McGuire (@morgan3d) - hash functions:
  https://www.shadertoy.com/view/4dS3Wd
  http://graphicscodex.com

- Fabrice Neyret (@FabriceNeyret2) - for his 'Shadertoy - Unofficial' blog:
  https://shadertoyunofficial.wordpress.com

- Alan Wolfe (@demofox) - for his blog post on making a ray-traced snake game in Shadertoy,
  which inspired me to get started with this shader:
  https://blog.demofox.org/2016/01/16/making-a-ray-traced-snake-game-in-shadertoy/
  https://www.shadertoy.com/view/XsdGDX

- Playdead - for their presentation/code on temporal reprojection antialiasing:
  http://twvideo01.ubm-us.net/o1/vault/gdc2016/Presentations/Pedersen_LasseJonFuglsang_TemporalReprojectionAntiAliasing.pdf
  https://github.com/playdeadgames/temporal

- Sebastian Aaltonen (@sebbbi) - for his 'Advection filter comparison' shader:
  https://www.shadertoy.com/view/lsG3D1

- Bart Wronski - for his Poisson sampling generator:
  https://github.com/bartwronski/PoissonSamplingGenerator

- And, of course, Inigo Quilez and Pol Jeremias - for Shadertoy.
  
  ---

  If you're interested in other recreations of id Software games,
  you might also like:
  
- Wolfenstein 3D by @reinder:
  https://www.shadertoy.com/view/4sfGWX

- [SH16C] Doom by @P_Malin - fully playable E1M1, pushing the Shadertoy game concept to its limits:
  https://www.shadertoy.com/view/lldGDr

- Doom 2 by @reinder:
  https://www.shadertoy.com/view/lsB3zD

- QTest by @P_Malin:
  https://www.shadertoy.com/view/MdGXDK

  ---

  * The blue noise texture doesn't count!
***************************************************************/

////////////////////////////////////////////////////////////////
// Image buffer:
// - loading screen/console
// - rendered image presentation + motion blur
// - pain blend
// - performance graph
// - text (console/skill selection/demo stage)
////////////////////////////////////////////////////////////////

// config.cfg //////////////////////////////////////////////////

#define FPS_GRAPH_MAX     60
#define PRINT_SKILL_MESSAGE   1
#define GAMMA_MODE        0   // [0=RGB; 1=luma]

#define USE_CRT_EFFECT      1
#define CRT_MASK_WEIGHT     1./16.
#define CRT_SCANLINE_WEIGHT   1./16.

#define USE_MOTION_BLUR     1
#define MOTION_BLUR_FPS     60
#define MOTION_BLUR_AMOUNT    0.5   // fraction of frame time the shutter is open
#define MOTION_BLUR_SAMPLES   9   // recommended range: 7..31

#define DEBUG_LIGHTMAP      0   // 1=packed (RGBA); 2=unpacked (greyscale)
#define DEBUG_ATLAS       0
#define DEBUG_TEXTURE     -1
#define DEBUG_CLICK_ZOOM    4.0   // zoom factor when clicking

// For key bindings/input settings, check out Buffer A

// For a more enjoyable experience, try my Shadertoy FPS mode script
// https://github.com/andrei-drexler/shadertoy-userscripts

// TODO (maybe) ////////////////////////////////////////////////

// - Improve texture quality
// - Clean-up & optimizations
// - Functional console
// - Gameplay & HUD polish

// Snapshots ///////////////////////////////////////////////////

// For a comparison with the initial release, check out
// https://www.shadertoy.com/view/MtVBzV

// For a comparison with the last version to use negative/capsule lights,
// ambient occlusion and temporal reprojection (for denoising), check out
// https://www.shadertoy.com/view/Ws2SR1

// Changelog ///////////////////////////////////////////////////

// 2021-01-06
// - Added support for +/- in Firefox (different keycodes...)
//
// 2020-11-15
// - Changed mouselook code to be compatible with recent Shadertoy update
//
// 2020-03-26
// - Slightly more Quake-like acceleration/friction
//
// 2020-03-21
// - Improved mouse movement handling for less common aspect ratios (e.g. portrait)
// - Switched to centered motion blur sampling (better near screen edges)
// - Moved scene quantization to Buffer D (before motion blur)
//
// 2020-03-20
// - Added workarounds for old ANGLE error (length can only be called on array names, not on array expressions)
//   https://github.com/google/angle/commit/bb2bbfbbf443fe0c1f8af12bacfdf1a945aea5a4#diff-b7bae477c1aea4edd01d4479fda69a87L5488
//
// 2019-09-03
// - Added window projection when light shafts are enabled
//
// 2019-09-01
// - Added basic/subtle CRT effect, with menu option (off by default)
//
// 2019-08-26
// - Added subtle light shaft animation (VOLUMETRIC_ANIM in Buffer D)
//
// 2019-08-24
// - Bumped default brightness up another notch
// - Fixed muzzle flash texture
//
// 2019-08-22
// - Added 'light shafts' menu option (off by default)
//
// 2019-08-21
// - Added volumetric lighting player shadow
// - Tweaked volumetric lighting falloff
//
// 2019-08-20
// - Added light shafts (RENDER_VOLUMETRICS 1 in Buffer D)
//
// 2019-07-02
// - Switched entity rotations to quaternions in order to reduce register
//   pressure => ~35% GPU usage @800x450, down from ~40% (both at 1974 MHz)
// - Bumped default brightness up a notch
// - Tweaked WINDOW02_1 texture
//
// 2019-06-30
// - Added texture filter menu option (shortcut: T)
//
// 2019-06-19
// - Tweaked console id logo background
// - Added motion blur menu option
//
// 2019-06-12
// - Tweaked QUAKE, WINDOW02_1 and WBRICK1_5 textures
// - Tweaked weapon model SDF, textures and movement anim range
// - Tweaked console background and text
// - Added back optional motion blur (USE_MOTION_BLUR 1 in Image tab)
// - Simplified naming scheme for Dave Hoskins' Hash without Sine functions
//
// 2019-06-07
// - Replaced FOV-based weapon model offset with pitch adjustment, eliminating
//   severe perspective distortion at higher vertical FOV's (e.g portrait mode)
// - Fixed console version string misalignment on aspect ratios other than 16:9
//
// 2019-06-06
// - Added FOV-based weapon model offset
// - Added BAKE_LIGHTMAP macro in Buffer B (for lower iteration times)
// - Tweaked weapon model SDF
// - Tweaked QUAKE texture
//
// 2019-06-04
// - Baked collision map planes to Buffer A, reducing its compilation time by ~6%
//   (~8.3 vs ~8.8 seconds) on my system
// - Baked font glyphs to Buffer C, reducing Image buffer compilation time by ~14%
//   (~3.7 vs ~4.3 seconds)
// - Removed unused UV visualisation code, reducing Buffer D compilation time by ~47%
//   (~1.0 vs ~1.9 seconds)
// - Optimized pow10 function used in number printing
//
// 2019-05-10
// - Fixed infinite loading screen on OpenGL (out vs inout)
//
// 2019-05-05
// - Freeze entity and texture animations when menu is open
//
// 2019-05-02
// - Added 'Show weapon' and 'Noclip' options
// - Tweaked WBRICK1_5 texture
//
// 2019-04-24
// - Switched to MBR's 2D Weyl hash for UI textures (for consistency across platforms)
// - More UI texture tweaks
//
// 2019-04-22
// - Tweaked engraved textures (QUAKE, Options title)
// - Added QUAKE/id image on the left side of the menu
// - Added 'Show lightmap' menu option
// - Removed left-over guard band code
//
// 2019-04-18
// - Tweaked brushwork for slightly better lightmap utilization
// - Changed lightmap dimensions to potentially accomodate another UI texture
//   in Buffer C even in 240x135 mode (smallest? Shadertoy thumbnail size)
// - Tweaked menu title texture
//
// 2019-04-17
// - Wired all options
// - Changed menu definition to array of structs (from array of ints), fixing menu
//   behavior on Surface 3 (codegen bug?)
// - Paused gameplay when menu is open
// - Changed default GAMMA_MODE (Image tab) to RGB (authentic)
//
// 2019-04-16
// - Added Options menu stub
//
// 2019-04-11
// - Added strafe key (Alt)
// - Added FPS display in demo mode
//
// 2019-04-10
// - Added macro-based RLE for runs of 0 in the lightmap tile array (-~270 chars)
// - Changed lightmap encoding and sample weighting
// - Changed lightmap padding to 0.5 texels (from 2), reduced number of dilation
//   passes to 1 (from 2) and disabled lightmap blur pass
// - Added zoom click for DEBUG_* modes (Image tab)
//
// 2019-04-09
// - More texture tweaks
// - Added LOD_SLOPE_SCALE option (Buffer D)
// - Reduced potentially lit surface set (4.3k smaller shader code)
// - Added basic compression for the lightmap tile array in Buffer A (-1k)
// - Changed default rendering scale to 1 (was 0.5)
//
// 2019-03-31
// - Changed GENERATE_TEXTURES macro (Buffer A) from on/off switch to bit mask,
//   enabling selective texture compilation (for faster iteration)
// - Tweaked WBRICK1_5 and WIZMET1_1 textures
//
// 2019-03-26
// - Tweaked CITY4_6, BRICKA2_2 and WINDOW02_1 textures
//
// 2019-03-24
// - Snapped console position to multiples of 2 pixels to avoid shimmering
//
// 2019-03-23
// - Robust thumbnail mode detection (based on iTime at iFrame 0)
// - Increased brightness, especially in thumbnail mode
// - Disabled weapon rendering for demo mode cameras 1 and 3 and tweaked their locations
//
// 2019-03-22
// - Fixed shadow discontinuities on the floor at the start of the skill hallways
// - Increased lightmap padding to 2 px
// - Optimized brushwork to reduce lightmapped area
// - Added 3 more viewpoints for the demo mode/thumbnail view
//
// 2019-03-20
// - Lightmap baking tweaks: 8xAA + 1 blur step, ignored solid samples,
//   extrapolation, better (but still hacky) handling of liquid brushes,
//   uv quantization, reduced baking time on low-end devices (e.g. Surface 3)
// - Added color quantization for console & composited scene
//
// 2019-03-18
// - Replaced AO+TSS combo with actual lightmaps. Saved old version as
//   https://www.shadertoy.com/view/Ws2SR1
//
// 2019-03-17
// - Octahedral encoding for gbuffer normals
//
// 2019-03-15
// - Major performance improvement for Intel iGPUs: 45+ fps
//   on a Surface 3 (Atom x7-Z8700), up from ~1.4 fps (window mode)
// - Added luminance gamma option (GAMMA_MODE in Image tab)
//
// 2019-03-07
// - Tighter encoding for axial brushes & atlas tiles
// - Added experimental TSS_FILTER (Buffer D), based on
//   https://www.shadertoy.com/view/lsG3D1 (by sebbbi)
//
// 2019-01-10
// - Added workaround for Shadertoy double-buffering bug on resize.
//   This fixes partially black textures when pausing the shader,
//   shrinking the window and then maximizing it again
//
// 2019-01-05
// - Reduced overall compilation time by ~30% on my system (~28.5s -> ~20s),
//   mostly from Buffer A optimizations (~14s -> ~6s) :>
//
// 2019-01-03
// - Minor map compiler tweaks: flat (degenerate) liquid brushes,
//   improved precision of certain operations
// - Added extra wall sliding friction
//
// 2018-12-21
// - Added Z/Q bindings for AZERTY users
//
// 2018-12-20
// - Added # of targets left to HUD
// - Added HUD line spacing, shadow box and color highlight effect
//
// 2018-12-16
// - Blue noise (instead of white) for the motion blur trail offset
//
// 2018-12-14
// - Disabled TSS/motion blur when teleporting
// - Minor motion blur tweaks
//
// 2018-12-10
// - Added experimental motion blur code (Buffer D),
//   mostly for video recording; off by default
//
// 2018-12-04
// - Slightly more compact map material assignment
// - Removed overly cautious fudge factor (-5%) from slide move code
//
// 2018-11-28
// - Disabled continuous texture generation (~11% perf boost)
//
// 2018-11-26
// - Changed USE_PARTITION macro (in Buffer B) to axial/non-axial bitmask
//
// 2018-11-25
// - Added BVL for axial brushes
// - Removed BVH code (USE_PARTITION 2), keeping just the BVLs
//
// 2018-11-24
// - Minor map brushwork optimization
// - Increased number of leaves in non-axial brush BVH from 7 to 10
// - Further reduced Buffer B compilation time by about 40%
//
// 2018-11-22
// - Reduced Buffer B compilation time (~10.8s vs ~12.6s) using Klems'
//   loop trick in normal estimation function
// - Added USE_ENTITY_AABB macro in Buffer B
//
// 2018-11-19
// - Resolution-dependent ray marching epsilon scale
//
// 2018-11-18
// - Tweaked loading screen sparks
// - Disabled weapon firing before console slide-out
// - Invalidated ground plane when noclipping
//
// 2018-11-14
// - More BVH/BVL tweaks: greedy split node selection instead of recursive,
//   leaf count limit instead of primitive count, sorted BVL elements
//   based on distance to world center
//
// 2018-11-13
// - Tweaked SAH builder to consider all axes, not just the largest one
//
// 2018-11-12
// - Added non-axial brush partition (USE_PARTITION in Buffer B)
//
// 2018-11-10
// - Fixed weapon model TSS ghosting
//
// 2018-11-09
// - Moved teleporter effect to Buffer C (lower resolution) and optimized
//   its hashing
// - Added lightning flash on game start
//
// 2018-11-08
// - Added fast path for axial brush rendering
//
// 2018-11-07
// - Added entity AABB optimization (and DEBUG_ENTITY_AABB option in Buffer B)
//
// 2018-11-06
// - Slightly optimized raymarching using bounding spheres
//   (~5% lower overall GPU usage at max frequency for 800x450 @144 fps)
//
// 2018-11-03
// - Started adding persistent state structs/macros to improve
//   code readability (e.g. game_state.level instead of fragColor.x);
//   see end of Common buffer
// - Reduced Buffer B compilation time (~6.9s vs ~7.5s on my system)
//
// 2018-11-02
// - Added credits
//
// 2018-10-29
// - Removed demo mode voronoi halftoning
// - Added two more balloon sets to provide some round-to-round variation
// - Tweaked round timing: can you make it to level 18?
//
// 2018-10-27
// - Added INVERT_MOUSE option (Buffer A)
// - Desaturated/darkened balloons during the game over transition
//
// 2018-10-26
// - Fixed bug that caused popped balloons to reappear during
//   the game over animation
// - Disabled balloon popping during game over animation
// - Added 'Game over' message
//
// 2018-10-25
// - Minor polish: blinking timer when almost out of time,
//   animated balloon scale-out when game is over
//
// 2018-10-24
// - Added level start countdown
// - Added game timer; game is over when time expires
// - Match particle color with balloon color, if a balloon was hit
//
// 2018-10-23
// - Added very basic target practice mode and reduced shotgun spread;
//   aim for the sky!
//
// 2018-10-17
// - Added automatic pitch adjustment when moving and not using the mouse
//   for looking around; see LOOKSPRING_DELAY in Buffer A
//
// 2018-10-16
// - Fixed occasional stair detection stutter at low FPS
//
// 2018-10-12
// - Optimized entity normal estimation: ~6.2 seconds to compile Buffer B,
//   down from ~8.6
//
// 2018-10-11
// - Sample weapon lighting at the ground level instead of a fixed distance
//   below the camera
// - Added sliding down slopes
//
// 2018-10-10
// - Fixed weapon model lighting seam when crossing a power-of-two boundary
// - Clamped lighting to prevent weapon model overdarkening from negative lights
// - Minor collision map brushwork tweak
//
// 2018-10-09
// - Optimized Buffer A compilation time a bit (~14.1 vs ~14.8 seconds on my system)
// - Fixed cloud tiling on Linux
//
// 2018-10-08
// - Added weapon firing on E/F (not Ctrl, to avoid closing the window on Ctrl+W)
// - Fixed TSS artifact when climbing stairs
//
// 2018-10-07
// - Tweaked ray-marching loop to eliminate silhouette sparkles
// - Tweaked weapon model colors (this time without f.lux...)
// - Disabled TSS for the weapon model
//
// 2018-10-06
// - Added shotgun model. Set RENDER_WEAPON to 0 in Buffer B to disable it
// - Minor brushwork optimizations
//
// 2018-10-04
// - Fixed brushwork that deviated from the original design in the playable area
// - Tightened up map definition some more (-13 lines)
//
// 2018-10-03
// - Added NOCLIP option (Buffer A)
//
// 2018-10-01
// - Faked two-sided lava/water surfaces
// - Added simple lava pain effect
//
// 2018-09-30
// - Tweaked player movement a bit (air control, smoother accel/decel, head bobbing)
// - Changed console typing animation :>
//
// 2018-09-29
// - Added basic collision detection; needs more work
//
// 2018-09-19
// - Reduced BufferC compilation time (~3s vs ~4.2s on my system)
//
// 2018-09-18
// - Added particle trail early-out (using screen-space bounds)
//
// 2018-09-17
// - Tightened up map definition even more (<100 lines now)
//
// 2018-09-15
// - Particle trail tweaks
//
// 2018-09-14
// - Made particles squares instead of disks (more authentic)
// - Added proper occlusion between particles
//
// 2018-09-11
// - Added fireball particle trail (unoptimized)
//
// 2018-09-10
// - Lighting tweaks
//
// 2018-09-08
// - Added option to reduce TSS when in motion (by 50% by default).
//   Seems counter-intuitive, but the end result is that the image
//   stays sharp in motion, and static shots are still denoised
// - Fixed temporal supersampling artifacts due to unclamped RGB input
// - Increased sky layer resolution (128x128, same as in Quake) and
//   adjusted atlas accordingly (mip 0 is 512x256 now, filled 100%)
// - Increased sky speed to roughly match Quake (super fast)
// - Added manual shadows for the spikes in the 'hard' area
//
// 2018-09-07
// - YCoCg for temporal supersampling (USE_YCOCG in the Common tab)
// - More map optimizations (~23% smaller now compared to first version)
//
// 2018-09-06
// - Added temporal supersampling (Buffer D), mostly to denoise AO
// - Disabled map rendering during loading screen
//
// 2018-09-05
// - Map optimizations: ~17% fewer brushes/planes, tweaked material
//   assignment, aligned some non-axial planes
// - Added basic FPS display (mostly for full-screen mode)
// - Added text scaling based on resolution
//
// 2018-09-03
// - Added depth/depth+angle mip-mapping (USE_MIPMAPS 2/3 in Buffer C)
// - Added mip level dithering (LOD_DITHER in Buffer C)
// - Even more compact map data storage (Buffer B)
// - Tweaked lava and water textures a bit
//
// 2018-08-30
// - Tweaked entity SDF's a bit
// - Slightly more compact map data formatting
//
// 2018-08-27
// - Added version number and id logo to console
// - More thumbnail time-shifting
//
// 2018-08-26
// - Rewrote font code
// - Added console loading/typing intro
//
// 2018-08-24
// - Added (static) console text
// - Added skill selection message triggers
//
// 2018-08-23
// - Added demo mode captions (with basic fixed-width font code).
//   Had to move some of the demo code, including the master switch,
//   to the Common tab.
//
// 2018-08-22
// - Enabled demo mode automatically for thumbnails and adjusted
//   thumbnail time again
//
// 2018-08-21
// - Added mouse filtering (Buffer A). Useful for video recording;
//   off by default
// - Added voronoi halftoning and DEMO_MODE_HALFTONE in Buffer C
//
// 2018-08-20
// - Use halftoning instead of blue noise dither for demo mode
//   transition (doesn't confuse video encoders as much)
//
// 2018-08-19
// - Reduced compilation time for Buffer B by almost 5 seconds
//   on my machine (~7.6 vs ~12.5)
//
// 2018-08-18
// - Generate lower-res atlas/mip chain when resolution is too low
//   to fit a full-res one (e.g. thumbnails, really small windows)
// - Show intro in thumbnail mode (by offsetting time by ~10s)
//

////////////////////////////////////////////////////////////////
// Implementation //////////////////////////////////////////////
////////////////////////////////////////////////////////////////

#define SETTINGS_CHANNEL  iChannel0
#define PRESENT_CHANNEL   iChannel2
#define NOISE_CHANNEL   iChannel1
#define LIGHTMAP_CHANNEL  iChannel3

////////////////////////////////////////////////////////////////

float g_downscale = 2.;
float g_animTime = 0.;

vec4 load(vec2 address)
{
    return load(address, SETTINGS_CHANNEL);
}

// Font ////////////////////////////////////////////////////////

const int
    _A_= 1, _B_= 2, _C_= 3, _D_= 4, _E_= 5, _F_= 6, _G_= 7, _H_= 8, _I_= 9, _J_=10, _K_=11, _L_=12, _M_=13,
    _N_=14, _O_=15, _P_=16, _Q_=17, _R_=18, _S_=19, _T_=20, _U_=21, _V_=22, _W_=23, _X_=24, _Y_=25, _Z_=26,
    _0_=27, _1_=28, _2_=29, _3_=30, _4_=31, _5_=32, _6_=33, _7_=34, _8_=35, _9_=36,
    _SPACE_         =  0,
    _CARET_         = 45,
    _PLUS_          = 37,
    _MINUS_         = 38,
    _SLASH_         = 39,
    _DOT_           = 40,
    _COMMA_         = 41,
    _SEMI_          = 42,
    _EXCL_          = 43,
    _LPAREN_        = 46,
    _RPAREN_        = 47,
    _LBRACKET_      = 49,
    _RBRACKET_      = 50,
    _HOURGLASS_     = 53,
    _BALLOON_       = 54,
    _RIGHT_ARROW_   = 55
;

const ivec2 CHAR_SIZE           = ivec2(8);
int g_text_scale_shift            = 0;

ivec2 raw_text_uv(vec2 fragCoord)     { return ivec2(floor(fragCoord)); }
ivec2 text_uv(vec2 fragCoord)       { return ivec2(floor(fragCoord)) >> g_text_scale_shift; }
int text_width(int num_chars)       { return num_chars << 3; }
int line_index(int pixels_y)        { return pixels_y >> 3; }
int glyph_index(int pixels_x)       { return pixels_x >> 3; }
int cluster_index(int pixels_x)       { return pixels_x >> 5; }
int get_byte(int index, int packed)     { return int((uint(packed) >> (index<<3)) & 255u); }

void init_text_scale()
{
  g_text_scale_shift = int(max(floor(log2(iResolution.x)-log2(799.)), 0.));
}

vec2 align(int num_chars, vec2 point, vec2 alignment)
{
    return point + alignment*-vec2(num_chars<<(3+g_text_scale_shift), 8<<g_text_scale_shift);
}

vec4 glyph_color(uint glyph, ivec2 pixel)
{
    uint x = glyph & 7u,
         y = glyph >> 3u;
    pixel = ivec2(ADDR2_RANGE_FONT.xy) + (ivec2(x, y) << 3) + (pixel & 7);
    return texelFetch(LIGHTMAP_CHANNEL, pixel, 0);
}

void print_glyph(inout vec4 fragColor, ivec2 pixel, int glyph, vec4 color)
{
    color *= glyph_color(uint(glyph), pixel);
    fragColor.rgb = mix(fragColor.rgb, color.rgb, color.a);
}

const int MAX_POW10_EXPONENT = 7;

uint pow10(uint e)
{
    uint result = (e & 1u) != 0u ? 10u : 1u;
    if ((e & 2u) != 0u) result *= 100u;
    if ((e & 4u) != 0u) result *= 10000u;
    return result;
}

int int_glyph(int number, int index)
{
    if (uint(index) >= uint(MAX_POW10_EXPONENT))
        return _SPACE_;
    if (number <= 0)
        return index == 0 ? _0_ : _SPACE_;
    uint power = pow10(uint(index));
    return uint(number) >= power ? _0_ + int((uint(number)/power) % 10u) : _SPACE_;
}

// Perf overlay ////////////////////////////////////////////////

vec3 fps_color(float fps)
{
    return
        fps >= 250. ? vec3(.75, .75,  1.) :
        fps >= 144. ? vec3( 1., .75,  1.) :
        fps >= 120. ? vec3( 1.,  1.,  1.) :
      fps >= 60.  ? vec3( .5,  1.,  .5) :
      fps >= 30.  ? vec3( 1.,  1.,  0.) :
                    vec3( 1.,  0.,  0.);
}

float shadow_box(vec2 fragCoord, vec4 box, float border)
{
    vec2 clamped = clamp(fragCoord, box.xy, box.xy + box.zw);
    return clamp(1.25 - length(fragCoord-clamped)*(1./border), 0., 1.);
}

void draw_shadow_box(inout vec4 fragColor, vec2 fragCoord, vec4 box, float border)
{
    fragColor.rgb *= mix(1.-shadow_box(fragCoord, box, border), 1., .5);
}

const float DEFAULT_SHADOW_BOX_BORDER = 8.;

void draw_shadow_box(inout vec4 fragColor, vec2 fragCoord, vec4 box)
{
    draw_shadow_box(fragColor, fragCoord, box, DEFAULT_SHADOW_BOX_BORDER);
}

void draw_perf(inout vec4 fragColor, vec2 fragCoord)
{
    Options options;
    LOAD(options);

    if (uint(g_demo_stage - DEMO_STAGE_FPS) < 2u)
        options.flags |= OPTION_FLAG_SHOW_FPS;
    
    if (!test_flag(options.flags, OPTION_FLAG_SHOW_FPS|OPTION_FLAG_SHOW_FPS_GRAPH))
        return;

    float margin = 16. * min(iResolution.x * (1./400.), 1.);
    vec2 anchor = iResolution.xy  - margin;
    
    if (test_flag(options.flags, OPTION_FLAG_SHOW_FPS_GRAPH))
    {
        const vec2 SIZE = vec2(ADDR_RANGE_PERF_HISTORY.z, 32.);
        vec4 box = vec4(anchor - SIZE, SIZE);
        draw_shadow_box(fragColor, fragCoord, box);

        if (is_inside(fragCoord, box) > 0.)
        {
            vec2 address = ADDR_RANGE_PERF_HISTORY.xy + vec2(ADDR_RANGE_PERF_HISTORY.z-(fragCoord.x-box.x),0.);
            vec4 perf_sample = load(address);
            if (perf_sample.x > 0.)
            {
                float sample_fps = 1000.0/perf_sample.x;
                float fraction = sample_fps * (1./float(FPS_GRAPH_MAX));
                //fraction = 1./sqr(perf_sample.y/MIN_DOWNSCALE);
                if ((fragCoord.y-box.y) / box.w <= fraction)
                    fragColor.rgb = fps_color(sample_fps);
            }
            return;
        }
        
        anchor.y -= SIZE.y + DEFAULT_SHADOW_BOX_BORDER * 2.;
    }

    int fps = int(round(iFrameRate));
    if (test_flag(options.flags, OPTION_FLAG_SHOW_FPS) && uint(fps - 1) < 9999u)
    {
        const int FPS_TEXT_LENGTH = 8; // 1234 FPS
        const int FPS_SUFFIX_GLYPHS = (_SPACE_<<24) | (_F_<<16) | (_P_<<8) | (_S_<<0);
    
      vec2 text_pos = anchor - vec2((CHAR_SIZE << g_text_scale_shift) * ivec2(FPS_TEXT_LENGTH,1));
    
        ivec2 uv = text_uv(fragCoord - text_pos);
        if (line_index(uv.y) == 0)
        {
            int glyph = FPS_TEXT_LENGTH - 1 - glyph_index(uv.x);
            if (uint(glyph) < 4u)
                glyph = get_byte(glyph, FPS_SUFFIX_GLYPHS);
            else if (uint(glyph) < uint(FPS_TEXT_LENGTH))
                glyph = int_glyph(fps, glyph-4);
            else
                glyph = _SPACE_;

      if (glyph != _SPACE_)
            {
                vec4 color = vec4(vec3(.875), 1.);
                print_glyph(fragColor, uv, glyph, color);
            }
        }
    }
}

// Console state ///////////////////////////////////////////////

struct Console
{
    float loaded;
    float expanded;
    float typing;
};
    
Console g_console;

void update_console()
{
    const float
        T0 = 0.,
      T1 = T0 + CONSOLE_XFADE_DURATION,
      T2 = T1 + CONSOLE_SLIDE_DURATION,
      T3 = T2 + CONSOLE_TYPE_DURATION,
      T4 = T3 + CONSOLE_SLIDE_DURATION;
    
    // snap console position to multiples of 2 pixels to avoid shimmering
    // due to the use of noise and dFd* functions
    float ysnap = iResolution.y * .5;
    
    g_console.loaded = linear_step(T0, T1, g_time);
    g_console.expanded = 1.+-.5*(linear_step(T1, T2, g_time) + linear_step(T3, T4, g_time));
    g_console.expanded = floor(g_console.expanded * ysnap + .5) / ysnap;
    g_console.typing = linear_step(0., CONSOLE_TYPE_DURATION, g_time - T2);
}

// Console text ////////////////////////////////////////////////

WRAP(GCONSOLE_TEXT,CONSOLE_TEXT,int,145)(33,0,9,16,16,28,28,29,29,60,60,60,80,98,117,133,151,179,179,203,203,204,204,230
,250,269,289,289,305,328,355,373,398,423,439,0xc030f0e,0xf001009,0xf0e320e,269028355,303304201,51708943,0xe0f0914,
302323238,0xe0f0913,455613440,85131297,302323218,522006016,2302498,788730371,50665477,462345,0xf141501,50665477,
0x7060328,50665477,462345,0x60e0f03,52954889,402982662,0xe090305,84148231,0xc150106,0x6032814,85460231,0x70e0903,
18157824,304612619,420416003,50926611,0xf0e000b,0xf060014,0x9040e15,85078030,0xf0a0401,0x9141319,2755331,301993742,
0xf101305,470094606,336921126,921364,320147213,369164293,17565953,637864962,0xe150f13,18022404,0x90c100d,301991694,
704975873,454827008,85336093,0xf091312,2031630,318767635,336724244,320147477,462345,68868,0xe010803,787726,353309470,
0x900040e,0x914090e,436800513,0xf091401,403968526,336530944,335873024,85197573,1970948,0xf030e09,302977042,67441665,
303300648,522260233,18023702,455613696,0xd0f0300,17370128,16782350,336593156,472519173,2369566,17237261,85203202,
17106944,85460240,488308778,707010602,318775067,503320581,605814811,85139748,0xc010912,0x9120400,1180950,336137737,
0x90c0109,0x904051a,0xe001810,67113999,50664453,263444));

vec2 closest_point_on_segment(vec2 p, vec2 a, vec2 b)
{
    vec2 ab = b-a;
    vec2 ap = p-a;
    float t = clamp(dot(ap, ab)/dot(ab, ab), 0., 1.);
    return ab*t + a;
}

vec2 lit_line(vec2 uv, vec2 a, vec2 b, float thickness)
{
    const vec2 LIGHT_DIR = vec2(0, 1);
    uv -= closest_point_on_segment(uv, a, b);
    float len = length(uv);
    return vec2(len > 0. ? dot(uv/len, LIGHT_DIR) : 1., len + -.5*thickness);
}

void print_console_version(inout vec4 fragColor, vec2 uv, vec2 mins, vec2 size)
{
    size.y *= .25;
    mins.y -= size.y * 1.75;
    
    if (is_inside(uv, vec4(mins, size)) < 0.)
        return;
    uv -= mins;
    uv *= 1./size;
    
    ivec2 iuv = ivec2(vec2(CHAR_SIZE) * vec2(4, 1) * uv);
    
    int glyph = glyph_index(iuv.x);
    if (uint(glyph) >= 4u)
        return;
    
    const int GLYPHS = (_1_) | (_DOT_<<8) | (_0_<<16) | (_6_<<24);
    const vec4 color = vec4(.62, .30, .19, 1);
    
    fragColor.rgb *= .625;
    glyph = get_byte(glyph, GLYPHS);
    print_glyph(fragColor, iuv, glyph, color);
}

void print_console_text(inout vec4 fragColor, vec2 fragCoord)
{
    float MARGIN = 12. * iResolution.x/800.;
    const vec4 COLORS[2] = vec4[2](vec4(vec3(.54), 1), vec4(.62, .30, .19, 1));
    const uint COLORED = (1u<<3) | (1u<<7);
    const int TYPING_LINE = 1;
    
    fragCoord.y -= iResolution.y * (1. - g_console.expanded);
    ivec2 uv = text_uv(fragCoord - MARGIN);
    bool typing = g_console.typing < 1.;
    int cut = int(mix(float(CONSOLE_TEXT.data[0]-1), 2., g_console.loaded));
    if (g_console.typing > 0.)
        --cut;
    
    int line = line_index(uv.y);
    if (uint(line) >= uint(CONSOLE_TEXT.data[0]-cut))
        return;
    line += cut;
    int start = CONSOLE_TEXT.data[1+line];
    int num_chars = CONSOLE_TEXT.data[2+line] - start;
    
    if (num_chars == 1)
    {
        const vec3 LINE_COLOR = vec3(.17, .13, .06);
        float LINE_END = min(iResolution.x - MARGIN*2., 300.);
        vec2 line = lit_line(vec2(uv.x, uv.y & 7) + .5, vec2(4. ,4.), vec2(LINE_END-4., 4.), 4.);
        line.x = mix(1. + .5 * line.x, 1., linear_step(-.5, -1.5, line.y));
        line.x *= 1. + -.25*random(vec2(uv));
    fragColor.rgb = mix(fragColor.rgb, LINE_COLOR * line.x, step(line.y, 0.));
        return;
    }
    
    int glyph = glyph_index(uv.x);
    if (line == TYPING_LINE)
    {
        float type_fraction = clamp(2. - abs(g_console.typing * 4. + -2.), 0., 1.);
        num_chars = clamp(int(float(num_chars-1)*type_fraction) + int(typing), 0, num_chars + int(typing));
    }
    if (uint(glyph) >= uint(num_chars))
        return;

    if (typing && line == TYPING_LINE && glyph == num_chars - 1)
    {
        glyph = fract(iTime*2.) < .5 ? _CARET_ : _SPACE_;
    }
    else
    {
        glyph += start;
        glyph = get_byte(glyph & 3, CONSOLE_TEXT.data[CONSOLE_TEXT.data[0] + 2 + (glyph>>2)]);
    }
    
    uint is_colored = line < 32 ? ((COLORED >> line) & 1u) : 0u;
    vec4 color = COLORS[is_colored];
    print_glyph(fragColor, uv, glyph, color);
}

// Menu ////////////////////////////////////////////////////////

WRAP(GOPTIONS,OPTIONS,int,53)(13,0,11,23,33,44,58,72,86,97,109,119,132,143,149,320147213,269680645,0x9040505,302323214,0xf0d0014
,33886997,0x8070912,319098388,302191379,918789,85592339,386861075,17958400,17958157,0x8130514,0x600170f,0x7001310,0x8100112,
337118484,332309,336333062,0xf0d1205,0xe0f0914,353108480,0x7090c12,318772232,335937800,336724755,0x6060500,320078597,1511176,
0x807090c,268504340,386861075,17110784,0xe0e0f10,0x90c030f,16));

void draw_menu(inout vec4 fragColor, vec2 fragCoord, Timing timing)
{
    MenuState menu;
    LOAD(menu);

    if (menu.open <= 0)
        return;

    vec4 options = load(ADDR_OPTIONS);

    if (!test_flag(int(options[get_option_field(OPTION_DEF_SHOW_LIGHTMAP)]), OPTION_FLAG_SHOW_LIGHTMAP))
    {
        // vanilla
        fragColor.rgb *= vec3(.57, .47, .23);
        fragColor.rgb = ceil(fragColor.rgb * 24. + .01) / 24.;
    }
    else
    {
        // GLQuake
        fragColor.rgb *= .2;
    }

    //g_text_scale_shift = 1;
    int text_scale = 1 << g_text_scale_shift;
    float image_scale = float(text_scale);
    vec2 header_size = ADDR2_RANGE_TEX_OPTIONS.zw * image_scale;
    vec2 left_image_size = ADDR2_RANGE_TEX_QUAKE.wz * image_scale;
    float left_image_offset = 120. * image_scale;

    vec2 ref = iResolution.xy * vec2(.5, 1.);
    ref.y -= min(float(CHAR_SIZE.y) * 4. * image_scale, iResolution.y / 16.);

    ref.x += left_image_size.x * .5;
    if (fragCoord.x < ref.x - left_image_offset)
    {
        fragCoord.y -= ref.y - left_image_size.y;
        fragCoord.x -= ref.x - left_image_offset - left_image_size.x;
        ivec2 addr = ivec2(floor(fragCoord)) >> g_text_scale_shift;
        if (uint(addr.x) < uint(ADDR2_RANGE_TEX_QUAKE.w) && uint(addr.y) < uint(ADDR2_RANGE_TEX_QUAKE.z))
          fragColor.rgb = texelFetch(LIGHTMAP_CHANNEL, addr.yx + ivec2(ADDR2_RANGE_TEX_QUAKE.xy), 0).rgb;
        return;
    }

    ref.y -= header_size.y;
    if (fragCoord.y >= ref.y)
    {
        fragCoord.y -= ref.y;
        fragCoord.x -= ref.x - header_size.x * .5;
        ivec2 addr = ivec2(floor(fragCoord)) >> g_text_scale_shift;
        if (uint(addr.x) < uint(ADDR2_RANGE_TEX_OPTIONS.z) && uint(addr.y) < uint(ADDR2_RANGE_TEX_OPTIONS.w))
          fragColor.rgb = texelFetch(LIGHTMAP_CHANNEL, addr + ivec2(ADDR2_RANGE_TEX_OPTIONS.xy), 0).rgb;
        return;
    }

    ref.y -= float(CHAR_SIZE.y) * 1. * image_scale;

    const int
        BASE_OFFSET   = CHAR_SIZE.x * 0,
        ARROW_OFFSET  = CHAR_SIZE.x,
        VALUE_OFFSET  = CHAR_SIZE.x * 3,
        MARGIN      = 0,
        LINE_HEIGHT   = MARGIN + CHAR_SIZE.y;

    ivec2 uv = text_uv(fragCoord - ref);
    uv.x -= BASE_OFFSET;
    int line = -uv.y / LINE_HEIGHT;
    if (uint(line) >= uint(NUM_OPTIONS))
        return;
    
    uv.y = uv.y + (line + 1) * LINE_HEIGHT;
    if (uint(uv.y - MARGIN) >= uint(CHAR_SIZE.y))
        return;
    uv.y -= MARGIN;
    
    int glyph = 0;
    if (uv.x < 0)
    {
        int begin = OPTIONS.data[1 + line];
        int end = OPTIONS.data[2 + line];
        int num_chars = end - begin;
        uv.x += num_chars * CHAR_SIZE.x;
      glyph = glyph_index(uv.x);
        if (uint(glyph) >= uint(num_chars))
            return;
        glyph += begin;
        glyph = get_byte(glyph & 3, OPTIONS.data[OPTIONS.data[0] + 2 + (glyph>>2)]);
    }
    else if (uint(uv.x - ARROW_OFFSET) < uint(CHAR_SIZE.x))
    {
        const float BLINK_SPEED = 2.;
        uv.x -= ARROW_OFFSET;
        if (menu.selected == line && (fract(iTime * BLINK_SPEED) < .5 || test_flag(timing.flags, TIMING_FLAG_PAUSED)))
            glyph = _RIGHT_ARROW_;
    }
    else if (uv.x >= VALUE_OFFSET)
    {
        uv.x -= VALUE_OFFSET;

        int item_height = CHAR_SIZE.y << g_text_scale_shift;

        MenuOption option = get_option(line);
        int option_type = get_option_type(option);
        int option_field = get_option_field(option);
        if (option_type == OPTION_TYPE_SLIDER)
        {
            const float RAIL_HEIGHT = 7.;
            vec2 p = vec2(uv.x, uv.y & 7) + .5;
            vec2 line = lit_line(p, vec2(8, 4), vec2(8 + 11*CHAR_SIZE.x, 4), RAIL_HEIGHT);
            float alpha = linear_step(-.5, .5, -line.y);
            line.y /= RAIL_HEIGHT;
            float intensity = 1. + line.x * step(-.25, line.y);
            intensity = mix(intensity, 1. - line.x * .5, line.y < -.375);
            fragColor.rgb = mix(fragColor.rgb, vec3(.25, .23, .19) * intensity, alpha);

            float value = options[option_field] * .1;
            float thumb_pos = 8. + value * float(CHAR_SIZE.x * 10);
            p.x -= thumb_pos;
            p -= vec2(4);
            float r = length(p);
            alpha = linear_step(.5, -.5, r - 4.);
            intensity = normalize(p).y * .25 + .75;
            p *= vec2(3., 1.5);
            r = length(p);
            intensity += linear_step(.5, -.5, r - 4.) * (safe_normalize(p).y * .125 + .875);

            fragColor.rgb = mix(fragColor.rgb, vec3(.36, .25, .16) * intensity, alpha);
            return;
        }
        else if (option_type == OPTION_TYPE_TOGGLE)
        {
            glyph = glyph_index(uv.x);
            if (uint(glyph) >= 4u)
                return;
        const int
                OFF = (_O_<<8) | (_F_<<16) | (_F_<<24),
          ON  = (_O_<<8) | (_N_<<16);
            int value = test_flag(int(options[option_field]), get_option_range(option)) ? ON : OFF;
            glyph = get_byte(glyph & 3, value);
        }
    }
    else
    {
        return;
    }
    
    vec4 color = vec4(.66, .36, .25, 1);
    print_glyph(fragColor, uv, glyph, color);
}

// Loading screen/console //////////////////////////////////////

vec3 loading_spinner(vec2 fragCoord)
{
    float radius = max(32./1920. * iResolution.x, 8.);
    float margin = max(96./1920. * iResolution.x, 12.);
    vec2 center = iResolution.xy - vec2(margin + radius);
    float angle = atan(fragCoord.y-center.y, fragCoord.x-center.x) / (PI*2.);
    float dist = length(fragCoord - center)/radius;
    angle += smoothen(fract(iTime*SQRT2));
    angle = fract(-angle);

    const float MAX_ANGLE = .98;
    const float MIN_ANGLE = .12;
    float angle_alpha = angle < MAX_ANGLE ? max((angle-MIN_ANGLE)/(MAX_ANGLE-MIN_ANGLE), 0.) :
      1.-(angle-MAX_ANGLE)/(1.-MAX_ANGLE);
    float radius_alpha = around(.85, mix(.09, .1, angle_alpha), dist);

    vec3 color = sqr(1.-clamp(dist*.375, 0., 1.)) * vec3(.25,.125,0.);
    color += sqrt(radius_alpha) * angle_alpha;
    
    return color;
}

vec3 burn_xfade(vec3 from, vec3 to, float noise_mask, float fraction)
{
    const float HEADROOM = .7;
    fraction = mix(-HEADROOM, 1.+HEADROOM, fraction);
    float burn_mask = linear_step(fraction-HEADROOM, fraction, noise_mask);
    from *= burn_mask;
    to = mix(from, to, linear_step(fraction, fraction-HEADROOM, noise_mask));
    
    const bool GARISH_FLAMES = false;
    if (GARISH_FLAMES)
    {
        to *= 1. - around(.5, .49, burn_mask);
        to += vec3(1.,.3,.2) * around(.80, .19, burn_mask);
        to += vec3(1.,.5,.3) * around(.84, .15, burn_mask) * .25;
        to += vec3(1.,1.,.4) * around(.94, .05, burn_mask);
    }

    return to;
}

float sdf_apply_light(float sdf, vec2 dir)
{
    vec2 grad = normalize(vec2(dFdx(sdf), dFdy(sdf)));
    return dot(dir, grad);
}

float sdf_shadow(float sdf, float size, vec2 light_dir)
{
    vec2 n = sdf_normal(sdf);
    float thresh = size * max(abs(dFdx(sdf)), abs(dFdy(sdf)));
    float mask = clamp(sdf/thresh, 0., 1.);
    return clamp(1. - sdf/size, 0., 1.) * clamp(-dot(light_dir, n), 0., 1.) * mask;
}

float sdf_modern_nail(vec2 uv, vec2 top, vec2 size)
{
    const float head_flat_frac = .025;
    const float head_round_frac = .05;
    const float body_thickness = .5;

    float h = clamp((top.y - uv.y) / size.y, 0., 1.);
    float w = (h < head_flat_frac) ? 1. :
        (h < head_flat_frac + head_round_frac) ? mix( body_thickness, 1., sqr(1.-(h-head_flat_frac)/head_round_frac)) :
      h > .6 ? ((1.05 - h) / (1.05 - .6)) * body_thickness : body_thickness;
    return sdf_centered_box(uv, top - vec2(0., size.y*.5), size*vec2(w, .5));
}

float sdf_modern_Q(vec2 uv, float age)
{
    float aspect_ratio = iResolution.x/iResolution.y;
    float noise = turb(uv * vec2(31.7,27.9)/aspect_ratio, .7, 1.83);
    float dist = sdf_disk(uv, vec2(.5, .68), .315);
    dist = sdf_exclude(dist, sdf_disk(uv, vec2(.5, .727), .267));
    dist = sdf_exclude(dist, sdf_disk(uv, vec2(.5, 1.1), .21));
    dist = sdf_union(dist, sdf_modern_nail(uv, vec2(.5, .59), vec2(.08, .52)));
    return dist + (noise * .01 - .005) * sqr(age);
}

vec2 embossed_modern_Q(vec2 uv, float age, float bevel, vec2 light_dir)
{
    float px = 2./iResolution.y, EPS = .1 * px;
    vec3 sdf;
    for (int i=0; i<3; ++i)
    {
        vec2 uv2 = uv;
        if (i != 2)
            uv2[i] += EPS;
        sdf[i] = sdf_modern_Q(uv2, age);
    }
    vec2 gradient = normalize(sdf.xy - sdf.z);
    float mask = sdf_mask(sdf.z, px);
    bevel = clamp(1. + sdf.z/bevel, 0., 1.);
    return vec2(mask * (.5 + sqrt(bevel) * dot(gradient, light_dir)), mask);
}

void print_console_logo(inout vec4 fragColor, vec2 uv, vec2 mins, vec2 size, float noise)
{
    float inside = is_inside(uv, vec4(mins, size));
    float fade = noise * -.01;
    if (inside < fade)
        return;
    vec3 background = mix(vec3(.09, .05, 0), vec3(.38, .17, .11), sqr(smoothen(noise)));
    fragColor.rgb = mix(fragColor.rgb, background, sqr(linear_step(fade, .001, inside)));
    const float QUANTIZE = 32.;
    uv = (uv - mins) / size;
    uv = round(uv * QUANTIZE) * (1./QUANTIZE);
    float logo = (sdf_id(uv) + noise*.015) * QUANTIZE;
    float mask = clamp(2.-logo, 0., 1.) * linear_step(1., .25, noise);
    fragColor.rgb = mix(fragColor.rgb, vec3(0), mask * .5);
    mask = clamp(1.-logo, 0., 1.) * linear_step(1., .25, noise);
    fragColor.rgb = mix(fragColor.rgb, vec3(.43, .22, .14), mask);
}

float sparks(vec2 uv, vec2 size)
{
    vec2 cell = floor(uv) + .5;
    vec2 variation = hash2(cell);
    cell += (variation-.5) * .9;
    return sqr(variation.x) * clamp(1.-length((cell - uv)*(1./size)), 0., 1.);
}

void draw_console(inout vec4 fragColor, vec2 fragCoord, Lighting lighting)
{
    fragColor.rgb *= linear_step(1., .5, g_console.expanded);

    vec2 uv = fragCoord.xy / iResolution.xy;
  if (uv.y < 1. - g_console.expanded)
        return;
    
    float loaded = lighting.progress;
    float xfade = clamp(g_time / CONSOLE_XFADE_DURATION, 0., 1.);
    
    uv.y -= 1. - g_console.expanded;
    float vignette = 1. - clamp(length(uv - .5)*2., 0., 1.);
  
    float aspect_ratio = iResolution.x/iResolution.y;
    uv.x = (uv.x - 0.5) * aspect_ratio + 0.5;

    float base = turb(uv * vec2(31.7,27.9)/aspect_ratio, .7, 2.5);
    
    // loading screen (modern style) //
    
    vec3 modern = vec3(linear_step(.45, .7, base) * 0.1);
    if (xfade < 1.)
    {
        modern *= sqr(vignette);

        const float MODERN_LOGO_SCALE = .75;

        vec2 logo_uv = (uv - .5) * (1./MODERN_LOGO_SCALE) + .5;
        vec4 modern_logo = embossed_modern_Q(logo_uv, loaded, .006, vec2(.7, .3)).xxxy;

        float flame_flicker = mix(.875, 1., smooth_noise(2.+iTime*7.3));
        float scratches = linear_step(.35, .6, turb(vec2(480.,8.)*rotate(uv, 22.5), .5, 2.) * base);
        scratches += linear_step(.25, .9, turb(vec2(480.,16.)*rotate(uv, -22.5), .5, 2.) * base);

        modern_logo.rgb *= vec3(.32,.24,.24);
        modern_logo.rgb *= smoothstep(.75, .0, abs(uv.x-.5));
        modern_logo.rgb *= 1.8 - 0.8 * linear_step(.55, mix(.15, .35, loaded), base);
        modern_logo.rgb *= 1. - scratches * .4;
        modern_logo.rgb *= 1. + 4. * sqr(clamp(1. - length(uv - vec2(.76, .37))*2.3, 0., 1.));
        modern_logo.rgb *= 1. + flame_flicker*2.5*vec3(1.,0.,0.) * sqr(clamp(1. - length(uv - vec2(.20, .40))*3., 0., 1.));

        modern = mix(modern, modern_logo.rgb, modern_logo.a);

        float flame_vignette = length((uv - vec2(.5,0.))*vec2(.5, 1.3));
        float flame_intensity = flame_vignette;
        flame_intensity = sqr(sqr(clamp(1.-flame_intensity, 0., 1.)) * flame_flicker);
        flame_intensity *=
            turb(uv * vec2(41.3,13.6)/aspect_ratio + vec2(0.,-iTime), .5, 2.5) +
            turb(uv * vec2(11.3,7.6)/aspect_ratio + vec2(0.,-iTime*.9), .5, 2.5);
        modern += vec3(.25,.125,0.) * flame_intensity;

        vec2 spark_uv = vec2(uv + vec2(turb(uv*1.3, .5, 2.)*.6, -iTime*.53));
        float spark_intensity =
            sparks(vec2(11.51, 3.13) * spark_uv,        vec2(.06,.05)) * 2. +
            sparks(vec2(4.19, 1.37) * spark_uv + vec2(1.3,3.7), vec2(.06,.05)) * 1.;
        spark_intensity *= flame_intensity;

        spark_uv = vec2(uv*.73 + vec2(turb(uv*1.25, .7, 1.9)*.4, -iTime*.31));
        float spark_intensity2 = turb(vec2(25.1, 11.5) * spark_uv, .5, 2.);
        spark_intensity2 = 0.*linear_step(.43, .95, spark_intensity2) * flame_intensity*.2;
        modern += vec3(1.,1.,.3) * (spark_intensity + spark_intensity2);

        modern += loading_spinner(fragCoord);
    }
    
    // console (classic style) //

    const float CLASSIC_LOGO_SCALE = 1.1;
    const vec2 CLASSIC_LOGO_CENTER = vec2(.5, .45);
    const vec2 CLASSIC_LIGHT_DIR = vec2(0, 1.5);
    float classic_shadow_size = mix(.01, .05, base);
    vec2 CLASSIC_SHADOW_OFFSET = CLASSIC_LIGHT_DIR * classic_shadow_size;
    float classic_logo_distortion = base * .015 - .01;
    float classic_logo = sdf_Q((uv-CLASSIC_LOGO_CENTER) / CLASSIC_LOGO_SCALE + .5) + classic_logo_distortion;
    
    vec2 aspect = vec2(iResolution.x / iResolution.y, 1.);
    vec2 box_size = vec2(.5) * aspect - mix(.005, .03, sqr(base));
    float classic_console_box = sdf_centered_box(uv, vec2(.5), box_size);
    
    const vec2 CLASSIC_ID_LOGO_MARGIN = vec2(24./450., 48./450.);
    const float CLASSIC_ID_LOGO_SIZE = 64./450.;
    const float CLASSIC_ID_LOGO_BOX_JAGGEDNESS = 0.; //0.02;
    
    vec2 logo_mins = vec2((.5+.5*aspect.x)-CLASSIC_ID_LOGO_MARGIN.x-CLASSIC_ID_LOGO_SIZE, CLASSIC_ID_LOGO_MARGIN.y);
    
    classic_console_box = sdf_exclude(classic_console_box,
                                      4.*sdf_box(uv, logo_mins, logo_mins + CLASSIC_ID_LOGO_SIZE) +
                                      (base*2.-1.) * CLASSIC_ID_LOGO_BOX_JAGGEDNESS);
   
    float noise2 = turb(uv*43.7, .5, 2.0)-.15;
    classic_console_box = sdf_exclude(classic_console_box, noise2*.1);
    
    float bevel_size = mix(.001, .07, sqr(base));
    float classic_sdf = sdf_exclude(classic_console_box, classic_logo+.01);
    float classic_base = sdf_emboss(classic_sdf, bevel_size, CLASSIC_LIGHT_DIR).x;

#if 1
    // slightly odd, gradient-based automatic shadow
    float classic_shadow = sdf_shadow(classic_sdf, classic_shadow_size, CLASSIC_LIGHT_DIR);
#else
    // smooth version with secondary SDF sample
    // only sampling the Q logo SDF, not the composite one!
    float classic_shadow_sample = sdf_Q((uv+CLASSIC_SHADOW_OFFSET-CLASSIC_LOGO_CENTER) / CLASSIC_LOGO_SCALE + .5) + classic_logo_distortion;
    float classic_shadow = sdf_mask(classic_logo) * clamp(classic_shadow_sample/classic_shadow_size+.3, 0., 1.);
#endif

    vec4 classic = vec4(mix(vec3(.07,.03,.02)*(1.+base*2.)*(1.-classic_shadow), vec3(.24,.12,.06), classic_base), 1.);
    classic.rgb *= 1. - .05*linear_step(.35, .3, base);
    classic.rgb *= 1. + .05*linear_step(.6, .65, base);
    
    print_console_logo(classic, uv, logo_mins, vec2(CLASSIC_ID_LOGO_SIZE), base);
  print_console_version(classic, uv, logo_mins, vec2(CLASSIC_ID_LOGO_SIZE));
    print_console_text(classic, fragCoord);
    
    classic.rgb = floor(classic.rgb * 64. + random(floor(uv*128.))) * (1./64.);

  float burn_fraction = xfade * (2.-clamp(length(uv-vec2(.5,0.)), 0., 1.));
    fragColor.rgb = burn_xfade(modern, classic.rgb, base, burn_fraction);
}

// Skill selection message /////////////////////////////////////

WRAP(GSKILL_MESSAGES,SKILL_MESSAGES,int,27)(3,0,28,58,86,319358996,0xc010800,85131276,335742220,17104915,318773523,
0xc0c090b,319358996,0xc010800,85131276,335742220,0xf0e0013,0xc010d12,0x90b1300,0x8140c0c,0x8001309,789505,84673811,
1250307,68288776,0x90b1300,3084));

void print_skill_message(inout vec4 fragColor, in vec2 fragCoord, vec3 cam_pos)
{
#if PRINT_SKILL_MESSAGE
    float time = fract(iTime*.5);
    if (time > .9375)
        return;

    MenuState menu;
    LOAD_PREV(menu);
    if (menu.open > 0)
        return;
    
    ivec2 uv = text_uv(fragCoord - iResolution.xy*vec2(.5,.64));
    if (line_index(uv.y) != 0)
        return;
    
  vec4 cam_angles = load(ADDR_CAM_ANGLES);
    if (min(cam_angles.x, 360.-cam_angles.x) >= 90.)
        return;

    const vec3 PLAYER_DIMS = vec3(16., 16., 48.);
    const vec3 SKILL_TRIGGER_BOUNDS[] = vec3[](
        vec3(112,832,-32),vec3(336,1216,16),
        vec3(448,832,-8),vec3(656,1232,40),
        vec3(752,800,-24),vec3(976,1248,24)
  );
    
    int line = -1;
    int num_skills = NO_UNROLL(3);
    for (int i=0; i<num_skills; ++i)
    {
        int i2 = i + i;
        vec3 mins = SKILL_TRIGGER_BOUNDS[i2];
        vec3 maxs = SKILL_TRIGGER_BOUNDS[i2+1];
        vec3 delta = clamp(cam_pos.xyz, mins, maxs) - cam_pos.xyz;
        if (max_component(abs(delta) - PLAYER_DIMS) <= 0.)
            line = i;
    }
    
    if (line == -1)
        return;

    int start = SKILL_MESSAGES.data[1+line];
    int num_chars = SKILL_MESSAGES.data[2+line] - start;
    uv.x += text_width(num_chars) >> 1;
    
    int glyph = glyph_index(uv.x);
    if (uint(glyph) >= uint(num_chars))
        return;
    
    glyph += start;
    glyph = get_byte(glyph & 3, SKILL_MESSAGES.data[SKILL_MESSAGES.data[0] + 2 + (glyph>>2)]);

    vec4 color = vec4(vec3(.6), 1);
    print_glyph(fragColor, uv, glyph, color);
#endif // PRINT_SKILL_MESSAGE
}

// Level start countdown ///////////////////////////////////////

WRAP(GGAME_OVER,GAME_OVER,int,4)(9,84738311,85331712,18));

bool print_countdown(inout vec4 fragColor, vec2 fragCoord)
{
    GameState game_state;
    MenuState menu;
    LOAD(game_state);
    LOAD(menu);
    if (game_state.level <= 0. && game_state.level == floor(game_state.level) || menu.open > 0)
        return false;
    
    float remaining = fract(abs(game_state.level)) * 10.;
    if (remaining <= 0. || remaining >= BALLOON_SCALEIN_TIME + LEVEL_COUNTDOWN_TIME)
        return true;
    
    ivec2 uv = text_uv(fragCoord - iResolution.xy*vec2(.5,.66)) >> 1;
    if (line_index(uv.y) != 0)
        return true;
    
    bool go = remaining < BALLOON_SCALEIN_TIME;
    
    int num_chars = (game_state.level < 0.) ? GAME_OVER.data[0] : go ? 4 : 1;
    
    uv.x += (num_chars * CHAR_SIZE.x) >> 1;

    int glyph = glyph_index(uv.x);
    if (uint(glyph) >= uint(num_chars))
        return true;
    
    const int GO_MESSAGE = (_SPACE_<<0) | (_G_<<8) | (_O_<<16) | (_SPACE_<<24);
    
    if (game_state.level < 0.)
      glyph = get_byte(glyph & 3, GAME_OVER.data[1 + (glyph>>2)]);
    else if (go)
        glyph = (GO_MESSAGE >> (glyph<<3)) & 255;
    else
      glyph = _0_ + int(ceil(remaining - BALLOON_SCALEIN_TIME));
    
    vec4 color = vec4(vec3(.875), 1.);
    if (fract(remaining - BALLOON_SCALEIN_TIME) > .875)
        color.rgb = vec3(.60, .30, .23);
    print_glyph(fragColor, uv, glyph, color);
    
    return true;
}

// Pain blend, skill message ///////////////////////////////////

void add_effects(inout vec4 fragColor, vec2 fragCoord, bool is_thumbnail)
{
    if (is_demo_mode_enabled(is_thumbnail))
        return;

    vec4 cam_pos = load(ADDR_CAM_POS);
    vec3 fireball = get_fireball_offset(g_animTime) + FIREBALL_ORIGIN;
    float pain = linear_step(80., 16., length(cam_pos.xyz - fireball));
    vec3 lava_delta = abs(cam_pos.xyz - clamp(cam_pos.xyz, LAVA_BOUNDS[0], LAVA_BOUNDS[1]));
    float lava_dist = max3(lava_delta.x, lava_delta.y, lava_delta.z);
    if (lava_dist <= 32.)
        pain = mix(.5, .75, clamp(fract(g_animTime*4.)*2.+-1., 0., 1.));
    if (lava_dist <= 0.)
        pain += .45;
    fragColor.rgb = mix(fragColor.rgb, vec3(1., .125, .0), sqr(clamp(pain, 0., 1.)) * .75);
    
    if (!print_countdown(fragColor, fragCoord))
      print_skill_message(fragColor, fragCoord, cam_pos.xyz);
}

// Demo stage descriptions /////////////////////////////////////

WRAP(GDEMO_STAGES,DEMO_STAGES,int,24)(6,0,12,19,29,37,45,64,1638674,336791812,84086273,0xd120f0e,353569793,17638934,
0xe091010,402985991,85071124,0x7090c13,0xe091408,402985991,85071124,2424851,0x807090c,0x70e0914));

void describe_demo_stage(inout vec4 fragColor, vec2 fragCoord)
{
    int line = -1;
    switch (g_demo_stage)
    {
        case DEMO_STAGE_DEPTH:    line = 0; break;
        case DEMO_STAGE_NORMALS:  line = 1; break; 
        case DEMO_STAGE_UV:     line = 2; break; 
        case DEMO_STAGE_TEXTURES: line = 3; break;
        case DEMO_STAGE_LIGHTING: line = 4; break;
        case DEMO_STAGE_COMPOSITE:  line = 5; break;
        default:          return;
    }

    int start = DEMO_STAGES.data[1+line];
    int num_chars = DEMO_STAGES.data[2+line] - start;

    vec2 margin = iResolution.xy * 16./450.;
    vec2 ref = vec2(iResolution.x - margin.x, margin.y);
    vec2 pos = align(num_chars, ref, vec2(1, 0));

    vec4 box = vec4(pos, (ivec2(num_chars, 1) * CHAR_SIZE) << g_text_scale_shift);
    float radius = 16. * exp2(float(g_text_scale_shift));
    box += radius * (.25 * vec4(1, 1, -2, -2));
    float intensity = (g_demo_stage == DEMO_STAGE_LIGHTING) ? .5 : .625;
    fragColor.rgb *= mix(1., intensity, sqr(shadow_box(fragCoord, box, radius)));

    ivec2 uv = text_uv(fragCoord - pos);
    if (line_index(uv.y) != 0)
        return;
    int glyph = glyph_index(uv.x);
    if (uint(glyph) >= uint(num_chars))
        return;

    glyph += start;
    glyph = get_byte(glyph & 3, DEMO_STAGES.data[DEMO_STAGES.data[0] + 2 + (glyph>>2)]);

    vec4 color = vec4(1);
    print_glyph(fragColor, uv, glyph, color);
}

////////////////////////////////////////////////////////////////

WRAP(GGAME_HUD_STATS,GAME_HUD_STATS,int,12)(3,0,9,18,27,85329164,12,302060544,320079111,581<<18,1293,0));

void draw_game_info(inout vec4 fragColor, vec2 fragCoord)
{
    GameState game_state;
    LOAD(game_state);
    if (game_state.level == 0.)
        return;

    const int NUM_LINES = GAME_HUD_STATS.data[0];
    const int PREFIX_LENGTH = GAME_HUD_STATS.data[2] - GAME_HUD_STATS.data[1];
    const int NUM_DIGITS = 4;
    const int LINE_LENGTH = PREFIX_LENGTH + NUM_DIGITS;
    
    const float MARGIN = 16.;
    vec2 anchor = vec2(MARGIN, iResolution.y - MARGIN - float((CHAR_SIZE*NUM_LINES) << g_text_scale_shift));
    
    ivec2 uv = text_uv(fragCoord - anchor);
    int line = NUM_LINES - 1 - line_index(uv.y);
    
    // ignore last 2 lines (time/targets left) if game is over
    int actual_num_lines = NUM_LINES - (int(game_state.level < 0.) << 1);
    
    vec4 box = vec4(MARGIN, iResolution.y-MARGIN, ivec2(LINE_LENGTH, (actual_num_lines<<1)-1)<<g_text_scale_shift);
    box.zw *= vec2(CHAR_SIZE);
    box.y -= box.w;
    draw_shadow_box(fragColor, fragCoord, box);
    
    // line spacing
    if ((line & 1) != 0)
        return;
    line >>= 1;
    
    if (uint(line) >= uint(actual_num_lines))
        return;
       
    int start = GAME_HUD_STATS.data[1+line];
    int num_chars = GAME_HUD_STATS.data[2+line] - start;
    int glyph = glyph_index(uv.x);
    if (uint(glyph) < uint(num_chars))
    {
        glyph += start;
        glyph = get_byte(glyph & 3, GAME_HUD_STATS.data[GAME_HUD_STATS.data[0] + 2 + (glyph>>2)]);
    }
    else
    {
        glyph -= num_chars;
        if (uint(glyph) >= uint(NUM_DIGITS))
            return;
        
        int stat;
        switch (line)
        {
            case 0: stat = int(abs(game_state.level)); break;
            case 1: stat = int(game_state.targets_left); break;
            case 2: stat = int(game_state.time_left); break;
            default: stat = 0; break;
        }
    glyph = NUM_DIGITS - 1 - glyph;
        glyph = int_glyph(stat, glyph);
    }

    const vec3 HIGHLIGHT_COLOR = vec3(.60, .30, .23);
    vec4 color = vec4(vec3(.75), 1.);
    if ((line == 0 && fract(game_state.level) > 0.) ||
        (line == 1 && fract(game_state.targets_left) > 0.))
    {
    color.rgb = HIGHLIGHT_COLOR;
    }
    else if (line == 2 && game_state.time_left < 10.)
    {
        float blink_rate = game_state.time_left < 5. ? 2. : 1.;
        if (fract(game_state.time_left * blink_rate) > .75)
            color.rgb = HIGHLIGHT_COLOR;
    }

    print_glyph(fragColor, uv, glyph, color);
}

////////////////////////////////////////////////////////////////

void apply_motion_blur(inout vec4 fragColor, vec2 fragCoord, vec4 camera_pos)
{
#if !USE_MOTION_BLUR
    return;
#endif

    // not right after teleporting
    float teleport_time = camera_pos.w;
    if (teleport_time > 0. && abs(iTime - teleport_time) < 1e-4)
        return;
    
    vec3 camera_angles = load(ADDR_CAM_ANGLES).xyz;
    vec3 prev_camera_pos = load(ADDR_PREV_CAM_POS).xyz;
    vec3 prev_camera_angles = load(ADDR_PREV_CAM_ANGLES).xyz;
    mat3 view_matrix = rotation(camera_angles.xyz);
    mat3 prev_view_matrix = rotation(prev_camera_angles.xyz);

    vec4 ndc_scale_bias = get_viewport_transform(iFrame, iResolution.xy, g_downscale);
    ndc_scale_bias.xy /= iResolution.xy;
    vec2 actual_res = ceil(iResolution.xy / g_downscale);
    vec4 coord_bounds = vec4(vec2(.5), actual_res - .5);

    vec3 dir = view_matrix * unproject(fragCoord * ndc_scale_bias.xy + ndc_scale_bias.zw);
    vec3 surface_point = camera_pos.xyz + dir * VIEW_DISTANCE * fragColor.w;
    dir = surface_point - prev_camera_pos;
    dir = dir * prev_view_matrix;
    vec2 prev_coord = project(dir).xy;
    prev_coord = (prev_coord - ndc_scale_bias.zw) / ndc_scale_bias.xy;
    float motion = length(prev_coord - fragCoord);

    if (fragColor.w <= 0. || motion * g_downscale < 4.)
        return;
    
    // Simulating a virtual shutter to avoid excessive blurring at lower FPS
    const float MOTION_BLUR_SHUTTER = MOTION_BLUR_AMOUNT / float(MOTION_BLUR_FPS);
    float shutter_fraction = clamp(MOTION_BLUR_SHUTTER/iTimeDelta, 0., 1.);

    vec2 rcp_resolution = 1./iResolution.xy;
    vec4 uv_bounds = coord_bounds * rcp_resolution.xyxy;
    vec2 trail_start = fragCoord * rcp_resolution;
    vec2 trail_end = prev_coord * rcp_resolution;
    trail_end = mix(trail_start, trail_end, shutter_fraction * linear_step(4., 16., motion * g_downscale));

    float mip_level = log2(motion / (float(MOTION_BLUR_SAMPLES) + 1.)) - 1.;
    mip_level = clamp(mip_level, 0., 2.);

    const float INC = 1./float(MOTION_BLUR_SAMPLES);
    float trail_offset = BLUE_NOISE(fragCoord).x * INC - .5;
    float trail_weight = 1.;
    for (float f=0.; f<float(MOTION_BLUR_SAMPLES); ++f)
    {
        vec2 sample_uv = mix(trail_start, trail_end, trail_offset + f * INC);
        if (is_inside(sample_uv, uv_bounds) < 0.)
            continue;
        vec4 s = textureLod(iChannel2, sample_uv, mip_level);
        // Hack: to avoid weapon model ghosting we'll ignore samples landing in that area.
        // This introduces another artifact (sharper area behind the weapon model), but
        // this one is harder to notice in motion...
        float weight = step(0., s.w);
        fragColor.rgb += s.xyz * weight;
        trail_weight += weight;
    }
    
    fragColor.rgb /= trail_weight;
}

void present_scene(out vec4 fragColor, vec2 fragCoord, Options options)
{
    fragCoord /= g_downscale;
    vec2 actual_res = ceil(iResolution.xy / g_downscale);

    // cover up our viewmodel lighting hack
    bool is_ground_sample = is_inside(fragCoord, iResolution.xy - 1.) > 0.;
    if (is_ground_sample)
        fragCoord.x--;

    vec4 camera_pos = load(ADDR_CAM_POS);
    vec3 lava_delta = abs(camera_pos.xyz - clamp(camera_pos.xyz, LAVA_BOUNDS[0], LAVA_BOUNDS[1]));
    float lava_dist = max3(lava_delta.x, lava_delta.y, lava_delta.z);
    if (lava_dist <= 0.) 
    {
        fragCoord += sin(iTime + 32. * (fragCoord/actual_res).yx) * actual_res * (1./192.);
        fragCoord = clamp(fragCoord, vec2(.5), actual_res - .5);
    }

    fragColor = texelFetch(PRESENT_CHANNEL, ivec2(fragCoord), 0);
    
    if (test_flag(options.flags, OPTION_FLAG_MOTION_BLUR))
      apply_motion_blur(fragColor, fragCoord, camera_pos);

    fragColor.rgb = linear_to_gamma(fragColor.rgb);
}

////////////////////////////////////////////////////////////////

void color_correction(inout vec4 fragColor, vec2 fragCoord, bool is_thumbnail)
{
    if (g_demo_stage != DEMO_STAGE_NORMALS)
    {
        Options options;
        LOAD(options);
      
        float gamma = is_thumbnail ? .8 : 1. - options.brightness * .05;
#if GAMMA_MODE
      fragColor.rgb = gamma_to_linear(fragColor.rgb);
      float luma = dot(fragColor.rgb, vec3(0.2126, 0.7152, 0.0722));
      if (luma > 0.)
        fragColor.rgb *= pow(luma, gamma) / luma;
      fragColor.rgb = linear_to_gamma(fragColor.rgb);
#else
      fragColor.rgb = pow(fragColor.rgb, vec3(gamma));
#endif
    }
    
    // dithering, for smooth depth/lighting visualisation (when not quantized!)
    fragColor.rgb += (BLUE_NOISE(fragCoord).rgb - .5) * (1./127.5);
}

////////////////////////////////////////////////////////////////

bool draw_debug(out vec4 fragColor, vec2 fragCoord)
{
    if (iMouse.z > 0.)
        fragCoord = (fragCoord - iMouse.xy) / DEBUG_CLICK_ZOOM + iMouse.xy;
    ivec2 addr = ivec2(fragCoord);

#if defined(DEBUG_TEXTURE) && (DEBUG_TEXTURE >= 0) && (DEBUG_TEXTURE < NUM_MATERIALS)
    vec4 atlas_info = load(ADDR_ATLAS_INFO);
    float atlas_lod = atlas_info.y;
    float atlas_scale = exp2(-atlas_lod);
    vec4 tile = get_tile(DEBUG_TEXTURE);
    vec2 uv = fragCoord / min_component(iResolution.xy/tile.zw) + tile.xy;
    fragColor = is_inside(uv, tile) < 0. ? vec4(0) :
      texelFetch(SETTINGS_CHANNEL, ivec2(ATLAS_OFFSET + uv * atlas_scale), 0);
    return true;
#endif

#if DEBUG_ATLAS
    fragColor = texelFetch(SETTINGS_CHANNEL, addr, 0);
    return true;
#endif

#if DEBUG_LIGHTMAP >= 2
    int channel = addr.x & 3;
    addr.x >>= 2;
    if (uint(addr.y) < LIGHTMAP_SIZE.x && uint(addr.x) < LIGHTMAP_SIZE.y/4u)
    {
        LightmapSample s = decode_lightmap_sample(texelFetch(LIGHTMAP_CHANNEL, addr.yx, 0));
        float l = s.values[channel], w = s.weights[channel];
#if DEBUG_LIGHTMAP >= 3
      fragColor = vec4(w <= 0. ? vec3(1,0,0) : l == 0. ? vec3(0,0,1) : vec3(l), 1);
#else
        fragColor = vec4(vec3(clamp(l, 0., 1.)), 1);
#endif
    }
    else
    {
        fragColor = vec4(0,0,0,1);
    }
    return true;
#elif DEBUG_LIGHTMAP
    vec4 texel = texelFetch(LIGHTMAP_CHANNEL, addr, 0);
    fragColor =
        uint(addr.x) < LIGHTMAP_SIZE.x && uint(addr.y) < LIGHTMAP_SIZE.y/4u ?
          decode_lightmap_sample(texel).values :
        texel;
    return true;
#endif

    return false;
}

////////////////////////////////////////////////////////////////

void crt_effect(inout vec4 fragColor, vec2 fragCoord, Options options)
{
#if USE_CRT_EFFECT
  if (!test_flag(options.flags, OPTION_FLAG_CRT_EFFECT))
        return;
    
    vec2 uv = fragCoord / iResolution.xy, offset = uv - .5;
    fragColor.rgb *= 1. + sin(fragCoord.y * (TAU/4.)) * (CRT_SCANLINE_WEIGHT);
    fragColor.rgb *= clamp(1.6 - sqrt(length(offset)), 0., 1.);
    
    const float
        MASK_LO = 1. - (CRT_MASK_WEIGHT) / 3.,
        MASK_HI = 1. + (CRT_MASK_WEIGHT) / 3.;

    vec3 mask = vec3(MASK_LO);
    float i = fract((floor(fragCoord.y) * 3. + fragCoord.x) * (1./6.));
    if (i < 1./3.)    mask.r = MASK_HI;
    else if (i < 2./3.) mask.g = MASK_HI;
    else        mask.b = MASK_HI;

  fragColor.rgb *= mask;
#endif // USE_CRT_EFFECT
}

////////////////////////////////////////////////////////////////

void mainImage( out vec4 fragColor, vec2 fragCoord )
{
    if (draw_debug(fragColor, fragCoord))
        return;
    
    Options options;
    LOAD(options);
    
    g_downscale = get_downscale(options);
    bool is_thumbnail = test_flag(int(load(ADDR_RESOLUTION).z), RESOLUTION_FLAG_THUMBNAIL);
    
    Lighting lighting;
    LOAD(lighting);

    UPDATE_TIME(lighting);
    UPDATE_DEMO_STAGE_EX(fragCoord/g_downscale, g_downscale, is_thumbnail);
    init_text_scale();
    update_console();

    Timing timing;
    LOAD(timing);
    g_animTime = timing.anim;

    present_scene   (fragColor, fragCoord, options);
    add_effects     (fragColor, fragCoord, is_thumbnail);
    describe_demo_stage (fragColor, fragCoord);
    draw_game_info    (fragColor, fragCoord);
    draw_perf     (fragColor, fragCoord);
    draw_menu     (fragColor, fragCoord, timing);
    draw_console    (fragColor, fragCoord, lighting);
    color_correction  (fragColor, fragCoord, is_thumbnail);
    crt_effect      (fragColor, fragCoord, options);
}
////////////////////////////////////////////////////////////////
// Buffer D:
// - UV/texture mapping
// - particles (fireball trail, shotgun pellets, teleporter effect)
// - volumetric light shafts
// - demo mode stages
// - GBuffer debug vis
////////////////////////////////////////////////////////////////

// config.cfg //////////////////////////////////////////////////

#define TEXTURE_FILTER        1   // [0=nearest; 1=linear]
#define USE_MIPMAPS         2   // [0=off; 1=derivative-based; 2=depth+slope]
#define LOD_SLOPE_SCALE       0.9   // [0.0=authentic/sharp/aliased - 1.0=smooth]
#define LOD_BIAS          0.0
#define LOD_DITHER          0.0   // 1.0=discount linear mip filtering

// I know, mixing my Marvel and my DC here...
// Note: if you enable UV dithering, make sure to also
// comment out QUANTIZE_SCENE below
// and set LIGHTMAP_FILTER to 2 in Buffer B
#define USE_UV_DITHERING      0
#define UV_DITHER_STRENGTH      1.00

#define QUANTIZE_SCENE        48    // comment out to disable

#define RENDER_PARTICLES      1
#define CULL_PARTICLES        1

#define RENDER_VOLUMETRICS      1
#define RENDER_WINDOW_PROJECTION  1
#define VOLUMETRIC_STRENGTH     0.125
#define VOLUMETRIC_SAMPLES      8   // 4=low..8=medium..16=high
#define VOLUMETRIC_MASK_LOD     1
#define VOLUMETRIC_FALLOFF      400.  // comment out to disable
#define VOLUMETRIC_SOFT_EDGE    64.   // comment out to disable
#define VOLUMETRIC_SUN_DIR      vec3(8, -2, -3)
#define VOLUMETRIC_PLAYER_SHADOW  2   // [0=off; 1=capsule; 2=capsule+sphere]
#define VOLUMETRIC_ANIM       1
#define WINDOW_PROJECTION_STRENGTH  64.

#define DEBUG_DEPTH         0
#define DEBUG_NORMALS       0
#define DEBUG_TEXTURES        0   // aka fullbright mode
#define DEBUG_MIPMAPS       0
#define DEBUG_LIGHTING        0
#define DEBUG_PARTICLE_CULLING    0
#define DEBUG_VOLUMETRICS     0

////////////////////////////////////////////////////////////////
// Implementation //////////////////////////////////////////////
////////////////////////////////////////////////////////////////

#define NOISE_CHANNEL       iChannel1
#define SETTINGS_CHANNEL      iChannel3

float g_downscale = 2.;
float g_animTime = 0.;

vec4 load(vec2 address)
{
    return load(address, SETTINGS_CHANNEL);
}

// Texturing ///////////////////////////////////////////////////

vec2 uv_map_axial(vec3 pos, int axis)
{
    return (axis==0) ? pos.yz : (axis==1) ? pos.xz : pos.xy;
}

vec2 tri(vec2 x)
{
    vec2 h = fract(x*.5)-.5;
    return 1.-2.*abs(h);
}

vec3 rainbow(float hue)
{
    return clamp(vec3(min(hue, 1.-hue), abs(hue-1./3.), abs(hue-2./3.))*-6.+2., 0., 1.);
}

vec4 get_balloon_color(const int material, const float current_level)
{
    vec4 color = vec4(vec3(.25),.35);
    float hue = float(material-BASE_TARGET_MATERIAL)*(1./float(NUM_TARGETS));
    hue = fract(hue + current_level * 1./6.);
    color.rgb += rainbow(hue) * .5;
    return color;
}

// iq: https://iquilezles.org/articles/checkerfiltering
float checkersGrad(vec2 uv, vec2 ddx, vec2 ddy)
{
    vec2 w = max(abs(ddx), abs(ddy)) + 1e-4;    // filter kernel
    vec2 i = (tri(uv+0.5*w)-tri(uv-0.5*w))/w;   // analytical integral (box filter)
    return 0.5 - 0.5*i.x*i.y;                   // xor pattern
}

struct SamplerState
{
    vec4 tile;
    float atlas_scale;
    int flags;
};

vec4 texture_lod(SamplerState state, vec2 uv, int lod)
{
    float texel_scale = state.atlas_scale * exp2i(-lod);
    bool use_filter = test_flag(state.flags, OPTION_FLAG_TEXTURE_FILTER);
  if (use_filter)
      uv += -.5 / texel_scale;
    
    uv = fract(uv / state.tile.zw);
    state.tile *= texel_scale;
    uv *= state.tile.zw;
  
    vec2 mip_base = mip_offset(lod) * ATLAS_SIZE * state.atlas_scale + state.tile.xy + ATLAS_OFFSET;

    if (use_filter)
    {
        ivec4 address = ivec2(mip_base + uv).xyxy;
        address.zw++;
        if (uv.x >= state.tile.z - 1.) address.z -= int(state.tile.z);
        if (uv.y >= state.tile.w - 1.) address.w -= int(state.tile.w);

        vec4 s00 = gamma_to_linear(texelFetch(iChannel3, address.xy, 0));
        vec4 s10 = gamma_to_linear(texelFetch(iChannel3, address.zy, 0));
        vec4 s01 = gamma_to_linear(texelFetch(iChannel3, address.xw, 0));
        vec4 s11 = gamma_to_linear(texelFetch(iChannel3, address.zw, 0));

        uv = fract(uv);
        return linear_to_gamma(mix(mix(s00, s10, uv.x), mix(s01, s11, uv.x), uv.y));
    }
    else
    {
        return texelFetch(iChannel3,  ivec2(mip_base + uv), 0);
    }
}

vec4 sample_tile(GBuffer gbuffer, vec2 uv, float depth, float alignment, int flags, vec2 noise)
{
    int material = gbuffer.material;
    
    vec4 atlas_info = load(ADDR_ATLAS_INFO);
    float atlas_lod = atlas_info.y;
    float atlas_scale = exp2(-atlas_lod);
    
#if USE_MIPMAPS
    int max_lod = clamp(int(round(atlas_info.x)) - 1, 0, MAX_MIP_LEVEL - int(atlas_lod));
    float lod_bias = LOD_BIAS - atlas_lod;
    lod_bias += LOD_DITHER * (noise.y - .5);

    #if USE_MIPMAPS >= 2
      float deriv = depth*VIEW_DISTANCE * FOV_FACTOR * g_downscale / (iResolution[FOV_AXIS]*.5 * alignment);
  #else
      float deriv = max(fwidth(uv.x), fwidth(uv.y));
      if (gbuffer.edge)
        max_lod = int(max(LOD_BIAS, atlas_lod));
  #endif

    int lod = int(floor(log2(max(1., deriv)) + lod_bias));
    lod = clamp(lod, 0, max_lod);
#else
    const int lod = 0;
#endif // USE_MIPMAPS
    //lod = 0;
    
#if USE_UV_DITHERING
    if (!test_flag(flags, OPTION_FLAG_TEXTURE_FILTER))
      uv += (noise - .5) * UV_DITHER_STRENGTH * exp2(float(lod)+atlas_lod);
#endif
    
#if DEBUG_MIPMAPS
  return vec4(vec3(float(lod)/6.), 0.);
#endif
    
    return texture_lod(SamplerState(get_tile(material), atlas_scale, flags), uv, lod);
}

vec4 apply_material(GBuffer gbuffer, vec3 surface_point, vec3 surface_normal, vec3 eye_dir, float depth, int flags, vec2 noise)
{
    int material = gbuffer.material;
    int on_edge = int(gbuffer.edge);
    int axis = gbuffer.uv_axis;
    
    if (material == MATERIAL_SHOTGUN_FLASH)
        material = MATERIAL_FLAME;

    GameState game_state;
    LOAD(game_state);
    if (is_material_balloon(material))
    {
        vec4 color = get_balloon_color(material, floor(abs(game_state.level)));
        if (game_state.level < 0.)
        {
            float fraction = linear_step(0., BALLOON_SCALEIN_TIME*.1, fract(-game_state.level));
            color.rgb = mix(vec3(.25), color.rgb, sqr(fraction));
        }
        return color;
    }
    
    if (is_material_viewmodel(material))
    {
        const vec4 SHOTGUN_COLORS[NUM_SHOTGUN_MATERIALS] = vec4[](vec4(.25,.18,.12,.5), vec4(.0,.0,.0,.5), vec4(0));
        vec4 color = SHOTGUN_COLORS[min(material - NUM_MATERIALS, NUM_SHOTGUN_MATERIALS)];
        float light = clamp(dot(vec2(abs(surface_normal.y), surface_normal.z), normalize(vec2(1, 8))), 0., 1.);
        vec2 uv = surface_point.xy * 8.;
#if USE_UV_DITHERING
        uv += (noise - .5) * UV_DITHER_STRENGTH;
#endif
        color.rgb *= .125 + .875*light;
        if (material == MATERIAL_SHOTGUN_BARREL)
        {
            color.rgb = mix(vec3(.14,.11,.06), color.rgb, sqr(sqr(light)));
            color.rgb = mix(color.rgb, vec3(.2,.2,.25), around(.87, .17, light));
            float specular = pow(light, 16.) * .75;
            color.rgb += specular;
        }
        else
        {
          light = clamp(dot(surface_normal.yz, normalize(vec2(-1, 1))), 0., 1.);
            float highlight = pow(light, 4.) * .125;
            color.rgb *= 1. + highlight;
        }
        if (!test_flag(flags, OPTION_FLAG_TEXTURE_FILTER))
            uv = round(uv);
        float variation = mix(1., .83, smooth_noise(uv));
        return vec4(color.rgb * variation, color.a);
    }
    
    // brief lightning flash when shooting the sky to start a new game
    const float LIGHTNING_DURATION = .125;
    bool lightning =
        game_state.level <= 1.+.1*LEVEL_WARMUP_TIME &&
        game_state.level >= 1.+.1*(LEVEL_WARMUP_TIME - LIGHTNING_DURATION);
    
#if USE_MIPMAPS >= 2
    float alignment = abs(dot(normalize(eye_dir), surface_normal));
    alignment = mix(1., alignment, LOD_SLOPE_SCALE);
#else
    float alignment = 1.;
#endif
    
    vec2 sample_uv[3];
    int material2 = material;
    int num_uvs;
    
    if (material == MATERIAL_SKY1)
    {
      // ellipsoidal mapping for the sky
        const float SKY_FLATTEN = 4.;
        sample_uv[0] = 512. * normalize(eye_dir*vec3(1.,1.,SKY_FLATTEN)).xy;
        sample_uv[1] = rotate(sample_uv[0] + g_animTime * 24., 30.);
        sample_uv[0] += g_animTime * 12.;
        material2 = MATERIAL_SKY1B;
        
        num_uvs = 2;
        depth = 0.;
        alignment = 1.;
    }
    else if (axis != 3)
    {
        // world brushes, project using dominant axis
        sample_uv[0] = uv_map_axial(surface_point, axis);
        if (is_material_liquid(material))
            sample_uv[0] += sin(g_animTime + sample_uv[0].yx * (1./32.)) * 12.;
        num_uvs = 1;
    }
    else
    {
      // triplanar mapping (for entities)
        const float SCALE = 2.; // higher res
        vec3 uvw = surface_point * SCALE;
        vec2 uv_bias = vec2(0);
        if (material == MATERIAL_FLAME)
        {
          float loop = floor(g_animTime * 10.) * .1;
          uv_bias.y = -fract(loop) * 64.;
        }
        
        sample_uv[0] = uvw.xy + uv_bias;
        sample_uv[1] = uvw.yz + uv_bias;
        sample_uv[2] = uvw.xz + uv_bias;
        num_uvs = 3;
        depth *= SCALE;
    }
    
    vec4 colors[3];
    gbuffer.material = material;
    colors[0] = sample_tile(gbuffer, sample_uv[0], depth, alignment, flags, noise);
    
    gbuffer.material = material2;
    if (num_uvs >= 2)
      colors[1] = sample_tile(gbuffer, sample_uv[1], depth, alignment, flags, noise);
    
    gbuffer.material = material;
    if (num_uvs >= 3)
      colors[2] = sample_tile(gbuffer, sample_uv[2], depth, alignment, flags, noise);
    
    vec4 textured;
    if (material == MATERIAL_SKY1)
    {
        //textured = (dot(colors[1].rgb, vec3(1)) + noise.y*.1 < .45) ? colors[0] : colors[1];
        textured = mix(colors[0], colors[1], linear_step(.35, .45, dot(colors[1].rgb, vec3(1))));
        textured.rgb *= mix(1., 2., lightning);
    }
    else if (axis != 3)
    {
        textured = colors[0];
    }
  else
    {
        vec3 axis_weights = abs(surface_normal);
        axis_weights *= 1. / (axis_weights.x + axis_weights.y + axis_weights.z);

        textured =
            colors[0] * axis_weights.z +
            colors[1] * axis_weights.x +
            colors[2] * axis_weights.y ;
    }
    
    // disable AO and reduce shadowing during flash
    textured.a = mix(textured.a, min(textured.a, .35), lightning);
    
    return textured;
}

// Fireball particle trail /////////////////////////////////////

void add_to_aabb(inout vec3 min_point, inout vec3 max_point, vec3 point)
{
    min_point = min(min_point, point);
    max_point = max(max_point, point);
}

void get_fireball_bounds
(
    const Fireball fireball,
    const vec3 camera_pos, const mat3 view_matrix,
    float zslack,
    out vec3 mins, out vec3 maxs
)
{
    float apex_time = fireball.velocity.z * (1./GRAVITY);
    vec3 apex;
    apex.z = sqr(fireball.velocity.z) * (.5/GRAVITY);
    apex.xy = fireball.velocity.xy * apex_time;
    
    vec3 pos = FIREBALL_ORIGIN - camera_pos;
    mins = maxs = project(pos * view_matrix);
    
    vec3 p;
    p = (pos + vec3(fireball.velocity.xy * (apex_time*2.), 0.));
    add_to_aabb(mins, maxs, project(p * view_matrix));

    p = pos + fireball.velocity * fireball.velocity.z * (.5/GRAVITY);
    p.z += zslack;
    add_to_aabb(mins, maxs, project(p * view_matrix));
    
    p = mix(p, pos + vec3(apex.xy, apex.z+zslack), 2.);
    add_to_aabb(mins, maxs, project(p * view_matrix));
}

void add_teleporter_effect(inout vec4 fragColor, vec2 fragCoordNDC, vec3 camera_pos, float teleport_time)
{
    if (teleport_time <= 0.)
        return;
    
    const float TELEPORT_EFFECT_DURATION = .25;

    // at 144 FPS the trajectories are too obvious/distracting
    const float TELEPORT_EFFECT_FPS = 60.;
    float fraction = floor((iTime - teleport_time)*TELEPORT_EFFECT_FPS+.5) * (1./TELEPORT_EFFECT_FPS);
    
    if (fraction >= TELEPORT_EFFECT_DURATION)
        return;
    fraction = fraction * (1./TELEPORT_EFFECT_DURATION);

    const int PARTICLE_COUNT = 96;
    const float MARGIN = .125;
    const float particle_radius = 12./1080.;
    float aspect = min_component(iResolution.xy) / max_component(iResolution.xy);
    float pos_bias = (-1. + MARGIN) * aspect;
    float pos_scale = pos_bias * -2.;

    // this vignette makes the transition stand out a bit more using just visuals
    // Quake didn't have it, but Quake had sound effects...
    float vignette = clamp(length(fragCoordNDC*.5), 0., 1.);
    fragColor.rgb *= 1. - vignette*(1.-fraction);

    int num_particles = NO_UNROLL(PARTICLE_COUNT);
    for (int i=0; i<num_particles; ++i) // ugh... :(
    {
        vec4 hash = hash4(teleport_time*13.37 + float(i));
        float speed = mix(1.5, 2., hash.z);
        float angle = hash.w * TAU;
        float intensity = mix(.25, 1., fract(float(i)*PHI + .1337));
        vec2 direction = vec2(cos(angle), sin(angle));
        vec2 pos = hash.xy * pos_scale + pos_bias;
        pos += (fraction * speed) * direction;
        pos -= fragCoordNDC;
        float inside = step(max(abs(pos.x), abs(pos.y)), particle_radius);
        if (inside > 0.)
            fragColor = vec4(vec3(intensity), 0.);
    }
}

void add_particles
(
    inout vec4 fragColor, vec2 fragCoordNDC,
    vec3 camera_pos, mat3 view_matrix, float depth,
    float attack, float teleport_time
)
{
#if RENDER_PARTICLES
    const float
        WORLD_RADIUS    = 1.5,
      MIN_PIXEL_RADIUS  = 2.,
      SPAWN_INTERVAL    = .1,
      LIFESPAN      = 1.,
      LIFESPAN_VARIATION  = .5,
      MAX_GENERATIONS   = ceil(LIFESPAN / SPAWN_INTERVAL),
      BUNCH       = 4.,
        ATTACK_FADE_START = .85,
        ATTACK_FADE_END   = .5,
        PELLET_WORLD_RADIUS = .5;
    const vec3 SPREAD   = vec3(3, 3, 12);
    
    add_teleporter_effect(fragColor, fragCoordNDC, camera_pos, teleport_time);
    
    float depth_scale = MIN_PIXEL_RADIUS * g_downscale/iResolution.x;
    depth *= VIEW_DISTANCE;
    
    // shotgun pellets //
    if (attack > ATTACK_FADE_END)
    {
        // Game stage advances immediately after the last balloon is popped.
        // When we detect a warmup phase (fractional value for game stage)
        // we have to use the previous stage for coloring the particles.

        vec4 game_state = load(ADDR_GAME_STATE);
        float level = floor(abs(game_state.x));
        if (game_state.x != level && game_state.x > 0.)
            --level;

        float fade = sqrt(linear_step(ATTACK_FADE_START, ATTACK_FADE_END, attack));
        vec3 base_pos = camera_pos;
        base_pos.z += (1. - attack) * 8.;

        float num_pellets = ADDR_RANGE_SHOTGUN_PELLETS.z + min(iTime, 0.);
        for (float f=0.; f<num_pellets; ++f)
        {
            vec2 address = ADDR_RANGE_SHOTGUN_PELLETS.xy;
            address.x += f;
            vec2 props = hash2(address);
            if (props.x <= fade)
                continue;
            vec4 pellet = load(address);
            int hit_material = int(pellet.w + .5);
            if (is_material_sky(hit_material))
                continue;
            vec3 pos = pellet.xyz - base_pos;
            float particle_depth = dot(pos, view_matrix[1]) + (-2.*PELLET_WORLD_RADIUS);
            if (particle_depth < 0. || particle_depth > depth)
                continue;
            vec2 ndc_pos = vec2(dot(pos, view_matrix[0]), dot(pos, view_matrix[2]));
            float radius = max(PELLET_WORLD_RADIUS, particle_depth * depth_scale);
            vec2 delta = abs(ndc_pos - fragCoordNDC * particle_depth);
            if (max(delta.x, delta.y) <= radius)
            {
                fragColor = vec4(vec3(.5 * (1.-sqr(props.y))), 0.);
                depth = particle_depth;
          if (is_material_balloon(hit_material))
                    fragColor.rgb *= get_balloon_color(hit_material, level).rgb * 2.;
            }
        }
    }
    
    Fireball fireball;
    get_fireball_props(g_animTime, fireball);

  #if CULL_PARTICLES
    {
        vec3 mins, maxs;
        get_fireball_bounds(fireball, camera_pos, view_matrix, 40., mins, maxs);
        if (maxs.z <= 0. || mins.z > depth)
            return;

        float slack = 8./mins.z + depth_scale;
        mins.xy -= slack;
        maxs.xy += slack;
        if (mins.z > 0. && is_inside(fragCoordNDC, vec4(mins.xy, maxs.xy - mins.xy)) < 0.)
            return;
    }
  #endif
    
  #if DEBUG_PARTICLE_CULLING
    {
        fragColor.rgb = mix(fragColor.rgb, vec3(1.), .25);
    }
  #endif
    
    float end_time = min(get_landing_time(fireball), g_animTime);
    float end_generation = ceil((end_time - fireball.launch_time) * (1./SPAWN_INTERVAL) - .25);
    
    for (float generation=max(0., end_generation - 1. - MAX_GENERATIONS); generation<end_generation; ++generation)
    {
        float base_time=fireball.launch_time + generation * SPAWN_INTERVAL;
        float base_age = (g_animTime - base_time) * (1./LIFESPAN) + (LIFESPAN_VARIATION * -.5);
        if (base_age > 1.)
            continue;
        
        vec3 base_pos = get_fireball_offset(base_time, fireball) + FIREBALL_ORIGIN;

        for (float f=0.; f<BUNCH; ++f)
        {
            float age = base_age + hash(f + base_time) * LIFESPAN_VARIATION;
            if (age > 1.)
                continue;
            vec3 pos = base_pos - camera_pos;
            pos += hash3(base_time + f*(SPAWN_INTERVAL/BUNCH)) * (SPREAD*2.) - SPREAD;
            pos.z += base_age * 32.;
            float particle_depth = dot(pos, view_matrix[1]);
            if (particle_depth < 0. || particle_depth > depth)
                continue;
            vec2 ndc_pos = vec2(dot(pos, view_matrix[0]), dot(pos, view_matrix[2]));
            float radius = max(WORLD_RADIUS, particle_depth * depth_scale);
            vec2 delta = abs(ndc_pos - fragCoordNDC * particle_depth);
            if (max(delta.x, delta.y) <= radius)
            {
                fragColor = vec4(mix(vec3(.75,.75,.25), vec3(.25), linear_step(.0, .5, age)), 0.);
                depth = particle_depth;
            }
        }
    }
#endif // RENDER_PARTICLES
}

////////////////////////////////////////////////////////////////

const vec3
    VOL_SUN_DIR   = normalize(VOLUMETRIC_SUN_DIR),
    VOL_WINDOW_MINS = vec3(64, 992, -32),
    VOL_WINDOW_MAXS = vec3(64, 1056, 160),
    VOL_WALL_POS  = vec3(368, VOL_WINDOW_MINS.y, -72);

#define MAKE_PLANE(dir, point) vec4(dir, -dot(point, dir))

const vec4 VOL_PLANES[7] = vec4[7]
(
  MAKE_PLANE(normalize(cross(vec3(0, 0,-1), VOL_SUN_DIR)), VOL_WINDOW_MINS),
  MAKE_PLANE(normalize(cross(vec3(0, 0, 1), VOL_SUN_DIR)), VOL_WINDOW_MAXS),
  MAKE_PLANE(vec3( 1, 0, 0), VOL_WALL_POS),
  MAKE_PLANE(vec3(-1, 0, 0), VOL_WINDOW_MINS),
  MAKE_PLANE(normalize(cross(vec3(0, 1, 0), VOL_SUN_DIR)), VOL_WINDOW_MINS),
  MAKE_PLANE(normalize(cross(vec3(0,-1, 0), VOL_SUN_DIR)), VOL_WINDOW_MAXS),
  MAKE_PLANE(vec3(0, 0, -1), VOL_WALL_POS)
);

float volumetric_falloff(float dist)
{
#if defined(VOLUMETRIC_FALLOFF)
    float x = clamp(sqr(1. - dist * (1./float(VOLUMETRIC_FALLOFF))), 0., 1.);
    return x;
    return (x * x + x) * .5;
#else
    return 1.;
#endif
}

float volumetric_player_shadow(vec3 p, vec3 rel_cam_pos)
{
#if VOLUMETRIC_PLAYER_SHADOW
    vec3 occluder_p0 = rel_cam_pos;
    vec3 occluder_p1 = occluder_p0 - vec3(0, 0, 48);
#if VOLUMETRIC_PLAYER_SHADOW >= 2
    occluder_p0.z -= 20.;
#endif // VOLUMETRIC_PLAYER_SHADOW >= 2

    float window_dist = p.x * (1. / VOL_SUN_DIR.x);
    float occluder_dist = occluder_p0.x * (1. / VOL_SUN_DIR.x);
    p -= VOL_SUN_DIR * max(0., window_dist - occluder_dist);
    vec3 occluder_point = closest_point_on_segment(p, occluder_p0, occluder_p1);
    float vis = linear_step(sqr(16.), sqr(24.), length_squared(p - occluder_point));

#if VOLUMETRIC_PLAYER_SHADOW >= 2
    vis = min(vis, linear_step(sqr(8.), sqr(12.), length_squared(p - rel_cam_pos)));
#endif // VOLUMETRIC_PLAYER_SHADOW >= 2

    return vis;
#else
    return 1.;
#endif // VOLUMETRIC_PLAYER_SHADOW
}

void add_volumetrics
(
    inout vec4 fragColor,
    vec3 camera_pos, vec3 dir, float depth01,
    vec3 normal, int uv_axis, bool viewmodel,
    int flags, float noise, bool thumbnail
)
{
#if RENDER_VOLUMETRICS
    if (is_demo_mode_enabled(thumbnail))
    {
        if (!is_demo_stage_composite() || g_demo_scene < 2)
            return;
    }
    else
    {
        if (!test_flag(flags, OPTION_FLAG_LIGHT_SHAFTS) || test_flag(flags, OPTION_FLAG_SHOW_LIGHTMAP))
            return;
    }

    dir *= VIEW_DISTANCE;

    float t_enter = 0.;
    float t_leave = depth01;
    for (int i=0; i<7; ++i)
    {
        vec4 plane = VOL_PLANES[i];
        float dist = dot(plane.xyz, camera_pos) + plane.w;
        float align = dot(plane.xyz, dir);
        if (align == 0.)
        {
            if (dist > 0.)
                return;
            continue;
        }
        dist /= -align;
        t_enter = (align < 0.) ? max(t_enter, dist) : t_enter;
        t_leave = (align > 0.) ? min(t_leave, dist) : t_leave;
        if (t_leave <= t_enter)
            return;
    }

    if (t_leave <= t_enter)
        return;
    
#if DEBUG_VOLUMETRICS
    fragColor.rgb = clamp(fragColor.rgb * 4., 0., 1.);
    return;
#endif

    vec4 atlas_info = load(ADDR_ATLAS_INFO);
    float num_mips = atlas_info.x;
    float atlas_lod = atlas_info.y;
    float atlas_scale = exp2(-atlas_lod);
    int mask_lod = clamp(VOLUMETRIC_MASK_LOD - int(atlas_lod), 0, int(num_mips) - 1);

    SamplerState sampler_state;
    sampler_state.tile      = get_tile(MATERIAL_WINDOW02_1);
  sampler_state.atlas_scale = atlas_scale;
    sampler_state.flags     = flags;
    vec2 uv_offset = -vec2(.5, .5/3.) * sampler_state.tile.zw;

    vec3 relative_cam_pos = camera_pos - VOL_WINDOW_MINS;
    vec3 enter = relative_cam_pos + dir * t_enter;
    vec3 travel = dir * (t_leave - t_enter);

#if RENDER_WINDOW_PROJECTION
    sampler_state.flags = flags | OPTION_FLAG_TEXTURE_FILTER;
    float n_dot_l = dot(-VOL_SUN_DIR, normal);
    if (abs(t_leave - depth01) < 1e-3/VIEW_DISTANCE && n_dot_l > 0.)
    {
        vec3 p = relative_cam_pos + dir * depth01;
        if (uint(uv_axis) < 3u && !test_flag(flags, OPTION_FLAG_TEXTURE_FILTER))
        {
            vec3 snap = .5 - fract(p);
            p += snap * vec3(notEqual(ivec3(uv_axis), ivec3(0, 1, 2)));
        }
        float window_dist = p.x * (1. / VOL_SUN_DIR.x);
        float weight = WINDOW_PROJECTION_STRENGTH * volumetric_falloff(window_dist);
        weight *= clamp(n_dot_l, 0., 1.);
        weight *= volumetric_player_shadow(p, relative_cam_pos);
        weight *= linear_step(0., 2., p.z - (VOL_WALL_POS.z - VOL_WINDOW_MINS.z));
        
        vec2 uv = (p - window_dist * VOL_SUN_DIR).yz + uv_offset;
        vec3 color = gamma_to_linear(texture_lod(sampler_state, uv, 0).rgb);
    
        fragColor.rgb += fragColor.rgb * color * weight;
    }
    sampler_state.flags = flags;
#endif // RENDER_WINDOW_PROJECTION

    const float SAMPLE_WEIGHT = 1. / float(VOLUMETRIC_SAMPLES);
    float base_weight = VOLUMETRIC_STRENGTH * SAMPLE_WEIGHT;

#if defined(VOLUMETRIC_SOFT_EDGE)
    float travel_dist = (t_leave - t_enter) * VIEW_DISTANCE;
    base_weight *= linear_step(0., sqr(VOLUMETRIC_SOFT_EDGE), sqr(travel_dist));
#endif

  for (float f=noise*SAMPLE_WEIGHT; f<1.; f+=SAMPLE_WEIGHT)
    {
        vec3 p = enter + travel * f;
        float window_dist = p.x * (1. / VOL_SUN_DIR.x);
        float weight = base_weight * volumetric_falloff(window_dist);

        vec2 uv = (p - window_dist * VOL_SUN_DIR).yz + uv_offset;
        vec4 sample_color = texture_lod(sampler_state, uv, mask_lod);
        sample_color = gamma_to_linear(sample_color);

#if VOLUMETRIC_ANIM
        float time = g_animTime;
        time += smooth_noise(window_dist * (1./16.));
        uv += time * vec2(7., 1.3);
        uv += sin(uv.yx * (1./15.) + time * .3) * 3.;
        weight *= smooth_noise(uv * (7./64.)) * 1.5 + .25;
#endif // VOLUMETRIC_ANIM

        weight *= volumetric_player_shadow(p, relative_cam_pos);
        
        fragColor.rgb += sample_color.rgb * weight;
    }

    fragColor.rgb = clamp(fragColor.rgb, 0., 1.);
#endif // RENDER_VOLUMETRICS
}

////////////////////////////////////////////////////////////////

void mainImage( out vec4 fragColor, vec2 fragCoord )
{
    Options options;
    LOAD(options);
    
    g_downscale = get_downscale(options);
    vec2 actual_res = min(ceil(iResolution.xy / g_downscale * .125) * 8., iResolution.xy);
    if (max_component(fragCoord - .5 - actual_res) > 0.)
        DISCARD;
    
    bool is_thumbnail = test_flag(int(load(ADDR_RESOLUTION).z), RESOLUTION_FLAG_THUMBNAIL);
    Lighting lighting;
    LOAD(lighting);
    
  UPDATE_TIME(lighting);
    UPDATE_DEMO_STAGE(fragCoord, g_downscale, is_thumbnail);

    vec4 current = texelFetch(iChannel0, ivec2(fragCoord), 0);
    GBuffer gbuffer = gbuffer_unpack(current);
    if (current.z <= 0.)
    {
        fragColor = vec4(vec3(1./16.), 1);
        return;
    }
    
    Timing timing;
    LOAD(timing);
    g_animTime = timing.anim;
    
    bool is_viewmodel = is_material_viewmodel(gbuffer.material);
    vec4 noise = BLUE_NOISE(fragCoord);
    
  vec4 camera_pos = load_camera_pos(SETTINGS_CHANNEL, is_thumbnail);
    vec3 camera_angles = load_camera_angles(SETTINGS_CHANNEL, is_thumbnail).xyz;
    vec3 velocity = load(ADDR_VELOCITY).xyz;
    Transitions transitions; LOAD(transitions);
    mat3 view_matrix = rotation(camera_angles.xyz);
    if (is_viewmodel)
    {
        float base_fov_y = scale_fov(FOV, 9./16.);
        float fov_y = compute_fov(iResolution.xy).y;
        float fov_y_delta = base_fov_y - fov_y;
        view_matrix = view_matrix * rotation(vec2(0, fov_y_delta*.5));
    }

    vec4 ndc_scale_bias = get_viewport_transform(iFrame, iResolution.xy, g_downscale);
    ndc_scale_bias.xy /= iResolution.xy;
    vec2 fragCoordNDC = fragCoord * ndc_scale_bias.xy + ndc_scale_bias.zw;
    
    vec4 plane;
    plane.xyz = gbuffer.normal;
    if (is_viewmodel)
      plane.xyz = plane.xyz * view_matrix;
    vec3 dir = view_matrix * unproject(fragCoordNDC);
    vec3 surface_point = dir * VIEW_DISTANCE * current.z;
    plane.w = -current.z * dot(plane.xyz, dir);
    
    if (is_viewmodel)
    {
        float light_level =
            is_demo_mode_enabled(is_thumbnail) ?
            mix(.4, .6, hash1(camera_pos.xy)) :
          gbuffer_unpack(texelFetch(iChannel0, ivec2(iResolution.xy)-1, 0)).light;
        gbuffer.light = mix(gbuffer.light, 1., .5) * light_level * 1.33;
        surface_point = surface_point * view_matrix;
        surface_point.y -= get_viewmodel_offset(velocity, transitions.bob_phase, transitions.attack);
    }
    else
    {
        surface_point += camera_pos.xyz;
    }
    
    fragColor = apply_material(gbuffer, surface_point, plane.xyz, dir, current.z, options.flags, noise.zw);
  add_particles(fragColor, fragCoordNDC, camera_pos.xyz, view_matrix, current.z, transitions.attack, camera_pos.w);

    if (g_demo_stage == DEMO_STAGE_DEPTH || DEBUG_DEPTH != 0)
        fragColor = vec4(vec3(sqrt(current.z)), 0);
    if (g_demo_stage == DEMO_STAGE_LIGHTING || DEBUG_LIGHTING != 0 || test_flag(options.flags, OPTION_FLAG_SHOW_LIGHTMAP))
        fragColor.rgb = vec3(1);
    if (g_demo_stage == DEMO_STAGE_NORMALS || DEBUG_NORMALS != 0)
        fragColor = vec4(plane.xyz*.5+.5, 0);
    if (g_demo_stage == DEMO_STAGE_TEXTURES || DEBUG_TEXTURES != 0)
        fragColor.a = 0.;
    
    fragColor.rgb *= mix(1., min(2., gbuffer.light), linear_step(.0, .5, fragColor.a));
    fragColor.rgb = clamp(fragColor.rgb, 0., 1.);

#ifdef QUANTIZE_SCENE
    const float LEVELS = float(QUANTIZE_SCENE);
    const int SMOOTH_STAGES = (1<<DEMO_STAGE_DEPTH) | (1<<DEMO_STAGE_LIGHTING) | (1<<DEMO_STAGE_NORMALS);
    if (!test_flag(options.flags, OPTION_FLAG_TEXTURE_FILTER) &&
        !test_flag(SMOOTH_STAGES, 1<<g_demo_stage) &&
        !test_flag(options.flags, OPTION_FLAG_SHOW_LIGHTMAP))
      fragColor.rgb = round(fragColor.rgb * LEVELS) * (1./LEVELS);
#endif
    
    fragColor.rgb = gamma_to_linear(fragColor.rgb);
    
    add_volumetrics
  (
        fragColor,
        camera_pos.xyz, dir, current.z,
        gbuffer.normal, gbuffer.uv_axis, is_viewmodel,
        options.flags, noise.y, is_thumbnail
    );

    // hack: disable motion blur for the gun model
    fragColor.a = is_viewmodel ? -1. : current.z;
}
////////////////////////////////////////////////////////////////
// Buffer C:
// - lightmap accumulation and post-processing
// - UI textures
// - font glyphs
////////////////////////////////////////////////////////////////

// config.cfg //////////////////////////////////////////////////

const int TEXTURE_AA = 4;

////////////////////////////////////////////////////////////////
// Implementation //////////////////////////////////////////////
////////////////////////////////////////////////////////////////

#define ALWAYS_REFRESH_TEXTURES   0
#define DEBUG_TEXT_MASK       0

////////////////////////////////////////////////////////////////

const int
    NUM_DILATE_PASSES       = 1,
    NUM_BLUR_PASSES         = 0,
    NUM_POSTPROCESS_PASSES      = NUM_DILATE_PASSES + NUM_BLUR_PASSES;

#define USE_DIAGONALS       0

void accumulate(inout LightmapSample total, LightmapSample new)
{
    total.values = (total.values*total.weights + new.values*new.weights) / max(total.weights + new.weights, 1.);
    total.weights += new.weights;
}

void accumulate(inout LightmapSample total, vec4 encoded_new)
{
    accumulate(total, decode_lightmap_sample(encoded_new));
}

bool postprocess(inout vec4 fragColor, ivec2 address)
{
  if (iFrame < NUM_LIGHTMAP_FRAMES)
        return false;

    int pass = iFrame - NUM_LIGHTMAP_FRAMES;
    bool blur = pass >= NUM_DILATE_PASSES;

    const ivec2 MAX_COORD = ivec2(LIGHTMAP_SIZE.x - 1u, LIGHTMAP_SIZE.y/4u - 1u);
    vec4
        N  = texelFetch(iChannel1, clamp(address + ivec2( 0, 1), ivec2(0), MAX_COORD), 0),
        S  = texelFetch(iChannel1, clamp(address + ivec2( 0,-1), ivec2(0), MAX_COORD), 0),
        E  = texelFetch(iChannel1, clamp(address + ivec2( 1, 0), ivec2(0), MAX_COORD), 0),
        W  = texelFetch(iChannel1, clamp(address + ivec2(-1, 0), ivec2(0), MAX_COORD), 0),
        NE = texelFetch(iChannel1, clamp(address + ivec2( 1, 1), ivec2(0), MAX_COORD), 0),
        SE = texelFetch(iChannel1, clamp(address + ivec2( 1,-1), ivec2(0), MAX_COORD), 0),
        NW = texelFetch(iChannel1, clamp(address + ivec2(-1, 0), ivec2(0), MAX_COORD), 0),
        SW = texelFetch(iChannel1, clamp(address + ivec2(-1, 0), ivec2(0), MAX_COORD), 0);

    N  = vec4(fragColor.yzw, N.x);
    NE = vec4(E.yzw, NE.x);
    NW = vec4(W.yzw, NW.x);
    S  = vec4(S .w, fragColor.xyz);
    SE = vec4(SE.w, E.xyz);
    SW = vec4(SW.w, W.xyz);

    LightmapSample
        current = decode_lightmap_sample(fragColor),
        total = empty_lightmap_sample();

    accumulate(total, N);
    accumulate(total, S);
    accumulate(total, E);
    accumulate(total, W);
#if USE_DIAGONALS
    accumulate(total, NE);
    accumulate(total, NW);
    accumulate(total, SE);
    accumulate(total, SW);
#endif

    if (blur)
    {
        accumulate(total, current);
      fragColor = encode(total);
    }
    else
    {
        vec4 neighbors = encode(total);
        fragColor = mix(fragColor, neighbors, lessThanEqual(current.weights, vec4(0)));
    }
    
    return true;
}

void accumulate_lightmap(inout vec4 fragColor, ivec2 address)
{
    if (uint(address.x) >= LIGHTMAP_SIZE.x || uint(address.y) >= LIGHTMAP_SIZE.y/4u)
        return;
    if (iFrame >= NUM_LIGHTMAP_FRAMES + NUM_POSTPROCESS_PASSES)
        return;

    if (postprocess(fragColor, address))
        return;
    
    int region = iFrame & 3;
    int base_y = region * int(LIGHTMAP_SIZE.y/16u);
    if (uint(address.y - base_y) >= LIGHTMAP_SIZE.y/16u)
        return;

    address.y = (address.y - base_y) * 4;
    vec4 light = vec4
        (
            texelFetch(iChannel0, address + ivec2(0,0), 0).x,
            texelFetch(iChannel0, address + ivec2(0,1), 0).x,
            texelFetch(iChannel0, address + ivec2(0,2), 0).x,
            texelFetch(iChannel0, address + ivec2(0,3), 0).x
    );

    vec4 weights = step(vec4(0), light);
    vec4 values = max(light, 0.);
    
    LightmapSample total = decode_lightmap_sample(fragColor);
    accumulate(total, LightmapSample(weights, values));
    fragColor = encode(total);
}

// Options text ////////////////////////////////////////////////

float sdf_Options(vec2 p)
{
    const float
        OFFSET_P = 15.,
        OFFSET_T = 35.,
        OFFSET_I = 53.,
        OFFSET_O = 63.,
        OFFSET_N = 81.,
        OFFSET_S = 98.;

    vec4 box = vec4(0);
    vec3 disk1 = vec3(0), disk2 = vec3(0,0,1), disk3 = vec3(0,0,2);
    float vline_x = 1e3, vline_thickness = 2.5;
    float max_ydist = 6.;
    
    p.x -= 15.;
    
  #define MIRROR(compare, midpoint, value) (compare <= midpoint ? value : midpoint*2.-value)
    
    if (p.x <= 20.)
    {
      max_ydist = 7.;
        disk1 = vec3(MIRROR(p.x, 8.5, 7.5), 11.5, 8.);
        disk2 = vec3(MIRROR(p.x, 8.5, 12.), 11.5, 9.);
    }
    else if (p.x <= OFFSET_T)
    {
        p.x -= OFFSET_P;
        vline_x = 9.;
        disk1 = vec3(14.5, 13.5, 4.);
        disk2 = vec3(10.5, 13.5, 5.);
    }
    else if (p.x <= OFFSET_I)
    {
        const float
            BOX_X = 4., BOX_Y = 15.5, BOX_SIZE = 1.5,
            X3 = BOX_X+BOX_SIZE, Y3 = BOX_Y-BOX_SIZE, R3 = BOX_SIZE * 2.;
        
        p.x -= OFFSET_T;
        disk3 = vec3(MIRROR(p.x, 9., X3), Y3, R3);
        box   = vec4(MIRROR(p.x, 9., BOX_X), BOX_Y, BOX_SIZE, BOX_SIZE);
        vline_x = 9.;
    }
    else if (p.x <= OFFSET_O)
    {
        vline_x = OFFSET_I + 4.5;
    }
    else if (p.x <= OFFSET_N)
    {
        p.x -= OFFSET_O;
        disk1 = vec3(MIRROR(p.x, 9., 8.), 11.5, 7.);
        disk2 = vec3(MIRROR(p.x, 9., 12.), 11.5, 8.);
    }
    else if (p.x <= OFFSET_S)
    {
        p.x -= OFFSET_N;
        vline_x = p.x < 9. ? 4.5 : 15.;
        vline_thickness = 2.;
        box = vec4(clamp(p.x, 5., 14.) * vec2(1, -.75) + vec2(0, 18), 1., 2.);
    }
    else
    {
        const float
            X1 = 8., Y1 = 14., R1 = 3.5,
            X2 = 9.5, Y2 = 15.5, R2 = 2.5,
            BOX_X = 6., BOX_Y = 7., BOX_SIZE = 1.5,
            X3 = BOX_X+BOX_SIZE, Y3 = BOX_Y+BOX_SIZE, R3 = BOX_SIZE * 2.;
        
        p.x -= OFFSET_S;
        // TODO: simplify
        if (p.x < 9.)
        {
            disk1 = vec3(X1, Y1, R1);
            disk2 = vec3(X2, Y2, R2);
            disk3 = vec3(X3, Y3, R3);
            box   = vec4(BOX_X, BOX_Y, BOX_SIZE, BOX_SIZE);
        }
        else
        {
            disk1 = vec3(18. - X1, 23. - Y1, R1);
            disk2 = vec3(18. - X2, 23. - Y2, R2);
            disk3 = vec3(18. - X3, 23. - Y3, R3);
            box   = vec4(18. - BOX_X, 23. - BOX_Y, BOX_SIZE, BOX_SIZE);
        }
    }
    
    #undef MIRROR
    
    float dist;
    dist = sdf_disk(p, disk1.xy, disk1.z);
    dist = sdf_exclude(dist, sdf_disk(p, disk2.xy, disk2.z));
    
    dist = sdf_union(dist, sdf_seriffed_box(p, vec2(vline_x, 5.5), vec2(vline_thickness, 12.), vec2(1,.2), vec2(1,.2)));
                       
  float d2 = sdf_centered_box(p, box.xy, box.zw);
    d2 = sdf_exclude(d2, sdf_disk(p, disk3.xy, disk3.z));
    dist = sdf_union(dist, d2);
    dist = sdf_exclude(dist, max_ydist - abs(p.y - 11.5));
   
    return dist;
}

vec2 engraved_Options(vec2 uv)
{
    const float EPS = .1, BEVEL_SIZE = 1.;
    vec3 sdf;
    for (int i=NO_UNROLL(0); i<3; ++i)
    {
        vec2 uv2 = uv;
        if (i != 2)
            uv2[i] += EPS;
        sdf[i] = sdf_Options(uv2);
    }
    vec2 gradient = safe_normalize(sdf.xy - sdf.z);
    float mask = sdf_mask(sdf.z, 1.);
    float bevel = clamp(1. + sdf.z/BEVEL_SIZE, 0., 1.);
    float intensity = .4 + sqr(bevel) * dot(gradient, vec2(0, -1.1));
    intensity = mix(1.5, intensity, mask);
    mask = sdf_mask(sdf.z - 1., 1.);
    return vec2(intensity, mask);
}

// QUAKE text //////////////////////////////////////////////////

float sdf_QUAKE(vec2 uv)
{
    uv /= 28.;
    uv.y -= 4.;
    uv.x -= .0625;
    
    float sdf              = sdf_Q_top(uv);
    uv.y += .875;   sdf = sdf_union(sdf, sdf_U(uv));
    uv.y += .75;  sdf = sdf_union(sdf, sdf_A(uv));
    uv.y += .75;  sdf = sdf_union(sdf, sdf_K(uv - vec2(.2, 0)));
    uv.y += .8125;  sdf = sdf_union(sdf, sdf_E(uv));
    
    sdf *= 28.;
    uv += sin(uv.yx * TAU) * (5./28.);
    sdf = sdf_union(sdf, 28. * (.75 - around(.3, .25, smooth_weyl_noise(2. + uv * 3.24))));
    return sdf_exclude(sdf, (uv.y - .15) * 28.);
}

vec2 engraved_QUAKE(vec2 uv)
{
    const float EPS = .1, BEVEL_SIZE = 2.;
    vec3 sdf;
    for (int i=NO_UNROLL(0); i<3; ++i)
    {
        vec2 uv2 = uv;
        if (i != 2)
            uv2[i] += EPS;
        sdf[i] = sdf_QUAKE(uv2);
    }
    vec2 gradient = safe_normalize(sdf.xy - sdf.z);
    float mask = sdf_mask(sdf.z, 1.);
    float bevel = clamp(1. + sdf.z/BEVEL_SIZE, 0., 1.);
    float intensity = .75 + sqr(bevel) * dot(gradient, vec2(0, -3));
    intensity = mix(1.5, intensity, mask);
    mask = sdf_mask(sdf.z - 1., 1.);
    return vec2(intensity, mask * .7);
}

////////////////////////////////////////////////////////////////

void generate_ui_textures(inout vec4 fragColor, vec2 fragCoord)
{
#if !ALWAYS_REFRESH_TEXTURES
    if (iFrame != 0)
        return;
#endif
    
    const int
    UI_TEXTURE_OPTIONS    = 0,
    UI_TEXTURE_QUAKE_ID   = 1,
        AA_SAMPLES        = clamp(TEXTURE_AA, 1, 128);
    int id = -1;

    vec2 texture_size, bevel_range;
    vec3 base_color;
    
    if (is_inside(fragCoord, ADDR2_RANGE_TEX_OPTIONS) > 0.)
    {
        id = UI_TEXTURE_OPTIONS;
        fragCoord -= ADDR2_RANGE_TEX_OPTIONS.xy;
        texture_size = ADDR2_RANGE_TEX_OPTIONS.zw;
        bevel_range = vec2(1.7, 3.9);
        base_color = vec3(.32, .21, .13);
    }

    if (is_inside(fragCoord, ADDR2_RANGE_TEX_QUAKE) > 0.)
    {
        id = UI_TEXTURE_QUAKE_ID;
        fragCoord -= ADDR2_RANGE_TEX_QUAKE.xy;
        fragCoord = fragCoord.yx;
        texture_size = ADDR2_RANGE_TEX_QUAKE.wz;
        bevel_range = vec2(2.7, 4.9);
        base_color = vec3(.16, .12, .07);
    }
    
    if (id == -1)
        return;

    vec2 base_coord = floor(fragCoord);
    float grain = random(base_coord);

    vec3 accum = vec3(0);
    for (int i=NO_UNROLL(0); i<AA_SAMPLES; ++i)
    {
        fragCoord = base_coord + hammersley(i, AA_SAMPLES);
        vec2 uv = fragCoord / min_component(texture_size);

        float base = weyl_turb(3.5 + uv * 3.1, .7, 1.83);
        if (id == UI_TEXTURE_QUAKE_ID && fragCoord.y < 26. + base * 4. && fragCoord.y > 3. - base * 2.)
        {
            base = mix(base, grain, .0625);
            fragColor.rgb = vec3(.62, .30, .19) * linear_step(.375, .85, base);
            vec2 logo_uv = (uv - .5) * vec2(1.05, 1.5) + .5;
            logo_uv.y += .0625;
            float logo_sdf = sdf_id(logo_uv);
            float logo = sdf_mask(logo_sdf + .25/44., 1.5/44.);
            fragColor.rgb *= 1. - sdf_mask(logo_sdf - 2./44., 1.5/44.);
            fragColor.rgb = mix(fragColor.rgb, vec3(.68, .39, .17) * mix(.5, 1.25, base), logo);
        }
        else
        {
            base = mix(base, grain, .3);
            fragColor.rgb = base_color * mix(.75, 1.25, smoothen(base));
        }

        float bevel_size = mix(bevel_range.x, bevel_range.y, smooth_weyl_noise(uv * 9.));
        vec2 mins = vec2(bevel_size), maxs = texture_size - bevel_size;
        vec2 duv = (fragCoord - clamp(fragCoord, mins, maxs)) * (1./bevel_size);
        float d = mix(length(duv), max_component(abs(duv)), .75);
        fragColor.rgb *= clamp(1.4 - d*mix(1., 1.75, sqr(base)), 0., 1.);
        float highlight = 
            (id == UI_TEXTURE_OPTIONS) ?
              max(0., duv.y) * step(d, .55) :
            sqr(sqr(1. + duv.y)) * around(.4, .4, d) * .35;
        fragColor.rgb *= 1. + mix(.75, 2.25, base) * highlight;

        if (DEBUG_TEXT_MASK != 0)
        {
            float sdf = (id == UI_TEXTURE_OPTIONS) ? sdf_Options(fragCoord) : sdf_QUAKE(fragCoord);
            fragColor.rgb = vec3(sdf_mask(sdf, 1.));
            accum += fragColor.rgb;
            continue;
        }

        vec2 engrave = (id == UI_TEXTURE_OPTIONS) ? engraved_Options(fragCoord) : engraved_QUAKE(fragCoord);
        fragColor.rgb *= mix(1., engrave.x, engrave.y);

        if (id == UI_TEXTURE_OPTIONS)
        {
            vec2 side = sign(fragCoord - texture_size * .5); // keep track of side before folding to 'unmirror' light direction
            fragCoord = min(fragCoord, texture_size - fragCoord);
            vec2 nail = add_knob(fragCoord, 1., vec2(6), 1.25, side * vec2(0, -1));
            fragColor.rgb *= mix(clamp(length(fragCoord - vec2(6, 6.+2.*side.y))/2.5, 0., 1.), 1., .25);
            nail.x += pow(abs(nail.x), 16.) * .25;
            fragColor.rgb = mix(fragColor.rgb, vec3(.7, .54, .43) * nail.x, nail.y * .75);
        }

        accum += fragColor.rgb;
    }
    fragColor.rgb = accum * (1./float(AA_SAMPLES));
}

////////////////////////////////////////////////////////////////

const int NUM_GLYPHS = 56;

WRAP(FontBitmap,FONT_BITMAP,int,NUM_GLYPHS*2)(0,0,0x7c2c3810,25190,0x663e663e,15974,0x606467c,31814,0x6666663e,15974,0x61e467e,
31814,0x61e467c,1542,0x7303233e,1062451,0x637f6363,25443,404232216,6168,808464432,792624,991638339,17251,0x6060606,32326,
0x6f7f7763,26989,0x5f4f4743,6320505,0x6363633e,15971,0x6666663e,1598,0x6b436322,526398,0x663e663e,17990,0x603c067c,15970,
404249214,530456,0x66666666,15462,0x3e367763,2076,0x7f7b5b5b,8758,941379271,58230,0xc1c3462,3084,473461374,32334,0x66663c00,
15462,404233216,6168,0x7c403e00,32258,945831424,536672,909651968,12415,0x3c043c00,536672,0x3e061c00,15462,541097472,12336,
0x3c663c00,15462,0x7c663c00,536672,0x7e181800,6168,63<<25,0,0xc183060,774,0,1542,0,198150,1579008,6168,404232216,6144,406347838,
6144,0x3f3f3f3f,16191,0xc0c1830,3151884,808458252,792624,0,32256,0xc0c0c3c,15372,808464444,15408,0xc183000,6303768,806882304,
396312,0x83e7f7f,8355646,0x7f7f3e1c,67640382,0x3f3f0f03,783));

int glyph_bit(uint glyph, int index)
{
    if (glyph >= uint(NUM_GLYPHS))
        return 0;
    uint data = uint(FONT_BITMAP.data[(glyph<<1) + uint(index>=32)]);
    return int(uint(data >> (index & 31)) & 1u);
}

vec4 glyph_color(uint glyph, ivec2 pixel, float variation)
{
    pixel &= 7;
    pixel.y = 7 - pixel.y;
    int bit_index = pixel.x + (pixel.y << 3);
    int bit = glyph_bit(glyph, bit_index);
    int shadow_bit = min(pixel.x, pixel.y) > 0 ? glyph_bit(glyph, bit_index - 9) : 0;
    return vec4(vec3(bit > 0 ? variation : .1875), float(bit|shadow_bit));
}

void bake_font(inout vec4 fragColor, vec2 fragCoord)
{
#if !ALWAYS_REFRESH_TEXTURES
    if (iFrame != 0)
        return;
#endif

    ivec2 addr = ivec2(floor(fragCoord - ADDR2_RANGE_FONT.xy));
    if (any(greaterThanEqual(uvec2(addr), uvec2(ADDR2_RANGE_FONT.zw))))
        return;
    
    const int GLYPHS_PER_LINE = int(ADDR2_RANGE_FONT.z) >> 3;
    
    int glyph = (addr.y >> 3) * GLYPHS_PER_LINE + (addr.x >> 3);
    float variation = mix(.625, 1., random(fragCoord));
    fragColor = glyph_color(uint(glyph), addr, variation);
}

////////////////////////////////////////////////////////////////

void mainImage( out vec4 fragColor, vec2 fragCoord )
{
    if (is_inside(fragCoord, ADDR2_RANGE_PARAM_BOUNDS) < 0.)
        DISCARD;

    ivec2 address = ivec2(fragCoord);
    fragColor = (iFrame == 0) ? vec4(0) : texelFetch(iChannel1, address, 0);
    
    accumulate_lightmap   (fragColor, address);
    generate_ui_textures  (fragColor, fragCoord);
    bake_font       (fragColor, fragCoord);
}
////////////////////////////////////////////////////////////////
// Buffer B: map/lightmap rendering
// - ray-tracing for the world brushes
// - ray-marching for the entities
// - GBuffer output
////////////////////////////////////////////////////////////////

#define RENDER_WORLD        3   // 0=off; 1=axial brushes; 2=non-axial brushes; 3=all
#define RENDER_ENTITIES       1
#define RENDER_WEAPON       1

#define USE_PARTITION       3   // 0=off; 1=axial brushes; 2=non-axial brushes; 3=all
#define USE_ENTITY_AABB       1

#define DEBUG_ENTITY_AABB     0

#define ENTITY_RAYMARCH_STEPS   96
#define ENTITY_RAYMARCH_TOLERANCE 1.0
#define ENTITY_LIGHT_DIR      normalize(vec3(.5, 1, -.5))
#define ENTITY_MIN_LIGHT      0.3125
#define DITHER_ENTITY_NORMALS   1

#define LIGHTMAP_FILTER       1   // 0=off; 1=linear, 1/16 UV increments; 2=linear
#define QUANTIZE_LIGHTMAP     24    // only when texture filtering is off; comment out to disable
#define QUANTIZE_DYNAMIC_LIGHTS   32    // only when texture filtering is off; comment out to disable

// Lightmap baking settings: changing these requires a restart
#define LIGHTMAP_HEIGHT_OFFSET    0.1
#define LIGHTMAP_EXTRAPOLATE    1.0   // max distance from brush edge, in texels

#define LIGHTMAP_SCALEDIST      1.0
#define LIGHTMAP_SCALECOS     0.5
#define LIGHTMAP_RANGESCALE     0.7

////////////////////////////////////////////////////////////////
// Implementation //////////////////////////////////////////////
////////////////////////////////////////////////////////////////

#define BAKE_LIGHTMAP       1
#define COMPRESSED_BRUSH_OFFSETS  1
#define SETTINGS_CHANNEL      iChannel0
#define NOISE_CHANNEL       iChannel2

float g_downscale = 2.;
float g_animTime = 0.;

vec4 load(vec2 address)
{
    return load(address, SETTINGS_CHANNEL);
}

////////////////////////////////////////////////////////////////
//
// World data
//
// Generated from a trimmed/tweaked version of
// the original map by John Romero
// https://rome.ro/news/2016/2/14/quake-map-sources-released
//
// Split between Buffer A and B to even out compilation time
////////////////////////////////////////////////////////////////

const vec3 AXIAL_MINS=vec3(48,176,-192), AXIAL_MAXS=vec3(1040,1424,336);

WRAP(AxialBrushes,axial_brushes,int,NUM_MAP_AXIAL_BRUSHES*2)(1073156,8464628,8431646,0xa8b822,8431622,0xa8b80a,8431854,0xa8b8f2,
8431830,0xa8b8da,8426512,0xa89c18,8426720,0xa89ce8,8439008,0xa8cce8,8438800,0xa8cc18,31483956,33630264,31484096,33630404,
31483960,33583296,8419328,25241604,8419572,25241848,0xc0003c,25168060,0xb07854,0xc088a4,8390660,0xc060f4,0xc02874,0xc44884,
0x998890,0xaa089c,0x99885c,0xaa0868,1134788,7430356,0x99886c,0xaa088c,1167556,8519892,8513700,9572592,1124376,7473180,75804,
1183984,1140816,0xe18858,1181720,7477308,1183804,9574562,8493060,0xa1b808,1151088,0x99a088,1171568,0x99f088,1126508,7422092,
7415900,8472732,7415872,8523852,7415816,8523836,1183906,9574640,1157280,0xe1c8a4,3745880,3805344,5318820,5378288,3221532,3280976
,29435992,29968544,1124432,25249884,34154584,34687136,1124588,34687220,29435916,34687064,29436064,34687212,1124508,25249960,
8464384,34686988,29960280,34162848,1132624,28403800,8472580,28416008,23160912,28411992,23173124,28424200,0xe1a8a2,23185572,
1132704,33663140,23177376,33671332,0xa19800,23181316,0xe16850,23169110,1149008,28446808,8501252,32663560,1132784,33712372,
1165472,33689764,0xa2109c,25309352,0xa21050,25309276,9572360,29503500,0xa210ec,29503728,29495304,33698032,0xa23054,32139352,
8521736,9594940,0xa230a0,33712292,19028166,20078802,0xb258c6,0xc260d2,0xb258c0,20078790,0xb258d2,20078808,0xb25870,0xc26088,
19028080,20078728,0xc25870,19030134,0xb25834,20078650,0xc25882,19030152,0xb25828,0xc26034,19028008,20078644,0xb25822,20078632,
0xc25c28,19030068,0xc25c76,19030146,0xc25cc6,19030226,0xa26808,29520034,0xa268a4,33714416));

WRAP(Brushes,brushes,int,NUM_MAP_PACKED_BRUSH_OFFSETS)(18822,805254,1591686,2378118,3164550,3950982,4737414,5522822,6241605,
6931846,7685510,8439174,9160005,9913734,0xa23d45,0xac4966,0xb7c565,0xc35186,0xcfbd45,0xd9c166,0xe43d45,0xeebd45,0xf894a5));

#define M(x)  x*0x111111,
#define M2(x) M(x)M(x)
#define M4(x) M2(x)M2(x)
#define M8(x) M4(x)M4(x)

const int NUM_MATERIAL_ENTRIES = 162;
WRAP(Materials,materials,int,NUM_MATERIAL_ENTRIES)(1315089,M8(2)M2(0)M(0)M2(1)M(1)M2(6)M(10)M4(0)M2(0)M4(1)M2(1)M2(3)M(3)M2(4)M(
4)M(5)1118485,M(11)M(12)M(13)M8(0)61440,M4(1)5592549,1118485,1118485,M(9)1118494,M2(1)M(5)1118485,M4(0)M(0)M(1)M(4)1118485,M8(0)
M4(0)M2(8)M(8)M(1)M(5)M8(2)M8(2)M4(6)M4(0)M(0)M4(3)M(3)M(0)17<<16,M(1)7829265,7,3355440,M(3)7829363,M(0)M2(3)489335,1<<22,M(4)
1328196,M2(1)M(1)4473921,68,M8(0)1118464,4473921,5592388,M(5)M4(0)M(7)M2(0)M(0)M2(7)1911,0));

// Geometry partitions /////////////////////////////////////////

const int
    NUM_AXIAL_NODES = 10,
    NUM_NONAXIAL_NODES = 10
;

struct PackedNode
{
    int begin;
    int end;
};
    
const struct BVL
{
    PackedNode axial[NUM_AXIAL_NODES];
    PackedNode nonaxial[NUM_NONAXIAL_NODES];
}
bvl = BVL
(
  #define N PackedNode

  N[10](N(687940864,857812285),N(855714048,991967529),N(301999361,688800572),N(17306113,0x90b1a3d),N(68609,17311037),N(
  989931777,0x3f204d3d),N(289<<19,304096574),N(0x3f084102,0x47204d3c),N(0x470b4b08,0x56134c36),N(0x560a4d02,0x58204e3c)),

  N[10](N(721429511,856174140),N(856041751,0x3c0a3937),N(0x4c132524,0x531b3c3d),N(461825,336403005),N(0x41162501,
  0x4c1e4d28),N(587663104,656286989),N(654772016,723395901),N(0x3c003902,0x41094d3c),N(336003073,589238333),N(0x53163d24,
  0x591b4d3b))

    #undef N
);

const vec3
  AXIAL_BVL_MINS      = vec3(48,176,-192),
  NONAXIAL_BVL_MINS   = vec3(48,176,-176);

struct Node
{
    vec3 mins;
    int begin;
    vec3 maxs;
    int end;
};
    
ivec3 unpack888(int n)
{
    return (ivec3(n) >> ivec3(0,8,16)) & 255;
}

Node unpack(const PackedNode p, vec3 bias)
{
    Node n;
    n.mins  = vec3(unpack888(p.begin)) * 16. + bias;
    n.begin = int(uint(p.begin) >> 24);
    n.maxs  = vec3(unpack888(p.end)) * 16. + bias;
    n.end = int(uint(p.end >> 24));
    return n;
}

// Lightmap UV-mapping /////////////////////////////////////////

const float LIGHTMAP_SCALE = 16.;
const vec2 LIGHTMAP_OFFSET = vec2(-169.5,-260.5);
#define _ -1
WRAP(LightmapOffsets,LIGHTMAP_OFFSETS,int,NUM_MAP_PLANES)(_,_,121456,_,145069,_,6702,2603,4629,6677,24073,_,8747,_,6169,12800,
22534,_,_,12831,12870,12873,25655,_,2606,4654,8774,10822,20096,_,12843,_,4120,6168,19465,_,_,8238,10316,12364,22592,_,_,16415,
16442,16448,22592,_,16427,_,16405,4124,25609,_,21180,22716,_,63045,57526,86682,18108,19644,_,21639,74457,52412,_,_,106628,115243
,123991,123956,75844,_,_,_,_,_,_,75867,_,_,_,_,_,_,104492,_,_,_,7190,2588,78447,_,78953,_,_,_,120944,_,139376,_,_,_,25763,_,
16450,_,_,64101,18491,2141,56466,47767,64067,_,7333,8869,47758,65157,7772,22100,22107,_,_,_,_,64084,29749,29758,56466,65170,
20087,24183,_,15974,_,_,_,_,_,76910,100930,100970,72404,_,_,_,_,_,_,_,_,_,135347,_,121398,32399,_,_,56335,_,11379,_,_,6747,_,_,_
,_,_,41142,_,_,20040,_,_,_,100912,_,41528,3205,3185,41572,_,_,3218,24721,41565,1710,_,_,_,_,33369,_,_,_,_,_,52318,_,79488,79471,
94282,95306,_,_,119823,119827,76500,_,_,_,119827,119841,_,_,_,1743,_,_,39575,46743,_,_,60585,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_
,_,_,_,87092,87111,_,82502,8921,19127,72316,72304,_,_,_,_,89121,75861,82521,_,_,73433,56551,39655,106038,_,_,_,72300,72280,85572
,85552,_,_,69265,72277,85569,85629,32435,19122,72327,72331,_,_,38078,_,35978,24720,46624,_,_,_,66175,66156,_,_,22749,68806,89615
,_,_,_,10427,_,35970,_,_,_,29274,32842,_,_,_,148495,32838,_,_,_,_,53323,22651,27259,_,_,_,_,89784,89771,89727,_,50815,_,35471,
138298,_,_,87586,101952,29845,_,_,_,_,_,36467,31859,_,_,_,_,55008,68832,_,68772,_,_,75966,_,_,24717,_,_,_,89821,_,_,_,_,89764,
89774,_,89729,_,_,45230,102037,45190,29832,_,_,16057,45235,37501,29821,_,_,76360,_,52865,52863,_,_,_,43209,43174,14004,_,_,_,_,
128056,125496,_,152750,64717,76884,_,_,_,_,1216,_,_,77857,54379,_,76900,76892,_,_,_,_,_,_,_,6246,47215,80432,_,_,_,35906,80459,_
,7820,51789,_,21609,69163,_,_,46303,_,46639,105665,_,_,_,_,3185,80456,_,_,_,_,39487,_,80465,49404,_,_,12391,_,_,_,10410,_,12358,
_,_,_,32508,_,9297,_,_,_,_,_,79373,54282,_,_,_,_,15881,_,55314,29853,_,_,16961,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,
109639,158323,_,_,_,_,76929,_,_,_,7708,14929,12877,28355,29891,_,_,8786,10834,29245,29249,132145,20116,3130,5175,23087,96365,
32876,27716,_,23078,3189,23082,54845,38970,23073,15934,37994,28733,48696,29251,23065,4160,23121,37473,16950,_,_,7686,27666,29696
,10303,_,7689,_,32265,32268,_,_,3105,8733,32774,36352,6184,_,14878,16926,36369,36373,43047,25614,7226,11319,21038,_,21121,28163,
13370,21045,4224,28674,32827,32793,28745,21050,17472,26653,_,5178,_,2182,33341,33350,45096,25714,8251,13371,37889,96769,_,17983,
18483,_,35916,21584,77957,_,122411,_,65562,2140,94293,_,144951,_,20585,21609,_,_,53945,_,35881,79379,77468,_,114816,_,11868,
110103,_,_,57465,57478,45702,44166,_,_,52822,57864,126985,76294,_,_,_,_,30328,23672,13440,13938,_,_,17000,110602,57412,57399,_,_
,36483,133162,_,_,116276,_,119383,118399,_,32403,_,_,12983,42156,38513,_,_,_,23145,38505,52368,_,_,_,42107,12928,30868,_,_,_,
23142,23154,_,_,16450,30762,29224,_,_,22038,18969,22033,_,_,_,_,52864,_,_,_,_,63615,_,_,_,133177,116349,_,_,43606,48214,81414,
82950,56926,_,_,_,51806,36452,22692,_,_,_,42095,22651,_,_,_,104506,104493,_,_,57413,57426,79926,24218,_,57391,_,_,36513,44184,_,
32415,_,_,42159,42162,_,_,_,104581,104567,28727,52277,78997,75925,19513,28720,_,_,_,33387,42086,7735,_,_,_,66719,30826,64066,_,_
,_,14935,3675,_,_,_,18519,7259,_,_,_,_,18011,_,_,_,_,90,_,_,50742,_,82503,82485,22571,_,36427,_,_,43618,57949,36418,_,_,_,34389,
38997,_,_,_,_,39011,34403,_,53819,53815,12492,8252,_,45125,45121,2112,13884,_,96302,52816,13878,13874,_,53838,55856,34378,9282,_
,31802,38955,3639,103471,_,59004,146045,9260,1095,_,94293,89683,89698,1126,22675,_,_,_,_,21120,_,_,_,113221,158894,158835,78355,
_,_,_,_,22133,_,_,_,_,7801,52761,52771,_,_,33331,27218,_,_,28758,25686,_,45654,_,_,49255,49245,75815,48231,47685,47675,_,_,33380
,22127,_,_,66737,_,104607,87199,_,_,76344,76367,97396,_,144920,47200,_,47651,47641,_,51275,_,47631,47621,_,58443,_,_,_,_,141895,
_,_,_,91305,77993,_,_,_,53405,53416,44652,41580,_,_,33356,16978,_,50798,42057,79430,66629,_,39504,39513,63074,66609,_,39488,
37440,64594,67671,_,40009,40018,62560,64603,_,40025,50777,48241,64606,_,38522,35450,_,48762,52830,52820,_,_,33370,19567,_,50767,
40034,80965,72236,_,36972,_,54880,62041,_,_,36964,50797,58470,_,11396,35933,51804,48220,_,52824,52814,_,61050));
#undef _

////////////////////////////////////////////////////////////////

uvec3 unpack(int data, const ivec3 bits)
{
    uvec3 mask = (uvec3(1) << bits) - 1u;
  uvec3 shift = uvec3(0, bits.x, bits.x + bits.y);
    return (uvec3(data) >> shift) & mask;
}

vec3 get_axial_point(int index)
{
    return vec3(unpack(axial_brushes.data[index], ivec3(9))) * 4. + AXIAL_MINS;
}

int get_nonaxial_brush_start(int index)
{
#if COMPRESSED_BRUSH_OFFSETS
    int data = brushes.data[index >> 2], base = data >> 15;
    return base + (((data << 5) >> (uint(index & 3) * 5u)) & 31);
#else
    return brushes.data[index];
#endif
}

vec4 get_nonaxial_plane(int index)
{
    ivec2 addr = ivec2(ADDR_RANGE_NONAXIAL_PLANES.xy) + ivec2(index&127,index>>7);
    return texelFetch(SETTINGS_CHANNEL, addr, 0);
}

vec4 get_plane(int index)
{
    vec4 plane;
    
    if (index < NUM_MAP_AXIAL_PLANES)
    {
        uint
            brush = uint(index) / 6u,
          side = uint(index) % 6u,
          axis = side >> 1,
          front = side & 1u;
        vec3 p = get_axial_point(int(brush * 2u + (front ^ 1u)));
        plane = vec4(0);
        plane[axis] = front == 1u ? -1. : 1.;
        plane.w = -p[axis] * plane[axis];
    }
    else
    {
        plane = get_nonaxial_plane(index - NUM_MAP_AXIAL_PLANES);
    }
    
    return plane;
}

int get_plane_material(mediump int plane_index)
{
    // An encoding that only needs bit shifts/masks to decode
    // (like 4x8b, 4x4b or 8x4b) would be a much smarter choice.
    // On the other hand, 6x4b leads to a shorter textual encoding
    // for the array (and a slower decoding sequence).
    // Let's pretend character count matters and go with 6x4b...
    
    // Side note: we can use just 4 bits for each plane material id
    // because the material list is sorted by frequency of use
    // before the id's are assigned, and it just so happens that the map
    // currently uses only 16 materials. If we wanted to add just one more
    // we'd have to bump up the number of bits per face to 5.
    
    // better division/modulo codegen for unsigned ints:
    // http://shader-playground.timjones.io/a05d99ce3e6e1ae57ef111c8323e52d2
    mediump uint index = uint(plane_index);
    
    mediump uint unit_index = index / 6u;
    lowp uint bit_index = index % 6u;
    bit_index <<= 2;
    int code = uint(unit_index) < uint(NUM_MATERIAL_ENTRIES) ? materials.data[uint(unit_index)] : 0;

    return 15 & (code >> bit_index);
}

int get_axial_brush_material(int brush, lowp int side)
{
    // Since we chose 6x4b and axial brushes happen to have 6 sides, the math gets easier
    int code = uint(brush) < uint(NUM_MATERIAL_ENTRIES) ? materials.data[uint(brush)] : 0;
    return 15 & (code >> (side << 2));
}

// Axial UV mapping ////////////////////////////////////////////

vec2 uv_map_axial(vec3 pos, int axis)
{
    return (axis==0) ? pos.yz : (axis==1) ? pos.xz : pos.xy;
}

vec3 uv_unmap(vec2 uv, vec4 plane, int axis)
{
    switch (axis)
    {
        case 0: return vec3(-(plane.w + dot(plane.yz, uv)) / plane.x, uv.x, uv.y);
        case 1: return vec3(uv.x, -(plane.w + dot(plane.xz, uv)) / plane.y, uv.y);
        case 2: return vec3(uv.x, uv.y, -(plane.w + dot(plane.xy, uv)) / plane.z);
        default:
          return vec3(0);
    }
}

vec3 uv_unmap(vec2 uv, vec4 plane)
{
    return uv_unmap(uv, plane, dominant_axis(plane.xyz));
}

///////////////////////////////////////////////////////////////

vec2 sdf_union(vec2 a, vec2 b)
{
    return (a.x < b.x) ? a : b;
}

float sdf_ellipsoid(vec3 p, vec3 r)
{
    return (length(p/r) - 1.) / min3(r.x, r.y, r.z);
}

float sdf_sphere(vec3 p, float r)
{
    return length(p) - r;
}

float sdf_box(vec3 p, vec3 center, vec3 half_bound)
{
    p = abs(p - center) - half_bound;
    return max3(p.x, p.y, p.z);
}

// iq
float sdf_round_box(vec3 p, vec3 b, float r)
{
    return length(max(abs(p) - b, 0.)) - r;
}

float sdf_torus(vec3 p, vec2 t)
{
    vec2 q = vec2(length(p.xz)-t.x,p.y);
    return length(q)-t.y;
}

float sdRoundCone( vec3 p, float r1, float r2, float h )
{
    vec2 q = vec2( length(p.xy), p.z );
    
    float b = (r1-r2)/h;
    float a = sqrt(1.0-b*b);
    float k = dot(q,vec2(-b,a));
    
    if( k < 0.0 ) return length(q) - r1;
    if( k > a*h ) return length(q-vec2(0.0,h)) - r2;
        
    return dot(q, vec2(a,b) ) - r1;
}

// http://mercury.sexy/hg_sdf
// Cone with correct distances to tip and base circle. Z is up, 0 is in the middle of the base.
float sdf_cone(vec3 p, float radius, float height) {
  vec2 q = vec2(length(p.xy), p.z);
  vec2 tip = q - vec2(0, height);
  vec2 mantleDir = normalize(vec2(height, radius));
  float mantle = dot(tip, mantleDir);
  float d = max(mantle, -q.y);
  float projected = dot(tip, vec2(mantleDir.y, -mantleDir.x));
  
  // distance to tip
  if ((q.y > height) && (projected < 0.)) {
    d = max(d, length(tip));
  }
  
  // distance to base ring
  if ((q.x > radius) && (projected > length(vec2(height, radius)))) {
    d = max(d, length(q - vec2(radius, 0)));
  }
  return d;
}

float fOpIntersectionRound(float a, float b, float r) {
  vec2 u = max(vec2(r + a,r + b), vec2(0));
  return min(-r, max (a, b)) + length(u);
}

float fOpDifferenceRound(float a, float b, float r) {
  return fOpIntersectionRound(a, -b, r);
}

float sdf_capsule(vec3 p, vec3 a, vec3 b, float radius)
{
    vec3 ab = b - a;
    vec3 ap = p - a;
    float t = clamp(dot(ap, ab) / dot(ab, ab), 0., 1.);
    return sdf_sphere(p - mix(a, b, t), radius);
}

// this is not quite right
// see https://www.shadertoy.com/view/4lcBWn for a correct solution
float sdf_capsule(vec3 p, vec3 a, vec3 b, float radius_a, float radius_b)
{
    vec3 ab = b - a;
    vec3 ap = p - a;
    float t = clamp(dot(ap, ab) / dot(ab, ab), 0., 1.);
    return sdf_sphere(p - mix(a, b, t), mix(radius_a, radius_b, t));
}

////////////////////////////////////////////////////////////////

struct Closest
{
    vec3 point;
    float distance_squared;
};

Closest init_closest(vec3 p, vec3 first)
{
    return Closest(first, length_squared(p - first));
}

void update_closest(vec3 p, inout Closest closest, vec3 candidate)
{
    float distance_squared = length_squared(p - candidate);
    if (distance_squared < closest.distance_squared)
    {
        closest.distance_squared = distance_squared;
        closest.point = candidate;
    }
}

#define FIND_CLOSEST(point, closest, vecs, len) \
  closest = init_closest(point, vecs[0]);   \
    for (int i=1; i<len; ++i)         \
        update_closest(p, closest, vecs[i])   \

////////////////////////////////////////////////////////////////

float overshoot(float x, float amount)
{
    amount *= .5;
    return x + amount + amount*sin(x * TAU + -PI/2.);
}

float fast_sqrt(float x2)
{
    return x2 * inversesqrt(x2);
}

vec2 bounding_sphere(const float distance_squared, const float radius, const int material)
{
    return vec2(fast_sqrt(distance_squared)+-radius, material);
}

bool ray_vs_aabb(vec3 ray_origin, vec3 rcp_ray_delta, float max_t, vec3 aabb_mins, vec3 aabb_maxs)
{
    vec3 t0 = (aabb_mins - ray_origin) * rcp_ray_delta;
    vec3 t1 = (aabb_maxs - ray_origin) * rcp_ray_delta;
    vec4 tmin = vec4(min(t0, t1), 0.);
    vec4 tmax = vec4(max(t0, t1), max_t);
    return max_component(tmin) <= min_component(tmax);
}

bool ray_vs_aabb(vec3 ray_origin, vec3 rcp_ray_delta, vec3 aabb_mins, vec3 aabb_maxs)
{
    return ray_vs_aabb(ray_origin, rcp_ray_delta, 1., aabb_mins, aabb_maxs);
}

// Minimalistic quaternion support /////////////////////////////

struct Quaternion
{
    float s;
    vec3 v;
};

Quaternion axis_angle(vec3 axis, float angle)
{
    angle = radians(angle * .5);
    return Quaternion(cos(angle), sin(angle) * axis);
}

Quaternion mul(Quaternion lhs, Quaternion rhs)
{
    return Quaternion(lhs.s * rhs.s - dot(lhs.v, rhs.v), lhs.s * rhs.v + rhs.s * lhs.v + cross(lhs.v, rhs.v));
}

Quaternion conjugate(Quaternion q)
{
    return Quaternion(q.s, -q.v);
}

Quaternion euler_to_quat(vec3 angles)
{
    Quaternion
        q0 = axis_angle(vec3(0, 0, 1), angles.x),
        q1 = axis_angle(vec3(1, 0, 0), angles.y),
        q2 = axis_angle(vec3(0, 1, 0),-angles.z);
    return mul(q0, mul(q1, q2));
}

vec3 rotate(Quaternion q, vec3 v)
{
    // https://fgiesen.wordpress.com/2019/02/09/rotating-a-single-vector-using-a-quaternion/
    vec3 t = 2. * cross(q.v, v);
    return v + q.s * t + cross(q.v, t);
}

////////////////////////////////////////////////////////////////

const int
    NUM_TORCHES         = 3,
    NUM_LARGE_FLAMES      = 2,
    NUM_ZOMBIES         = 4,
    NUM_BALLOONS        = 14,
    NUM_BALLOON_SETS      = 3
;

const struct EntityPositions
{
    vec3 torches[NUM_TORCHES];
    vec3 large_flames[NUM_LARGE_FLAMES];
    vec3 zombies[NUM_ZOMBIES];
    vec3 balloons[NUM_BALLOONS];
    uint balloon_sets[NUM_BALLOON_SETS];
}
g_ent_pos = EntityPositions
(
  vec3[3](vec3(698,764,84),vec3(394,762,84),vec3(362,1034,20)),
  vec3[2](vec3(126,526,12),vec3(958,526,12)),
  vec3[4](vec3(1004,928,72),vec3(1004,1048,124),vec3(708,992,52),vec3(708,1116,120)),
  vec3[14](vec3(848,896,152),vec3(128,528,144),vec3(960,528,144),vec3(344,1064,168),vec3(440,1344,208),vec3(696,744,224),
             vec3(664,1056,136),vec3(984,1336,176),vec3(392,744,200),vec3(120,1336,208),vec3(104,1000,96),vec3(656,1328,216),
             vec3(472,1096,-112),vec3(416,936,112)),
  uint[3](0xa9865320u,0xdcba8510u,0x76543210u)
);

const uint
    ENTITY_BIT_TARGET     = 0u,
    ENTITY_BIT_VIEWMODEL    = uint(NUM_TARGETS),
    ENTITY_BIT_LARGE_FLAMES   = ENTITY_BIT_VIEWMODEL + 1u,
    ENTITY_BIT_TORCHES      = ENTITY_BIT_LARGE_FLAMES + 1u,
    ENTITY_BIT_FIREBALL     = ENTITY_BIT_TORCHES + 1u,

    ENTITY_MASK_TARGETS     = (1u << NUM_TARGETS) - 1u,
    ENTITY_MASK_LARGE_FLAMES  = (1u << NUM_LARGE_FLAMES) - 1u,
    ENTITY_MASK_TORCHES     = (1u << NUM_TORCHES) - 1u
;
    
struct FlameState
{
    float loop;
    vec2 sin_cos;
};

struct FireballState
{
    vec3 offset;
    Quaternion rotation;
};

struct TargetState
{
    uint indices;
    float scale;
};
    
struct ViewModelState
{
    vec3 offset;
    float attack;
    Quaternion rotation;
};

struct EntityState
{
    FlameState    flame;
    FireballState fireball;
    ViewModelState  viewmodel;
    TargetState   target;
    uint      mask;
};
    
EntityState g_entities;

void update_entity_state(vec3 camera_pos, vec3 camera_angles, vec3 direction, float depth, bool is_thumbnail)
{
    g_entities.mask = 0u;
    
    g_entities.flame.loop     = fract(floor(g_animTime * 10.) * .1);
    g_entities.flame.sin_cos    = vec2(sin(g_entities.flame.loop * TAU), cos(g_entities.flame.loop * TAU));
    g_entities.fireball.offset    = get_fireball_offset(g_animTime);
    g_entities.fireball.rotation  = axis_angle(normalize(vec3(1, 8, 4)), g_animTime * 360.);

    float base_fov_y = scale_fov(FOV, 9./16.);
    float fov_y = compute_fov(iResolution.xy).y;
    float fov_y_delta = base_fov_y - fov_y;

    vec3 velocity = load(ADDR_VELOCITY).xyz;
    Transitions transitions;
    LOAD(transitions);
    float offset = get_viewmodel_offset(velocity, transitions.bob_phase, transitions.attack);
    g_entities.viewmodel.offset   = camera_pos;
    g_entities.viewmodel.rotation = mul(euler_to_quat(camera_angles), axis_angle(vec3(1,0,0), fov_y_delta*.5));
    g_entities.viewmodel.offset   += rotate(g_entities.viewmodel.rotation, vec3(0,1,0)) * offset;
    g_entities.viewmodel.rotation = conjugate(g_entities.viewmodel.rotation);
    g_entities.viewmodel.attack   = linear_step(.875, 1., transitions.attack);
    
#if USE_ENTITY_AABB
    #define TEST_AABB(pos, rcp_delta, mins, maxs) ray_vs_aabb(pos, rcp_delta, mins, maxs)
#else
    #define TEST_AABB(pos, rcp_delta, mins, maxs) true
#endif
    
    Options options;
    LOAD(options);
    
    const vec3 VIEWMODEL_MINS = vec3(-1.25,       0, -8);
    const vec3 VIEWMODEL_MAXS = vec3( 1.25,      18, -4);
    vec3 viewmodel_ray_origin = vec3(    0, -offset,  0);
    vec3 viewmodel_ray_delta  = rotate(g_entities.viewmodel.rotation, direction);
    bool draw_viewmodel = is_demo_mode_enabled(is_thumbnail) ? (g_demo_scene & 1) == 0 : true;
    draw_viewmodel = draw_viewmodel && test_flag(options.flags, OPTION_FLAG_SHOW_WEAPON);
    if (draw_viewmodel && TEST_AABB(viewmodel_ray_origin, 1./viewmodel_ray_delta, VIEWMODEL_MINS, VIEWMODEL_MAXS))
        g_entities.mask |= 1u << ENTITY_BIT_VIEWMODEL;
    
    vec3 inv_world_ray_delta = 1./(direction*depth);

    const vec3 TORCH_MINS = vec3(-4, -4, -28);
  const vec3 TORCH_MAXS = vec3( 4,  4,  18);
    for (int i=0; i<NUM_TORCHES; ++i)
        if (TEST_AABB(camera_pos - g_ent_pos.torches[i], inv_world_ray_delta, TORCH_MINS, TORCH_MAXS))
            g_entities.mask |= (1u<<ENTITY_BIT_TORCHES) << i;
    
    const vec3 LARGE_FLAME_MINS = vec3(-10, -10, -18);
  const vec3 LARGE_FLAME_MAXS = vec3( 10,  10,  34);
    for (int i=0; i<NUM_LARGE_FLAMES; ++i)
        if (TEST_AABB(camera_pos - g_ent_pos.large_flames[i], inv_world_ray_delta, LARGE_FLAME_MINS, LARGE_FLAME_MAXS))
            g_entities.mask |= (1u<<ENTITY_BIT_LARGE_FLAMES) << i;
        
  const vec3 FIREBALL_MINS = vec3(-10);
  const vec3 FIREBALL_MAXS = vec3( 10);
    if (g_entities.fireball.offset.z > 8. &&
        TEST_AABB(camera_pos - FIREBALL_ORIGIN - g_entities.fireball.offset, inv_world_ray_delta, FIREBALL_MINS, FIREBALL_MAXS))
        g_entities.mask |= 1u << ENTITY_BIT_FIREBALL;

    GameState game_state;
    LOAD(game_state);
    g_entities.target.scale = 0.;
    g_entities.target.indices = 0u;
    if (abs(game_state.level) >= 1.)
    {
        vec2 scale_bias = game_state.level > 0. ? vec2(1, 0) : vec2(-1, 1);
        float fraction = linear_step(BALLOON_SCALEIN_TIME * .1, 0., fract(abs(game_state.level)));
        g_entities.target.scale = fraction * scale_bias.x + scale_bias.y;
        if (g_entities.target.scale > 1e-2)
        {
            float level = floor(abs(game_state.level));
            int set = int(fract(level * PHI + .15) * float(NUM_BALLOON_SETS));
            uint indices = g_ent_pos.balloon_sets[set];
            g_entities.target.scale = overshoot(g_entities.target.scale, .5);
          g_entities.target.indices = indices;
            
            vec3 BALLOON_MINS = vec3(-28, -28, -20) * g_entities.target.scale;
            vec3 BALLOON_MAXS = vec3( 28,  28,  64) * g_entities.target.scale;
            for (int i=0; i<NUM_TARGETS; ++i, indices>>=4)
            {
                Target target;
                LOADR(vec2(i, 0.), target);
                if (target.hits < ADDR_RANGE_SHOTGUN_PELLETS.z * .5)
                    if (TEST_AABB(camera_pos - g_ent_pos.balloons[indices & 15u], inv_world_ray_delta, BALLOON_MINS, BALLOON_MAXS))
                      g_entities.mask |= (1u << i);
            }
        }
    }
}

////////////////////////////////////////////////////////////////

vec2 map_torch_handle(vec3 p)
{
    p = rotate(p, 45.);
    float dist = sdf_box(p, vec3(0, 0, -17), vec3(1, 1, 10));
    dist = sdf_smin(dist, sdf_box(p, vec3(0, 0, -9), vec3(2, 2, 3)), 3.);
    vec2 wood = vec2(dist, MATERIAL_WIZWOOD1_5);
    dist = sdf_box(p, vec3(0, 0, p.z > -20.5 ? -14.5 : -26.5), vec3(1.25, 1.25, .75));
    return sdf_union(wood, vec2(dist, MATERIAL_WIZMET1_1));
}

vec2 map_flame(vec3 p)
{
    const float scale = 1.;
    p *= 1./scale;

    p.z += 6.;
    
    float loop = g_entities.flame.loop;
    float angle_jitter = hash(g_entities.flame.loop) * 360.;

    vec3 ofs = vec3(-.5, -.5, 0);
    vec3 p1 = rotate(p, angle_jitter + p.z * (360./16.)) + ofs;
    float dist = sdf_cone(p1, 2.5, 16.);

    ofs = vec3(-1, -1, -2);
    p1 = rotate(p, angle_jitter + 180. - p.z * (360./32.)) + ofs;
    dist = sdf_smin(dist, sdf_cone(p1, 1.75, 10.), 1.);
    
    dist = sdf_smin(dist, sdf_capsule(p, vec3(0, 0, 1), vec3(0, 0, 4), 2.5, 1.), 3.);

    mat2 loop_rotation = mat2(g_entities.flame.sin_cos.yxxy * vec4(1, 1, -1, 1));
    p1 = vec3(loop_rotation * p.xy, p.z - 2.);
    dist = sdf_union(dist, sdf_sphere(p1 - vec3( 2,  2, mix(8., 20., loop)), .25));
    dist = sdf_union(dist, sdf_sphere(p1 - vec3(-2,  1, mix(12., 22., fract(loop + .3))), .25));
    dist = sdf_union(dist, sdf_sphere(p1 - vec3(-1, -2, mix(10., 16., fract(loop + .6))), .25));

    return vec2(dist*scale, MATERIAL_FLAME);
}

vec2 map_large_flame(vec3 p)
{
    const float scale = 2.;
    p *= 1./scale;
    p.z += 6.;

    float loop = g_entities.flame.loop;
    float angle_jitter = hash(g_entities.flame.loop) * 360.;

    vec3 ofs = vec3(-.5, -.5, 0.);
    vec3 p1 = rotate(p, angle_jitter + p.z * (360./16.)) + ofs;
    float dist = sdf_cone(p1, 2., 14.);
    
    ofs = vec3(1., 1., 0.);
    p1 = rotate(p, angle_jitter + p.z * (360./32.)) + ofs;
    dist = sdf_smin(dist, sdf_cone(p1, 2., 10.), .25);

    ofs = vec3(-.75, -.75, -1.5);
    p1 = rotate(p, angle_jitter + 180. - p.z * (360./32.)) + ofs;
    dist = sdf_smin(dist, sdf_cone(p1, 2., 10.), .25);

    dist = sdf_smin(dist, sdf_capsule(p, vec3(0, 0, 1), vec3(0, 0, 5), 3.25, 1.5), 2.);

    mat2 loop_rotation = mat2(g_entities.flame.sin_cos.yxxy * vec4(1, 1, -1, 1));
    p1 = vec3(loop_rotation * p.xy, p.z);
    dist = sdf_union(dist, sdf_sphere(p1 - vec3( 2, 2, mix(8., 20., loop)), .25));
    dist = sdf_union(dist, sdf_sphere(p1 - vec3(-2, 1, mix(12., 22., fract(loop + .3))), .25));
    dist = sdf_union(dist, sdf_sphere(p1 - vec3(-1,-2, mix(10., 16., fract(loop + .6))), .25));

    return vec2(dist*scale, MATERIAL_FLAME);
}

vec2 map_torch(vec3 p, vec3 origin)
{
    p -= origin;
    return sdf_union(map_torch_handle(p), map_flame(p));
}

vec2 map_fireball(vec3 p, vec3 origin)
{
    vec3 current_pos = origin + g_entities.fireball.offset;
    p -= current_pos;
    p = rotate(g_entities.fireball.rotation, p);
    float dist = sdf_sphere(p, 3.);
    dist = sdf_smin(dist, sdf_sphere(p - vec3(1.5, 1.5, 4), 4.), 3.);
    dist = sdf_smin(dist, sdf_sphere(p - vec3(2.5,-1.5, 3), 2.5), 3.);
    return vec2(dist, MATERIAL_LAVA1);
}

// very rough draft; work in progress
vec2 map_zombie(vec3 p)
{
    const vec3
        hip = vec3(2, 3, 2),
      knee = vec3(-1, 1.5, -9),
      ankle = vec3(4, 1.25, -21),
      toe1 = vec3(1.5, 1.6, -24),
      toe2 = vec3(1, 1.1, -24),
    
        spine1 = vec3(1.5, 0, 2),
        spine2 = vec3(1, 0, 13.5),

        shoulder = vec3(2, 6, 16),
        elbow = vec3(2, 14, 20),
        wrist = vec3(2, 22, 26),

        neck = vec3(1, 0, 18),
        head = vec3(-1.5, 0, 22),
        mouth = vec3(-1.5, 0, 20)
  ;
    
    vec3 mp = p;
    mp.y = abs(mp.y);

    float dist = sdf_capsule(mp, ankle, knee, 1., 1.5);
    dist = sdf_smin(dist, sdf_capsule(mp, knee, hip, 1.5, 2.), .05);
    dist = sdf_smin(dist, sdf_capsule(mp, ankle, toe1, 1., .5), .5);
  dist = sdf_smin(dist, sdf_capsule(mp, ankle, toe2, 1., .5), .5);
    
    dist = sdf_smin(dist, sdf_capsule(mp, shoulder, elbow, 1.3, 1.2), 2.);
    dist = sdf_smin(dist, sdf_capsule(mp, elbow, wrist, 1.2, .9), .5);

    dist = sdf_smin(dist, sdf_round_box(p - spine1, vec3(.25, 3., 3.), .25), 1.5);
    dist = sdf_smin(dist, sdf_capsule(p, spine1, spine2, 1.), 4.);
    dist = sdf_smin(dist, sdf_round_box(p - spine2, vec3(.75, 2.5, 2.5), 1.25), 4.);

    dist = sdf_smin(dist, sdf_capsule(p, neck, head, 1.5, 1.1), 1.);
    dist = sdf_smin(dist, sdf_sphere(p - head, 2.5), 2.);
    dist = sdf_smin(dist, sdf_round_box(p - mouth, vec3(.5, .5, .5), 1.), 1.);

    //return vec2(dist, MATERIAL_COP3_4);
    return vec2(dist, MATERIAL_ZOMBIE);
}

vec2 map_viewmodel(vec3 p)
{
    p -= g_entities.viewmodel.offset;
    float sq_dist = length_squared(p);
    if (sq_dist > sqr(32.))
        return bounding_sphere(sq_dist, 24., MATERIAL_SHOTGUN_BARREL);
    
    p = rotate(g_entities.viewmodel.rotation, p);
    
    const vec3
        BARREL_0  = vec3(0,    4, -4.375),
      BARREL_1  = vec3(0, 13.5, -4.375),
      FLASH_0   = vec3(0, 14.6, -4.375),
      FLASH_1   = vec3(0, 16.1, -4.375),
      BODY_0    = vec3(0,    2, -4.7),
      BODY_1    = vec3(0,  7.5, -4.7),
        INDENT    = vec3(0,  7.5, -4.7),
    PUMP_0    = vec3(0,    9, -6.),
      PUMP_1    = vec3(0, 12.9, -6.),
        PUMP_GROOVE = vec3(0, 12.7, -6.)
  ;
    
    vec2 body = vec2(sdf_capsule(p, BARREL_0, BARREL_1, .5), MATERIAL_SHOTGUN_BARREL);
    body.x = sdf_smin(body.x, sdf_capsule(p, BODY_0, BODY_1, .875), .05);
    body.x = sdf_smin(body.x, sdf_torus(p - INDENT, vec2(.7, .3)), .05);
    
    float attack = g_entities.viewmodel.attack;
    if (attack > 0.)
        body = sdf_union(body, vec2(sdf_capsule(p, FLASH_0, FLASH_1, .6, .2), MATERIAL_SHOTGUN_FLASH));

    const float GROOVE_SPACING = .675;
    vec2 pump = vec2(sdf_capsule(p, PUMP_0, PUMP_1, 1.4), MATERIAL_SHOTGUN_PUMP);
    p -= PUMP_GROOVE;
    p.y = fract(p.y * GROOVE_SPACING - .25) * (1./GROOVE_SPACING) - .5;
    pump.x = fOpDifferenceRound(pump.x, sdf_torus(p, vec2(1.3125, .375)), .125);
    
    return sdf_union(body, pump);
}

void add_targets(vec3 p, inout vec2 result)
{
    uint mask = g_entities.mask & ENTITY_MASK_TARGETS;
    if (mask == 0u)
        return;

    float best_sq_dist = 1e+8;
    int best_index = -1;
    int best_material = 0;
    uint indices = g_entities.target.indices;
    for (int i=0; i<NUM_TARGETS; ++i, mask>>=1, indices>>=4)
    {
        if ((mask & 1u) == 0u)
            continue;
        int index = int(indices & 15u);
        float sq_dist = length_squared(p - g_ent_pos.balloons[index]);
        if (sq_dist < best_sq_dist)
        {
            best_sq_dist = sq_dist;
            best_index = index;
            best_material = i;
        }
    }

    best_material += BASE_TARGET_MATERIAL;
    if (best_sq_dist > sqr(64.))
    {
        result = sdf_union(result, bounding_sphere(best_sq_dist, 56., best_material));
        return;
    }
    
    vec3 target = g_ent_pos.balloons[best_index];
    target.z += 8. * sin(TAU * fract(g_animTime * .25 + dot(target.xy, vec2(1./137., 1./163.))));
    float scale = g_entities.target.scale;
    result = sdf_union(result, vec2(sdRoundCone(p - target, 8.*scale, 24.*scale, 28.*scale), best_material));
}

vec2 map_entities(vec3 p)
{
    // Finding the closest instance and only mapping it instead of the whole list (even for such tiny lists)
    // shaves about 4.9 seconds off the compilation time on my machine (~7.6 vs ~12.5)

    vec2 entities = vec2(1e+8, MATERIAL_SKY1);
    Closest closest;
    
    if (0u != (g_entities.mask & (ENTITY_MASK_TORCHES << ENTITY_BIT_TORCHES)))
    {
        FIND_CLOSEST(p, closest, g_ent_pos.torches, NUM_TORCHES);
        if (closest.distance_squared > sqr(40.))
            entities = bounding_sphere(closest.distance_squared, 32., MATERIAL_FLAME);
        else
            entities = map_torch(p, closest.point);
    }

    if (0u != (g_entities.mask & (ENTITY_MASK_LARGE_FLAMES << ENTITY_BIT_LARGE_FLAMES)))
    {
        FIND_CLOSEST(p, closest, g_ent_pos.large_flames, NUM_LARGE_FLAMES);
        if (closest.distance_squared > sqr(48.))
            entities = sdf_union(entities, bounding_sphere(closest.distance_squared, 40., MATERIAL_FLAME));
        else
            entities = sdf_union(entities, map_large_flame(p - closest.point));
    }
    
#if 0
    FIND_CLOSEST(p, closest, ZOMBIES);
  entities = sdf_union(entities, map_zombie(p - closest.point));
    //int num_zombies = NO_UNROLL(NUM_ZOMBIES);
    //for (int i=0; i<num_zombies; ++i)
    //    entities = sdf_union(entities, map_zombie(p - zombies[i]));
#endif
    
    if (0u != (g_entities.mask & (1u << ENTITY_BIT_FIREBALL)))
      entities = sdf_union(entities, map_fireball(p, FIREBALL_ORIGIN));

    add_targets(p, entities);

    #if RENDER_WEAPON
    {
        if ((g_entities.mask & (1u << ENTITY_BIT_VIEWMODEL)) != 0u)
        entities = sdf_union(entities, map_viewmodel(p));
    }
  #endif
    
    return entities;
}

vec3 estimate_entity_normal(vec3 p, float dist)
{
    const float EPSILON = 1e-3;
    vec3 normal = vec3(-dist);
    for (int i=NO_UNROLL(0); i<3; ++i)
    {
        vec3 p2 = p;
        p2[i] += EPSILON;
        normal[i] += map_entities(p2).x;
    }
    return normalize(normal);
}

////////////////////////////////////////////////////////////////

struct Intersection
{
    float t;
    vec3  normal;
    int   plane;
    int   material;
    int   uv_axis;
    bool  mips;
};

void reset_intersection(out Intersection result)
{
    result.t      = 1.;
    result.normal   = vec3(0.);
    result.plane    = -1;
    result.material   = -1;
    result.mips     = false;
    result.uv_axis    = 0;
}

void intersect_entities(vec3 campos, vec3 angles, vec3 dir, bool is_thumbnail, inout Intersection result)
{
#if RENDER_ENTITIES
    update_entity_state(campos, angles, dir, result.t, is_thumbnail);
    if (g_entities.mask == 0u)
        return;

    float t = 0.;
    float rcp_length = 1./length(dir);
    int max_steps = NO_UNROLL(ENTITY_RAYMARCH_STEPS);
    vec2 current = vec2(2, -1);
    float tolerance = 1e-4;
    float max_tolerance = ENTITY_RAYMARCH_TOLERANCE * VIEW_DISTANCE * FOV_FACTOR * .5 * g_downscale / iResolution[FOV_AXIS];
    for (int i=0; i<max_steps; ++i)
    {
        current = map_entities(campos + dir * t);
        tolerance = t*max_tolerance + .015;
        if (current.x < tolerance)
            break;
        t += current.x * rcp_length;
        if (t >= result.t)
            break;
    }
    
    if (t < result.t && t > 0. && current.x < tolerance)
    {
        vec3 hit_point = campos + dir * t;
        
        result.t      = t;
        result.material   = int(current.y);
        result.plane    = -1;
        result.normal   = estimate_entity_normal(hit_point, current.x);
        result.mips     = true;
        result.uv_axis    = 3;
    }
#if DEBUG_ENTITY_AABB
    else
    {
        result.material   = BASE_TARGET_MATERIAL;
    }
#endif // DEBUG_ENTITY_AABB
#endif // RENDER_ENTITIES
}

void intersect_axial_brushes
(
    int brush_begin, int brush_end,
    vec3 campos, vec3 rcp_delta, float znear,
    inout float best_dist, inout int best_index
)
{
#if RENDER_WORLD & 1
    brush_begin = brush_begin << 1;
    brush_end = NO_UNROLL(brush_end) << 1;
    for (int i=brush_begin; i<brush_end; i+=2)
    {
        vec3 mins = get_axial_point(i);
        vec3 maxs = get_axial_point(i+1);
        vec3 t0 = (mins - campos) * rcp_delta;
        vec3 t1 = (maxs - campos) * rcp_delta;
        vec3 tmin = min(t0, t1);
        vec4 tmax = vec4(max(t0, t1), best_dist);
        float t_enter = max_component(tmin);
        float t_exit = min_component(tmax);
        if (t_exit >= max(t_enter, 0.) && t_enter > znear)
        {
            best_dist = t_enter;
            best_index = i;
        }
    }
#endif // RENDER_WORLD & 1    
}

void resolve_axial_intersection(vec3 campos, vec3 rcp_delta, inout Intersection result, int best_index)
{
    if (best_index == -1)
        return;

    vec3 mins = get_axial_point(best_index);
    vec3 maxs = get_axial_point(best_index+1);
    vec3 t0 = (mins - campos) * rcp_delta;
    vec3 t1 = (maxs - campos) * rcp_delta;
    vec3 tmin = min(t0, t1);
    float t = max_component(tmin);
    int axis =
        (t == tmin.x) ? 0 :
      (t == tmin.y) ? 1 :
      2;
    bool side = rcp_delta[axis] > 0.;
    int face = (axis << 1) + int(side);

    result.plane    = (best_index + (best_index<<1)) + face;
    result.material   = get_axial_brush_material(best_index>>1, face);
    result.normal   = vec3(0);
    result.normal[axis] = side ? -1. : 1.;
    result.uv_axis    = axis;
    result.mips     = fwidth(float(result.plane)) < 1e-4;
}

void intersect_nonaxial_brushes
(
    int brush_begin, int brush_end,
    vec3 campos, vec3 dir, float znear, 
    inout float best_dist, inout int best_plane
)
{
#if RENDER_WORLD & 2
    if (brush_begin >= brush_end)
        return;
    brush_end = NO_UNROLL(brush_end);
    for (int i=brush_begin, first_plane=get_nonaxial_brush_start(i), last_plane; i<brush_end; ++i, first_plane=last_plane)
    {
        last_plane = get_nonaxial_brush_start(i + 1);
        int best_brush_plane = -1;
        float t_enter = -1e6;
        float t_leave = 1.;
        for (int j=first_plane; j<last_plane; ++j)
        {
            vec4 plane = get_nonaxial_plane(j);
            float dist = dot(plane.xyz, campos) + plane.w;
            float align = dot(plane.xyz, dir);
            if (align == 0.)
            {
                if (dist > 0.)
                {
                    t_enter = 2.;
                    break;
                }
                continue;
            }
            dist /= -align;
            best_brush_plane = (align < 0. && t_enter < dist) ? j : best_brush_plane;
            t_enter = (align < 0.) ? max(t_enter, dist) : t_enter;
            t_leave = (align > 0.) ? min(t_leave, dist) : t_leave;
            if (t_leave <= t_enter)
                break;
        }
        if (t_leave > max(t_enter, 0.) && t_enter > znear && best_dist > t_enter)
        {
            best_plane = best_brush_plane;
            best_dist = t_enter;
        }
    }
#endif // RENDER_WORLD & 2
}

void resolve_nonaxial_intersection(inout Intersection result, int best_index)
{
    if (best_index == -1)
        return;
    
    vec4 plane = get_nonaxial_plane(best_index);
    
    result.normal = plane.xyz;
    result.uv_axis = dominant_axis(plane.xyz);
    result.plane = best_index + NUM_MAP_AXIAL_PLANES;
    result.material = get_plane_material(result.plane);

    // pixel quad straddling geometric planes? no mipmaps for you!
    float plane_hash = dot(plane, vec4(17463.12, 25592.53, 15576.84, 19642.77));
    result.mips = fwidth(plane_hash) < 1e-4;
}

////////////////////////////////////////////////////////////////

void intersect_world(vec3 campos, vec3 dir, float znear, inout Intersection result)
{
    vec3 inv_world_dir = 1./dir;
    int best_index = -1;
    
    // axial brushes //
    
  #if USE_PARTITION & 1
    {
        for (int i=NO_UNROLL(0); i<NUM_AXIAL_NODES; ++i)
        {
            Node n = unpack(bvl.axial[i], AXIAL_BVL_MINS);
            if (ray_vs_aabb(campos, inv_world_dir, result.t, n.mins, n.maxs))
                intersect_axial_brushes(int(n.begin), int(n.end), campos, inv_world_dir, znear, result.t, best_index);
        }
    }
  #else
    {
      intersect_axial_brushes(0, NUM_MAP_AXIAL_BRUSHES, campos, inv_world_dir, znear, result.t, best_index);
    }
  #endif
    resolve_axial_intersection(campos, inv_world_dir, result, best_index);
    
    // non-axial brushes //
    
    best_index = -1;

  #if USE_PARTITION & 2
    {
        for (int i=NO_UNROLL(0); i<NUM_NONAXIAL_NODES; ++i)
        {
            Node n = unpack(bvl.nonaxial[i], NONAXIAL_BVL_MINS);
            if (ray_vs_aabb(campos, inv_world_dir, result.t, n.mins, n.maxs))
                intersect_nonaxial_brushes(int(n.begin), int(n.end), campos, dir, znear, result.t, best_index);
        }
    }
  #else
    {
      intersect_nonaxial_brushes(0, NUM_MAP_NONAXIAL_BRUSHES, campos, dir, znear, result.t, best_index);
    }
  #endif
    resolve_nonaxial_intersection(result, best_index);
}

////////////////////////////////////////////////////////////////

vec4 get_light(int index)
{
    ivec2 addr = ivec2(ADDR_RANGE_LIGHTS.xy);
    addr.x += index;
    return texelFetch(SETTINGS_CHANNEL, addr, 0);
}

vec4 get_lightmap_tile(int index)
{
    ivec2 addr = ivec2(ADDR_RANGE_LMAP_TILES.xy);
    addr.x += index & 127;
    addr.y += index >> 7;
    return texelFetch(SETTINGS_CHANNEL, addr, 0);
}

int find_tile(vec2 fragCoord, int num_tiles)
{
#if BAKE_LIGHTMAP
    for (int i=NO_UNROLL(0); i<num_tiles; ++i)
    if (is_inside(fragCoord, get_lightmap_tile(i)) > 0.)
            return i;
#endif
    return -1;
}

ivec2 get_brush_and_side(int plane_index)
{
    if (plane_index < NUM_MAP_AXIAL_PLANES)
        return ivec2(uint(plane_index) / 6u, uint(plane_index) % 6u);
    
    plane_index -= NUM_MAP_AXIAL_PLANES;
  
    #define TEST(dist) (int((get_nonaxial_brush_start(brush + (dist)) <= plane_index)) * (dist))
    
  int brush = 0;
    brush  = TEST(NUM_MAP_NONAXIAL_BRUSHES-64);
    brush += TEST(32);
    brush += TEST(16);
    brush += TEST(8);
    brush += TEST(4);
    brush += TEST(2);
    brush += TEST(1);

    #undef TEST

    return ivec2(brush + NUM_MAP_AXIAL_BRUSHES, plane_index - get_nonaxial_brush_start(brush));
}

float find_edge_distance(vec3 p, int brush, int side)
{
    float dist = -1e8;
    
    if (brush < NUM_MAP_AXIAL_BRUSHES)
    {
        vec3[2] deltas;
        deltas[0] = get_axial_point(brush*2) - p;
        deltas[1] = p - get_axial_point(brush*2+1);
        int axis = side >> 1;
        int front = side & 1;
        for (int i=0; i<6; ++i)
            if (i != side)
              dist = max(dist, deltas[1&~i][i>>1]);
    }
    else
    {
        int begin = get_nonaxial_brush_start(brush - NUM_MAP_AXIAL_BRUSHES);
        int end = get_nonaxial_brush_start(brush - (NUM_MAP_AXIAL_BRUSHES - 1));
        for (int i=begin; i<end; ++i)
        {
            if (i == begin + side)
                continue;
            vec4 plane = get_nonaxial_plane(i);
            dist = max(dist, dot(p, plane.xyz) + plane.w);
        }
    }
    
    return dist;
}

vec2 get_lightmap_offset(int plane_index)
{
  const int NUM_BITS = 9, MASK = (1 << NUM_BITS) - 1;
  int packed_offset = LIGHTMAP_OFFSETS.data[plane_index];
    return (packed_offset >= 0) ?
        LIGHTMAP_OFFSET + vec2(packed_offset & MASK, packed_offset >> NUM_BITS) :
        LIGHTMAP_OFFSET + vec2(-1);
}

float fetch_lightmap_texel(ivec2 addr)
{
    addr = clamp(addr, ivec2(0), ivec2(LIGHTMAP_SIZE) - 1);
    int channel = addr.y & 3;
    addr.y >>= 2;
    return decode_lightmap_sample(texelFetch(iChannel1, addr, 0)).values[channel];
}

float sample_lightmap(vec3 camera_pos, vec3 dir, Options options, Intersection result)
{
    if (result.uv_axis == 3)
        return clamp(-dot(result.normal, ENTITY_LIGHT_DIR), ENTITY_MIN_LIGHT, 1.);
    if (result.plane == -1)
        return 1.;

    float unmapped_light = is_material_any_of(result.material, MATERIAL_MASK_LIQUID|MATERIAL_MASK_SKY) ? 1. : 0.;
    vec3 point = camera_pos + dir * result.t;
    vec2 offset = get_lightmap_offset(result.plane);
    if (any(lessThan(offset, LIGHTMAP_OFFSET)))
        return unmapped_light;

    vec2 uv = uv_map_axial(point, result.uv_axis);
#if LIGHTMAP_FILTER == 1 // snap to world texels
    if (!test_flag(options.flags, OPTION_FLAG_TEXTURE_FILTER))
      uv = floor(uv) + .5;
#endif
    uv = uv / LIGHTMAP_SCALE - offset;
    
#if LIGHTMAP_FILTER > 0
    uv -= .5;
#endif
    
    vec2 base = floor(uv);
    ivec2 addr = ivec2(base);
    if (uint(addr.x) >= LIGHTMAP_SIZE.x || uint(addr.y) >= LIGHTMAP_SIZE.y)
        return unmapped_light;
    
    uv -= base;
#if !LIGHTMAP_FILTER
    uv = vec2(0);
#endif
    
    float
        s00 = fetch_lightmap_texel(addr + ivec2(0,0)),
        s01 = fetch_lightmap_texel(addr + ivec2(0,1)),
        s10 = fetch_lightmap_texel(addr + ivec2(1,0)),
        s11 = fetch_lightmap_texel(addr + ivec2(1,1)),
        light = mix(mix(s00, s01, uv.y), mix(s10, s11, uv.y), uv.x);

#ifdef QUANTIZE_LIGHTMAP
    const float LEVELS = float(QUANTIZE_LIGHTMAP);
    if (!test_flag(options.flags, OPTION_FLAG_TEXTURE_FILTER) && g_demo_stage != DEMO_STAGE_LIGHTING)
        light = floor(light * LEVELS + .5) * (1./LEVELS);
#endif
    
    return light;
}

vec3 lightmap_to_world(vec2 fragCoord, int plane_index)
{
    fragCoord += get_lightmap_offset(plane_index);
    vec4 plane = get_plane(plane_index);
    return uv_unmap(fragCoord * LIGHTMAP_SCALE, plane) + plane.xyz * LIGHTMAP_HEIGHT_OFFSET;
}

float compute_light_atten(vec4 light, vec3 surface_point, vec3 surface_normal)
{
    vec3 light_dir = light.xyz - surface_point;
  float
        dist = length(light_dir) * LIGHTMAP_SCALEDIST,
      angle = mix(1., dot(surface_normal, normalize(light_dir)), LIGHTMAP_SCALECOS);
    return max(0., (light.w - dist) * angle * (LIGHTMAP_RANGESCALE / 255.));
}

// dynamic lights didn't seem to be taking normals into account
// (fireball light showing up on the right wall of the Normal hallway)
float compute_dynamic_light_atten(vec4 light, vec3 surface_point)
{
    vec3 light_dir = light.xyz - surface_point;
    float dist = length(light_dir);
    float radius = light.w;
    return clamp(1.-dist/abs(radius), 0., 1.) * sign(radius);
}

vec3 simulate_lightmap_distortion(vec3 surface_point)
{
    surface_point = floor(surface_point);
    surface_point *= 1./LIGHTMAP_SCALE;
    vec3 f = fract(surface_point + .5);
    return (surface_point + f - smoothen(f)) * LIGHTMAP_SCALE;
}

float sample_lighting(vec3 camera_pos, vec3 dir, Options options, Intersection result)
{
#if !BAKE_LIGHTMAP
    return 1.;
#endif

    Transitions transitions;
    LOAD(transitions);
    
    float lightmap = sample_lightmap(camera_pos, dir, options, result);
    
    float dynamic_lighting = 0.;
    vec3 surface_point = camera_pos + dir * result.t;
    surface_point = simulate_lightmap_distortion(surface_point);
    
    vec3 fireball_offset = get_fireball_offset(g_animTime);
    vec4 fireball_light = vec4(fireball_offset + FIREBALL_ORIGIN, 150);
    if (fireball_offset.z > 8.)
      dynamic_lighting += compute_dynamic_light_atten(fireball_light, surface_point);
    if (transitions.attack > .875)
        dynamic_lighting += compute_dynamic_light_atten(vec4(camera_pos, 200), surface_point);
    
#ifdef QUANTIZE_DYNAMIC_LIGHTS
    const float LEVELS = float(QUANTIZE_DYNAMIC_LIGHTS);
    if (!test_flag(options.flags, OPTION_FLAG_TEXTURE_FILTER))
      dynamic_lighting = floor(dynamic_lighting * LEVELS + .5) * (1./LEVELS);
#endif
    
    return lightmap + dynamic_lighting;
}

////////////////////////////////////////////////////////////////

void mainImage( out vec4 fragColor, vec2 fragCoord )
{
    ivec2 addr = ivec2(fragCoord);
    
    vec3 pos, dir, angles;
    float light_level = -1., znear = 0.;
    bool is_ground_sample = false;
    bool is_thumbnail = test_flag(int(load(ADDR_RESOLUTION).z), RESOLUTION_FLAG_THUMBNAIL);
    vec4 plane;

    Lighting lighting;
    LOAD(lighting);
    
    Options options;
    LOAD(options);

    Timing timing;
    LOAD(timing);
    g_animTime = timing.anim;
    
    // initial setup //

    bool baking = iFrame < NUM_LIGHTMAP_FRAMES;
    if (baking)
    {
        if (uint(addr.x) >= LIGHTMAP_SIZE.x || uint(addr.y) >= LIGHTMAP_SIZE.y/4u)
            DISCARD;
        fragColor = vec4(-1);
        
        int region = iFrame & 3;
        int frame = iFrame >> 2;
        addr.y += region * int(LIGHTMAP_SIZE.y/4u);
        vec2 lightmap_coord = vec2(addr);
        
        int plane_index = find_tile(lightmap_coord + .5, lighting.num_tiles);
        if (plane_index == -1)
            return;
        lightmap_coord += hammersley(frame % NUM_LIGHTMAP_SAMPLES, NUM_LIGHTMAP_SAMPLES);

        ivec2 brush_side = get_brush_and_side(plane_index);
        int brush = brush_side.x;
        int side = brush_side.y;

        pos = lightmap_to_world(lightmap_coord, plane_index);
        if (find_edge_distance(pos, brush, side) > LIGHTMAP_SCALE * LIGHTMAP_EXTRAPOLATE)
            return;

        plane = get_plane(plane_index);
        znear = -1e8;
    }
    else
    {
        g_downscale = get_downscale(options);
        is_ground_sample = all(equal(ivec2(fragCoord), ivec2(iResolution.xy)-1));
        vec2 actual_res = min(ceil(iResolution.xy / g_downscale * .125) * 8., iResolution.xy);
        if (max_component(fragCoord - .5 - actual_res) > 0. && !is_ground_sample)
          DISCARD;

        vec2 demo_coord = is_ground_sample ?
            iResolution.xy * vec2(.5, .25) / g_downscale :
          fragCoord;

        UPDATE_TIME(lighting);
        UPDATE_DEMO_STAGE(demo_coord, g_downscale, is_thumbnail);

        pos = load_camera_pos(SETTINGS_CHANNEL, is_thumbnail).xyz;
        angles = load_camera_angles(SETTINGS_CHANNEL, is_thumbnail).xyz;
        if (!is_ground_sample)
        {
            vec2 uv = (fragCoord * 2. * g_downscale - iResolution.xy) / iResolution.x;
            dir = unproject(uv) * VIEW_DISTANCE;
            dir = rotate(dir, angles);
        }
        else
        {
            dir = vec3(0, 0, -VIEW_DISTANCE);
            angles = vec3(0, -90, 0);
        }
    }

    // render loop //
    
    Intersection result;
    
#if !BAKE_LIGHTMAP
    lighting.num_lights = 0;
#endif

    int num_iter = baking ? lighting.num_lights : 1;
    for (int i=0; i<num_iter; ++i)
    {
        float contrib;
        if (baking)
        {
            vec4 light = get_light(i);
            contrib = compute_light_atten(light, pos, plane.xyz);
            if (contrib <= 0.)
                continue;
            dir = light.xyz - pos;
        }

        reset_intersection(result);
      intersect_world(pos, dir, znear, result);

        if (baking)
        {
            if (result.t >= 1. || is_material_liquid(result.material))
                light_level = max(light_level, 0.) + contrib;
            else if (result.t > 0.)
                light_level = max(light_level, 0.);
    }
    }

    if (!baking && !is_ground_sample)
      intersect_entities(pos, angles, dir, is_thumbnail, result);

    // output //

    if (baking)
    {
        fragColor.rgb = vec3(light_level);
    }
    else
    {
        if (result.material == -1)
        {
            result.t = 1.;
            result.material = MATERIAL_SKY1;
            result.normal = vec3(0, 0, -1);
            result.mips = true;
        }

        GBuffer g;
        g.normal  = result.normal;
        g.light   = sample_lighting(pos, dir, options, result);
        g.z     = result.t;
        g.material  = result.material;
        g.uv_axis = result.uv_axis;
        g.edge    = !result.mips;

        // In demo mode, we can have the shotgun model rendered at two different map locations
        // at the same time (during the stage transitions). We only take a single lightmap sample
        // for the ground point per frame, so to avoid harsh transitions between different light
        // levels, we just use a default light value for the weapon model if demo mode is enabled
        if (is_ground_sample && test_flag(int(load(ADDR_RESOLUTION).z), RESOLUTION_FLAG_THUMBNAIL))
            g.light = .5;
        
#if DITHER_ENTITY_NORMALS
        vec2 noise = result.plane == -1 ? fract(BLUE_NOISE(fragCoord).xy) : vec2(.5);
#else
        vec2 noise = vec2(.5);
#endif

        fragColor = gbuffer_pack(g, noise);
    }
}
////////////////////////////////////////////////////////////////
// Buffer A: persistent state handling
// - procedural texture generation
// - mipmap generation
// - player input/physics
// - partial map data serialization
// - perf stats
////////////////////////////////////////////////////////////////

// config.cfg //////////////////////////////////////////////////

#define INVERT_MOUSE      0
#define NOCLIP          0

#define MOVE_FORWARD_KEY1   KEY_W
#define MOVE_FORWARD_KEY2   KEY_UP
#define MOVE_FORWARD_KEY3   KEY_Z     // azerty
#define MOVE_LEFT_KEY1      KEY_A
#define MOVE_LEFT_KEY2      KEY_Q     // azerty
#define MOVE_BACKWARD_KEY1    KEY_S
#define MOVE_BACKWARD_KEY2    KEY_DOWN
#define MOVE_RIGHT_KEY1     KEY_D
#define MOVE_RIGHT_KEY2     unassigned
#define MOVE_UP_KEY1      KEY_SPACE
#define MOVE_UP_KEY2      unassigned
#define MOVE_DOWN_KEY1      KEY_C
#define MOVE_DOWN_KEY2      unassigned
#define RUN_KEY1        KEY_SHIFT
#define RUN_KEY2        unassigned
#define LOOK_LEFT_KEY1      KEY_LEFT
#define LOOK_LEFT_KEY2      unassigned
#define LOOK_RIGHT_KEY1     KEY_RIGHT
#define LOOK_RIGHT_KEY2     unassigned
#define LOOK_UP_KEY1      KEY_PGDN
#define LOOK_UP_KEY2      unassigned
#define LOOK_DOWN_KEY1      KEY_DELETE
#define LOOK_DOWN_KEY2      unassigned
#define CENTER_VIEW_KEY1    KEY_END
#define CENTER_VIEW_KEY2    unassigned
#define STRAFE_KEY1       KEY_ALT
#define STRAFE_KEY2       unassigned
#define RESPAWN_KEY1      KEY_BKSP
#define RESPAWN_KEY2      KEY_HOME
#define ATTACK_KEY1       KEY_E
#define ATTACK_KEY2       KEY_F

#define MENU_KEY1       KEY_ESC
#define MENU_KEY2       KEY_TAB

#define SHOW_PERF_STATS_KEY   KEY_P
#define TOGGLE_TEX_FILTER_KEY KEY_T
#define TOGGLE_LIGHT_SHAFTS_KEY KEY_L
#define TOGGLE_CRT_EFFECT_KEY KEY_V

const float
  SENSITIVITY         = 1.0,
  MOUSE_FILTER        = 0.0,    // mostly for video recording
  TURN_SPEED          = 180.0,  // keyboard turning rate, in degrees per second
  WALK_SPEED          = 400.0,
  JUMP_SPEED          = 270.0,
    STAIR_CLIMB_SPEED     = 128.0,
    STOP_SPEED          = 100.0,
    
    GROUND_ACCELERATION     = 10.0,
    AIR_ACCELERATION      = 1.0,

    GROUND_FRICTION       = 4.0,
  NOCLIP_START_FRICTION   = 18.0,
  NOCLIP_STOP_FRICTION    = 12.0,

    ROLL_ANGLE          = 2.0,    // maximum roll angle when moving sideways
  ROLL_SPEED          = 200.0,  // sideways speed at which the roll angle reaches its maximum
  BOB_CYCLE         = 0.6,    // seconds
  BOB_SCALE         = 0.02,

    AUTOPITCH_DELAY       = 2.0,    // seconds between last mouse look and automatic pitch adjustment
    STAIRS_PITCH        = 10.0,

    RECOIL_ANGLE        = 2.0,
    WEAPON_SPREAD       = 0.05,   // slightly higher than in Quake, for dramatic effect
    RATE_OF_FIRE        = 2.0;

////////////////////////////////////////////////////////////////
// Implementation //////////////////////////////////////////////
////////////////////////////////////////////////////////////////

//#define GENERATE_TEXTURES   (1<<MATERIAL_WIZMET1_1) | (1<<MATERIAL_WBRICK1_5)
#define GENERATE_TEXTURES   -1
#define ALWAYS_REFRESH      0
#define WRITE_MAP_DATA      1
#define ENABLE_MENU       1

////////////////////////////////////////////////////////////////

const int
  KEY_A = 65, KEY_B = 66, KEY_C = 67, KEY_D = 68, KEY_E = 69, KEY_F = 70, KEY_G = 71, KEY_H = 72, KEY_I = 73, KEY_J = 74,
  KEY_K = 75, KEY_L = 76, KEY_M = 77, KEY_N = 78, KEY_O = 79, KEY_P = 80, KEY_Q = 81, KEY_R = 82, KEY_S = 83, KEY_T = 84,
  KEY_U = 85, KEY_V = 86, KEY_W = 87, KEY_X = 88, KEY_Y = 89, KEY_Z = 90,

    KEY_0 = 48, KEY_1 = 49, KEY_2 = 50, KEY_3 = 51, KEY_4 = 52, KEY_5 = 53, KEY_6 = 54, KEY_7 = 55, KEY_8 = 56, KEY_9 = 57,

  KEY_PLUS    = 187,
  KEY_MINUS   = 189,
  KEY_EQUAL   = KEY_PLUS,

    // firefox...
    KEY_PLUS_FF   = 61,
    KEY_MINUS_FF  = 173, 

    KEY_SHIFT   = 16,
  KEY_CTRL    = 17,
  KEY_ALT     = 18,
    
    KEY_ESC     = 27,
  
    KEY_BKSP    =  8,
    KEY_TAB     =  9,
  KEY_END     = 35,
  KEY_HOME    = 36,
  KEY_INS     = 45,
  KEY_DEL     = 46,
  KEY_INSERT    = KEY_INS,
  KEY_DELETE    = KEY_DEL,

  KEY_ENTER   = 13,
  KEY_SPACE     = 32,
  KEY_PAGE_UP   = 33,
  KEY_PAGE_DOWN   = 34,
  KEY_PGUP    = KEY_PAGE_UP,
  KEY_PGDN    = KEY_PAGE_DOWN,

  KEY_LEFT    = 37,
  KEY_UP      = 38,
  KEY_RIGHT   = 39,
  KEY_DOWN    = 40,
  
  unassigned    = 0;

////////////////////////////////////////////////////////////////

float is_key_down(int code)       { return code != 0 ? texelFetch(iChannel0, ivec2(code, 0), 0).r : 0.; }
float is_key_pressed(int code)      { return code != 0 ? texelFetch(iChannel0, ivec2(code, 1), 0).r : 0.; }

////////////////////////////////////////////////////////////////

float cmd(int code1, int code2)     { return max(is_key_down(code1), is_key_down(code2)); }
float cmd(int c1, int c2, int c3)   { return max(is_key_down(c1), max(is_key_down(c2), is_key_down(c3))); }
float cmd_press(int code1, int code2) { return max(is_key_pressed(code1), is_key_pressed(code2)); }

float cmd_move_forward()        { return cmd(MOVE_FORWARD_KEY1,   MOVE_FORWARD_KEY2,    MOVE_FORWARD_KEY3); }
float cmd_move_backward()       { return cmd(MOVE_BACKWARD_KEY1,  MOVE_BACKWARD_KEY2); }
float cmd_move_left()         { return cmd(MOVE_LEFT_KEY1,    MOVE_LEFT_KEY2); }
float cmd_move_right()          { return cmd(MOVE_RIGHT_KEY1,   MOVE_RIGHT_KEY2); }
float cmd_move_up()           { return cmd(MOVE_UP_KEY1,      MOVE_UP_KEY2); }
float cmd_move_down()         { return cmd(MOVE_DOWN_KEY1,    MOVE_DOWN_KEY2); }
float cmd_run()             { return cmd(RUN_KEY1,        RUN_KEY2); }
float cmd_look_left()         { return cmd(LOOK_LEFT_KEY1,    LOOK_LEFT_KEY2); }
float cmd_look_right()          { return cmd(LOOK_RIGHT_KEY1,   LOOK_RIGHT_KEY2); }
float cmd_look_up()           { return cmd(LOOK_UP_KEY1,      LOOK_UP_KEY2); }
float cmd_look_down()         { return cmd(LOOK_DOWN_KEY1,    LOOK_DOWN_KEY2); }
float cmd_center_view()         { return cmd(CENTER_VIEW_KEY1,    CENTER_VIEW_KEY2); }
float cmd_strafe()            { return cmd(STRAFE_KEY1,     STRAFE_KEY2); }
float cmd_respawn()           { return cmd_press(RESPAWN_KEY1,  RESPAWN_KEY2); }
float cmd_attack()            { return cmd(ATTACK_KEY1,     ATTACK_KEY2); }
float cmd_menu()            { return cmd_press(MENU_KEY1,   MENU_KEY2); }

float is_input_enabled()        { return step(INPUT_ACTIVE_TIME, g_time); }

////////////////////////////////////////////////////////////////

#define SETTINGS_CHANNEL iChannel1

vec4 load(vec2 address)
{
    return load(address, SETTINGS_CHANNEL);
}

////////////////////////////////////////////////////////////////
//
// World data
//
// Generated from a trimmed/tweaked version of
// the original map by John Romero
// https://rome.ro/news/2016/2/14/quake-map-sources-released
//
// Split between Buffer A and B to even out compilation time
////////////////////////////////////////////////////////////////

const float H0=0.707107,H1=0.992278,H2=0.124035,H3=0.83205,H4=0.5547,H5=0.948683,H6=0.316228,H7=0.894427,
H8=0.447214,H9=0.863779,H10=0.503871,H11=0.913812,H12=0.406138,H13=0.970143,H14=0.242536;

#define V(x,y,z,w) vec4(x,y,z,w),
#define D(w) V( 1,0,0,float(w)*16.+-1040.)
#define G(w) V(-1,0,0,float(w)*16.+48.)
#define E(w) V(0, 1,0,float(w)*16.+-1424.)
#define H(w) V(0,-1,0,float(w)*16.+176.)
#define F(w) V(0,0, 1,float(w)*16.+-336.)
#define I(w) V(0,0,-1,float(w)*16.+-192.)
#define X(v0,v1) D(v0)G(v1)
#define Y(v0,v1) E(v0)H(v1)
#define Z(v0,v1) F(v0)I(v1)

WRAP(Planes,planes,vec4,NUM_MAP_NONAXIAL_PLANES+1)(X(6.5,53.5)Y(55,21)V(.6,0,-.8,-574.4)V(-.6,0,.8,561.6)X(1.5,58.5)Y(55,21)V(.6
,0,.8,-590.4)V(-.6,0,-.8,577.6)Z(20,10)V(H0,H0,0,-1052.17)V(H0,-H0,0,-305.47)V(H3,0,H4,-781.018)V(-H3,0,-H4,767.705)Z(20,10)V(H3
,0,-H4,-829.832)V(-H0,H0,0,305.47)V(-H0,-H0,0,1052.17)V(-H3,0,H4,816.519)Z(20,10)V(H0,-H0,0,-305.47)V(-H0,-H0,0,1052.17)V(0,H3,
-H4,-470.386)V(0,-H3,H4,457.073)Z(20,10)V(H0,H0,0,-1052.17)V(-H0,H0,0,305.47)V(0,H3,H4,-421.572)V(0,-H3,-H4,408.259)X(56,4)Y(
57.5,18.5)V(0,.6,-.8,-315.2)V(0,-.6,.8,302.4)X(56,4)Y(52.5,23.5)V(0,.6,.8,-331.2)V(0,-.6,-.8,318.4)X(58.5,1.5)Y(55,21)V(.6,0,-.8
,-75.2)V(-.6,0,.8,62.4)X(53.5,6.5)Y(55,21)V(.6,0,.8,-91.2)V(-.6,0,-.8,78.4)Z(20,10)V(H0,H0,0,-463.862)V(H0,-H0,0,282.843)V(H3,0,
H4,-88.752)V(-H3,0,-H4,75.4392)Z(20,10)V(H0,-H0,0,282.843)V(-H0,-H0,0,463.862)V(0,H3,-H4,-470.386)V(0,-H3,H4,457.073)Z(20,10)V(
H3,0,-H4,-137.566)V(-H0,H0,0,-282.843)V(-H0,-H0,0,463.862)V(-H3,0,H4,124.253)X(4,56)Y(57.5,18.5)V(0,.6,-.8,-315.2)V(0,-.6,.8,
302.4)Z(20,10)V(H0,H0,0,-463.862)V(-H0,H0,0,-282.843)V(0,H3,H4,-421.572)V(0,-H3,-H4,408.259)X(4,56)Y(52.5,23.5)V(0,.6,.8,-331.2)
V(0,-.6,-.8,318.4)Y(59,12)Z(22,10)V(H0,H0,0,-837.214)V(-H0,H0,0,-67.8823)Y(55,12)Z(24,8)V(H0,H0,0,-927.724)V(-H0,H0,0,-158.392)Y
(63,12)Z(21,11)V(H0,H0,0,-791.96)V(-H0,H0,0,-22.6274)Y(57,12)Z(23,9)V(H0,H0,0,-882.469)V(-H0,H0,0,-113.137)X(1,49)Y(63,13)F(2)V(
-H10,0,-H9,667.989)X(49,1)Y(63,13)F(2)V(H10,0,-H9,119.777)X(30,30)Y(65,1)F(2)V(0,H10,-H9,55.2819)X(46,14)Y(65,1)F(2)V(0,H10,-H9,
55.2819)X(14,46)Y(65,1)F(2)V(0,H10,-H9,55.2819)X(14.5,14.5)Y(75.5,1)V(0,H6,H5,-189.737)V(0,H6,-H5,22.7684)X(1,59.5)Z(7.5,8)V(-H6
,H5,0,-78.4245)V(-H6,-H5,0,680.522)Y(75.5,1)Z(7.5,12)V(H5,H6,0,-333.936)V(-H5,H6,0,166.968)X(59.5,1)Z(7.5,8)V(H6,H5,0,-422.48)V(
H6,-H5,0,336.466)Y(75.5,1)Z(7.5,12)V(H5,H6,0,-865.199)V(-H5,H6,0,698.231)D(14)H(14)Z(1,30)V(-H0,H0,0,271.529)G(14)H(14)Z(1,30)V(
H0,H0,0,-497.803)G(1)H(1)Z(5,8)V(H0,H0,0,-316.784)D(1)H(1)Z(5,8)V(-H0,H0,0,452.548)X(7,7)H(1)F(2)V(0,H10,-H9,69.1023)X(49,1)Y(47
,29)F(2)V(H10,0,-H9,119.777)X(59.5,1)Y(49,15)V(H6,0,H5,-149.259)V(H6,0,-H5,63.2456)X(59.5,1)Z(7.5,8)V(H6,H5,0,-665.343)V(H6,-H5,
0,579.329)G(1)Y(41,7)F(2)V(H10,0,-H9,133.598)X(1,49)Y(47,29)F(2)V(-H10,0,-H9,667.989)X(1,59.5)Y(49,15)V(-H6,0,H5,194.796)V(-H6,0
,-H5,407.301)X(1,59.5)Z(7.5,8)V(-H6,H5,0,-321.287)V(-H6,-H5,0,923.385)D(1)Y(41,7)F(2)V(-H10,0,-H9,681.809)Y(33,41)Z(24.5,7.5)V(
H7,H8,0,-1230.73)V(-H7,H8,0,314.838)X(46,15)H(37)Z(25,7)V(H7,H8,0,-658.298)X(42,19)H(37)Z(25,7)V(H8,H7,0,-887.272)H(37)Z(26,1)V(
H0,H0,0,-848.528)V(-H0,H0,0,-294.156)H(37)Z(26,1)V(H0,H0,0,-1063.49)V(-H0,H0,0,-79.196)G(7)H(37)Z(26,1)V(H0,H0,0,-678.823)D(2)H(
37)Z(26,1)V(-H0,H0,0,147.078)X(3,42)Y(37,37)Z(25,7)V(-H8,H7,0,-400.703)X(23,36)Y(29,41)V(0,H14,-H13,-271.64)V(0,-H14,H13,256.118
)X(36,23)Y(29,41)V(0,H14,-H13,-271.64)V(0,-H14,H13,256.118)X(27,27)Y(29,41)V(0,H14,-H13,-271.64)V(0,-H14,H13,256.118)E(21)Z(24,8
)V(H7,-H8,0,-343.46)V(-H4,-H3,0,1402.28)H(45)Z(24.5,7.5)V(.8,.6,0,-1280)V(-H4,H3,0,-257.381)H(45)Z(24.5,7.5)V(H7,H8,0,-1187.8)V(
-H0,H0,0,-45.2549)E(21)Z(24,8)V(H0,-H0,0,135.764)V(-H7,-H8,0,1245.04)H(45)Z(24.5,7.5)V(H4,H3,0,-1207.03)V(-H7,H8,0,314.838)E(21)
Z(24,8)V(H4,-H3,0,434.885)V(-.8,-.6,0,1292.8)Y(17,57)Z(24,8)V(H7,-H8,0,-343.46)V(-H7,-H8,0,1202.11)D(22)E(12)Z(24,1)V(-H0,-H0,0,
1335.02)X(2,2)Y(1,65)Z(23,9)V(-.524097,-.851658,0,1186.56)G(41)E(12)Z(25,1)V(H0,-H0,0,350.725)D(2)E(12)Z(25,1)V(-H0,-H0,0,
1561.29)Y(37,37)Z(5,24)V(.8,0,-.6,-217.6)V(-.8,0,-.6,409.6)X(51,2)Y(25,51)F(2)V(H12,0,-H11,172.203)X(22,31)Y(25,51)F(2)V(-H12,0,
-H11,484.117)Y(8,66)Z(5,24)V(.8,0,-.6,-217.6)V(-.8,0,-.6,409.6)Y(12,41)Z(2,27)V(H12,0,-H11,56.8594)V(-H12,0,-H11,368.774)X(31,11
)Y(25,51)Z(2,26)V(H12,0,-H11,42.2384)V(-H12,0,-H11,354.153)G(3)Y(37,37)F(5)V(.501036,0,-.865426,106.812)G(3)Y(8,66)F(5)V(.501036
,0,-.865426,106.812)X(22,2)E(1)F(2.5)V(0,-.393919,-.919145,782.586)D(22)Y(12,41)F(2)V(-H12,0,-H11,498.738)G(2)Y(12,41)F(2)V(H12,
0,-H11,186.824)Y(37,37)Z(5,24)V(.8,0,-.6,-460.8)V(-.8,0,-.6,652.8)D(2)V(-H2,H1,0,-1002.2)V(-H2,-H1,0,1220.5)V(-H2,0,H1,-128.996)
V(-H2,0,-H1,347.297)G(41)V(H2,H1,0,-992.278)V(H2,-H1,0,785.884)V(H2,0,H1,-254.023)V(H2,0,-H1,47.6293)D(2)V(-H2,H1,0,-811.683)V(
-H2,-H1,0,1029.98)V(-H2,0,H1,-41.6757)V(-H2,0,-H1,259.977)D(2)V(-H2,H1,0,-875.189)V(-H2,-H1,0,1093.49)V(-H2,0,H1,-128.996)V(-H2,
0,-H1,347.297)G(41)V(H2,H1,0,-1151.04)V(H2,-H1,0,944.649)V(H2,0,H1,-341.344)V(H2,0,-H1,134.95)D(3)Y(37,37)F(5)V(-.501036,0,
-.865426,651.939)Y(8,66)Z(5,24)V(.8,0,-.6,-460.8)V(-.8,0,-.6,652.8)G(41)V(H2,H1,0,-1278.05)V(H2,-H1,0,1071.66)V(H2,0,H1,-341.344
)V(H2,0,-H1,134.95)E(1)V(H1,-H2,0,-591.398)V(-H1,-H2,0,916.865)V(0,-H2,H1,-75.4131)V(0,-H2,-H1,400.88)E(1)V(H1,-H2,0,-797.791)V(
-H1,-H2,0,1123.26)V(0,-H2,H1,-75.4131)V(0,-H2,-H1,400.88)E(1)V(H1,-H2,0,-694.594)V(-H1,-H2,0,1020.06)V(0,-H2,H1,-51.5984)V(0,-H2
,-H1,377.066)D(3)Y(8,66)F(5)V(-.501036,0,-.865426,651.939)vec4(0)));

#define L(x,y,z) vec4(x,y,z,300),
#define LR(x,y,z,r) vec4(x,y,z,r),

WRAP(Lights,lights,vec4,NUM_LIGHTS+1)(LR(224,880,248,200)LR(224,1008,248,200)LR(224,1136,248,200)L(88,1024,64)L(362,1034,20)L(
200,712,120)LR(128,624,-32,220)LR(128,528,-48,220)L(126,526,12)LR(128,432,-32,220)LR(224,528,-32,220)L(224,352,120)L(864,352,120
)L(544,312,104)LR(544,496,40,250)LR(544,584,424,500)LR(960,432,-32,220)LR(864,528,-32,220)LR(960,624,-32,220)L(958,526,12)L(394,
762,84)LR(544,864,-8,200)L(698,762,84)LR(960,528,-48,220)LR(408,928,96,120)LR(680,1056,96,120)L(544,1016,72)LR(544,1136,248,200)
LR(544,880,248,200)L(336,1152,-144)L(336,1024,-144)L(336,896,-144)LR(448,1152,-56,120)LR(480,1032,-152,200)LR(488,840,-152,200)
LR(600,840,-152,200)LR(608,1032,-152,200)LR(608,1120,-152,200)LR(544,1192,-152,200)L(728,1080,-144)L(984,1080,-144)L(984,904,-
144)L(728,904,-144)LR(864,888,-32,120)LR(720,992,64,120)LR(864,1112,-24,150)LR(992,928,64,120)LR(992,1056,128,120)L(976,928,312)
L(976,1056,312)LR(976,1184,312,200)L(736,992,312)L(736,1120,312)LR(736,864,312,200)LR(720,1120,128,120)L(912,1360,296)L(808,1360
,296)L(888,712,120)L(864,1336,48)L(544,1336,48)LR(232,1336,48,350)vec4(0)));

// Lightmap layout /////////////////////////////////////////////

#define O2 0,0
#define O4 O2,O2

WRAP(LightmapTiles,LIGHTMAP_TILES,int,NUM_MAP_PLANES)(O2,0x8f4187a,0,1725<<18,0,68020022,32849,16467,85,51243860,0,114779,0,
28927,68013914,53467,O2,119,201,827,51242336,0,68024118,49153,81923,65541,64795,0,114699,0,12351,68020528,51246926,O2,16391,
98313,114699,57403,O2,68010330,113,147,51246920,0,201,0,877,99231,57403,0,57006602,28673,0,50935528,420042758,913857,122891,
118797,0,487963,159755,107069,O2,59542636,474001,42751174,3665,291297534,O4,O2,381,O4,O2,0xd84711e,O2,0,34466650,45217,39113892,
0,55904432,O2,0,99887226,0,0xcf4007a,O2,0,34972748,0,85321998,O2,54855936,51493674,134753,286368892,200817,553,0,190651,186557,
200891,77837,0x70dd504,147585,0x7159d04,O4,54855970,52793124,433,287679594,77825,0x81da4ee,98305,0,0x815b4ee,O4,0,38851790,
0x6510f26,3969,0x774d80a,O4,O4,0,517494784,0,0xe141956,749457,O2,84778338,0,0x70dc704,O2,0x729d8ae,O4,0,0x96d520a,O2,51753790,O2
,0,84474618,0,0xa0d515c,376049,0xa1de6ae,373553,O2,376587,401181,373707,21725,O4,0x72570dc,O4,0,38089418,0,88420998,369,41455390
,8193,O2,504403290,65,186539,O2,0,507024694,353,O2,0,0x955ec0a,O2,0xe1558a4,204801,O2,84515926,O4,O4,O4,O4,O4,0,38589724,3921,0,
88942328,404082688,246305,403757192,321,O4,38589762,978113,88942290,O2,555013120,554505728,159745,84724578,O2,0,0x6510e86,321,
89201366,321,O2,40971,973,3979,973,404059212,450577,403757208,65,O2,437611574,0,437351492,435169,85046040,O2,0,0x64d2686,337,O2,
454400512,778609,453809506,O2,0,338548804,0,336163926,O2,0,0x615ab0e,110849,O2,0,84686178,111211,O4,84522180,0x915b4dc,45057,O4,
540316746,369,537433258,0,0xd09aaaa,0,0xb1592b4,827217,O2,84489060,934497,0xd1590a4,O4,0,0x9157edc,487425,O4,457776138,0xfd001,0
,453830200,O2,408737882,O2,403284032,O2,0,546608128,O4,539530386,1889,0,537433254,O2,0xf155490,594577,0xf11549a,401505,O2,300891
,189,78059,401645,O2,336910684,0,336147546,33,O2,0,320167002,319380610,300833,O4,99366124,20481,0,251749,371263578,952497,O4,
37092458,O2,37277978,0xd39cc86,0,388028716,129,O4,O2,0,34995438,401,277923,O2,0,278219,278109,0,0x909e8e6,437265,0,0x90db2e6,
34449250,O2,382269,0,465963,391915,O4,35519150,265585,O4,265707,0,262671,0x7094806,O2,0x70dd8c2,O2,0,0x909deaa,0,0x90dd6e6,O2,0,
0x7098a06,0,0x70de4f6,O4,0,34656594,622673,O4,536651,0,549087,0x90992c4,O2,0x90dc4e6,O4,O4,O4,O4,O4,O2,0,329275598,44060278,O4,
391174312,O2,0,51242330,68274464,49217,51501628,28673,O2,81995,32845,27515,27581,0x80c3f62,50985632,68286752,16385,68791088,
69242034,0xfdcab,68017474,0,68528968,171873,109,52787528,51753810,217,85051160,86339822,69047626,520411,51500866,68528988,106683
,68791076,3965,68272416,O2,51242324,68015932,49889,85056280,0,217,0,50075,50093,O2,68286232,45505,51497294,233633,51243842,0,
127371,110989,233579,234413,0x70d9d0e,28685,68282656,98305,68530996,0,35519578,34725208,114699,249,69334106,68785496,0x811c4ee,
68012336,197943,68530976,85311758,85564758,0,68284184,0,51247804,68010318,913,52532492,34989178,85058840,106497,85815642,0xfd001
,0,180233,51231586,0,68010288,114881,39375992,0,0x990591e,0,35718476,36043976,41717966,0,0xcd400f4,0,36810926,57345,O2,74277898,
0,34700078,873377,40162374,0,0xbac766c,0,36295880,36459858,O2,0xa353cc8,817,53839534,20481,O2,0x93550ae,225057,950075,282381,O4,
0xd0daaa4,225281,0xa35e86a,30561,O2,237579,974381,435275,435389,O2,0xd0d92ae,810609,O2,59526924,0,3667,60373,0,319914586,O2,
319671904,299185,0xf11728e,O2,0,0xf0dae98,201,319894626,O2,0,319642724,299505,0xf11908e,O2,0,0xf0dae9e,193,O2,68024624,115329,
51493682,O2,51242312,41041,102739,O4,355807850,O4,356059242,O2,0,0xec418f4,0xfc4586c,O2,0x93574ae,45057,53812046,28673,54343370,
O2,0,0xf0d729e,450657,319924314,O2,0,319642748,457,O2,0,523539686,3889,O2,0xa353d30,305,53813582,450113,0,71119656,O2,0xf0dae8c,
127121,0,319914594,O2,319642736,209,O2,0,523801774,801,52793160,54342470,0x63d3c86,237569,51753800,52006746,O2,0,0xa09e6a0,69713
,34726224,O2,0,67724352,304977,34145530,O2,0,0x711b904,192449,O2,0,36873,229323,O4,0x711acfc,O4,192521,O2,37566790,0,88680222,
737,34973526,0,86339876,O2,0x911c6dc,147921,153,O2,0,90123,61453,O4,0x925d8c8,102401,0,51493706,193,34197568,34726216,0,234473,
234411,51246914,233583,0,68747612,348705,51497302,65,0,51755808,17377,34700096,340097,0,34471234,57617,51245384,50883428,0,
51750590,0xfa011,34725216,95,0,35950860,87896842,3889,34995470,175569,O4,0x911a0e6,O2,0,45631724,0xdec3400,945,37278006,O4,
0x8119cfc,O4,0x80dd4f6,0x6295148,993,O2,0x6159d4c,85309198,O2,0x62db4ae,237569,0,53334702,O2,0x62964c8,161,53042504,306177,455,
3689,O2,0x6159d24,85314318,O2,442067978,0,439146564,926545,O2,0x654fafe,369,55904390,0,53235538,794593,0,0x6296518,417,0,
86614216,0,713,619,0,61449,O4,0x99c7ece,O2,0,438897732,155649,O2,0,439197730,439459850,0x62970c8,24577,O2,0x6159d1a,85319438,0,
69819620,69566254,53042484,52793142,0,295,3769,3659,29501,0,69566272,507905,739,86339858,0,508793,508651,52793088,508511,0,
69568248,69819640,53071560,3963,0,0x62988c8,57345,0,86616776,442727,442761,O2,0x6159d38,85316878,0,69819660,69568230,355,511395,
0,52532508,0,0x811d4ee,0x711bafc,O2,395,98313,233481,0,69072110,200337,0x70de304,36865,0,0x629510c,353,0,86619336));

// Collision map ///////////////////////////////////////////////

#undef D
#undef E
#undef F
#undef G
#undef H
#undef I

#define D(w) V( 1,0,0,float(w)*16.+-1040.)
#define G(w) V(-1,0,0,float(w)*16.+48.)
#define E(w) V(0, 1,0,float(w)*16.+-1440.)
#define H(w) V(0,-1,0,float(w)*16.+176.)
#define F(w) V(0,0, 1,float(w)*16.+-320.)
#define I(w) V(0,0,-1,float(w)*16.+-192.)
#define B(x0,y0,z0,x1,y1,z1) D(x0)G(x1)E(y0)H(y1)F(z0)I(z1)

WRAP(CMPlanes,cm_planes,vec4,NUM_MAP_COLLISION_PLANES+1)(B(1,53.5,19,53.5,18.5,8)B(0,2,0,59,0,0)B(59,2,0,0,0,8)B(1,38,24,41,0,0)
B(28,2,13,28,75,10)B(47.5,2,13,8.5,75,10)B(8,2,13,48,75,10)B(20,2,0,39,37,0)B(3,76.5,0,3,0,8)B(0,0,0,0,77,0)B(3,13,31,42,37,0)B(
3,67,20,3,2.5,8)B(39,2,0,20,37,0)B(42,2,23,3,65,8)B(21,62,20,21,15,8)B(21,2,24,0,0,0)B(53.5,53.5,19,1,18.5,8)B(2,2,23,41,61,0)D(
23)G(22)E(14)F(21.5)I(8)V(0,-.242536,.970142,256.118)E(64)H(12)F(20)I(8)V(H0,H0,0,-791.96)V(-H0,H0,0,-22.6274)E(60)H(12)F(21)I(8
)V(H0,H0,0,-837.214)V(-H0,H0,0,-67.8823)E(58)H(12)F(22)I(8)V(H0,H0,0,-882.469)V(-H0,H0,0,-113.137)E(56)H(12)F(23)I(8)V(H0,H0,0,
-927.724)V(-H0,H0,0,-158.392)E(32.5)H(41)F(23.5)I(1)V(.910366,.413803,0,-1218.24)V(-.910366,.413803,0,354.877)E(18)H(55.5)F(23)I
(1)V(.910366,-.413803,0,-397.251)V(-.910366,-.413803,0,1175.86)G(3)H(2.5)F(0)I(12)V(H0,H0,0,-328.098)D(3)H(2.5)F(0)I(12)V(-H0,H0
,0,441.235)D(3)G(2)E(0)H(65)F(22)I(8)V(-.524097,-.851658,0,1186.56)vec4(0)));

#define S(d,b) b,b+d,b+d*2,b+d*3,
#define S4(d,b) S(d,b)S(d,b+d*4)S(d,b+d*8)S(d,b+d*12)

WRAP(CMBrushes,cm_brushes,int,NUM_MAP_COLLISION_BRUSHES+1)(S4(6,0)S(6,96)S(6,120)144,150,155,160,167));

// Map data serialization //////////////////////////////////////

bool is_inside(vec2 fragCoord, vec4 box, out ivec2 offset)
{
    offset = ivec2(floor(fragCoord - box.xy));
    return all(lessThan(uvec2(offset), uvec2(box.zw)));
}

void write_map_data(inout vec4 fragColor, vec2 fragCoord)
{
    if (is_inside(fragCoord, ADDR_LIGHTING) > 0.)
    {
        Lighting lighting;
        if (iFrame == 0)
            clear(lighting);
        else
            from_vec4(lighting, fragColor);

        lighting.progress = clamp(min(float(iFrame)/float(NUM_WAIT_FRAMES), iTime/LOADING_TIME), 0., 1.);
        if (lighting.progress >= 1. && lighting.bake_time <= 0.)
            lighting.bake_time = iTime;
        
        to_vec4(fragColor, lighting);
        return;
    }
    
#if WRITE_MAP_DATA
    if (iFrame > 0)
#endif
        return;
    
    ivec2 offset;

    if (is_inside(fragCoord, ADDR_RANGE_NONAXIAL_PLANES, offset))
    {
        int index = offset.y * int(ADDR_RANGE_NONAXIAL_PLANES.z) + offset.x;
        if (uint(index) < uint(NUM_MAP_NONAXIAL_PLANES + 1))
            fragColor = planes.data[index];
    }
    else if (is_inside(fragCoord, ADDR_RANGE_LIGHTS, offset))
    {
        if (uint(offset.x) < uint(NUM_LIGHTS + 1))
          fragColor = lights.data[offset.x];
    }
    else if (is_inside(fragCoord, ADDR_RANGE_LMAP_TILES, offset))
    {
        int index = offset.y * int(ADDR_RANGE_LMAP_TILES.z) + offset.x;
        if (uint(index) < uint(NUM_MAP_PLANES))
        {
            int tile = LIGHTMAP_TILES.data[index];
            bool delta_encoded = (tile & 1) != 0;
            if (delta_encoded)
            {
                int offset = (tile >> 1) & 7;
                tile = (tile >> 3) ^ LIGHTMAP_TILES.data[index - offset - 1];
            }
            tile >>= 1;
            
            int x = tile & 255,
                y = (tile >> 8) & 511,
                w = (tile >> 17) & 63,
                h = (tile >> 23) & 63;
            
            fragColor = vec4(x, y, w, h);
        }
    }
    else if (is_inside(fragCoord, ADDR_RANGE_COLLISION_PLANES, offset))
    {
        if (uint(offset.x) < uint(NUM_MAP_COLLISION_PLANES + 1))
          fragColor = cm_planes.data[offset.x];
    }
}


// Collision detection / response //////////////////////////////

vec4 get_collision_plane(int index)
{
    ivec2 addr = ivec2(ADDR_RANGE_COLLISION_PLANES.xy);
    addr.x += index;
    return texelFetch(SETTINGS_CHANNEL, addr, 0);
}

float get_player_radius(vec3 direction)
{
    const float HORIZONTAL_RADIUS = 16., VERTICAL_RADIUS = 48.;

    direction = abs(direction);
    return direction.z > max(direction.x, direction.y) ? VERTICAL_RADIUS : HORIZONTAL_RADIUS;
}

float get_player_distance(vec3 position, vec4 plane)
{
    return dot(position, plane.xyz) + plane.w - get_player_radius(plane.xyz);
}

bool is_touching_ground(vec3 position, vec4 ground)
{
    return ground.z > 0. && abs(get_player_distance(position, ground)) < 1.;
}

bool is_valid_ground(vec3 position, vec4 ground)
{
    return ground.z > 0. && get_player_distance(position, ground) > -1.;
}

void find_collision(inout vec3 start, inout vec3 delta, out int hit_plane, out float step_height)
{
    const float STEP_SIZE = 18.;

    // We iterate through all the collision brushes, tracking the closest plane the ray hits and the top plane
    // of the colliding brush.
    // If, at the end of the loop, the closest hit plane is vertical and the corresponding top plane
    // is within stepping distance, we move the start position up by the height difference, update the stepping
    // offset for smooth camera interpolation and defer all forward movement to the next step (handle_collision).
    // If we're not stepping up then we move forward as much as possible, discard the remaining forward movement
    // blocked by the colliding plane and pass along what's left (wall sliding) to the next phase.
    
    step_height = 0.;
    hit_plane = -1;
    float travel_dist = 1.;
    int ground_plane = -1;
    float ground_dist = 0.;
    float eps = 1./(length(delta) + 1e-6);
    vec3 dir = normalize(delta);

    int num_brushes = NO_UNROLL(NUM_MAP_COLLISION_BRUSHES);
    for (int i=0; i<num_brushes; ++i)
    {
        int first_plane = cm_brushes.data[i];
        int last_plane = cm_brushes.data[i + 1];
        int plane_enter = -1;
        int brush_ground_plane = -1;
        float brush_ground_dist = 1e+6;
        float t_enter = -1e+6;
        float t_leave = 1e+6;
        for (int j=first_plane; j<last_plane; ++j)
        {
            vec4 plane = get_collision_plane(j);
            float dist = get_player_distance(start, plane);
            
            // Note: top plane detection only takes into account fully horizontal planes.
            // This means that stair stepping won't work with brushes that have an angled top surface, 
            // such as the ramp in the 'Normal' hallway. If you stop on the ramp and let gravity slide
            // you down you'll notice the sliding continues for a bit after the ramp ends - the collision
            // map doesn't fully match the rendered geometry (and now you know why).
            
            if (abs(dir.z) < .7 && plane.z > .99 && brush_ground_dist > dist)
            {
                brush_ground_dist = dist;
                brush_ground_plane = j;
            }
            float align = dot(plane.xyz, delta);
            if (align == 0.)
            {
                if (dist > 0.)
                {
                    t_enter = 2.;
                    break;
                }
                continue;
            }
            align = -1./align;
            dist *= align;
            if (align > 0.)
            {
                if (t_enter < dist)
                {
                    plane_enter = j;
                    t_enter = dist;
                }
            }
            else
            {
                t_leave = min(t_leave, dist);
            }

            if (t_leave <= t_enter)
                break;
        }

        if (t_leave > max(t_enter, 0.) && t_enter > -eps)
        {
            if (t_enter <= travel_dist)
            {
                if (brush_ground_plane != -1 && -brush_ground_dist > ground_dist)
                {
                    ground_plane = brush_ground_plane;
                    ground_dist = -brush_ground_dist;
                }
                hit_plane = plane_enter;
                travel_dist = t_enter;
            }
        }
    }

    vec4 plane;
    bool blocked = hit_plane != -1;
    if (blocked)
    {
        plane = get_collision_plane(hit_plane);
        if (abs(plane.z) < .7 && ground_plane != -1 && ground_dist > 0. && ground_dist <= STEP_SIZE)
        {
            ground_dist += .05; // fixes occasional stair stepping stutter at low FPS
            step_height = ground_dist;
            start.z += ground_dist;
            return; // defer forward movement to next step
        }
    }

    start += delta * clamp(travel_dist, 0., 1.);
    delta *= 1. - clamp(travel_dist, 0., 1.);

    if (blocked)
    {
        start += 1e-2 * plane.xyz;
        delta -= dot(plane.xyz, delta) * plane.xyz;
    }
}


void handle_collision(inout vec3 start, vec3 delta, int slide_plane, out int hit_plane, out int ground_plane)
{
    // We iterate again through all the collision brushes, this time performing two ray intersections:
    // one determines how far we can actually move, while the other does a ground check from the starting
    // point, giving us an approximate ground plane.
    // Note that the ground plane isn't computed from the final position - that would require another pass
    // through all the brushes!
    
    const float LARGE_NUMBER = 1e+6;

    hit_plane = -1;
    ground_plane = -1;
    float travel_dist = 1.;
    float ground_dist = LARGE_NUMBER;
    float eps = 1./(length(delta) + 1e-6);

    int num_brushes = NO_UNROLL(NUM_MAP_COLLISION_BRUSHES);
    for (int i=0; i<num_brushes; ++i)
    {
        int first_plane = cm_brushes.data[i];
        int last_plane = cm_brushes.data[i + 1];
        int plane_enter = -1;
        int plane_enter_ground = -1;
        float t_enter = -LARGE_NUMBER;
        float t_leave = LARGE_NUMBER;
        float t_enter_ground = t_enter;
        float t_leave_ground = t_leave;
        for (int j=first_plane; j<last_plane; ++j)
        {
            vec4 plane = get_collision_plane(j);
            float dist = get_player_distance(start, plane);

            // handle ground ray
            if (plane.z == 0.)
            {
                if (dist > 0.)
                    t_enter_ground = LARGE_NUMBER;
            }
            else
            {
                float height = dist / plane.z;
                if (plane.z > 0.)
                {
                    if (t_enter_ground < height)
                    {
                        plane_enter_ground = j;
                        t_enter_ground = height;
                    }
                }
                else
                {
                    t_leave_ground = min(t_leave_ground, height);
                }
            }

            // handle movement ray
            float align = dot(plane.xyz, delta);
            if (align == 0.)
            {
                if (dist > 0.)
                    t_enter = LARGE_NUMBER;
                continue;
            }
            align = -1./align;
            dist *= align;
            if (align > 0.)
            {
                if (t_enter < dist)
                {
                    plane_enter = j;
                    t_enter = dist;
                }
            }
            else
            {
                t_leave = min(t_leave, dist);
            }
        }

        if (t_leave_ground > t_enter_ground && t_enter_ground > -8.)
        {
            if (t_enter_ground < ground_dist)
            {
                ground_plane = plane_enter_ground;
                ground_dist = t_enter_ground;
            }
        }

        if (t_leave > max(t_enter, 0.) && t_enter > -eps)
        {
            if (t_enter < travel_dist)
            {
                hit_plane = plane_enter;
                travel_dist = t_enter;
            }
        }
    }

    start += delta * clamp(travel_dist, 0., 1.);
    delta *= 1. - clamp(travel_dist, 0., 1.);

    if (hit_plane != -1)
    {
        vec4 plane = get_collision_plane(hit_plane);
        start += 1e-2 * plane.xyz;
        delta -= dot(plane.xyz, delta) * plane.xyz;
    }
}

void clip_velocity(inout vec3 velocity, int first_hit_plane, int second_hit_plane, float step_size)
{
    if (step_size > 0.)
    {
        first_hit_plane = second_hit_plane;
      second_hit_plane = -1;
    }

    if (first_hit_plane != -1)
    {
        vec4 first = get_collision_plane(first_hit_plane);
        if (second_hit_plane != -1)
        {
            vec4 second = get_collision_plane(second_hit_plane);
            vec3 crease = normalize(cross(first.xyz, second.xyz));
            velocity = dot(velocity, crease) * crease;
        }
        else
        {
            float align = dot(first.xyz, normalize(velocity));
            velocity -= 1.001 * dot(velocity, first.xyz) * first.xyz;
            velocity *= mix(1., .5, abs(align)); // extra friction
        }
    }
}

void slide_move(inout vec3 position, inout vec3 velocity, inout vec4 ground, inout float step_transition)
{
    vec3 dir = velocity * iTimeDelta;

    int first_hit_plane = -1,
      second_hit_plane = -1,
      ground_plane = -1;
    float step_size = 0.;

    find_collision(position, dir, first_hit_plane, step_size);
    handle_collision(position, dir, first_hit_plane, second_hit_plane, ground_plane);
    clip_velocity(velocity, first_hit_plane, second_hit_plane, step_size);
    
    ground = vec4(0);
    if (ground_plane != -1)
    {
        vec4 plane = get_collision_plane(ground_plane);
        if (is_valid_ground(position, plane))
            ground = plane;
    }

    step_transition += step_size;
}

// UV distortions //////////////////////////////////////////////

vec2 running_bond(vec2 uv, float rows)
{
    uv.x += floor(uv.y * rows) * .5;
    return uv;
}

vec2 running_bond(vec2 uv, float cols, float rows)
{
    uv.x += floor(uv.y * rows) * (.5 / cols);
    return uv;
}

vec3 herringbone(vec2 uv)
{
    uv *= 4.;
    float horizontal = step(1., mod(uv.x + floor(uv.y) + 3., 4.) - 1.);
    uv = mix(-uv.yx + vec2(3. - floor(uv.x), 0.), uv.xy + vec2(3. - floor(uv.y), 0.), horizontal);
    return vec3(uv * .25, horizontal);
}

// 3D effects //////////////////////////////////////////////////

// centered on 0; >0 for lightness, <0 for darkness
float add_bevel(vec2 uv, float cols, float rows, float thickness, float light, float dark)
{
    uv = fract(uv * vec2(cols, rows));
    vec4 d = clamp(vec4(uv.xy, 1.-uv.xy)/vec2(thickness*cols, thickness*rows).xyxy, 0., 1.);
    return light*(2. - d.x - d.w) - dark*(2. - d.y - d.z);
}

// QUAKE text //////////////////////////////////////////////////

float sdf_QUAKE(vec2 uv)
{
    uv.x *= .9375;
    float sdf              = sdf_Q_top(uv);
    uv.x -= .875; sdf = sdf_union(sdf, sdf_U(uv));
    uv.x -= .8125;  sdf = sdf_union(sdf, sdf_A(uv));
    uv.x -= 1.0625; sdf = sdf_union(sdf, sdf_K(uv));
    uv.x -= .625; sdf = sdf_union(sdf, sdf_E(uv));
    return sdf;
}

vec2 engraved_QUAKE(vec2 uv, float size, vec2 light_dir)
{
    const float EPS = .1/64.;
    vec3 sdf;
    for (int i=NO_UNROLL(0); i<3; ++i)
    {
        vec2 uv2 = uv;
        if (i != 2)
            uv2[i] += EPS;
        sdf[i] = sdf_QUAKE(uv2);
    }
    vec2 gradient = safe_normalize(sdf.xy - sdf.z);
    float mask = sdf_mask(sdf.z, 1./64.);
    float bevel = clamp(1. + sdf.z/size, 0., 1.);
    float intensity = .5 + sqr(bevel) * dot(gradient, light_dir);
    intensity = mix(1.125, intensity, mask);
    mask = sdf_mask(sdf.z - 1./64., 1./64.);
    return vec2(intensity, mask);
}

////////////////////////////////////////////////////////////////

// waves.x = amplitude; .y = frequency; .z = phase offset
float sdf_flame_segment(vec2 uv, vec2 size, vec3 waves)
{
    float h = linear_step(0., size.y, uv.y);
    float width = mix(.5, .005, sqr(h)) * size.x;
    return sdf_centered_box(uv, vec2(.5 + sin((h+waves.z)*TAU*waves.y)*waves.x, size.y*.5), vec2(width, size.y*.5));
}

float sdf_flame_segment2(vec2 uv, vec2 size, vec3 waves)
{
    float width = mix(size.x*.005, size.x*.5, smoothen(around(size.y*.5, size.y*.5, uv.y)));
    float h = linear_step(0., size.y, uv.y);
    return sdf_centered_box(uv, vec2(.5 + sin((h+waves.z)*TAU*waves.y)*waves.x, size.y*.5), vec2(width, size.y*.5));
}

float sdf_window_flame(vec2 uv)
{
    bool left = uv.x < .5;
    float sdf = uv.y - 1.;
    uv.y -= .95;
    sdf = sdf_union(sdf, sdf_flame_segment(skew(uv, -.02), vec2(.4, 1.9), vec3(.11, 1., .0)));
    sdf = sdf_union(sdf, sdf_flame_segment(skew(uv, .21)-vec2(-.13, 0.), vec2(.3, 1.2), vec3(.08, 1.2, .95)));
  sdf = sdf_union(sdf, sdf_flame_segment(skew(uv, .0)-vec2(.31, 0.), vec2(.3, 1.4), vec3(.1, 1.2, .55)));
    
    sdf = sdf_union(sdf, sdf_flame_segment(skew(uv, left ? .3 : -.3) - (left ? vec2(-.28, 0.) : vec2(.37, -.1)),
                                           vec2(.2, left ? .31 : .35), vec3(left ? -.03 : .03, 1., .5)));
    
    sdf = sdf_union(sdf, sdf_flame_segment2(uv - (left ? vec2(-.35, 1.25) : vec2(.17, 1.5)), vec2(.11, left ? .4 : .35),
                                            vec3(-.02, 1., .5)));
    sdf = sdf_union(sdf, sdf_flame_segment2(skew(uv-vec2(.35, 1.35), -.0), vec2(.11, .24), vec3(.02, 1., .5)));
    return sdf;
}

float sdf_window_emblem(vec2 uv)
{
    vec2 uv2 = vec2(min(uv.x, 1.-uv.x), uv.y);
  
    float sdf = sdf_centered_box(uv, vec2(.5, .25), vec2(.375, .1));
    sdf = sdf_exclude(sdf, sdf_disk(uv2, vec2(.36, .1), .15));
    
    float h = linear_step(.35, .8, uv.y);
    float w = mix(.27, .35, sqr(triangle_wave(.5, h))) + sqrt(h) * .15;
    sdf = sdf_union(sdf, sdf_centered_box(uv, vec2(.5, .6), vec2(w, .26)));
    
    h = linear_step(.95, .6, uv.y);
    w = .6 - around(.9, .8, h) * .5;
    sdf = sdf_exclude(sdf, .75*sdf_centered_box(uv, vec2(.5, .75), vec2(w, .21)));
    
    // eyes
    sdf = sdf_exclude(sdf, sdf_line(uv2, vec2(.45, .4), vec2(.4, .45), .04));

    sdf = sdf_exclude(sdf, sdf_disk(uv2, vec2(.15, .2), .15));
  sdf = sdf_union(sdf, sdf_line(uv, vec2(.5, .125), vec2(.5, .875), .0625));
    return sdf;
}

////////////////////////////////////////////////////////////////

float line_sqdist(vec2 uv, vec2 a, vec2 b)
{
    vec2 ab = b-a, ap = uv-a;
    float t = clamp(dot(ap, ab)/dot(ab, ab), 0., 1.);
    return length_squared(uv - (ab*t + a));
}

float sdf_chickenscratch(vec2 uv, vec2 mins, vec2 maxs, float thickness)
{
    uv -= mins;
    maxs -= mins;
    
    vec2 p0, p1, p2, p3;
    if (uv.x < maxs.x*.375)
    {
        p0 = vec2(0.);
        p1 = vec2(.3, 1.);
        p2 = vec2(.3, 0.);
        p3 = vec2(.09, .28);
    }
    else
    {
        p0 = vec2(.45, 0.);
        p1 = vec2(.45, 1.);
        p2 = vec2(.75, 0.);
        p3 = p0;
    }
    p0 *= maxs;
    p1 *= maxs;
    p2 *= maxs;
    p3 *= maxs;

    float dist = line_sqdist(uv, p0, p1);
    dist = min(dist, line_sqdist(uv, p1, p2));
    dist = min(dist, line_sqdist(uv, p2, p3));

    #define LINE(a, b) line_sqdist(uv, maxs*a, maxs*b)
    
    dist = min(dist, LINE(vec2(.65, 1.), vec2(.95, 0.)));
    dist = min(dist, LINE(vec2(.85, 1.), vec2(.65, .65)));

  #undef LINE
    
    return sqrt(dist) + thickness * -.5;
}

////////////////////////////////////////////////////////////////
//  Cellular noise code by Brian Sharpe
//  https://briansharpe.wordpress.com/
//  https://github.com/BrianSharpe/GPU-Noise-Lib
//
//  Modified to add tiling
////////////////////////////////////////////////////////////////

//
//  FAST32_hash
//  A very fast hashing function.  Requires 32bit support.
//  http://briansharpe.wordpress.com/2011/11/15/a-fast-and-simple-32bit-floating-point-hash-function/
//
//  The 2D hash formula takes the form....
//  hash = mod( coord.x * coord.x * coord.y * coord.y, SOMELARGEFLOAT ) / SOMELARGEFLOAT
//  We truncate and offset the domain to the most interesting part of the noise.
//  SOMELARGEFLOAT should be in the range of 400.0->1000.0 and needs to be hand picked.  Only some give good results.
//  A 3D hash is achieved by offsetting the SOMELARGEFLOAT value by the Z coordinate
//

const vec2 OFFSET = vec2( 26.0, 161.0 );
const vec2 SOMELARGEFLOATS = vec2( 951.135664, 642.949883 );

void FAST32_hash_2D_tile( vec2 gridcell, vec2 gridsize, out vec4 hash_0, out vec4 hash_1 )
{
    //    gridcell is assumed to be an integer coordinate
    vec4 P = vec4( gridcell.xy, gridcell.xy + 1.0 );
    P = P - floor(P * ( 1.0 / gridsize.xyxy )) * gridsize.xyxy;
    P += OFFSET.xyxy;
    P *= P;
    P = P.xzxz * P.yyww;
    hash_0 = fract( P * ( 1.0 / SOMELARGEFLOATS.x ) );
    hash_1 = fract( P * ( 1.0 / SOMELARGEFLOATS.y ) );
}

vec4 FAST32_hash_2D_tile( vec2 gridcell, vec2 gridsize )
{
    //    gridcell is assumed to be an integer coordinate
    vec4 P = vec4( gridcell.xy, gridcell.xy + 1.0 );
    P = P - floor(P * ( 1.0 / gridsize.xyxy )) * gridsize.xyxy;
    P += OFFSET.xyxy;
    P *= P;
    P = P.xzxz * P.yyww;
    return fract( P * ( 1.0 / SOMELARGEFLOATS.x ) );
}

float FAST32_smooth_noise(vec2 P, vec2 gridsize)
{
    P *= gridsize;
    vec2 Pi = floor(P), Pf = smoothen(P - Pi);
    vec4 hash = FAST32_hash_2D_tile(Pi, gridsize);
    return mix(mix(hash.x, hash.y, Pf.x), mix(hash.z, hash.w, Pf.x), Pf.y);
}

//  convert a 0.0->1.0 sample to a -1.0->1.0 sample weighted towards the extremes
vec4 Cellular_weight_samples( vec4 samples )
{
    samples = samples * 2.0 - 1.0;
    //return (1.0 - samples * samples) * sign(samples); // square
    return (samples * samples * samples) - sign(samples); // cubic (even more variance)
}

//
//  Cellular Noise 2D
//  Based off Stefan Gustavson's work at http://www.itn.liu.se/~stegu/GLSL-cellular
//  http://briansharpe.files.wordpress.com/2011/12/cellularsample.jpg
//
//  Speed up by using 2x2 search window instead of 3x3
//  produces a range of 0.0->1.0
//
float Cellular2D(vec2 P, vec2 gridsize)
{
    P *= gridsize;  // adx: multiply here instead of requiring callers to do it
    //  establish our grid cell and unit position
    vec2 Pi = floor(P);
    vec2 Pf = P - Pi;

    //  calculate the hash.
    vec4 hash_x, hash_y;
    FAST32_hash_2D_tile( Pi, gridsize, hash_x, hash_y );

    //  generate the 4 random points
#if 0
    //  restrict the random point offset to eliminate artifacts
    //  we'll improve the variance of the noise by pushing the points to the extremes of the jitter window
    const float JITTER_WINDOW = 0.25; // 0.25 will guarentee no artifacts.  0.25 is the intersection on x of graphs f(x)=( (0.5+(0.5-x))^2 + (0.5-x)^2 ) and f(x)=( (0.5+x)^2 + x^2 )
    hash_x = Cellular_weight_samples( hash_x ) * JITTER_WINDOW + vec4(0.0, 1.0, 0.0, 1.0);
    hash_y = Cellular_weight_samples( hash_y ) * JITTER_WINDOW + vec4(0.0, 0.0, 1.0, 1.0);
#else
    //  non-weighted jitter window.  jitter window of 0.4 will give results similar to Stefans original implementation
    //  nicer looking, faster, but has minor artifacts.  ( discontinuities in signal )
    const float JITTER_WINDOW = 0.4;
    hash_x = hash_x * JITTER_WINDOW * 2.0 + vec4(-JITTER_WINDOW, 1.0-JITTER_WINDOW, -JITTER_WINDOW, 1.0-JITTER_WINDOW);
    hash_y = hash_y * JITTER_WINDOW * 2.0 + vec4(-JITTER_WINDOW, -JITTER_WINDOW, 1.0-JITTER_WINDOW, 1.0-JITTER_WINDOW);
#endif

    //  return the closest squared distance
    vec4 dx = Pf.xxxx - hash_x;
    vec4 dy = Pf.yyyy - hash_y;
    vec4 d = dx * dx + dy * dy;
    d.xy = min(d.xy, d.zw);
    return min(d.x, d.y) * ( 1.0 / 1.125 ); //  scale return value from 0.0->1.125 to 0.0->1.0  ( 0.75^2 * 2.0  == 1.125 )
}


////////////////////////////////////////////////////////////////
// The MIT License
// Copyright © 2013 Inigo Quilez
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
////////////////////////////////////////////////////////////////

// XY=offset, Z=dist
vec3 voronoi(vec2 x, vec2 grid)
{
    x *= grid; // adx: multiply here instead of requiring callers to do it
    vec2 n = floor(x);
    vec2 f = x - n;

    //----------------------------------
    // first pass: regular voronoi
    //----------------------------------
  vec2 mg, mr;

    float md = 8.0;
    for (int j=-1; j<=1; j++)
    for (int i=-1; i<=1; i++)
    {
        vec2 g = vec2(i, j);
    vec2 o = hash2(mod(n + g, grid)); // adx: added domain wrapping
        vec2 r = g + o - f;
        float d = dot(r,r);

        if (d < md)
        {
            md = d;
            mr = r;
            mg = g;
        }
    }

    //----------------------------------
    // second pass: distance to borders
    //----------------------------------
    md = 8.0;
    for (int j=-2; j<=2; j++)
    for (int i=-2; i<=2; i++)
    {
        vec2 g = mg + vec2(i, j);
    vec2 o = hash2(mod(n + g, grid)); // adx: added domain wrapping
        vec2 r = g + o - f;
        float d = length_squared(mr - r);

        if (d > 0.00001)
          md = min(md, -inversesqrt(d)*dot(mr+r, mr-r));
    }

    return vec3(mr, 0.5*md); // adx: changed order
}

////////////////////////////////////////////////////////////////

float tileable_smooth_noise(vec2 p, vec2 scale)
{
#if 0
    p *= scale;
    vec2 pi = floor(p);
    p = smoothen(p - pi);
    vec4 seed = fract(vec4(pi, pi + 1.) * (1./scale).xyxy);
    float s00 = random(seed.xy);
    float s01 = random(seed.zy);
    float s10 = random(seed.xw);
    float s11 = random(seed.zw);
    return mix(mix(s00, s01, p.x), mix(s10, s11, p.x), p.y);
#else
    return FAST32_smooth_noise(p, scale);
#endif
}

float tileable_turb(vec2 uv, vec2 scale, float gain, float lacunarity)
{
  float accum = tileable_smooth_noise(uv, scale);
    float octave_weight = gain;
    float total_weight = 1.;

    scale *= lacunarity;
    accum += tileable_smooth_noise(uv, scale) * octave_weight;
    total_weight += octave_weight;

    scale *= lacunarity; octave_weight *= gain;
    accum += tileable_smooth_noise(uv, scale) * octave_weight;
    total_weight += octave_weight;

    scale *= lacunarity; octave_weight *= gain;
    accum += tileable_smooth_noise(uv, scale) * octave_weight;
    total_weight += octave_weight;

    return accum / total_weight;
}

////////////////////////////////////////////////////////////////

vec4 generate_texture(const int material, vec2 uv)
{
    vec2 tile_size = get_tile(material).zw;

  vec3 clr;
    float shaded = 1.;  // 0 = fullbright; 0.5 = lit; 1.0 = lit+AO
    
    float grain = random(uv*128.);
    
    // gathering FBM parameters first and calling the function once
    // instead of per material reduces the compilation time for this buffer
    // by about 4 seconds (~9.4 seconds vs ~13.4) on my machine...
    
    // array-based version compiles about 0.7 seconds faster
    // than the equivalent switch (~14.1 seconds vs ~14.8)...

    const vec4 MATERIAL_SETTINGS[]=vec4[7](vec4(3,5,1,3),vec4(3,5,1,4),vec4(6,6,.5,3),vec4(10,10,.5,2),vec4(3,5,1,2),vec4(7,3,.5
    ,2),vec4(7,5,.5,2));
    const int MATERIAL_INDICES[]=int[NUM_MATERIALS+1](1,1,1,0,1,0,1,0,0,0,1,6,6,6,1,1,2,3,4,5,0);

    vec4 settings = MATERIAL_SETTINGS[MATERIAL_INDICES[min(uint(material), uint(NUM_MATERIALS))]];
    vec2 base_grid = settings.xy;
    float base_gain = settings.z;
    float base_lacunarity = settings.w;

    if (is_material_sky(material))
        uv += sin(uv.yx * (3.*PI)) * (4./128.);

    vec2 aspect = tile_size / min(tile_size.x, tile_size.y);
    float base = tileable_turb(uv * aspect, base_grid, base_gain, base_lacunarity);
    
    // this switch compiles ~2.2 seconds faster on my machine
    // than an equivalent if/else if chain (~11.5s vs ~13.7s)
    
  #define GENERATE(mat) ((GENERATE_TEXTURES) & (1<<(mat)))

    switch (material)
    {
#if GENERATE(MATERIAL_WIZMET1_2) || GENERATE(MATERIAL_QUAKE)
        case MATERIAL_WIZMET1_2:
        case MATERIAL_QUAKE:
        {
            uv.x *= tile_size.x/tile_size.y;
            uv += vec2(.125, .0625);
            base = mix(base, grain, .2);
            clr = mix(vec3(.16, .13, .06), vec3(.30, .23, .12), sqr(base));
            clr = mix(clr, vec3(.30, .23, .13), sqr(linear_step(.5, .9, base)));
            clr = mix(clr, vec3(.10, .10, .15), smoothen(linear_step(.7, .1, base)));
            if (material == MATERIAL_WIZMET1_2 || (material == MATERIAL_QUAKE && uv.y < .375))
            {
                vec2 knob_pos = floor(uv*4.+.5)*.25;
                vec2 knob = add_knob(uv, 1./64., knob_pos, 3./64., vec2(-.4, .4));
                clr = mix(clr, vec3(.22, .22, .28)*mix(1., knob.x, .8), knob.y);
                knob = add_knob(uv, 1./64., knob_pos, 1.5/64., vec2(.4, -.4));
                clr = mix(clr, .7*vec3(.22, .22, .28)*mix(1., knob.x, .7), knob.y);
            }
            if (material == MATERIAL_QUAKE)
            {
                uv -= vec2(1.375, .15625);
                uv.x = mod(uv.x, 5.);
                uv.y = fract(uv.y);
                vec2 engraved = engraved_QUAKE(uv, 5./64., vec2(0, -1));
                clr *= mix(1., mix(1., engraved.x*1.25, .875), engraved.y);
            }
        }
        break;
#endif

#if GENERATE(MATERIAL_WIZMET1_1)
        case MATERIAL_WIZMET1_1:
        {
            base = mix(base, grain, .4);
            float scratches = linear_step(.15, .9, smooth_noise(vec2(32,8)*rotate(uv, 22.5).x) * base);
            clr = vec3(.17, .17, .16) * mix(.5, 1.5, base);
            clr = mix(clr, vec3(.23, .19, .15), scratches);
            scratches *= linear_step(.6, .25, smooth_noise(vec2(16,4)*rotate(uv, -45.).x) * base);
            clr = mix(clr, vec3(.21, .21, .28) * 1.5, scratches);
            float bevel = .6 *mix(3.5/64., 11./64., base);
            float d = min(1., min(uv.x, 1.-uv.y) / bevel);
            float d2 = min(d, 3. * min(uv.y, 1.-uv.x) / bevel);
            clr *= 1. - (1. - d2) * mix(.3, .8, base);
            clr = mix(clr, vec3(.39, .39, .57) * base, around(.6, .4, d));
        }
        break;
#endif

#if GENERATE(MATERIAL_WIZ1_4)
        case MATERIAL_WIZ1_4:
        {
            base = mix(smoothen(base), grain, .3);
            clr = mix(vec3(.37, .28, .21), vec3(.52, .41, .33), smoothen(base));
            clr = mix(clr, vec3(.46, .33, .15), around(.45, .05, base));
            clr = mix(clr, vec3(.59, .48, .39), around(.75, .09, base)*.75);
            float bevel = mix(4./64., 12./64., FAST32_smooth_noise(uv, vec2(21)));
            vec2 mins = vec2(bevel, bevel * 2.);
            vec2 maxs = 1. - vec2(bevel, bevel * 2.);
            uv = running_bond(uv, 1., 2.) * vec2(1, 2);
            vec2 duv = (fract(uv) - clamp(fract(uv), mins, maxs)) * (1./bevel) * vec2(2, 1);
            float d = mix(length(duv), max_component(abs(duv)), .75);
            clr *= clamp(2.1 - d*mix(.75, 1., sqr(base)), 0., 1.);
            clr *= 1. + mix(.25, .5, base) * max(0., dot(duv, INV_SQRT2*vec2(-1,1)) * step(d, 1.2));
        }
    break;
#endif

#if GENERATE(MATERIAL_WBRICK1_5)
        case MATERIAL_WBRICK1_5:
        {
            vec2 uv2 = uv + sin(uv.yx * (3.*PI)) * (4./64.);
            uv = running_bond(uv + vec2(.5, 0), 1., 2.) * vec2(1, 2);
            base = mix(smoothen(base), grain, .3);
            float detail = tileable_smooth_noise(uv2, vec2(11));
            detail = sqr(around(.625, .25, detail)) * linear_step(.5, .17, base);
            clr = mix(vec3(.21, .17, .06)*.75, vec3(.30, .26, .15), base);
            clr *= mix(.95, 2., sqr(sqr(base)));
            clr = mix(clr, vec3(.41, .32, .14), detail);
            float bevel = mix(4./64., 8./64., base);
            vec2 mins = vec2(bevel, bevel * 1.75);
            vec2 maxs = 1. - vec2(bevel, bevel * 2.);
            vec2 duv = (fract(uv) - clamp(fract(uv), mins, maxs)) * (1./bevel) * vec2(2, 1);
            float d = length(duv);
            if (uv.y > 1. || uv.y < .5)
                d = mix(d, max_component(abs(duv)), .5);
            //clr *= mix(1., mix(.25, .625, base), linear_step(1., 2., d)*step(1.5, d));
            clr *= clamp(2.1 - d*mix(.75, 1., sqr(base)), 0., 1.);
            clr *= 1. + mix(.25, .5, base) * max(0., dot(duv, INV_SQRT2*vec2(-1,1)) * step(d, 1.2));
        }
        break;
#endif

#if GENERATE(MATERIAL_CITY4_7)
        case MATERIAL_CITY4_7:
        {
            base = mix(base, grain, .4);
            vec3 brick = herringbone(uv);
            uv = brick.xy;
            clr = mix(vec3(.23, .14, .07), vec3(.29, .16, .08), brick.z) * mix(.3, 1.7, base);
            clr = mix(clr, vec3(.24, .18, .10), linear_step(.6, .9, base));
            clr = mix(clr, vec3(.47, .23, .12), linear_step(.9, 1., sqr(grain))*.6);
            clr *= (1. + add_bevel(uv, 2., 4., mix(1.5/64., 2.5/64., base), -mix(.05, .15, grain), 0.6));
        }
        break;
#endif

#if GENERATE(MATERIAL_CITY4_6)
    case MATERIAL_CITY4_6:
        {
            base = mix(base, grain, .5);
            vec3 brick = herringbone(uv);
            uv = brick.xy;
            clr = mix(vec3(.09, .08, .01)*1.25, 2.*vec3(.21, .15, .08), sqr(base));
            clr *= mix(.85, 1., brick.z);
            clr = mix(clr, mix(.25, 1.5, sqr(base))*vec3(.11, .11, .22), around(.8, mix(.24, .17, brick.z), (grain)));
            clr = mix(clr, mix(.75, 1.5, base)*vec3(.26, .20, .10), .75*sqr(around(.8, .2, (base))));
            clr *= (1. + add_bevel(uv, 2., 4., 2.1/64., .0, .25));
            clr *= (1. + add_bevel(uv, 2., 4., mix(1.5/64., 2.5/64., base), mix(.25, .05, grain), .35));
        }
        break;
#endif

#if GENERATE(MATERIAL_DEM4_1)
        case MATERIAL_DEM4_1:
        {
            base = mix(base, grain, .2);
            clr = mix(vec3(.18, .19, .21), vec3(.19, .15, .06), linear_step(.4, .7, base));
            shaded = .75; // lit, half-strength AO
        }
        break;
#endif

#if GENERATE(MATERIAL_COP3_4)
        case MATERIAL_COP3_4:
        {
            float sdf = sdf_chickenscratch(uv, vec2(.25, .125), vec2(.75, .375), 1.5/64.);
            base = mix(base, grain, .2);
            base *= mix(1., .625, sdf_mask(sdf, 1./64.));
            clr = mix(vec3(.14, .15, .13), vec3(.41, .21, .12), base);
            clr = mix(clr, vec3(.30, .32, .34), linear_step(.6, 1., base));
            float bevel = mix(2./64., 6./64., sqr(FAST32_smooth_noise(uv, vec2(13))));
            clr *= (1. + add_bevel(uv, 1., 1., bevel, .5, .5));
            clr *= 1.5;
        }
        break;
#endif

#if GENERATE(MATERIAL_BRICKA2_2) || GENERATE(MATERIAL_WINDOW02_1)
        case MATERIAL_BRICKA2_2:
        case MATERIAL_WINDOW02_1:
        {
            vec2 grid = (material == MATERIAL_BRICKA2_2) ? vec2(6., 5.) : vec2(8., 24.);
            uv = (material == MATERIAL_WINDOW02_1) ? fract(uv + vec2(.5, .5/3.)) : uv;
            vec3 c = voronoi(uv, grid);
            if (material == MATERIAL_BRICKA2_2)
            {
                float dark_edge = linear_step(.0, mix(.05, .45, base), c.z);
                float lit_edge = linear_step(.35, .25, c.z);
                float lighting = -normalize(c.xy).y * .5;
                clr = vec3(.25, .18, .10) * mix(.8, 1.2, grain);
                clr *= (1. + lit_edge * lighting) * mix(.35, 1., dark_edge);
                uv = fract(running_bond(uv, 1., 2.) * vec2(1, 2));
                clr *=
                    mix(1., min(1., 4.*min(uv.y, 1.-uv.y)), .5) *
                  mix(1., min(1., 8.*min(uv.x, 1.-uv.x)), .3125);
                clr *= 1.25;
            }
            else
            {
                // Note: using x*48 instead of x/fwidth(x) reduces compilation time
                // for this buffer by ~23% (~10.3 seconds vs ~13.4) on my system
                float intensity = mix(1.25, .75, hash1(uv*grid + c.xy)) * (1. - .5*length(c.xy));
                uv.y *= 3.;
                float flame = sdf_window_flame(uv) * 48.;
                float emblem = sdf_window_emblem(uv) * 48.;
                float edge = linear_step(.0, .15, c.z);
                clr = mix(vec3(1., .94, .22) * 1.125, vec3(.63, .30, .19), clamp(flame, 0., 1.));
                clr = mix(clr, vec3(.55, .0, .0), clamp(1.-emblem, 0., 1.));
                clr = mix(vec3(dot(clr, vec3(1./3.))), clr, intensity);
                edge *= clamp(abs(flame), 0., 1.) * clamp(abs(emblem), 0., 1.);
                edge *= step(max(abs(uv.x - .5) - .5, abs(uv.y - 1.5) - 1.5), -2./64.);
                clr *= intensity * edge;
                shaded = .75; // lit, half-strength AO
            }
      }
        break;
#endif

#if GENERATE(MATERIAL_LAVA1) || GENERATE(MATERIAL_WATER2) || GENERATE(MATERIAL_WATER1)
        case MATERIAL_LAVA1:
        case MATERIAL_WATER2:
        case MATERIAL_WATER1:
        {
            vec2 grid = (material == MATERIAL_WATER1) ? vec2(5., 7.) : vec2(5., 5.);
            uv += base * (1./31.) * sin(PI * 7. * uv.yx);
            float cellular = Cellular2D(uv, grid);
            float grain_amount = (material == MATERIAL_LAVA1) ? .125 : .25;
            float high_point = (material == MATERIAL_WATER2) ? .8 : .9;
            base = mix(base, grain, grain_amount);
            cellular = sqrt(cellular) + mix(-.3, .3, base);
            base = linear_step(.1, high_point, cellular);
            if (material == MATERIAL_LAVA1)
            {
                clr = mix(vec3(.24,.0,.0), vec3(1.,.40,.14), base);
                clr = mix(clr, vec3(1.,.55,.23), linear_step(.5, 1., base));
            }
            else if (material == MATERIAL_WATER2)
            {
                clr = mix(vec3(.10,.10,.14)*.8, vec3(.17,.17,.24)*.8, base);
                clr = mix(clr, vec3(.16,.13,.06)*mix(.8, 2.5, sqr(sqr(base))), around(.5, .1, grain));
                clr = mix(clr, vec3(.20,.20,.29)*.8, linear_step(.5, 1., base));
            }
            else // if (material == MATERIAL_WATER1)
            {
                clr = mix(vec3(.08,.06,.04), vec3(.30,.23,.13), base);
                clr = mix(clr, vec3(.36,.28,.21), linear_step(.5, 1., base));
            }
            shaded = 0.;
        }
        break;
#endif

#if GENERATE(MATERIAL_WIZWOOD1_5)
    case MATERIAL_WIZWOOD1_5:
        {
            const vec2 GRID = vec2(1, 4);
            uv = running_bond(fract(uv.yx), GRID.x, GRID.y);
            vec2 waviness = vec2(sin(3. * TAU * uv.y), 0) * .0;
            waviness.x += smooth_noise(uv.y * 16.) * (14./64.);
            base = tileable_turb(uv + waviness, vec2(2, 32), .5, 3.);
            clr = mix(vec3(.19, .10, .04)*1.25, vec3(.64, .26, .17), around(.5, .4, smoothen(base)));
            clr = mix(clr, vec3(.32, .17, .08), around(.7, .3, base)*.7);
            
            float across = fract(uv.y * GRID.y);
            clr *= 1. + .35 * linear_step(1.-4./16., 1.-2./16., across) * step(across, 1.-2./16.);
            across = min(across, 1. - across);
            clr *= mix(1., linear_step(0., 2./16., across), mix(.25, .75, base));
      float along = fract(uv.x * GRID.x);
            clr *= 1. + .25 * linear_step(2./64., 0., along);
            clr *= mix(1., linear_step(1., 1.-2.5/64., along), mix(.5, .75, base));
            
            const vec2 LIGHT_DIR = INV_SQRT2 * vec2(-1, 1);
            uv = fract(uv * GRID);
            vec2 side = sign(.5 - uv); // keep track of side before folding to 'unmirror' light direction
            uv = min(uv, 1. - uv) * (1./GRID);
            vec2 nail = add_knob(uv, 1./64., vec2(4./64.), 1./64., side * LIGHT_DIR);
            clr = mix(clr, vec3(.64, .26, .17) * nail.x, nail.y * .75);

            clr *= .9 + grain*.2;
            clr *= .75;
        }
        break;
#endif

#if GENERATE(MATERIAL_TELEPORT)
        case MATERIAL_TELEPORT:
        {
            uv *= 64./3.;
            vec2 cell = floor(uv);
            vec4 n = hash4(cell);
            uv -= cell;
            float radius = mix(.15, .5, sqr(sqr(n.z)));
            n.xy = mix(vec2(radius), vec2(1.-radius), smoothen(n.xy));
            uv = clamp((n.xy - uv) * (1./radius), -1., 1.);
            clr = (1.-length_squared(uv)) * (1.-sqr(sqr(n.w))) * vec3(.44, .36, .26);
            shaded = 0.;
        }
        break;
#endif

#if GENERATE(MATERIAL_FLAME)
        case MATERIAL_FLAME:
        {
            base = mix(base, grain, .1);
            clr = mix(vec3(.34, .0, .0), vec3(1., 1., .66), smoothen(base));
            clr = clamp(clr * 1.75, 0., 1.);
            shaded = 0.;
        }
        break;
#endif

#if GENERATE(MATERIAL_ZOMBIE)
        case MATERIAL_ZOMBIE:
        {
            base = mix(base, grain, .2);
            clr = vec3(.57, .35, .24) * mix(.6, 1., sqr(base));
            clr = mix(clr, vec3(.17, .08, .04), linear_step(.3, .7, base));
        }
        break;
#endif

#if GENERATE(MATERIAL_SKY1)
        case MATERIAL_SKY1:
        {
            clr = vec3(.18, .10, .12) * 1.5 * smoothen(base);
            shaded = 0.;
        }
        break;
#endif

#if GENERATE(MATERIAL_SKY1B)
        case MATERIAL_SKY1B:
        {
            clr = vec3(.36, .19, .23) * 1.125 * smoothen(base);
            shaded = 0.;
        }
        break;
#endif

        default:
        {
            clr = vec3(base * .75);
        }
        break;
    }
    
  #undef GENERATE
    
    clr = clamp(clr, 0., 1.);

    return vec4(clr, shaded);
}

void generate_tiles(inout vec4 fragColor, vec2 fragCoord, float lod)
{
    float atlas_scale = exp2(-lod);
    if (is_inside(fragCoord, atlas_mip0_bounds(atlas_scale)) < 0.)
        return;
    
    int material = -1;
    vec4 bound;
    
    int num_materials = NO_UNROLL(NUM_MATERIALS);
    for (int i=0; i<num_materials; ++i)
    {
        bound = get_tile(i) * atlas_scale;
        bound.xy += ATLAS_OFFSET;
        if (is_inside(fragCoord, bound) > 0.)
        {
            material = i;
            break;
        }
    }
    
    if (material == -1)
        return;
    
    vec2 local_uv = (fragCoord - bound.xy) / bound.zw;
    fragColor = generate_texture(material, local_uv);
}

void update_mips(inout vec4 fragColor, vec2 fragCoord, float atlas_lod, inout int available_mips)
{
    int mip_start = ALWAYS_REFRESH > 0 ? 1 : available_mips;
    available_mips = min(available_mips + 1, MAX_MIP_LEVEL + 1 - int(atlas_lod));
    
    float atlas_scale = exp2(-atlas_lod);

    if (is_inside(fragCoord, atlas_chain_bounds(atlas_scale)) < 0.)
        return;
    if (is_inside(fragCoord, atlas_mip0_bounds(atlas_scale)) > 0.)
        return;

    int mip_end = available_mips;
    int mip;
    vec2 atlas_size = ATLAS_SIZE * atlas_scale;
    vec2 ofs;
    for (mip=mip_start; mip<mip_end; ++mip)
    {
        float fraction = exp2(-float(mip));
        ofs = mip_offset(mip) * atlas_size + ATLAS_OFFSET;
        vec2 size = atlas_size * fraction;
        if (is_inside(fragCoord, vec4(ofs, size)) > 0.)
            break;
    }
    
    if (mip == mip_end)
        return;
    
    vec2 src_ofs = mip_offset(mip-1) * atlas_size + ATLAS_OFFSET;
    vec2 uv = fragCoord - ofs - .5;

    // A well-placed bilinear sample would be almost equivalent,
    // except the filtering would be done in sRGB space instead
    // of linear space. Of course, the textures could be created
    // in linear space to begin with, since we're rendering to
    // floating-point buffers anyway... but then we'd be a bit too
    // gamma-correct for 1996 :)

    ivec4 iuv = ivec2(uv * 2. + src_ofs).xyxy + ivec2(0, 1).xxyy;
    vec4 t00 = gamma_to_linear(texelFetch(iChannel1, iuv.xy, 0));
    vec4 t01 = gamma_to_linear(texelFetch(iChannel1, iuv.xw, 0));
    vec4 t10 = gamma_to_linear(texelFetch(iChannel1, iuv.zy, 0));
    vec4 t11 = gamma_to_linear(texelFetch(iChannel1, iuv.zw, 0));

    fragColor = linear_to_gamma((t00 + t01 + t10 + t11) * .25);
}

void update_tiles(inout vec4 fragColor, vec2 fragCoord)
{
    const vec4 SENTINEL_COLOR = vec4(1, 0, 1, 0);
    
    vec4 resolution = vec4(iResolution.xy, 0, 0);
    vec4 old_resolution = (iFrame==0) ? vec4(0) : load(ADDR_RESOLUTION);
    int flags = int(old_resolution.z);
    if (iFrame == 0 && iTime >= THUMBNAIL_MIN_TIME)
        flags |= RESOLUTION_FLAG_THUMBNAIL;
    vec4 atlas_info = (iFrame==0) ? vec4(0) : load(ADDR_ATLAS_INFO);
    int available_mips = int(round(atlas_info.x));
   
    vec2 available_space = (resolution.xy - ATLAS_OFFSET) / ATLAS_CHAIN_SIZE;
    float atlas_lod = max(0., -floor(log2(min(available_space.x, available_space.y))));
    if (atlas_lod != atlas_info.y)
        available_mips = 0;
    if (max(abs(resolution.x-old_resolution.x), abs(resolution.y-old_resolution.y)) > .5)
        flags |= RESOLUTION_FLAG_CHANGED;
    
    // Workaround for Shadertoy double-buffering bug on resize
    // (this.mBuffers[i].mLastRenderDone = 0; in effect.js/Effect.prototype.ResizeBuffer)
    vec2 sentinel_address = ATLAS_OFFSET + ATLAS_CHAIN_SIZE * exp2(-atlas_lod) - 1.;
    vec4 sentinel = (iFrame == 0) ? vec4(0) : load(sentinel_address);
    if (any(notEqual(sentinel, SENTINEL_COLOR)))
    {
        available_mips = 0;
        flags |= RESOLUTION_FLAG_CHANGED;
    }
    
    resolution.z = float(flags);

    if (available_mips > 0)
      update_mips(fragColor, fragCoord, atlas_lod, available_mips);
    
    if (ALWAYS_REFRESH > 0 || available_mips == 0)
    {
        if (available_mips == 0)
          store(fragColor, fragCoord, ADDR_RANGE_ATLAS_CHAIN, vec4(0.));
        generate_tiles(fragColor, fragCoord, atlas_lod);
        available_mips = max(available_mips, 1);
    }
    atlas_info.x = float(available_mips);
    atlas_info.y = atlas_lod;

    store(fragColor, fragCoord, ADDR_RESOLUTION, resolution);
    store(fragColor, fragCoord, ADDR_ATLAS_INFO, atlas_info);
    store(fragColor, fragCoord, sentinel_address, SENTINEL_COLOR);
}

////////////////////////////////////////////////////////////////

#define T(x0,y0,z0,x1,y1,z1) vec3(x0,y0,z0),vec3(x1,y1,z1)

WRAP(Teleporters,teleporters,vec3,6)(T(208,1368,-0,256,1384,96),T(520,1368,-0,568,1384,96),T(840,1368,-0,888,1384,96)));

bool touch_tele(vec3 pos, float radius)
{
    radius *= radius;
    bool touch = false;
    for (int i=0; i<6; i+=2)
    {
        vec3 mins = teleporters.data[i];
        vec3 maxs = teleporters.data[i+1];
        vec3 delta = clamp(pos, mins, maxs) - pos;
        if (dot(delta, delta) <= radius)
            touch = true;
    }
    return touch;
}

////////////////////////////////////////////////////////////////

// returns true if processing should stop
bool fire_weapon(inout vec4 fragColor, vec2 fragCoord,
                 vec3 old_pos, vec3 old_angles,
                 inout float attack_cycle, inout float shots_fired)
{
  const float ATTACK_DURATION = .75;
    const float ATTACK_WAIT = 1. - 1./(ATTACK_DURATION*RATE_OF_FIRE);

    if (attack_cycle > 0.)
        attack_cycle = max(0., attack_cycle - iTimeDelta * (1./ATTACK_DURATION));
    
    bool wants_to_fire = cmd_attack() > 0.;
    bool resolution_changed = any(notEqual(load(ADDR_RESOLUTION).xy, iResolution.xy));
    if (attack_cycle > ATTACK_WAIT || !wants_to_fire || iFrame <= 0 || resolution_changed)
        return false;

    attack_cycle = 1.;
    shots_fired += 1.;
    if (is_inside(fragCoord, ADDR_RANGE_SHOTGUN_PELLETS) < 0.)
        return false;
    
    Options options;
    LOAD_PREV(options);
    
    float prev_downscale = get_downscale(options);
    vec4 ndc_scale_bias = get_viewport_transform(iFrame-1, iResolution.xy, prev_downscale);
    vec2 ndc = hash2(iTime + fragCoord) * (WEAPON_SPREAD*2.) + -WEAPON_SPREAD;
    vec2 coord = iResolution.xy * (ndc - ndc_scale_bias.zw) / ndc_scale_bias.xy;
    mat3 prev_view_matrix = rotation(old_angles);
    vec3 fire_dir = prev_view_matrix * unproject(ndc);
    GBuffer gbuffer = gbuffer_unpack(texelFetch(iChannel2, ivec2(coord), 0));
    vec3 normal = gbuffer.normal;

    fragColor.xyz = old_pos + fire_dir * (gbuffer.z * VIEW_DISTANCE);
    int material = (gbuffer.z > 12./VIEW_DISTANCE) ? gbuffer.material : MATERIAL_SKY1B;
    fragColor.w = float(material);
    
    // prevent particles from clipping the ground when falling
    // not using 0 as threshold since reconstructed normal isn't 100% accurate
    if (normal.z > .01) 
        fragColor.z += 8.;
    
    return true;
}

////////////////////////////////////////////////////////////////

void update_ideal_pitch(vec3 pos, vec3 forward, vec3 velocity, inout float ideal_pitch)
{
    if (iMouse.z > 0. || length_squared(velocity.xy) < sqr(WALK_SPEED/4.))
        return;
    
    if (dot(forward, normalize(velocity)) < .7)
    {
        ideal_pitch = 0.;
        return;
    }
    
    // look up/down near stairs
    // totally ad-hoc, but it kind of works...
  const vec3 STAIRS[] = vec3[](vec3(272, 496, 24), vec3(816, 496, 24));

    vec3 to_stairs = closest_point_on_segment(pos, STAIRS[0], STAIRS[1]) - pos;
    float sq_dist = length_squared(to_stairs);
    if (sq_dist < sqr(48.))
        return;
    
    float facing_stairs = dot(to_stairs, forward);
    if (sq_dist > (facing_stairs > 0. ? sqr(144.) : sqr(64.)))
    {
        ideal_pitch = 0.;
        return;
    }
    
    if (facing_stairs * inversesqrt(sq_dist) < .7)
        return;

    ideal_pitch = to_stairs.z < 0. ? -STAIRS_PITCH : STAIRS_PITCH;
}

////////////////////////////////////////////////////////////////

#if 0 // 4:3
const vec3 SPAWN_POS    = vec3(544, 296, 49);
const vec4 DEFAULT_ANGLES = vec4(0);
#else
const vec3 SPAWN_POS    = vec3(544, 272, 49);
const vec4 DEFAULT_ANGLES = vec4(0, 5, 5, 0);
#endif
const vec4 DEFAULT_POS    = vec4(SPAWN_POS, 0);

void update_input(inout vec4 fragColor, vec2 fragCoord)
{
    float allow_input = is_input_enabled();
    vec4 pos      = (iFrame==0) ? DEFAULT_POS : load(ADDR_POSITION);
    vec4 angles     = (iFrame==0) ? DEFAULT_ANGLES : load(ADDR_ANGLES);
    vec4 old_pos    = (iFrame==0) ? DEFAULT_POS : load(ADDR_CAM_POS);
    vec4 old_angles   = (iFrame==0) ? DEFAULT_ANGLES : load(ADDR_CAM_ANGLES);
    vec4 velocity   = (iFrame==0) ? vec4(0) : load(ADDR_VELOCITY);
    vec4 ground_plane = (iFrame==0) ? vec4(0) : load(ADDR_GROUND_PLANE);
    bool thumbnail    = (iFrame==0) ? true : (int(load(ADDR_RESOLUTION).z) & RESOLUTION_FLAG_THUMBNAIL) != 0;
    
    Transitions transitions;
    LOAD_PREV(transitions);
    
    MenuState menu;
    LOAD_PREV(menu);
    if (iFrame > 0 && menu.open > 0)
        return;
    
    if (iFrame == 0 || is_demo_mode_enabled(thumbnail))
        allow_input = 0.;

    if (allow_input > 0. && fire_weapon(fragColor, fragCoord, old_pos.xyz, old_angles.xyz, transitions.attack, transitions.shot_no))
        return;
    
    Options options;
    LOAD_PREV(options);

    angles.w = max(0., angles.w - iTimeDelta);
    if (angles.w == 0.)
      angles.y = mix(angles.z, angles.y, exp2(-8.*iTimeDelta));

  vec4 mouse_status = (iFrame==0) ? vec4(0) : load(ADDR_PREV_MOUSE);
    if (allow_input > 0.)
    {
        float mouse_lerp = MOUSE_FILTER > 0. ?
            min(1., iTimeDelta/.0166 / (MOUSE_FILTER + 1.)) :
          1.;
        if (iMouse.z > 0.)
        {
            float mouse_y_scale = INVERT_MOUSE != 0 ? -1. : 1.;
            if (test_flag(options.flags, OPTION_FLAG_INVERT_MOUSE))
                mouse_y_scale = -mouse_y_scale;
            float sensitivity = SENSITIVITY * exp2((options.sensitivity - 5.) * .5);
            
            if (iMouse.z > mouse_status.z)
                mouse_status = iMouse;
            vec2 mouse_delta = (iMouse.z > mouse_status.z) ?
                vec2(0) : mouse_status.xy - iMouse.xy;
            mouse_delta.y *= -mouse_y_scale;
            angles.xy += 360. * sensitivity * mouse_lerp / max_component(iResolution.xy) * mouse_delta;
            angles.z = angles.y;
            angles.w = AUTOPITCH_DELAY;
        }
        mouse_status = vec4(mix(mouse_status.xy, iMouse.xy, mouse_lerp), iMouse.zw);
    }
    
    float strafe = cmd_strafe();
    float run = (cmd_run()*.5 + .5) * allow_input;
    float look_side = cmd_look_left() - cmd_look_right();
    angles.x += look_side * (1. - strafe) * run * TURN_SPEED * iTimeDelta;
    float look_up = cmd_look_up() - cmd_look_down();
    angles.yz += look_up * run * TURN_SPEED * iTimeDelta;
    // delay auto-pitch for a bit after looking up/down
    if (abs(look_up) > 0.)
        angles.w = .5;
    if (cmd_center_view() * allow_input > 0.)
        angles.zw = vec2(0);
    angles.x = mod(angles.x, 360.);
    angles.yz = clamp(angles.yz, -80., 80.);

#if NOCLIP
    const bool noclip = true;
#else
    bool noclip = test_flag(options.flags, OPTION_FLAG_NOCLIP);
#endif

    mat3 move_axis = rotation(vec3(angles.x, noclip ? angles.y : 0., 0));

    vec3 input_dir    = vec3(0);
    input_dir     += (cmd_move_forward() - cmd_move_backward()) * move_axis[1];
    float move_side   = cmd_move_right() - cmd_move_left();
    move_side     = clamp(move_side - look_side * strafe, -1., 1.);
    input_dir     += move_side * move_axis[0];
    input_dir.z     += (cmd_move_up() - cmd_move_down());
    float wants_to_move = step(0., dot(input_dir, input_dir));
    float wish_speed  = WALK_SPEED * allow_input * wants_to_move * (1. + -.5 * run);

    float lava_dist   = max_component(abs(pos.xyz - clamp(pos.xyz, LAVA_BOUNDS[0], LAVA_BOUNDS[1])));

  if (noclip)
    {
        float friction = mix(NOCLIP_STOP_FRICTION, NOCLIP_START_FRICTION, wants_to_move);
        float velocity_blend = exp2(-friction * iTimeDelta);
        velocity.xyz = mix(input_dir * wish_speed, velocity.xyz, velocity_blend);
        pos.xyz += velocity.xyz * iTimeDelta;
        ground_plane = vec4(0);
    }
    else
    {
        // if not ascending, allow jumping when we touch the ground
        if (input_dir.z <= 0.)
            velocity.w = 0.;
        
        input_dir.xy = safe_normalize(input_dir.xy);
        
        bool on_ground = is_touching_ground(pos.xyz, ground_plane);
        if (on_ground)
        {
            // apply friction
            float speed = length(velocity.xy);
            if (speed < 1.)
            {
                velocity.xy = vec2(0);
            }
            else
            {
                float drop = max(speed, STOP_SPEED) * GROUND_FRICTION * iTimeDelta;
                velocity.xy *= max(0., speed - drop) / speed;
            }
        }
        else
        {
            input_dir.z = 0.;
        }

        if (lava_dist <= 0.)
            wish_speed *= .25;

        // accelerate
    float current_speed = dot(velocity.xy, input_dir.xy);
    float add_speed = wish_speed - current_speed;
    if (add_speed > 0.)
        {
      float accel = on_ground ? GROUND_ACCELERATION : AIR_ACCELERATION;
      float accel_speed = min(add_speed, accel * iTimeDelta * wish_speed);
            velocity.xyz += input_dir * accel_speed;
    }

        if (on_ground)
        {
            velocity.z -= (GRAVITY * .25) * iTimeDelta; // slowly slide down slopes
            velocity.xyz -= dot(velocity.xyz, ground_plane.xyz) * ground_plane.xyz;

            if (transitions.stair_step <= 0.)
                transitions.bob_phase = fract(transitions.bob_phase + iTimeDelta * (1./BOB_CYCLE));

            update_ideal_pitch(pos.xyz, move_axis[1], velocity.xyz, angles.z);

            if (input_dir.z > 0. && velocity.w <= 0.)
            {
                velocity.z += JUMP_SPEED;
                // wait for the jump key to be released
                // before jumping again (no auto-hopping)
                velocity.w = 1.;
            }
        }
        else
        {
            velocity.z -= GRAVITY * iTimeDelta;
        }

        if (is_inside(fragCoord, ADDR_RANGE_PHYSICS) > 0.)
            slide_move(pos.xyz, velocity.xyz, ground_plane, transitions.stair_step);
    }

    bool teleport = touch_tele(pos.xyz, 16.);
    if (!noclip)
      teleport = teleport || ((DEFAULT_POS.z - pos.z) > VIEW_DISTANCE); // falling too far below the map

    if (cmd_respawn() * allow_input > 0. || teleport)
    {
        pos = vec4(DEFAULT_POS.xyz, iTime);
        angles = teleport ? vec4(0) : DEFAULT_ANGLES;
        velocity.xyz = vec3(0, teleport ? WALK_SPEED : 0., 0);
        ground_plane = vec4(0);
        transitions.stair_step = 0.;
        transitions.bob_phase = 0.;
    }
    
    // smooth stair stepping
    transitions.stair_step = max(0., transitions.stair_step - iTimeDelta * STAIR_CLIMB_SPEED);

    vec4 cam_pos = pos;
    cam_pos.z -= transitions.stair_step;
    
    // bobbing
    float speed = length(velocity.xy);
    if (speed < 1e-2)
        transitions.bob_phase = 0.;
    cam_pos.z += clamp(speed * BOB_SCALE * (.3 + .7 * sin(TAU * transitions.bob_phase)), -7., 4.);
    
    vec4 cam_angles = vec4(angles.xy, 0, 0);
    
    // side movement roll
    cam_angles.z += clamp(dot(velocity.xyz, move_axis[0]) * (1./ROLL_SPEED), -1., 1.) * ROLL_ANGLE;

    // lava pain roll
    if (lava_dist <= 32.)
      cam_angles.z += 5. * clamp(fract(iTime*4.)*-2.+1., 0., 1.);
    
    // shotgun recoil
    cam_angles.y += linear_step(.75, 1., transitions.attack) * RECOIL_ANGLE;

    store(fragColor, fragCoord, ADDR_POSITION, pos);
    store(fragColor, fragCoord, ADDR_ANGLES, angles);
    store(fragColor, fragCoord, ADDR_CAM_POS, cam_pos);
    store(fragColor, fragCoord, ADDR_CAM_ANGLES, cam_angles);
    store(fragColor, fragCoord, transitions);
    store(fragColor, fragCoord, ADDR_PREV_CAM_POS, old_pos);
    store(fragColor, fragCoord, ADDR_PREV_CAM_ANGLES, old_angles);
    store(fragColor, fragCoord, ADDR_PREV_MOUSE, mouse_status);
    store(fragColor, fragCoord, ADDR_VELOCITY, velocity);
    store(fragColor, fragCoord, ADDR_GROUND_PLANE, ground_plane);
}

////////////////////////////////////////////////////////////////

void update_perf_stats(inout vec4 fragColor, vec2 fragCoord)
{
    vec4 perf = (iFrame==0) ? vec4(0) : load(ADDR_PERF_STATS);
    perf.x = mix(perf.x, iTimeDelta*1000., 1./16.);
    store(fragColor, fragCoord, ADDR_PERF_STATS, perf);
    
  // shift old perf samples
    const vec4 OLD_SAMPLES = ADDR_RANGE_PERF_HISTORY + vec4(1,0,-1,0);
    if (is_inside(fragCoord, OLD_SAMPLES) > 0.)
        fragColor = texelFetch(iChannel1, ivec2(fragCoord)-ivec2(1,0), 0);

    // add new sample
    if (is_inside(fragCoord, ADDR_RANGE_PERF_HISTORY.xy) > 0.)
    {
        Options options;
        LOAD_PREV(options);
        fragColor = vec4(iTimeDelta*1000., get_downscale(options), 0., 0.);
    }
}

void update_game_rules(inout vec4 fragColor, vec2 fragCoord)
{
    if (is_inside(fragCoord, ADDR_RANGE_TARGETS) > 0.)
    {
        Target target;
        Transitions transitions;
        GameState game_state;
        
        from_vec4(target, fragColor);
        LOAD_PREV(game_state);
        LOAD_PREV(transitions);
        float level = floor(game_state.level);
        float index = floor(fragCoord.x - ADDR_RANGE_TARGETS.x);

        if (target.level != level)
        {
            target.level = level;
            target.shot_no = transitions.shot_no;
            if (level > 0. || index == SKY_TARGET_OFFSET.x)
              target.hits = 0.;
            to_vec4(fragColor, target);
            return;
        }

        // already processed this shot?
        if (target.shot_no == transitions.shot_no)
            return;
        target.shot_no = transitions.shot_no;
        
        // disable popping during game over animation
        if (game_state.level < 0. && game_state.level != floor(game_state.level))
            return;

        float target_material = index < float(NUM_TARGETS) ? index + float(BASE_TARGET_MATERIAL) : float(MATERIAL_SKY1);
        int hits = 0;

        // The smart thing to do here would be to split the sum over several frames
        // in a binary fashion, but the shader is already pretty complicated,
        // so to make my life easier I'll go with a naive for loop.
        // To save face, let's say I'm doing this to avoid the extra latency
        // of log2(#pellets) frames the smart method would incur...

        for (float f=0.; f<ADDR_RANGE_SHOTGUN_PELLETS.z; ++f)
        {
            vec4 pellet = load(ADDR_RANGE_SHOTGUN_PELLETS.xy + vec2(f, 0.));
            hits += int(pellet.w == target_material);
        }
        
        // sky target is all or nothing
        if (target_material == float(MATERIAL_SKY1))
            hits = int(hits == int(ADDR_RANGE_SHOTGUN_PELLETS.z));
        
        target.hits += float(hits);
        to_vec4(fragColor, target);

        return;
    }
    
    if (is_inside(fragCoord, ADDR_GAME_STATE) > 0.)
    {
        const float
            ADVANCE_LEVEL     = 1. + LEVEL_WARMUP_TIME * .1,
          FIRST_ROUND_DURATION  = 15.,
          MIN_ROUND_DURATION    = 6.,
          ROUND_TIME_DECAY    = -1./8.;
        
        GameState game_state;
        from_vec4(game_state, fragColor);
        
        MenuState menu;
        LOAD(menu);
        float time_delta = menu.open > 0 ? 0. : iTimeDelta;

        if (game_state.level <= 0.)
        {
            float level = ceil(game_state.level);
            if (level < 0. && game_state.level != level)
            {
                game_state.level = min(level, game_state.level + time_delta * .1);
                to_vec4(fragColor, game_state);
                return;
            }
            Target target;
            LOADR(SKY_TARGET_OFFSET, target);
            if (target.hits > 0. && target.level == game_state.level)
            {
                game_state.level = ADVANCE_LEVEL;
                game_state.time_left = FIRST_ROUND_DURATION;
                game_state.targets_left = float(NUM_TARGETS);
            }
        }
        else
        {
            float level = floor(game_state.level);
            if (level != game_state.level)
            {
                game_state.level = max(level, game_state.level - time_delta * .1);
                to_vec4(fragColor, game_state);
                return;
            }
            
            game_state.time_left = max(0., game_state.time_left - time_delta);
            if (game_state.time_left == 0.)
            {
                game_state.level = -(level + BALLOON_SCALEIN_TIME * .1);
                to_vec4(fragColor, game_state);
                return;
            }
            
            float targets_left = 0.;
            Target target;
            for (vec2 addr=vec2(0); addr.x<ADDR_RANGE_TARGETS.z-1.; ++addr.x)
            {
                LOADR(addr, target);
                if (target.hits < ADDR_RANGE_SHOTGUN_PELLETS.z * .5 || target.level != level)
                    ++targets_left;
            }
            
            if (floor(game_state.targets_left) != targets_left)
                game_state.targets_left = targets_left + HUD_TARGET_ANIM_TIME * .1;
            else
                game_state.targets_left = max(floor(game_state.targets_left), game_state.targets_left - time_delta * .1);

            if (targets_left == 0.)
            {
                game_state.level = level + ADVANCE_LEVEL;
                game_state.time_left *= .5;
                game_state.time_left += mix(MIN_ROUND_DURATION, FIRST_ROUND_DURATION, exp2(level*ROUND_TIME_DECAY));
                game_state.targets_left = float(NUM_TARGETS);
            }
        }

        to_vec4(fragColor, game_state);
        return;
    }
}

////////////////////////////////////////////////////////////////

void update_menu(inout vec4 fragColor, vec2 fragCoord)
{
#if ENABLE_MENU
    if (is_inside(fragCoord, ADDR_MENU) > 0.)
    {
        MenuState menu;
        if (iFrame == 0)
            clear(menu);
        else
            from_vec4(menu, fragColor);

      if (is_input_enabled() > 0.)
        {
            if (cmd_menu() > 0.)
            {
                menu.open ^= 1;
            }
            else if (menu.open > 0)
            {
                menu.selected += int(is_key_pressed(KEY_DOWN) > 0.) - int(is_key_pressed(KEY_UP) > 0.) + NUM_OPTIONS;
                menu.selected %= NUM_OPTIONS;
            }
        }
       
        to_vec4(fragColor, menu);
        return;
    }
    
    if (is_inside(fragCoord, ADDR_OPTIONS) > 0.)
    {
        if (iFrame == 0)
        {
            Options options;
            clear(options);
            to_vec4(fragColor, options);
            return;
        }
        
        MenuState menu;
        LOAD(menu);

        int screen_size_field = get_option_field(OPTION_DEF_SCREEN_SIZE);
        float screen_size = fragColor[screen_size_field];
        if (is_key_pressed(KEY_1) > 0.)   screen_size = 10.;
        if (is_key_pressed(KEY_2) > 0.)   screen_size = 8.;
        if (is_key_pressed(KEY_3) > 0.)   screen_size = 6.;
        if (is_key_pressed(KEY_4) > 0.)   screen_size = 4.;
        if (is_key_pressed(KEY_5) > 0.)   screen_size = 2.;
        if (max(is_key_pressed(KEY_MINUS), is_key_pressed(KEY_MINUS_FF)) > 0.)
            screen_size -= 2.;
        if (max(is_key_pressed(KEY_PLUS), is_key_pressed(KEY_PLUS_FF)) > 0.)
            screen_size += 2.;
        fragColor[screen_size_field] = clamp(screen_size, 0., 10.);
        
        int flags_field = get_option_field(OPTION_DEF_SHOW_FPS);
        int flags = int(fragColor[flags_field]);

        if (is_key_pressed(TOGGLE_TEX_FILTER_KEY) > 0.)
            flags ^= OPTION_FLAG_TEXTURE_FILTER;
        if (is_key_pressed(TOGGLE_LIGHT_SHAFTS_KEY) > 0.)
            flags ^= OPTION_FLAG_LIGHT_SHAFTS;
        if (is_key_pressed(TOGGLE_CRT_EFFECT_KEY) > 0.)
            flags ^= OPTION_FLAG_CRT_EFFECT;
        
        if (is_key_pressed(SHOW_PERF_STATS_KEY) > 0.)
        {
            const int MASK = OPTION_FLAG_SHOW_FPS | OPTION_FLAG_SHOW_FPS_GRAPH;
            // https://fgiesen.wordpress.com/2011/01/17/texture-tiling-and-swizzling/
            // The line below combines Fabian Giesen's trick (offs_x = (offs_x - x_mask) & x_mask)
            // with another one for efficient bitwise integer select (c = a ^ ((a ^ b) & mask)),
            // which I think I also stole from his blog, but I can't find the link
            flags ^= (flags ^ (flags - MASK)) & MASK;
            
            // don't show FPS graph on its own when using keyboard shortcut to cycle through options
            if (test_flag(flags, OPTION_FLAG_SHOW_FPS_GRAPH))
                flags |= OPTION_FLAG_SHOW_FPS;
        }
        
        fragColor[flags_field] = float(flags);

        if (menu.open <= 0)
            return;
        float adjust = is_key_pressed(KEY_RIGHT) - is_key_pressed(KEY_LEFT);

        MenuOption option = get_option(menu.selected);
        int option_type = get_option_type(option);
        int option_field = get_option_field(option);
        if (option_type == OPTION_TYPE_SLIDER)
        {
            fragColor[option_field] += adjust;
            fragColor[option_field] = clamp(fragColor[option_field], 0., 10.);
        }
        else if (option_type == OPTION_TYPE_TOGGLE && (abs(adjust) > .5 || is_key_pressed(KEY_ENTER) > 0.))
        {
            int value = int(fragColor[option_field]);
            value ^= get_option_range(option);
            fragColor[option_field] = float(value);
        }
        
        return;
    }
#endif // ENABLE_MENU
}

void advance_time(inout vec4 fragColor, vec2 fragCoord)
{
    if (is_inside(fragCoord, ADDR_TIMING) > 0.)
    {
        MenuState menu;
        LOAD_PREV(menu);
        
        Timing timing;
        if (iFrame == 0)
            clear(timing);
        else
          from_vec4(timing, fragColor);

        bool paused = timing.prev == iTime;
        if (paused)
            timing.flags |= TIMING_FLAG_PAUSED;
        else
            timing.flags &= ~TIMING_FLAG_PAUSED;
        
        // Note: on 144 Hz monitors, in thumbnail mode, iTimeDelta
        // seems to be incorrect (1/60 seconds instead of 1/144)
        float time_delta = iTime - timing.prev;
        if (!paused && menu.open == 0 && g_time > WORLD_RENDER_TIME)
            timing.anim += time_delta;
        timing.prev = iTime;
        
        to_vec4(fragColor, timing);
        return;
    }
}

////////////////////////////////////////////////////////////////

void mainImage( out vec4 fragColor, vec2 fragCoord )
{
  if (iFrame > 0 && is_inside(fragCoord, ADDR_RANGE_PARAM_BOUNDS) < 0.)
      DISCARD;

    fragColor = (iFrame==0) ? vec4(0) : texelFetch(iChannel1, ivec2(fragCoord), 0);

    Lighting lighting;
    LOAD_PREV(lighting);
    
    UPDATE_TIME(lighting);

    write_map_data    (fragColor, fragCoord);
    update_tiles    (fragColor, fragCoord);
    update_input    (fragColor, fragCoord);
    update_game_rules (fragColor, fragCoord);
    update_perf_stats (fragColor, fragCoord);
    update_menu     (fragColor, fragCoord);
    advance_time    (fragColor, fragCoord);
}
////////////////////////////////////////////////////////////////
// config.cfg //////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

#define DEMO_MODE       0
#define DEMO_STAGE_DURATION   1.5
#define DEMO_MODE_HALFTONE    1   // 0=blue noise dither; 1=disks

#define GRAVITY         800.0
#define LOADING_TIME      2.5   // lower bound, in seconds
#define LIGHTMAP_AA_SAMPLES   8

// Used for mipmap generation, texture filtering and light shafts
// Lighting is performed in 'gamma' (non-linear) space for a more authentic look
#define USE_GAMMA_CORRECTION  2   // 0=off; 1=sRGB; 2=gamma 2.0

#define VIEW_DISTANCE     2048.0

// Debug switches //////////////////////////////////////////////

#define COMPILE_FASTER      1
#define USE_DISCARD       1

////////////////////////////////////////////////////////////////
// Implementation //////////////////////////////////////////////
////////////////////////////////////////////////////////////////

#if COMPILE_FASTER
  #define NO_UNROLL(k)    ((k)+min(iFrame,0))
#else
  #define NO_UNROLL(k)    (k)
#endif

#if USE_DISCARD
  #define DISCARD       discard
#else
  #define DISCARD       return
#endif

// Speed up your shaders on Intel iGPUs with this one weird trick!
// No, seriously - on a Surface 3 (Atom x7-Z8700), wrapping global
// arrays in structs increased framerate from ~1.4 FPS to 45+!

// Side note: token pasting would be really handy right about now...
#define WRAP(struct_name, name, type, count)\
    const struct struct_name { type data[count]; } name = struct_name(type[count]

// Standard materials (cached in Buffer A) /////////////////////

#define MATERIAL_WIZMET1_2      0
#define MATERIAL_WBRICK1_5      1
#define MATERIAL_WIZMET1_1      2
#define MATERIAL_WIZ1_4         3
#define MATERIAL_CITY4_7        4
#define MATERIAL_BRICKA2_2      5
#define MATERIAL_CITY4_6        6
#define MATERIAL_WIZWOOD1_5     7
#define MATERIAL_TELEPORT       8
#define MATERIAL_WINDOW02_1     9
#define MATERIAL_COP3_4         10
#define MATERIAL_WATER1         11
#define MATERIAL_LAVA1          12
#define MATERIAL_WATER2         13
#define MATERIAL_DEM4_1         14
#define MATERIAL_QUAKE          15
#define MATERIAL_SKY1           16
#define MATERIAL_SKY1B          17
#define MATERIAL_FLAME          18
#define MATERIAL_ZOMBIE         19
#define NUM_MATERIALS           20

const int
  ATLAS_WIDTH                 = 512,
  ATLAS_HEIGHT                = 256;
// atlas usage: 100%

WRAP(Tiles,tiles,int,10)(1703961,1835035,393221,851975,67633173,917533,1441807,16777246,38470217,2031639));

vec4 get_tile(int index)
{
    int data = (tiles.data[index >> 1] >> ((index & 1) << 4)) & 4095;
    return vec4(ivec4(data & 7, (data >> 3) & 7, ((data >> 6) & 7) + 1, ((data >> 9) & 7) + 1) << 6);
}

// Extra materials /////////////////////////////////////////////

const int
    BASE_SHOTGUN_MATERIAL   = NUM_MATERIALS,
    MATERIAL_SHOTGUN_PUMP   = 0 + BASE_SHOTGUN_MATERIAL,
    MATERIAL_SHOTGUN_BARREL   = 1 + BASE_SHOTGUN_MATERIAL,
    MATERIAL_SHOTGUN_FLASH    = 2 + BASE_SHOTGUN_MATERIAL,
    NUM_SHOTGUN_MATERIALS   = 3,

    BASE_TARGET_MATERIAL    = BASE_SHOTGUN_MATERIAL + NUM_SHOTGUN_MATERIALS,
    NUM_TARGETS         = 8;

// Material helpers ////////////////////////////////////////////

const int
    MATERIAL_MASK_SKY     = (1<<MATERIAL_SKY1) | (1<<MATERIAL_SKY1B),
    MATERIAL_MASK_LIQUID    = (1<<MATERIAL_WATER1) | (1<<MATERIAL_WATER2) | (1<<MATERIAL_LAVA1) | (1<<MATERIAL_TELEPORT);

bool is_material_viewmodel(const int material)
{
    return uint(material-BASE_SHOTGUN_MATERIAL) < uint(NUM_SHOTGUN_MATERIALS);
}

bool is_material_balloon(const int material)
{
    return uint(material-BASE_TARGET_MATERIAL) < uint(NUM_TARGETS);
}

bool is_material_any_of(const int material, const int mask)
{
    return uint(material) < 32u && (mask & (1<<material)) != 0;
}

bool is_material_sky(const int material)
{
    return is_material_any_of(material, MATERIAL_MASK_SKY);
}

bool is_material_liquid(const int material)
{
    return is_material_any_of(material, MATERIAL_MASK_LIQUID);
}

////////////////////////////////////////////////////////////////

const vec3 LAVA_BOUNDS[]=vec3[2](vec3(704,768,-176),vec3(1008,1232,-112));

////////////////////////////////////////////////////////////////

const vec2  ATLAS_OFFSET    = vec2(0, 24);
const vec2  ATLAS_SIZE      = vec2(ATLAS_WIDTH, ATLAS_HEIGHT);
const float ATLAS_CHAIN_WIDTH = float(ATLAS_WIDTH) * 1.5;
const vec2  ATLAS_CHAIN_SIZE  = vec2(ATLAS_CHAIN_WIDTH, ATLAS_HEIGHT);
const int MAX_MIP_LEVEL   = 6;

float exp2i(lowp int exponent)
{
    return intBitsToFloat(floatBitsToInt(1.) + (exponent << 23));
}

vec2 mip_offset(lowp int level)
{
    return level < 2 ?
        vec2(level, 0) :
      vec2(1.5 - exp2i(1 - level), .5);
}

vec4 atlas_chain_bounds(float scale)
{
    return vec4(ATLAS_OFFSET, ATLAS_CHAIN_SIZE*scale);
}

vec4 atlas_mip0_bounds(float scale)
{
    return vec4(ATLAS_OFFSET, ATLAS_SIZE*scale);
}

////////////////////////////////////////////////////////////////

const float
  PI      = 3.1415926536,
  HALF_PI   = PI * 0.5,
    TAU     = PI * 2.0,
    PHI     = 1.6180340888,
  SQRT2   = 1.4142135624,
  INV_SQRT2 = SQRT2 * 0.5;

// Rotations ///////////////////////////////////////////////////

mat3 rotation(vec3 angles)
{
    angles = radians(angles);
  float sy = sin(angles.x), sp = sin(angles.y), sr = sin(angles.z);
  float cy = cos(angles.x), cp = cos(angles.y), cr = cos(angles.z);
    
    return mat3
  (
        cr*cy+sy*sp*sr,   cr*sy-cy*sp*sr,   cp*sr,
        -sy*cp,       cy*cp,        sp,
        cr*sy*sp-cy*sr,   -cr*cy*sp-sy*sr,  cr*cp
  );
}

mat3 rotation(vec2 angles)
{
    angles = radians(angles);
  float sy = sin(angles.x), sp = sin(angles.y);
  float cy = cos(angles.x), cp = cos(angles.y);
   
    return mat3
  (
        cy,   sy,   0.,
        -sy*cp, cy*cp,  sp,
        sy*sp,  -cy*sp, cp
  );
}

mat2 rotation(float angle)
{
    angle = radians(angle);
    float s = sin(angle);
    float c = cos(angle);
    return mat2(c,s,-s,c);
}

vec2 rotate(vec2 p, float angle)
{
    return rotation(angle)*p;
}

vec3 rotate(vec3 p, float yaw)
{
    p.xy = rotate(p.xy, yaw);
    return p;
}

vec3 rotate(vec3 p, vec2 angles)
{
    p.yz = rotate(p.yz, angles.y);
    p.xy = rotate(p.xy, angles.x);
    return p;
}

vec3 rotate(vec3 p, vec3 angles)
{
    return rotation(angles)*p;
}

////////////////////////////////////////////////////////////////

// TODO: make sure these values are used explicitly where needed

const float FOV = 90.;
const float FOV_FACTOR = tan(radians(FOV*.5));
const int FOV_AXIS = 0;

float scale_fov(float fov, float scale)
{
    return 2. * degrees(atan(scale * tan(radians(fov * .5))));
}

vec2 get_resolution_fov_scale(vec2 resolution)
{
    return resolution / resolution[FOV_AXIS];
}

vec2 compute_fov(vec2 resolution)
{
    vec2 scale = get_resolution_fov_scale(resolution);
    return vec2(scale_fov(FOV, scale.x), scale_fov(FOV, scale.y));
}

vec3 unproject(vec2 ndc)
{
    return vec3(ndc.x, 1., ndc.y);
}

vec3 project(vec3 direction)
{
    return vec3(direction.xz/direction.y, direction.y);
}

vec2 taa_jitter(int frame)
{
#if 0
    const float SCALE = 1./8.;
    const float BIAS = .5 * SCALE - .5;
    frame &= 7;
    int ri = ((frame & 1) << 2) | (frame & 2) | ((frame & 4) >> 2);
    return vec2(frame,ri)*SCALE + BIAS;
#else
    return vec2(0);
#endif
}

// xy=scale, zw=bias
vec4 get_viewport_transform(int frame, vec2 resolution, float downscale)
{
    vec2 ndc_scale = vec2(downscale);
    vec2 ndc_bias = vec2(0);//ndc_scale * taa_jitter(frame) / resolution.xy;
    ndc_scale *= 2.;
    ndc_bias  *= 2.;
    ndc_bias  -= 1.;
    ndc_scale.y *= resolution.y / resolution.x;
    ndc_bias.y  *= resolution.y / resolution.x;
    return vec4(ndc_scale, ndc_bias);
}

vec2 hammersley(int i, int total)
{
    uint r = uint(i);
  r = ((r & 0x55u) << 1u) | ((r & 0xAAu) >> 1u);
  r = ((r & 0x33u) << 2u) | ((r & 0xCCu) >> 2u);
  r = ((r & 0x0Fu) << 4u) | ((r & 0xF0u) >> 4u);
    return vec2(float(i)/float(total), float(r)*(1./256.)) + .5/float(total);
}

////////////////////////////////////////////////////////////////

bool test_flag(int var, int flag)
{
    return (var & flag) != 0;
}

////////////////////////////////////////////////////////////////

float max3(float a, float b, float c)
{
    return max(a, max(b, c));
}

float min3(float a, float b, float c)
{
    return min(a, min(b, c));
}

float min_component(vec2 v)   { return min(v.x, v.y); }
float min_component(vec3 v)   { return min(v.x, min(v.y, v.z)); }
float min_component(vec4 v)   { return min(min(v.x, v.y), min(v.z, v.w)); }
float max_component(vec2 v)   { return max(v.x, v.y); }
float max_component(vec3 v)   { return max(v.x, max(v.y, v.z)); }
float max_component(vec4 v)   { return max(max(v.x, v.y), max(v.z, v.w)); }

////////////////////////////////////////////////////////////////

int dominant_axis(vec3 nor)
{
    nor = abs(nor);
    float max_comp = max(nor.x, max(nor.y, nor.z));
    return
        (max_comp==nor.x) ? 0 : (max_comp==nor.y) ? 1 : 2;
}

////////////////////////////////////////////////////////////////

float smoothen(float x)     { return x * x * (3. - 2. * x); }
vec2  smoothen(vec2 x)      { return x * x * (3. - 2. * x); }
vec3  smoothen(vec3 x)      { return x * x * (3. - 2. * x); }

float quintic(float t)      { return t * t * t * (t * (t * 6. - 15.) + 10.); }

float sqr(float x)        { return x * x; }

////////////////////////////////////////////////////////////////

float length_squared(vec2 v)  { return dot(v, v); }
float length_squared(vec3 v)  { return dot(v, v); }
float length_squared(vec4 v)  { return dot(v, v); }

vec2 safe_normalize(vec2 v)   { return all(equal(v, vec2(0))) ? vec2(0) : normalize(v); }
vec3 safe_normalize(vec3 v)   { return all(equal(v, vec3(0))) ? vec3(0) : normalize(v); }

////////////////////////////////////////////////////////////////

float around(float center, float max_dist, float var)
{
    return 1. - clamp(abs(var - center)*(1./max_dist), 0., 1.);
}

float linear_step(float low, float high, float value)
{
    return clamp((value-low)*(1./(high-low)), 0., 1.);
}

float triangle_wave(float period, float t)
{
    return abs(fract(t*(1./period))-.5)*2.;
}

// UV distortions //////////////////////////////////////////////

vec2 skew(vec2 uv, float factor)
{
    return vec2(uv.x + uv.y*factor, uv.y);
}

// Gamma <-> linear ////////////////////////////////////////////

float linear_to_gamma(float f)
{
#if USE_GAMMA_CORRECTION == 2
    return sqrt(f);
#elif USE_GAMMA_CORRECTION == 1
    return f <= 0.0031308 ? f * 12.92 : (1.055 * pow(f, (1./2.4)) - 0.055);
#else
    return f;
#endif
}

vec3 linear_to_gamma(vec3 c)
{
    return vec3(linear_to_gamma(c.r), linear_to_gamma(c.g), linear_to_gamma(c.b));
}

vec4 linear_to_gamma(vec4 c)
{
    return vec4(linear_to_gamma(c.r), linear_to_gamma(c.g), linear_to_gamma(c.b), c.a);
}

float gamma_to_linear(float f)
{
#if USE_GAMMA_CORRECTION == 2
    return f * f;
#elif USE_GAMMA_CORRECTION == 1
    return f <= 0.04045 ? f * (1./12.92) : pow((f + 0.055) * (1./1.055), 2.4);
#else
    return f;
#endif
}

vec3 gamma_to_linear(vec3 c)
{
    return vec3(gamma_to_linear(c.r), gamma_to_linear(c.g), gamma_to_linear(c.b));
}

vec4 gamma_to_linear(vec4 c)
{
    return vec4(gamma_to_linear(c.r), gamma_to_linear(c.g), gamma_to_linear(c.b), c.a);
}

// Noise functions /////////////////////////////////////////////

// Dave Hoskins/Hash without Sine
// https://www.shadertoy.com/view/4djSRW

const vec4 HASHSCALE = vec4(.1031, .1030, .0973, .1099);

float hash1(vec2 p)
{
  vec3 p3  = fract(vec3(p.xyx) * HASHSCALE.x);
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 hash3(vec3 p3)
{
  p3 = fract(p3 * HASHSCALE.xyz);
    p3 += dot(p3, p3.yxz + 19.19);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}

vec3 hash3(vec2 p)
{
  vec3 p3 = fract(vec3(p.xyx) * HASHSCALE.xyz);
    p3 += dot(p3, p3.yxz + 19.19);
    return fract((p3.xxy + p3.yzz) * p3.zyx);
}

vec3 hash3(float p)
{
   vec3 p3 = fract(vec3(p) * HASHSCALE.xyz);
   p3 += dot(p3, p3.yzx + 19.19);
   return fract((p3.xxy + p3.yzz) * p3.zyx); 
}

vec4 hash4(float p)
{
  vec4 p4 = fract(vec4(p) * HASHSCALE);
    p4 += dot(p4, p4.wzxy + 19.19);
    return fract((p4.xxyz + p4.yzzw) * p4.zywx);
}

vec4 hash4(vec2 p)
{
  vec4 p4 = fract(vec4(p.xyxy) * HASHSCALE);
    p4 += dot(p4, p4.wzxy + 19.19);
    return fract((p4.xxyz + p4.yzzw) * p4.zywx);
}

// https://www.shadertoy.com/view/4dS3Wd
// By Morgan McGuire @morgan3d, http://graphicscodex.com
float hash(float n) { return fract(sin(n) * 1e4); }
float hash(vec2 p)  { return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x)))); }

// 2D Weyl hash #1, by MBR
// https://www.shadertoy.com/view/Xdy3Rc
// http://marc-b-reynolds.github.io/math/2016/03/29/weyl_hash.html
float weyl_hash(vec2 c)
{
    c *= fract(c * vec2(.5545497, .308517));
    return fract(c.x * c.y);
}

// by iq
vec2 hash2(vec2 p)
{
  return fract(sin(vec2(dot(p,vec2(127.1,311.7)),dot(p,vec2(269.5,183.3))))*43758.5453);
}

float random(vec2 p)
{
  return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

#define SMOOTH_NOISE_FUNC(p, hash_name)             \
  vec2 i = floor(p);                      \
    p -= i;                           \
    p *= p * (3. - 2.*p);                   \
    float                           \
      s00 = hash_name(i),                   \
        s01 = hash_name(i + vec2(1, 0)),            \
        s10 = hash_name(i + vec2(0, 1)),            \
        s11 = hash_name(i + vec2(1, 1));            \
  return mix(mix(s00, s01, p.x), mix(s10, s11, p.x), p.y)   \

float smooth_noise(vec2 p)
{
    SMOOTH_NOISE_FUNC(p, random);
}

float smooth_weyl_noise(vec2 p)
{
    SMOOTH_NOISE_FUNC(p, weyl_hash);
}

float smooth_noise(float f)
{
    float i = floor(f);
    f -= i;
    f *= f * (3. - 2.*f);
    return mix(hash(i), hash(i + 1.), f);
}

#define FBM_FUNC(uv, gain, lacunarity, noise)   \
  float accum = noise(uv);            \
    float octave_weight = gain;           \
    float total_weight = 1.;            \
                          \
    uv *= lacunarity;               \
    accum += noise(uv) * octave_weight;       \
    total_weight += octave_weight;          \
                          \
    uv *= lacunarity; octave_weight *= gain;    \
    accum += noise(uv) * octave_weight;       \
    total_weight += octave_weight;          \
                          \
    uv *= lacunarity; octave_weight *= gain;    \
    accum += noise(uv) * octave_weight;       \
    total_weight += octave_weight;          \
                          \
    return accum / total_weight           \

float turb(vec2 uv, float gain, float lacunarity)
{
    FBM_FUNC(uv, gain, lacunarity, smooth_noise);
}

float weyl_turb(vec2 uv, float gain, float lacunarity)
{
    FBM_FUNC(uv, gain, lacunarity, smooth_weyl_noise);
}

vec4 blue_noise(vec2 fragCoord, sampler2D channel, int frame)
{
    ivec2 uv = ivec2(fragCoord) + frame * ivec2(19, 23);
    return texelFetch(channel, uv & (textureSize(channel, 0) - 1), 0);
}

#define BLUE_NOISE(fragCoord) blue_noise(fragCoord, NOISE_CHANNEL, iFrame)

// SDF operations //////////////////////////////////////////////

float sdf_exclude(float from, float what)
{
    return max(from, -what);
}

float sdf_union(float a, float b)
{
    return min(a, b);
}

float sdf_intersection(float a, float b)
{
    return max(a, b);
}

// polynomial smooth min
// https://iquilezles.org/articles/smin
float sdf_smin(float a, float b, float k)
{
    float h = clamp( 0.5+0.5*(b-a)/k, 0.0, 1.0 );
    return mix( b, a, h ) - k*h*(1.0-h);
}

// SDF effects /////////////////////////////////////////////////

float sdf_mask(float sdf, float px)
{
    return clamp(1. - sdf/px, 0., 1.);
}

float sdf_mask(float sdf)
{
    float px = max(abs(dFdx(sdf)), abs(dFdy(sdf)));
    return sdf_mask(sdf, px);
}

vec2 sdf_normal(float sdf)
{
    vec2 n = vec2(dFdx(sdf), dFdy(sdf));
  float sqlen = dot(n, n);
    return n * ((sqlen > 0.) ? inversesqrt(sqlen) : 1.);
}

vec2 sdf_emboss(float sdf, float bevel, vec2 light_dir)
{
    float mask = sdf_mask(sdf);
    bevel = clamp(1. + sdf/bevel, 0., 1.);
    return vec2(mask * (.5 + sqrt(bevel) * dot(sdf_normal(sdf), light_dir)), mask);
}

// SDF generators //////////////////////////////////////////////

float sdf_disk(vec2 uv, vec2 center, float radius)
{
    return length(uv - center) - radius;
}

float sdf_ellipse(vec2 uv, vec2 center, vec2 r)
{
    return (length((uv-center)/r) - 1.) / min(r.x, r.y);
}

float sdf_centered_box(vec2 uv, vec2 center, vec2 size)
{
    return max(abs(uv.x-center.x) - size.x, abs(uv.y-center.y) - size.y);
}

float sdf_box(vec2 uv, vec2 mins, vec2 maxs)
{
    return sdf_centered_box(uv, (mins+maxs)*.5, (maxs-mins)*.5);
}

float sdf_line(vec2 uv, vec2 a, vec2 b, float thickness)
{
    vec2 ab = b-a;
    vec2 ap = uv-a;
    float t = clamp(dot(ap, ab)/dot(ab, ab), 0., 1.);
    return length(uv - (ab*t + a)) - thickness*.5;
}

float sdf_seriffed_box(vec2 uv, vec2 origin, vec2 size, vec2 top_serif, vec2 bottom_serif)
{
    float h = clamp((uv.y - origin.y) / size.y, 0., 1.);
    float xmul = h < bottom_serif.y ? mix(1.+bottom_serif.x, 1., sqrt(1.-sqr(1.-(h/bottom_serif.y)))) :
        h > (1.-top_serif.y) ? 1.+top_serif.x*sqr(1.-(1.-h)/(top_serif.y)) :
            1.;
    return sdf_centered_box(uv, vec2(origin.x, origin.y+size.y*.5), vec2(size.x*xmul*.5, size.y*.5));
}

float sdf_nail(vec2 uv, vec2 top, vec2 size)
{
    const float head_flat_frac = .02;
    const float head_round_frac = .08;
    const float body_thickness = .5;

    float h = clamp((top.y - uv.y) / size.y, 0., 1.);
    float w = (h < head_flat_frac) ? 1. :
        (h < head_flat_frac + head_round_frac) ? mix( body_thickness, 1., sqr(1.-(h-head_flat_frac)/head_round_frac)) :
      h > .6 ? ((1.05 - h) / (1.05 - .6)) * body_thickness : body_thickness;
    return sdf_centered_box(uv, top - vec2(0., size.y*.5), size*vec2(w, .5));
}

float sdf_Q(vec2 uv)
{
    float dist = sdf_disk(uv, vec2(.5, .67), .32);
    dist = sdf_exclude(dist, sdf_disk(uv, vec2(.5, .735), .27));
    dist = sdf_union(dist, sdf_nail(uv, vec2(.5, .59), vec2(.09, .52)));
    return dist;
}

float sdf_id(vec2 uv)
{
    float d = .06*sdf_ellipse(uv, vec2(.52, .38), vec2(.26, .28));
    d = sdf_exclude(d, .02*sdf_ellipse(uv, vec2(.57, .39), vec2(.12, .18)));
    d = sdf_union(d, sdf_centered_box(uv, vec2(.75, .51), vec2(.09, .30)));
    d = sdf_union(d, sdf_centered_box(uv, vec2(.80, .80), vec2(.04, .10)));
    d = sdf_smin(d, sdf_centered_box(uv, vec2(.78, .15), vec2(.12, .05)), .05);
    d = sdf_smin(d, sdf_centered_box(uv, vec2(.66, .81), vec2(.10, .05)), .05);
    float i = sdf_centered_box(uv, vec2(.25, .40), vec2(.09, .23));
    i = sdf_union(i, sdf_disk(uv, vec2(.24, .79), .09));
    i = sdf_smin(i, sdf_centered_box(uv, vec2(.25, .15), vec2(.15, .05)), .05);
    i = sdf_smin(i, sdf_centered_box(uv, vec2(.20, .60), vec2(.10, .03)), .05);
    return sdf_exclude(sdf_union(i, d), sdf_intersection(i, d));
}

vec2 add_knob(vec2 uv, float px, vec2 center, float radius, vec2 light_dir)
{
    float light = dot(normalize(uv-center), light_dir)*.5 + .5;
    float mask = sdf_mask(sdf_disk(uv, center, radius), px);
    return vec2(light, mask);
}

vec3 closest_point_on_segment(vec3 p, vec3 a, vec3 b)
{
    vec3 ab = b-a, ap = p-a;
    float t = clamp(dot(ap, ab)/dot(ab, ab), 0., 1.);
    return ab*t + a;
}

// QUAKE text //////////////////////////////////////////////////

float sdf_nail_v2(vec2 uv, vec2 top, vec2 size)
{
    const float
        head_flat_frac = .1,
      head_round_frac = .1,
      body_thickness = .5;

    float h = clamp((top.y - uv.y) / size.y, 0., 1.);
    float w = (h < head_flat_frac) ? 1. :
        (h < head_flat_frac + head_round_frac) ? mix( body_thickness, 1., sqr(1.-(h-head_flat_frac)/head_round_frac)) :
      h > .6 ? ((1.05 - h) / (1.05 - .6)) * body_thickness : body_thickness;
    return sdf_centered_box(uv, top - vec2(0., size.y*.5), size*vec2(w, .5));
}

float sdf_Q_top(vec2 uv)
{
    uv.y -= .01;
    float dist = sdf_disk(uv, vec2(.5, .64), .36);
    dist = sdf_exclude(dist, sdf_disk(uv, vec2(.5, .74), .29));
    dist = sdf_union(dist, sdf_nail_v2(uv, vec2(.5, .61), vec2(.125, .57)));
    dist = sdf_exclude(dist, .95 - uv.y);
    return dist;
}

float sdf_U(vec2 uv)
{
    float sdf = sdf_seriffed_box(uv, vec2(.5, .3), vec2(.58, .6), vec2(.25, .35), vec2(-.7,.3));
    sdf = sdf_exclude(sdf, sdf_seriffed_box(uv, vec2(.5, .34), vec2(.3, .58), vec2(-.5, .35), vec2(-.75, .2)));
    sdf = sdf_exclude(sdf, sdf_centered_box(uv, vec2(.5, .3), vec2(.04, .15)));
    return sdf;
}

float sdf_A(vec2 uv)
{
    float h = linear_step(.3, .9, uv.y);
  float sdf = sdf_seriffed_box(uv, vec2(.5, .3), vec2(mix(.7, .01, h), .6), vec2(0.,.3), vec2(.2,.3));
    h = linear_step(.1, .65, uv.y);
  sdf = sdf_exclude(sdf, sdf_seriffed_box(uv, vec2(.45, .1), vec2(mix(.7, .01, h), .55), vec2(0.,.3), vec2(0.,.3)));
    sdf = sdf_union(sdf, sdf_centered_box(uv, vec2(.45, .47), vec2(.18, .02)));
    return sdf;
}

float sdf_K(vec2 uv)
{
  float sdf = sdf_seriffed_box(uv, vec2(.1, .3), vec2(.15, .6), vec2(.5,.2), vec2(.5,.2));
  sdf = sdf_disk(uv, vec2(.17, .17), .5);
  sdf = sdf_exclude(sdf, sdf_disk(uv, vec2(.1, -.05), .6));
    sdf = sdf_exclude(sdf, sdf_centered_box(uv, vec2(-.32, .3), vec2(.4, .8)));
    sdf = sdf_union(sdf, sdf_seriffed_box(uv, vec2(.1, .3), vec2(.15, .6), vec2(.5,.2), vec2(.5,.2)));
  sdf = sdf_union(sdf, sdf_seriffed_box(skew(uv+vec2(.25,-.3),-1.3), vec2(.1, .3), vec2(mix(.25, .01, linear_step(.5, 1., uv.y)), .3), vec2(0.,.3), vec2(.5,.3)));
    sdf = sdf_exclude(sdf, sdf_centered_box(uv, vec2(.5, .1), vec2(.4, .2)));
    return sdf;
}

float sdf_E(vec2 uv)
{
    float sdf_r = sdf_centered_box(uv, vec2(.66, .6), vec2(.1, .3));
    sdf_r = sdf_exclude(sdf_r, sdf_disk(uv, vec2(.58, .6), .25));
    sdf_r = sdf_exclude(sdf_r, sdf_centered_box(uv, vec2(.33, .6), vec2(.25, .35)));
    float sdf = sdf_seriffed_box(uv, vec2(.5, .3), vec2(.55, .6), vec2(.2, .15), vec2(-.5,.3));
    sdf = sdf_exclude(sdf, sdf_seriffed_box(uv, vec2(.5, .33), vec2(.3, .57), vec2(-.5, .15), vec2(-.75, .2)));
    sdf = sdf_exclude(sdf, sdf_centered_box(uv, vec2(.65, .6), vec2(.2, .35)));
    sdf = sdf_union(sdf, sdf_r);
    float t = linear_step(.4, .5, uv.x);
    sdf = sdf_union(sdf, sdf_centered_box(uv, vec2(.4, .6), vec2(.1, mix(.03, .01, t))));
    return sdf;
}

// Time ////////////////////////////////////////////////////////

const float
    CONSOLE_XFADE_DURATION  = 1.,
  CONSOLE_SLIDE_DURATION  = .5,
  CONSOLE_TYPE_DURATION = 2.,
    WORLD_RENDER_TIME   = CONSOLE_XFADE_DURATION,
  INPUT_ACTIVE_TIME   = (CONSOLE_XFADE_DURATION + CONSOLE_SLIDE_DURATION + CONSOLE_TYPE_DURATION),
    
    // TODO: s/TIME/DURATION/
    LEVEL_COUNTDOWN_TIME  = 3.,
    BALLOON_SCALEIN_TIME  = .5,
    LEVEL_WARMUP_TIME   = LEVEL_COUNTDOWN_TIME + BALLOON_SCALEIN_TIME,
    HUD_TARGET_ANIM_TIME  = .25,
    
    THUMBNAIL_MIN_TIME    = 5.
;

float g_time = 0.;

void update_time(float bake_time, float uniform_time)
{
    if (bake_time > 0.)
      g_time = uniform_time - bake_time;
    else
        g_time = -uniform_time;
}

#define UPDATE_TIME(lighting)   update_time(lighting.bake_time, iTime)

// GBuffer /////////////////////////////////////////////////////

const int GBUFFER_NORMAL_BITS = 8;
const float
    GBUFFER_NORMAL_SCALE = float(1 << GBUFFER_NORMAL_BITS),
    GBUFFER_NORMAL_MAX_VALUE = GBUFFER_NORMAL_SCALE - 1.;

// http://jcgt.org/published/0003/02/01/paper.pdf
vec2 signNotZero(vec2 v)
{
  return vec2((v.x >= 0.0) ? +1.0 : -1.0, (v.y >= 0.0) ? +1.0 : -1.0);
}

// Assume normalized input. Output is on [-1, 1] for each component.
vec2 vec3_to_oct(vec3 v)
{
  // Project the sphere onto the octahedron, and then onto the xy plane
  vec2 p = v.xy * (1.0 / (abs(v.x) + abs(v.y) + abs(v.z)));
  // Reflect the folds of the lower hemisphere over the diagonals
  return (v.z <= 0.0) ? ((1.0 - abs(p.yx)) * signNotZero(p)) : p;
}

vec3 oct_to_vec3(vec2 e)
{
    vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0)
        v.xy = (1.0 - abs(v.yx)) * signNotZero(v.xy);
    return normalize(v);
}

float compress(vec2 v, vec2 noise)
{
    v = floor(clamp(v, 0., 1.) * GBUFFER_NORMAL_MAX_VALUE + noise);
    return v.y * GBUFFER_NORMAL_SCALE + v.x;
}

vec2 uncompress(float f)
{
    vec2 v = vec2(mod(f, GBUFFER_NORMAL_SCALE), f / GBUFFER_NORMAL_SCALE);
    return clamp(floor(v) / GBUFFER_NORMAL_MAX_VALUE, 0., 1.);
}

struct GBuffer
{
    vec3  normal;
    float z;
    float light;
    int   material;
    int   uv_axis;
    bool  edge;
};

vec4 gbuffer_pack(GBuffer g, vec2 noise)
{
    int props =
        (g.material   << 3) |
        (g.uv_axis    << 1) |
        int(g.edge);
    return vec4(compress(vec3_to_oct(g.normal) * .5 + .5, noise), g.light, g.z, float(props));
}

GBuffer gbuffer_unpack(vec4 v)
{
    int props = int(round(v.w));
    
    GBuffer g;
    g.normal  = oct_to_vec3(uncompress(v.x) * 2. - 1.);
    g.light   = v.y;
    g.z     = v.z;
    g.material  = props >> 3;
    g.uv_axis = (props >> 1) & 3;
    g.edge    = (props & 1) != 0;
    
    return g;
}

// Lightmap encoding ///////////////////////////////////////////

const float LIGHTMAP_OVERBRIGHT = 2.;

struct LightmapSample
{
    vec4 weights;
    vec4 values;
};

LightmapSample empty_lightmap_sample()
{
    return LightmapSample(vec4(0), vec4(0));
}

LightmapSample decode_lightmap_sample(vec4 encoded)
{
    return LightmapSample(floor(encoded), fract(encoded) * LIGHTMAP_OVERBRIGHT);
}

vec4 encode(LightmapSample s)
{
    return floor(s.weights) + clamp(s.values * (1./LIGHTMAP_OVERBRIGHT), 0., 1.-1./1024.);
}

// Fireball ////////////////////////////////////////////////////

const vec3 FIREBALL_ORIGIN = vec3(864, 992, -168);

struct Fireball
{
    float launch_time;
    vec3 velocity;
};
    
void get_fireball_props(float time, out Fireball props)
{
    const float INTERVAL  = 5.;
    const float MAX_DELAY = 2.;
    const float BASE_SPEED  = 600.;

    float interval_start = floor(time*(1./INTERVAL)) * INTERVAL;
    float delay = hash(interval_start) * MAX_DELAY;
    
    props.launch_time = interval_start + delay;
    props.velocity.x = hash(interval_start + .23) * 100. - 50.;
    props.velocity.y = hash(interval_start + .37) * 100. - 50.;
    props.velocity.z = hash(interval_start + .71) * 200. + BASE_SPEED;
}

vec3 get_fireball_offset(float time, Fireball props)
{
    float elapsed = max(0., time - props.launch_time);

    vec3 offset = elapsed * props.velocity;
    offset.z -= GRAVITY * .5 * elapsed * elapsed;
    offset.z = max(offset.z, 0.);
    
    return offset;
}

vec3 get_fireball_offset(float time)
{
    Fireball props;
    get_fireball_props(time, props);
    return get_fireball_offset(time, props);
}

float get_landing_time(Fireball props)
{
    return props.launch_time + props.velocity.z * (2./GRAVITY);
}

// Classic (dot) halftoning ////////////////////////////////////

vec2 halftone_point(vec2 fragCoord, float grid_size)
{
    const mat2 rot = INV_SQRT2 * mat2(1,1,-1,1);
    fragCoord = rot * fragCoord;
    vec2 fc2 = fragCoord * (1./grid_size);
    vec2 nearest = (floor(fc2) + .5) * grid_size;
    nearest.x += grid_size*.5 * step(.5, fract(fc2.y * .5)) * sign(round(fc2.x)*grid_size - fragCoord.x);
    return nearest * rot;
}

float halftone(vec2 fragCoord, vec2 center, float grid_size, float fraction)
{
    fraction *= grid_size * INV_SQRT2;
    return step(length_squared(fragCoord - center), sqr(fraction));
}

float halftone_classic(vec2 fragCoord, float grid_size, float fraction)
{
    vec2 point = halftone_point(fragCoord, grid_size);
    return halftone(fragCoord, point, grid_size, fraction);
}

// Demo mode ///////////////////////////////////////////////////

const int
  DEMO_STAGE_NONE     = 0,
  DEMO_STAGE_DEPTH    = 2,
  DEMO_STAGE_NORMALS    = 3,
  DEMO_STAGE_UV     = -1, // disabled for now
  DEMO_STAGE_TEXTURES   = 4,
  DEMO_STAGE_LIGHTING   = 5,
  DEMO_STAGE_COMPOSITE  = 6,
  DEMO_STAGE_FPS      = 7,
  DEMO_NUM_STAGES     = 9;

int g_demo_stage = DEMO_STAGE_NONE;
int g_demo_scene = 0;

bool is_demo_mode_enabled(bool thumbnail)
{
#if !DEMO_MODE
    if (!thumbnail)
        return false;
#endif // !DEMO_MODE
    return true;
}

bool is_demo_stage_composite(int stage)
{
    return uint(stage - DEMO_STAGE_DEPTH) >= uint(DEMO_STAGE_COMPOSITE - DEMO_STAGE_DEPTH);
}

bool is_demo_stage_composite()
{
    return is_demo_stage_composite(g_demo_stage);
}

void update_demo_stage(vec2 fragCoord, vec2 resolution, float downscale, sampler2D noise, int frame, bool thumbnail)
{
    float time = g_time;

    if (!is_demo_mode_enabled(thumbnail))
    {
    g_demo_stage = DEMO_STAGE_NONE;
        return;
    }
    
    resolution *= 1./downscale;
    vec2 uv = clamp(fragCoord/resolution, 0., 1.);
        
    const float TRANSITION_WIDTH = .125;
    const vec2 ADVANCE = vec2(.5, -.125);

    time = max(0., time - INPUT_ACTIVE_TIME);
    time *= 1./DEMO_STAGE_DURATION;
    time += dot(uv, ADVANCE) - ADVANCE.y;

#if !DEMO_MODE_HALFTONE
    time += TRANSITION_WIDTH * sqrt(blue_noise(fragCoord, noise, frame).x);
#else
    const float HALFTONE_GRID = 8.;
    float fraction = clamp(1. - (round(time) - time) * (1./TRANSITION_WIDTH), 0., 1.);
    time += TRANSITION_WIDTH * halftone_classic(fragCoord, HALFTONE_GRID, fraction);
#endif // !DEMO_MODE_HALFTONE

    g_demo_stage = int(mod(time, float(DEMO_NUM_STAGES)));
    g_demo_scene = int(time * (1./float(DEMO_NUM_STAGES)));
}
  
#define UPDATE_DEMO_STAGE_EX(fragCoord, downscale, thumbnail) \
  update_demo_stage(fragCoord, iResolution.xy, downscale, NOISE_CHANNEL, iFrame, thumbnail)

#define UPDATE_DEMO_STAGE(fragCoord, downscale, thumbnail)  \
  UPDATE_DEMO_STAGE_EX(fragCoord, downscale, thumbnail)

const struct DemoScene
{
    vec3 pos;
    vec2 angles;
}
g_demo_scenes[] = DemoScene[4]
(
    DemoScene(vec3(544,272,49),   vec2(0,5)),
    DemoScene(vec3(992,1406,196), vec2(138,-26)),
    DemoScene(vec3(323,890,-15),  vec2(35.5,4)),
    //DemoScene(vec3(1012,453,73),  vec2(38.25,-8))
    DemoScene(vec3(1001,514,37),  vec2(42.3,.2))
);

DemoScene get_demo_scene()
{
    return g_demo_scenes[g_demo_scene % g_demo_scenes.length()];
}

////////////////////////////////////////////////////////////////

float get_viewmodel_offset(vec3 velocity, float bob_cycle, float attack)
{
    const float BOB_FRACTION = .003;
    float speed = length(velocity.xy);
    float bob = speed * BOB_FRACTION * (.3 + .7 * sin(TAU * bob_cycle));
    bob = clamp(bob, -.5, 1.);
    attack = attack * attack * -4.;
    return bob + attack;
}

////////////////////////////////////////////////////////////////
// Persistent state ////////////////////////////////////////////
////////////////////////////////////////////////////////////////

// https://www.shadertoy.com/view/XsdGDX

float is_inside(vec2 fragCoord, vec2 address)
{
    vec2 d = abs(fragCoord - (0.5 + address)) - 0.5;
    return -max(d.x, d.y);
}

// changed from original: range is half-open
float is_inside(vec2 fragCoord, vec4 address_range)
{
    vec2 d = abs(fragCoord - (address_range.xy + address_range.zw*0.5)) + -0.5*address_range.zw;
    return -max(d.x, d.y);
}

vec4 load(vec2 address, sampler2D channel)
{
    return texelFetch(channel, ivec2(address), 0);
}

void store(inout vec4 fragColor, vec2 fragCoord, vec2 address, vec4 value)
{
    if (is_inside(fragCoord, address) > 0.0) fragColor = value;
}

void store(inout vec4 fragColor, vec2 fragCoord, vec4 address_range, vec4 value)
{
    if (is_inside(fragCoord, address_range) > 0.0) fragColor = value;
}

void assign(out float dst,  float src)    { dst = src; }
void assign(out float dst,  int src)    { dst = float(src); }
void assign(out int dst,  int src)    { dst = src; }
void assign(out int dst,  float src)    { dst = int(src); }
void assign(out vec2 dst, vec2 src)   { dst = src; }
void assign(out vec3 dst, vec3 src)   { dst = src; }

// Serialization codegen macros ////////////////////////////////

#define FN_DEFINE_FIELD(pack, field_type, field_name, init)   field_type field_name;
#define FN_CLEAR_FIELD(pack, field_type, field_name, init)    assign(data.field_name, init);
#define FN_LOAD_FIELD(pack, field_type, field_name, init)   assign(data.field_name, v.pack);
#define FN_STORE_FIELD(pack, field_type, field_name, init)    assign(v.pack, data.field_name);

////////////////////////////////////////////////////////////////////////////////
#define DEFINE_STRUCT_BASE(type_name, field_list)               \
  struct type_name                              \
    {                                     \
        field_list(FN_DEFINE_FIELD)                       \
  };                                      \
  void from_vec4(out type_name data, const vec4 v)              \
  {                                     \
        field_list(FN_LOAD_FIELD)                       \
    }                                     \
  void to_vec4(out vec4 v, const type_name data)                \
  {                                     \
        v = vec4(0);                              \
        field_list(FN_STORE_FIELD)                        \
    }                                     \
  void clear(out type_name data)                        \
    {                                     \
        field_list(FN_CLEAR_FIELD)                        \
  }                                     \
////////////////////////////////////////////////////////////////////////////////
#define DEFINE_STRUCT(type_name, address, field_list)             \
  DEFINE_STRUCT_BASE(type_name, field_list)                 \
    void load(out type_name data, sampler2D channel)              \
  {                                     \
        from_vec4(data, load(address, channel));                \
    }                                     \
    void store(inout vec4 fragColor, vec2 fragCoord, const type_name data)    \
  {                                     \
        if (is_inside(fragCoord, address) > 0.)                 \
          to_vec4(fragColor, data);                     \
    }                                     \
////////////////////////////////////////////////////////////////////////////////
#define DEFINE_STRUCT_RANGE(type_name, address_range, field_list)       \
  DEFINE_STRUCT_BASE(type_name, field_list)                 \
    void load(vec2 offset, out type_name data, sampler2D channel)       \
  {                                     \
        from_vec4(data, load(address_range.xy + offset, channel));        \
    }                                     \
    void store(inout vec4 fragColor, vec2 fragCoord, const type_name data)    \
  {                                     \
        if (is_inside(fragCoord, address_range) > 0.)             \
          to_vec4(fragColor, data);                     \
    }                                     \
////////////////////////////////////////////////////////////////////////////////

#define LOAD(what)        load(what, SETTINGS_CHANNEL)
#define LOADR(ofs, what)      load(ofs, what, SETTINGS_CHANNEL)
#define LOAD_PREV(what)     if (iFrame==0) clear(what); else LOAD(what)
#define LOAD_PREVR(ofs, what) if (iFrame==0) clear(what); else LOADR(ofs, what)

// Persistent state addresses //////////////////////////////////
        
const uvec2 LIGHTMAP_SIZE       = uvec2(180,256);

const int
  NUM_MAP_AXIAL_BRUSHES           = 88,
  NUM_MAP_AXIAL_PLANES            = NUM_MAP_AXIAL_BRUSHES * 6,
  NUM_MAP_NONAXIAL_PLANES         = 502,
  NUM_MAP_NONAXIAL_BRUSHES        = 89,
  NUM_MAP_PLANES                  = NUM_MAP_AXIAL_PLANES + NUM_MAP_NONAXIAL_PLANES,
  NUM_MAP_PACKED_BRUSH_OFFSETS    = (NUM_MAP_NONAXIAL_BRUSHES + 3) / 4,
  NUM_LIGHTS                      = 61,
  NUM_LIGHTMAP_SAMPLES            = clamp(LIGHTMAP_AA_SAMPLES, 1, 128),
  NUM_LIGHTMAP_POSTPROCESS_STEPS  = 4,
  NUM_LIGHTMAP_REGIONS            = 4 /*RGBA*/,
  NUM_LIGHTMAP_FRAMES             = NUM_LIGHTMAP_SAMPLES * NUM_LIGHTMAP_REGIONS,
  NUM_WAIT_FRAMES                 = NUM_LIGHTMAP_FRAMES + NUM_LIGHTMAP_POSTPROCESS_STEPS;

const int
    NUM_MAP_COLLISION_BRUSHES   = 28,
    NUM_MAP_COLLISION_PLANES    = 167;

const vec2
  ADDR_POSITION         = vec2(0,0),
  ADDR_VELOCITY         = vec2(0,1),  // W=jump key state
  ADDR_GROUND_PLANE       = vec2(0,2),
  ADDR_CAM_POS          = vec2(1,0),
  ADDR_TRANSITIONS        = vec2(1,1),  // X=stair step offset; Y=bob phase; Z=attack phase; W=# of shots fired
  ADDR_CAM_ANGLES         = vec2(1,2),
    ADDR_ANGLES           = vec2(2,0),  // X=yaw; Y=pitch; Z=ideal pitch; W=autopitch delay

    ADDR_PREV_MOUSE         = vec2(3,1),
  ADDR_PREV_CAM_POS       = vec2(4,0),
  ADDR_PREV_CAM_ANGLES      = vec2(4,1),
  ADDR_RESOLUTION         = vec2(5,0),  // XY=resolution; Z=flags
  ADDR_ATLAS_INFO         = vec2(5,1),  // X=max mip; Y=lod
  ADDR_PERF_STATS         = vec2(6,0),  // X=filtered frame time; W=UI state
  ADDR_GAME_STATE         = vec2(6,1),  // X=level; Y=time left; Z=score; W=last shot #
  ADDR_LIGHTING         = vec2(6,2),
  ADDR_TIMING           = vec2(7,0),
    ADDR_MENU           = vec2(7,1),
    ADDR_OPTIONS          = vec2(7,2);

const vec4
  ADDR_RANGE_PHYSICS        = vec4(0,0, 3,3),
  ADDR_RANGE_PERF_HISTORY     = vec4(8,0, 192,1),
  ADDR_RANGE_SHOTGUN_PELLETS    = vec4(8,1, 24,1),
    ADDR_RANGE_TARGETS        = vec4(8,2, NUM_TARGETS+1,1), // X=level; Y=last shot #; Z=hits
    ADDR_RANGE_LIGHTS       = vec4(0,3, NUM_LIGHTS,1),
    ADDR_RANGE_COLLISION_PLANES   = vec4(0,4, NUM_MAP_COLLISION_PLANES,1),
  ADDR_RANGE_NONAXIAL_PLANES    = vec4(0,8, 128,(NUM_MAP_NONAXIAL_PLANES+127)/128),
    ADDR_RANGE_LMAP_TILES     = vec4(0,ADDR_RANGE_NONAXIAL_PLANES.y + ADDR_RANGE_NONAXIAL_PLANES.w,
                                           128, (NUM_MAP_PLANES+127)/128),
    ADDR_RANGE_ATLAS_MIP0     = vec4(ATLAS_OFFSET, ATLAS_SIZE),
    ADDR_RANGE_ATLAS_CHAIN      = vec4(ATLAS_OFFSET, ATLAS_CHAIN_SIZE),
  ADDR_RANGE_PARAM_BOUNDS     = vec4(0,0, ATLAS_CHAIN_SIZE + ATLAS_OFFSET);

// Secondary buffer (Buffer C) addresses
const vec4
    ADDR2_RANGE_LIGHTMAP      = vec4(0,0,   LIGHTMAP_SIZE.x, LIGHTMAP_SIZE.y / 4u),
    ADDR2_RANGE_TEX_OPTIONS     = vec4(0,ADDR2_RANGE_LIGHTMAP.w, 144,24),
    ADDR2_RANGE_TEX_QUAKE     = vec4(0,ADDR2_RANGE_TEX_OPTIONS.y+ADDR2_RANGE_TEX_OPTIONS.w, 144,32),
    ADDR2_RANGE_FONT        = vec4(ADDR2_RANGE_TEX_OPTIONS.zy, 64,56),
    ADDR2_RANGE_PARAM_BOUNDS    = vec4(0,0,   max(ADDR2_RANGE_LIGHTMAP.xy + ADDR2_RANGE_LIGHTMAP.zw,
                                                        max(ADDR2_RANGE_TEX_QUAKE.xy + ADDR2_RANGE_TEX_QUAKE.zw,
                                                            ADDR2_RANGE_FONT.xy + ADDR2_RANGE_FONT.zw)));
const vec2
    SKY_TARGET_OFFSET       = vec2(NUM_TARGETS, 0);

const int
    RESOLUTION_FLAG_CHANGED     = 1 << 0,
    RESOLUTION_FLAG_THUMBNAIL   = 1 << 1;

// Persistent state structs (x-macros) /////////////////////////

#define PERF_STATS_FIELD_LIST(_)      \
    _(x, float, smooth_frametime, 0.)   \
  _(w, float, ui_state,     0.)
DEFINE_STRUCT(PerfStats, ADDR_PERF_STATS, PERF_STATS_FIELD_LIST)

#define TRANSITIONS_FIELD_LIST(_)     \
    _(x, float, stair_step,   0.)     \
  _(y, float, bob_phase,    0.)     \
  _(z, float, attack,     0.)     \
  _(w, float, shot_no,    0.)
DEFINE_STRUCT(Transitions, ADDR_TRANSITIONS, TRANSITIONS_FIELD_LIST)

#define TARGET_FIELD_LIST(_)        \
    _(x, float, level,      0.)     \
  _(y, float, shot_no,    0.)     \
  _(z, float, hits,     0.)
DEFINE_STRUCT_RANGE(Target, ADDR_RANGE_TARGETS, TARGET_FIELD_LIST)
        
#define LIGHTING_FIELD_LIST(_)          \
  _(x, int, num_lights,   NUM_LIGHTS)   \
  _(y, int, num_tiles,    NUM_MAP_PLANES) \
  _(z, float, bake_time,    0.)       \
  _(w, float, progress,   0.)
DEFINE_STRUCT(Lighting, ADDR_LIGHTING, LIGHTING_FIELD_LIST)

////////////////////////////////////////////////////////////////

#define GAME_STATE_FIELD_LIST(_)      \
    _(x, float, level,      0.)     \
  _(y, float, time_left,    0.)     \
  _(z, float, targets_left, 0.)
DEFINE_STRUCT(GameState, ADDR_GAME_STATE, GAME_STATE_FIELD_LIST)

bool in_progress(GameState game_state) { return game_state.level > 0.; }

////////////////////////////////////////////////////////////////

const int
    OPTION_TYPE_SLIDER            = 0,
    OPTION_TYPE_TOGGLE            = 1,

  OPTION_FLAG_INVERT_MOUSE        = 1 << 0,
  OPTION_FLAG_SHOW_FPS          = 1 << 1,
  OPTION_FLAG_SHOW_FPS_GRAPH        = 1 << 2,
  OPTION_FLAG_SHOW_LIGHTMAP       = 1 << 3,
  OPTION_FLAG_SHOW_WEAPON         = 1 << 4,
  OPTION_FLAG_NOCLIP            = 1 << 5,
  OPTION_FLAG_MOTION_BLUR         = 1 << 6,
  OPTION_FLAG_TEXTURE_FILTER        = 1 << 7,
  OPTION_FLAG_LIGHT_SHAFTS        = 1 << 8,
  OPTION_FLAG_CRT_EFFECT          = 1 << 9,
    
    DEFAULT_OPTION_FLAGS          = OPTION_FLAG_SHOW_WEAPON | OPTION_FLAG_MOTION_BLUR;

#define MENU_STATE_FIELD_LIST(_)      \
  _(x, int, selected,   0)      \
  _(y, int, open,     0)
DEFINE_STRUCT(MenuState, ADDR_MENU, MENU_STATE_FIELD_LIST)

#define OPTIONS_FIELD_LIST(_)       \
  _(x, float, brightness,   5.)     \
  _(y, float, screen_size,  10.)    \
  _(z, float, sensitivity,  5.)     \
  _(w, int, flags,      DEFAULT_OPTION_FLAGS)
DEFINE_STRUCT(Options, ADDR_OPTIONS, OPTIONS_FIELD_LIST)

struct MenuOption { int data; };
int get_option_type(MenuOption option)    { return option.data & 1; }
int get_option_field(MenuOption option)   { return (option.data >> 1) & 3; }
int get_option_range(MenuOption option)   { return option.data >> 3; }

#define MENU_OPTION_SLIDER(index)     MenuOption(OPTION_TYPE_SLIDER | ((index) << 1))
#define MENU_OPTION_TOGGLE(index, bit)    MenuOption(OPTION_TYPE_TOGGLE | ((index) << 1) | ((bit) << 3))

const MenuOption
    // must match Options struct defined above
  OPTION_DEF_BRIGHTNESS         = MENU_OPTION_SLIDER(0),
    OPTION_DEF_SCREEN_SIZE          = MENU_OPTION_SLIDER(1),
  OPTION_DEF_SENSITIVITY          = MENU_OPTION_SLIDER(2),
    OPTION_DEF_INVERT_MOUSE         = MENU_OPTION_TOGGLE(3, OPTION_FLAG_INVERT_MOUSE),
    OPTION_DEF_SHOW_FPS           = MENU_OPTION_TOGGLE(3, OPTION_FLAG_SHOW_FPS),
    OPTION_DEF_SHOW_FPS_GRAPH       = MENU_OPTION_TOGGLE(3, OPTION_FLAG_SHOW_FPS_GRAPH),
    OPTION_DEF_MOTION_BLUR          = MENU_OPTION_TOGGLE(3, OPTION_FLAG_MOTION_BLUR),
    OPTION_DEF_TEXTURE_FILTER       = MENU_OPTION_TOGGLE(3, OPTION_FLAG_TEXTURE_FILTER),
    OPTION_DEF_SHOW_LIGHTMAP        = MENU_OPTION_TOGGLE(3, OPTION_FLAG_SHOW_LIGHTMAP),
    OPTION_DEF_SHOW_WEAPON          = MENU_OPTION_TOGGLE(3, OPTION_FLAG_SHOW_WEAPON),
    OPTION_DEF_NOCLIP           = MENU_OPTION_TOGGLE(3, OPTION_FLAG_NOCLIP),
    OPTION_DEF_LIGHT_SHAFTS         = MENU_OPTION_TOGGLE(3, OPTION_FLAG_LIGHT_SHAFTS),
    OPTION_DEF_CRT_EFFECT         = MENU_OPTION_TOGGLE(3, OPTION_FLAG_CRT_EFFECT),

    // must match string table used in draw_menu (Image tab)
  OPTION_DEFS[] = MenuOption[]
  (
        OPTION_DEF_SENSITIVITY,
        OPTION_DEF_INVERT_MOUSE,
        OPTION_DEF_BRIGHTNESS,
        OPTION_DEF_SCREEN_SIZE,
        OPTION_DEF_SHOW_FPS,
        OPTION_DEF_SHOW_FPS_GRAPH,
        OPTION_DEF_TEXTURE_FILTER,
        OPTION_DEF_MOTION_BLUR,
        OPTION_DEF_LIGHT_SHAFTS,
        OPTION_DEF_CRT_EFFECT,
        OPTION_DEF_SHOW_LIGHTMAP,
        OPTION_DEF_SHOW_WEAPON,
        OPTION_DEF_NOCLIP
  )
;

const int NUM_OPTIONS           = OPTION_DEFS.length();
MenuOption get_option(int index)      { return OPTION_DEFS[index]; }

float get_downscale(Options options)    { return max(6. - options.screen_size * .5, 1.); }

////////////////////////////////////////////////////////////////

const int
    TIMING_FLAG_PAUSED            = 1 << 0;

#define TIMING_FIELD_LIST(_)        \
    _(x, float, anim,     0.)     \
  _(y, float, prev,     0.)     \
  _(z, int, flags,      0)
DEFINE_STRUCT(Timing, ADDR_TIMING, TIMING_FIELD_LIST)

////////////////////////////////////////////////////////////////

vec4 load_camera_pos(sampler2D settings, bool thumbnail)
{
    if (!is_demo_mode_enabled(thumbnail))
        return load(ADDR_CAM_POS, settings);
    return vec4(get_demo_scene().pos, 0);
}

vec4 load_camera_angles(sampler2D settings, bool thumbnail)
{
    if (!is_demo_mode_enabled(thumbnail))
        return load(ADDR_CAM_ANGLES, settings);
    return vec4(get_demo_scene().angles, 0, 0);
}
