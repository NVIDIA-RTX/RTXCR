# RTXCR Sample User Guide

![banner](figures/UserGuide/UserGuide.png)

The RTXCR Sample provides an example of using the RTXCR libraries for hair and skin. It also provides a GUI to easily interact with the sample, even changing materials in real-time to demonstrate the rendering details for skin and hair.

The GUI can be opened or closed using the `ESC` key on the keyboard.

## Basic Camera Controls

- `W`: Move forward
- `S`: Move backward
- `A`: Move left
- `D`: Move right
- `Q`: Move down
- `E`: Move up
- `Left Mouse Button`: Rotate the camera

## Generic

|         Name       |   Description   |
|:----------------------------|:-----------------------------|
|    Camera Speed   |   The speed of camera   |
|    Lock Camera   |   Lock the camera or allow moving/rotation   |
|    Scene   |   Select the render scene   |
|    Refresh Scene   |   Reload the current scene   |
|    Back Face Culling   |   Enable/Disable backface culling   |
|    Enable Soft Shadows   |   Enable/Disable the soft shadows from punctual lights   |
|    Transmission   |   Allow ray to penetrate the surface and sample on the other side as refraction ray  |
|    Recompile Shader   |   Recompile shader at real-time, and any compilation errors will be displayed in the console window  |
|    Capture Screenshot   |  A screenshot of the current render result will be captured and saved as a `.png` file in the `bin/screenshots/` folder |

## Path Tracing

|         Name       |   Description   |
|:----------------------------|:-----------------------------|
|    Enable Random   |   Enable randomly light and BSDF sampling   |
|    Bounces   |   Maximum number of bounces allowed for each path    |
|    Exposure Adjustment   |   Post-processing exposure adjustment   |
|    Debug Output   |   Show Debug Views    |

## Denoiser

Denoiser selection: we currently support DLSS-RR and reference mode.

DLSS-RR has following 5 quality modes:
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
|    Show Emissive Surfaces  |    Show the emissive surfaces. If disabled, the emissive will still be calculated, but the emissive surfaces will be invisiable   |
|    Enable Lighting  |    Enable/Disable all lights   |
|    Enable Direct Lighting  |    Enable the direct lighting   |
|    Enable Indirect Lighting  |    Enable the indirect lighting   |

## Hair

See the explainations of parameters here in [RTXCR Hair Guide], Step 2

To use the GUI parameters, tick "Enable Hair Material Override"

## Subsurface Scattering

See the explainations of parameters here in [RTXCR SSS Guide], Step 2

To use the GUI parameters, tick "Enable Hair Material Override"

## Tone mapping

|         Name       |   Description   |
|:----------------------------|:-----------------------------|
|    Operator  |   Tonemapping operator, support Reinhard and Linear   |
|    Clamp  |   Clamp final output color to 0-1  |

[RTXCR Hair Guide]: docs/RtxcrHairGuide.md
[RTXCR SSS Guide]: docs/RtxcrSssGuide.md

## Cmdline Options

The default graphics settings are: `1080p`, `windowed`, `DLSS-RR` in `Quality mode`, `LSS` for Blackwell or `DOTS` for all other RTX GPUs.

We also provide the following command line options for users:

- `-d3d12' or '-dx12`: Choose DirectX 12 as Graphics API (Default). DirectX 11 is not supported in this sample.
- `-vk` or `-vulkan`: Choose Vulkan as Graphics API
- `-borderless`: Create sample window without border.
- `-fullscreen`: Fullscreen mode.
- `-1080p`, `-1440p`, `-2160p`: Initial resolution of sample, the options are 1080p, 2k and 4k.
- `-scene`: The scene of sample
- `-denoiser`: Select denoiser mode, None(0) or DLSS-RR(1)
- `-accumulate`: Enable reference mode of sample
- `-enableSky`: Enable/Disable skybox
- `-hairBsdf`: Select hair BSDF, Chiang BSDF(0) or FarField BSDF(1)
- `-hairColorMode`: Select hair color mode, color(0) or physics(1)
- `-enableHairOverride`: Enable the hair override from GUI
- `-hairRadiusScale`: Scalar for the radius of hair
- `-hairTessellationType`: Select the tessellation type of hair geometry, Polytube(0), DOTS(1) or LSS(2), we currently only support LSS in DirectX12
