# RTX Character Rendering Sample User Guide

![banner](figures/UserGuide/UserGuide.png)

The RTX Character Rendering Sample provides an example of using the RTX Character Rendering libraries for hair and skin. It also provides a GUI to easily interact with the sample, including changing materials in real-time to demonstrate rendering details for skin and hair.

The GUI can be opened or closed using the `ESC` key on the keyboard.

## Generic

|         Name       |   Description   |
|:----------------------------|:-----------------------------|
|    Camera Speed   |   The speed of camera   |
|    Lock Camera   |   Lock the camera or allow moving/rotation   |
|    Scene   |   Select the render scene   |
|    Refresh Scene   |   Reload the current scene   |
|    Back Face Culling   |   Enable/Disable backface culling   |
|    Enable Soft Shadows   |   Enable/Disable soft shadows from punctual lights   |
|    Transmission   |   Allow rays to penetrate the surface and sample on the other side as refraction ray  |
|    Recompile Shader   |   Recompile shaders in real time; any compilation errors will be displayed in the console window  |
|    Capture Screenshot   |  Capture a screenshot of the current render result and save it as a `.png` file in the `bin/screenshots/` folder |

### DLFG

DLFG (DLSS Frame Generation) is supported in our sample on RTX 40 and 50 series GPUs.  
- `RTX 40 series`: Supports single-frame generation.  
- `RTX 50 series`: Supports up to 3 generated frames (4× DLFG) for higher frame rates.

### Reflex

Reflex is supported in our sample to reduce system latency and improve responsiveness.
- `Low Latency`: Minimizes input lag by syncing CPU and GPU work.
- `Low Latency + Boost`: Includes Low Latency plus keeps GPU at high clocks to avoid slowdowns when CPU-limited.

## Path Tracing

|         Name       |   Description   |
|:----------------------------|:-----------------------------|
|    Enable Random   |   Enable random light and BSDF sampling   |
|    Bounces   |   Maximum number of bounces allowed for each path    |
|    Exposure Adjustment   |   Post-processing exposure adjustment   |
|    Debug Output   |   Show Debug Views    |

## Denoiser

Denoiser selection: we currently support DLSS-RR, NRD, and reference mode.

DLSS-RR has following 5 quality modes:
- `DLAA`: Full-screen denoiser. Ultimate Quality.
- `Quality`: Best quality with some performance cost.
- `Balance`: Best balance mode between quality and performance.
- `Performance`: Better performance, could have minor artifacts or blurries.
- `UltraPerformance`: Best performance, but could cause significant artifacts and blurries.

We also provide NRD as an alternative denoiser, compatible with any ray tracing-capable GPU. For more details, check the [NRD Guide]

### Upscaler

Upscaler Selection: When using NRD or disabling the denoiser, we currently support DLSS-SR and TAA as upscalers.

Similar to DLSS-RR, DLSS-SR also has 5 quality modes:
- `DLAA`: Full-screen denoiser. Ultimate Quality.
- `Quality`: Best quality with some performance cost.
- `Balance`: Best balance mode between quality and performance.
- `Performance`: Better performance, could have minor artifacts or blurries.
- `UltraPerformance`: Best performance, but could cause significant artifacts and blurries.

## Lighting

|         Name       |   Description   |
|:----------------------------|:-----------------------------|
|    Enable Sky  |   Enable skybox   |
|    Sky Type  |   Constant/Procedural/EnvironmentMap   |
|    Environment Map  |   Choose the environment map from assets folder    |
|    Environment Light Intensity |   The scalar for environment map lighting    |
|    Enable Emissives  |   Enable the emissive surfaces    |
|    Show Emissive Surfaces  |    Show the emissive surfaces. If disabled, the emissives will still be calculated, but the surfaces will be invisible   |
|    Enable Lighting  |    Enable/Disable all lights   |
|    Enable Direct Lighting  |    Enable the direct lighting   |
|    Enable Indirect Lighting  |    Enable the indirect lighting   |

## Hair

See explanations of parameters in [RTX Character Rendering Hair Guide], Step 2.

To use the GUI parameters, tick "Enable Hair Material Override".

## Subsurface Scattering

See explanations of parameters in [RTX Character Rendering SSS Guide], Step 2.

To use the GUI parameters, tick "Enable Hair Material Override".

## Animation

| Name | Description |
|:----------------------------|:-----------------------------|
| Enable Animation | Globally enable animation. |
| Speed | Animation playback speed, in seconds per frame. |
| Enable Animation Smoothing | Smooth the transition of the last frame to avoid sudden jumps back to the first frame. |
| Smoothing Factor | Controls the amount of smoothing applied to the animation. |
| Enable Animation Debugging | Enable debugging for animation playback. |
| Animation Keyframe Index Override | Render the animation at a specific keyframe index. |
| Animation Keyframe Weight Override | Render the animation between keyframe N and N+1 with a specific interpolation weight. |

## Tone mapping

|         Name       |   Description   |
|:----------------------------|:-----------------------------|
|    Operator  |   Tonemapping operator, support Reinhard and Linear   |
|    Clamp  |   Clamp final output color to 0-1  |

## Cmdline Options

### Generic
- `-d3d12` or `-dx12`: Use DirectX 12 as the graphics API (default). DirectX 11 is not supported in this sample.
- `-vk` or `-vulkan`: Use Vulkan as the graphics API.
- `-borderless`: Create a window without borders.
- `-fullscreen`: Launch in fullscreen mode.
- `-1080p`, `-1440p`, `-2160p`: Set the initial resolution (1080p, 2K, or 4K).
- `-scene`: Specify the scene to load in the sample.
- `-screenshot`: Specify the screenshot filename.
- `-enableSky`: Enable or disable the skybox.

### Denoiser
- `-denoiser`: Select denoiser mode: None(0), NRD(1), or DLSS-RR(2).
- `-nrdMode`: Select NRD mode: Reblur(0) or Relax(1).
- `-accumulate`: Enable reference accumulation mode.

### Hair
- `-hairBsdf`: Select hair BSDF: Chiang BSDF(0) or FarField BSDF(1).
- `-hairColorMode`: Select hair color mode: color(0) or physics(1).
- `-enableHairOverride`: Enable hair override from the GUI.
- `-hairRadiusScale`: Scale factor for hair radius.
- `-hairTessellationType`: Select hair geometry tessellation: Polytube(0), DOTS(1), or LSS(2).

### Animation
- `-enableAnimation`: Enable morph target animation.
- `-animationKeyframeIndex`: Debugging option. Render the animation at a specific keyframe index.
- `-animationKeyframeWeight`: Debugging option. Render the animation between keyframe N and N+1 with a specific interpolation weight.


[RTX Character Rendering Hair Guide]: RtxcrHairGuide.md
[RTX Character Rendering SSS Guide]: RtxcrSssGuide.md
[NRD Guide]: https://github.com/NVIDIA-RTX/NRD/blob/master/README.md
