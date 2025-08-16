# RTX Character Rendering

![banner](docs/figures/Claire_Demo.png)

## Introduction

RTX Character Rendering implements techniques used for rendering realistic human hair and skin.

RTX Character Rendering implements two hair shading techniques, the traditional near-field Chiang BSDF [ [Chiang15] ], and a novel far-field analytical approach developed by the Omniverse team. It also includes an optimized, high-fidelity morph target animation system for realistic hair animation. For more information on that solution and how to integrate it, please check the [RTX Character Rendering Hair Guide].

RTXCR introduces a novel hair data structure called Linear-Swept Sphere (LSS). It delivers high performance in tracing and BVH updates while maintaining exceptionally low memory consumption. Additionally, it enables SOL tracing using the LSS Hardware Intersector on 4th-generation Ray Tracing Cores in Blackwell GPUs. For other RTX GPUs, we provide a fallback solution known as Disjoint Orthogonal Triangle Strips (DOTS), which replaces traditional triangle-based strands. For more details, check our [LSS Technical Blog].

For skin, RTX Character Rendering implements a combined subsurface scattering solution which extended SOTA Burley Diffusion Profile [ [Burley15] ] with a single scattering term. This provides better support for both backward scattering from diffusion profile and forward transmission from single scattering.
For Technique Details and integration, check [RTX Character Rendering SSS Guide].

## Project structure
| Directory                                                | Details                                                |
|----------------------------------------------------------|--------------------------------------------------------|
|[/docs](docs)                                             |_Documentation for showcased tech_                      |
|[/external](external)                                     |_Helper dependencies and framework used for the samples_|
|[/assets](https://github.com/NVIDIA-RTX/RTXCR-Assets.git) |_Assets and scene definitions_                          |
|[/samples](samples/pathtracer/)                           |_Samples showcasing usage of RTX Character Rendering_   |
|[/libraries](libraries/rtxcr/)                            |_Binaries, src, includes for RTX Character Rendering_   |

### Provided samples:
- Pathtracer (default): showcases the most common use case - a pathtracer that relies on RTX Character Rendering to show the hair and skin techniques, including the hair BCSDF/BSSRDF evaluation, importance sampling and denoising (DLSS-RR/NRD).

For more details of running sample, check [RTX Character Rendering User Guide].

## Getting up and running

### Prerequisites
RTX 20 series or newer. Radeon RX 7000 Series or newer. Intel Arc Series. **|** Driver ≥ 580.88 **|** [CMake] v3.30.0 **|** [Git LFS] **|** [Vulkan SDK] 1.4.313.2 **|** Windows SDK ≥ 10.0.22621.0

### Building and Running the Sample Apps

- To get started, clone the repository along with all submodules:

    - `git clone --recursive https://github.com/NVIDIA-RTX/RTXCR.git`

    If the repository was cloned without submodules, update them separately:

    - `git submodule update --init --recursive`

There are three ways to build and run the sample. Choose the method that best suits your workflow.

#### 1. Building and Running with Scripts
If you only need to build and run the sample without modifying the code:

1. Run the `build.bat` script.
	- For AMD or Intel users, include the `-disableStreamline` parameter to disable Streamline.

2. Once the build completes successfully, execute `run.bat` to launch the application.

#### 2. Generating and Using a Visual Studio Solution
If you want to inspect or modify the code in Visual Studio:

1. Run the `generateSolution.bat` script to generate a Visual Studio solution. To automatically open the solution after generation, add the `-v` parameter.  
	- For AMD or Intel users, include the `-disableStreamline` parameter to disable Streamline.

2. We recommend selecting the `RelWithDebInfo` solution configuration and building the project in Visual Studio to achieve the best balance between performance and debugging capabilities.

3. Run the application from within Visual Studio.

#### 3. Generating a Visual Studio Solution Using CMake GUI
If you prefer configuring the solution manually:

1. Assuming that the RTXCR SDK tree is located in `D:\RTXCR`, open [CMake GUI] and set the following parameters:
	- "Where is the source code" to `D:\RTXCR`
	- "Where to build the binaries" to `D:\RTXCR\build`

2. Click "Configure", set "Generator" to the Visual Studio version you're using, set "Optional platform" to x64, click "Finish".
	- The data assets used for the sample will be automatically downloaded to the `/assets` folder in this step.
	- For AMD or Intel users, untick the "USE_VK_STREAMLINE" option in the CMake GUI to disable Streamline.

3. Click "Generate", then "Open Project".

4. We recommend selecting the `RelWithDebInfo` solution configuration and building the project in Visual Studio to achieve the best balance between performance and debugging capabilities.

5. Open the `RTXCR.sln` in the `/build` folder.

6. Build and run the application using Visual Studio.

#### 4. Run Animation (Optional)
Animation is not enabled by default. To run the animation, you can either:

1. From the command line:
   `run.bat -enableAnimation 1 -scene claire.animation.scene.json` or add `-enableAnimation 1 -scene claire.animation.scene.json`

2. From Visual Studio:
- Go to the **Debugging** tab under your project properties.
- Add the following to **Command Arguments**:
  `-enableAnimation 1 -scene claire.animation.scene.json`

## Contact

RTXCR is actively being developed. Please report any issues directly through the GitHub issue tracker. For any information or suggestions, contact us at rtxcr-sdk-support@nvidia.com

## Citation
If you use RTXCR in a research project leading to a publication,
please cite the project.

BibTex:
```bibtex
@online{RTXCR,
   title   = {{{NVIDIA}}\textregistered{} {RTXCR}},
   author  = {{NVIDIA}},
   year    = 2025,
   url     = {https://github.com/NVIDIA-RTX/RTXCR},
   urldate = {2025-02-06},
}
```

## License
See [License.txt](License.txt)

## Reference
[1]A practical and controllable hair and fur model for production path tracing, MJY Chiang, B Bitterli, C Tappan, B Burley
Computer Graphics Forum 35 (2), 275-283 | [Paper Link](https://benedikt-bitterli.me/pchfm/)

[2]Approximate Reflectance Profiles for Efficient Subsurface Scattering. Per H. Christensen, B Burley.“An approximate reflectance profile for efficient subsurface scattering.” ACM SIGGRAPH (2015): 1-1 | [Paper Link](https://graphics.pixar.com/library/ApproxBSSRDF/paper.pdf)


[RTX Character Rendering Hair Guide]: docs/RtxcrHairGuide.md
[LSS Technical Blog]: https://developer.nvidia.com/blog/render-path-traced-hair-in-real-time-with-nvidia-geforce-rtx-50-series-gpus/
[RTX Character Rendering SSS Guide]: docs/RtxcrSssGuide.md
[RTX Character Rendering User Guide]: docs/RtxcrUserGuide.md
[Chiang15]: https://benedikt-bitterli.me/pchfm/
[Burley15]: https://graphics.pixar.com/library/ApproxBSSRDF/paper.pdf
[CMake]: https://cmake.org/download/
[Git LFS]: https://git-lfs.com/
[Vulkan SDK]: https://vulkan.lunarg.com/sdk/home#windows
[CMake GUI]: https://cmake.org/download/
