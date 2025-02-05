# RTXCR Subsurface Scattering Rendering Integration Guide

## Introduction

Evaluating subsurface scattering requires doing integration on the whole surface of geometry, which needs to perform Monte Carlo Integration with random walk algorithm. This method is very time consuming and not suitable for real-time rendering. [ [Burley15] ] provides a good approxiamtion for the whole multiple scattering process with a cheaper radius-based simplified solution called diffusion profile. Screen space based diffusion profile SSS is widely used in state of art real-time SSS solutions. Use Burley diffusion profile as an example, it is created based on search light configuration, which shoots a light beam into a semi-infinite thick flat surface and perform Monte Carlo random walk. After that the distribution of how much light come out relative to the distance from the light beam entering point is measured. Finally, we can create some functions to approximate such dataset and the function is what we called diffusion profile.

However, diffusion profile doesn't work well when the assumption of search light configuration is broken, which means the volume is not semi-infinite anymore or it's not flat surface, such as the thin geometries which is a common case for human ears.

<p align="center">
  <img src="figures/Subsurface/Diffusion_vs_Ground_Truth.png" width="550" height="500">
</p>

As we can see here, the diffusion profile misses a lot of the details that really represents the feature of the asset.

To alleviate this issue, we implemented a fully ray-trace based SSS (RTSSS) solution with a combined approach: diffusion profile to provide a cheap estimation of multiple scattering, with ray trace single scattering to capture important light phenomenon caused by geometry features.

### Diffusion Profile
We implement the diffusion profile for real-time in screen space.

### Single Scattering Transmission
In our implementation, we sample the integration with the following process.

First according to the transmission bsdf, we select a direction. We currently only handle diffuse transmission but it’s also possible to sample specular transmission bsdf.

Then we trace a ray.Once it hit the other side of the geometry, we evaluate the transmission bsdf again to exit the volume.This whole path is called boundary term in volume scattering.

Then along the ray, we importance sample the distance according to the volume coefficients and compute the single scattering contribution.

<p align="center">
  <img src="figures/Subsurface/SSS_Transmission.png" width="280" height="500">
</p>

## Integration

RTXCR SDK SSS Material Library is a library based on the shader instructions, which provides functions to evalute and sample the BCSDF. We also define the SSS material to help developers easily integrate into their path tracer. We will show the actual steps and some sample code to provide a guidance on the integration:

### Step 1: Add RTXCR SDK Material Library as Submodule

    - git submodule add https://github.com/NVIDIA-RTX/RTXCR-Material-Library.git

### Step 2: Extend the material system

Add new SSS material type in the material system of your path tracer and your tools which are provided to the artists.
Make sure to support all these required variables in 'RTXCR_SubsurfaceMaterialData':

- 'transmissionColor': Determines the base color of the SSS surface, it's similar to the diffuse albedo color for diffuse materials. This parameter can also be set with a texture map.
- 'scatteringColor': Determines the distance (mean free path) that light will be transported inside the SSS object for each color channel. Larger value will allow the corresponding color scattered further on the surface, it will look like a tail extends from the diffuse model.
- 'scale': A scale that controls the SSS intensity of the whole object.
- 'g' (Anisotropy): Determines the overall scattering direction of the volume phase function, the range is (-1, 1). When this value less then 0, it models backwards scattering. Vice versa, it models forward scattering when the value larger than 0. The volume is isotropic when this value is 0.

### Step 3: Evaluate Diffusion Profile

The SDK provides all the utility function to implement our SSS algorithm.

A developer has the freedom to generate a diffusion profile sample with either a camera facing, screen space kernel or a geometry normal face world space kernel.
Then SDK will help to importance sample the diffusion profile. Engine can then combine it with irradiance to compute the sss response.

#### Step 3.1: Generate sample on disk from SSS library

The first step is generating a sample on the disk and the diffusion profile weight for the sample from SSS library:

```cpp
SubsurfaceSample subsurfaceSample; // SamplePosition and bssrdfWeight (diffusion profile weight)
RTXCR_EvalBurleyDiffusionProfile(subsurfaceMaterialData, subsurfaceInteraction, /* out */ subsurfaceSample)
```

#### Step 3.2: Sample Ray Generation on engine

Then from engine side, developers need to either trace a ray or project to screen space to get a sample from surface.

#### Step 3.3: Get irradiance of the sample from surface, evaluate SSS (BSSRDF)

With the sample geometry information and sample irradiance which can be cached or calcuated at real-time, we have enough information to send to SDK to evaluate the BSSRDF of the sample:

```cpp
const float cosThetaI = min(max(0.00001f, dot(sampleShadingNormal, lightVector)), 1.0f);
radiance += RTXCR_EvalBssrdf(subsurfaceSample, sampleLightRadiance, cosThetaI);
```

### Step 4: Evaluate SSS Transmission
The transmission part is more straightforward.

#### Step 4.1: Refraction Sample Ray Generation

Given a scene on the right side, camera shoot a primary ray, which hits the geometry, it gets refracted according to transmission bsdf, and directly hit the other side of the geometry and exit.

Currently the default sampling method is simple cosine-weighted transmission hemisphere sampling:

```cpp
RTXCR_CalculateRefractionRay(subsurfaceInteraction, rand2);
```

Developers can also use customized sampling functions for specular transmission by replacing the default sampling method here.

<p align="center">
  <img src="figures/Subsurface/SSS_Transmission_Trace_1.png" width="280" height="500">
</p>

#### Step 4.2: Single Scattering

On the other hand, if we sample the distance according to volume coefficient and scatter ray once within the volume, then exit the geometry we can get the single scattering contribution.

We first importance sample along the refraction ray:
```cpp
float T = RTXCR_SampleDistance(float random /* inout */ , float sigmaT)
```

Then, the SDK provides utility functions to importance sample on the piecewise scattering distance function and to importance sample the phase function:

```cpp
// Sample single scattering direction with HG (Henyey Greenstein) phase function
float3 dir = RTXCR_SampleDirectionHG(float2 random2 /* inout * /, float g, float3 wo)
```

Base on the direction, we trace single scattering ray, get boundary surface normal Ns, position and HitT. And do light sampling to evaluate single scattering contribution.

```cpp
float scatteringDistance = T + HitT;
RTXCR_EvaluateSingleScattering(Li, Ns, scatteringDistance, sssMaterial);
```
<p align="center">
  <img src="figures/Subsurface/SSS_Transmission_Trace_2.png" width="280" height="500">
</p>


## Reference
[1] Approximate Reflectance Profiles for Efficient Subsurface Scattering. Per H. Christensen, B Burley.“An approximate reflectance profile for efficient subsurface scattering.” ACM SIGGRAPH (2015): 1-1 | [Paper Link](https://graphics.pixar.com/library/ApproxBSSRDF/paper.pdf)