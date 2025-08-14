# Hair Rendering Integration Guide

## Introduction

![banner](figures/HairIntegration/MarschnerHairModel.png)

<p align="center">
    <em>Figure1: Marschner's 3 semi-separable lobe model.</em>
</p>


RTX Character Rendering SDK implemented 2 strand-based hair BCSDF shading models, near-field `Chiang BCSDF` and a novel `Far-Field BCSDF`. Both models are based on the hair model by [Marschner03][Figure1], which breaks down the BCSDF into 3 semi-separable lobes R, TT and TRT. The scattering result of the three lobes are then added together as the final evaluation for the BCSDF.

`R` refers to the part of the ray that directly reflect back without entering into the volume of the hair.
`TT` refers to the transmission + transmission: the ray first refracts into the inside of the hair then refracts again on the other side of the volume boundary to exit the hair curve.
`TRT` refers to transmission + reflection  + transmission: the ray refracts into the hair but it reflects on the back of the hair so it stays inside of the hair volume. Then when it arrive the boundary again, it refracts out to leave the hair curve.

![banner](figures/HairIntegration/CurveCoordinate.png)

<p align="center">
    <em>Figure2: Strand-Based hair curve coordinate system. At any parametric point u along a Curve shape, the cross-section of the curve is defined by
        a circle. All of the circle’s surface normals at u (arrows) lie in a plane (dashed lines), dubbed the "normal plane".</em>
</p>


For every lobe, this hair model splits into a product of longitudinal direction contribution **M** and azimuthal direction contribution **N**. As shown in [Figure2], the longitudinal direction is defined as ∂p/∂u of the curve coordinate system, where *p* is a point on the curve and *u* is the coordinate along the curve direction, perpendicular to the normal plane. The azimuthal direction is defined as ∂p/∂v on the normal plane, where *v* is the coordinate around the curve’s outline on the normal plane.

The final equation for the BCSDF is: `Fp(wo, wi) = M_R * N_R + M_TT * N_TT + M_TRT * N_TRT / cosθ`

### Near-Field and Far-Field

|              _Near-Field BCSDF_              |               _Far-Field BCSDF_              |
|:--------------------------------------------:|:--------------------------------------------:|
| <img src="figures/Hair/Chiang_Near.png" width="600"/> | <img src="figures/Hair/Farfiedld_Near.png" width="600"/> |

#### Near-Field Hair BCSDF

When an incident ray hits a hair/fur fiber with near-field scattering on the actual incident position, the actual azimuthal offset h is used to calculate the azimuthal part of BCSDF weight. The advantage of this method is the detail on the azimuthal part is conserved, so we can see some highlights along the longitudinal direction with this method. However, this feature is subtle when the camera is not very close to the hair geometries, and introduces significant noise due to its high frequency nature.

#### Far-Field Hair BCSDF

In contrast, the far-Field model averages the energy to the entire azimuthal part without considering the exact azimuthal offset h. It may lose some details but in most cases the result is close to the near-field mode since the camera is not so close to the hair/fur. If we consider rendering a human or animal, it doesn't make sense to move the camera to such a close distance to the hair/fur unless for debugging purposes.

The advantage of this model is obvious: It has less noise and render similar quality as near-field model. For real-time path tracer, noise is already very severe problem and needs lots of effort to handle. Even if we have decent denoisers such as NRD/DLSS-RR, it always benefit to have a less noise original signal.

In addition, our new hair Far-Field BCSDF model optimizes approximation with multiple Gaussians to better match the Monte Carlo Simulation.[[Eugene24](elliptic_hair.pdf)] Our rendering result clearly shows it better matches the real photograph compare to the Chiang BCSDF:

|         _Chiang BCSDF_       |   _Our New Far-Field BCSDF_   | _Microscope Photograph (REF)_  |
|:----------------------------:|:-----------------------------:|:------------------------------:|
| <img src="figures/Hair/Chiang.png" width="400"/> | <img src="figures/Hair/Farfield.png" width="400"/> | <img src="figures/Hair/Dielectric.png" width="400"/> |

---

### Hair Animation

RTX Character Rendering supports morph target animation for hair, enabling smooth and realistic motion.

At runtime, we compute the current **keyframe index** and **interpolation weight** based on the total elapsed time and animation speed (assuming each animation frame has an equal duration):

`keyFrameIndex = (int)(fmod(totalTime, totalAnimationTime) / animationTimestampPerFrame)`

`lerpWeight = (fmod(totalTime, totalAnimationTime) - keyFrameIndex * animationTimestampPerFrame) / animationTimestampPerFrame`

We then update the animated vertex positions in a compute shader:

`vertexBuffer[globalIndex] = lerp(morphTargetVertexData[keyFrameIndex], morphTargetVertexData[(keyFrameIndex + 1) % totalMorphTargetFrameSize], lerpWeight)`

**Note:** If `keyFrameIndex` is the last frame, wrap around using modulo for the next frame:  
`nextKeyFrameIndex = (keyFrameIndex + 1) % totalMorphTargetFrameSize`

#### Hair Animation Denoising

Denoising thin geometry like hair is challenging. Denoisers such as **DLSS-RR** and **NRD** require accurate motion vectors for temporal reprojection. However, because hair strands are often sub-pixel in width, screen-space motion vectors can be unreliable or missing.

To address this, we perform the following steps:
1. Compute **3D world-space motion vectors** based on the previous and current keyframe data.
2. Project these to screen space to generate stable screen-space motion vectors.

Since LSS hair geometry is extremely thin, we then do the following:
- Use the **midpoint** of the hair segment for motion tracking.
- Ignore azimuthal (rotational) variation for simplicity.

```cpp
const float u = rayBarycentrics.x; // Ignore azimuthal coordinate v

// Current mid-line position in world space
const float3[2] lssSegmentPositions     = { lssSegmentBuffer[primitiveIndex].p0, lssSegmentBuffer[primitiveIndex].p1 };
const float3     lssCenterPosition      = lerp(lssSegmentPositions[0], lssSegmentPositions[1], u);
const float3     lssCenterWorldPosition = mul(instance.transform, float4(lssCenterPosition, 1.0f)).xyz;

// Previous frame mid-line position in world space
const float3[2] lssSegmentPositionsPrev     = { prevLssSegmentBuffer[primitiveIndex].p0, prevLssSegmentBuffer[primitiveIndex].p1 };
const float3     lssCenterPositionPrev      = lerp(lssSegmentPositionsPrev[0], lssSegmentPositionsPrev[1], u);
const float3     lssCenterWorldPositionPrev = mul(instance.transform, float4(lssCenterPositionPrev, 1.0f)).xyz;

// Project both positions to clip space
float4 clipCurr = mul(float4(lssCenterWorldPosition, 1.0f), matWorldToClip);
clipCurr.xyz /= clipCurr.w;
float4 clipPrev = mul(float4(lssCenterWorldPositionPrev, 1.0f), matWorldToClipPrev);
clipPrev.xyz /= clipPrev.w;

// Final motion vector in screen space
motionVector = (clipPrev.xy - clipCurr.xy) * clipToWindowScale;
```

This method works well for both DLSS-RR and NRD, significantly reducing ghosting artifacts on hair.

Comparison:

|              _No_Motion_Vector_              |               _Motion_Vector_              |
|:--------------------------------------------:|:--------------------------------------------:|
| <img src="figures/Hair/Claire_No_MV.png" width="600"/> | <img src="figures/Hair/Claire_MV.png" width="600"/> |


## Integration

The Hair Material Library is based on shader instructions and provides functions to evaluate and sample the BCSDF. We define a hair material type to help developers easily integrate into their path tracer. We will show the actual steps and some sample code to provide a guidance on the integration:

### Step 1: Add Material Library as Submodule

    - git submodule add https://github.com/NVIDIA-RTX/RTXCR-Material-Library.git

### Step 2: Extend the material system

Add a hair material type in the path tracer’s material system and in the tools provided to artists.

Required variables in `HairMaterialData`:
- `baseColor`: The color of the hair, only will be used when the absorption model is HairAbsorptionModel::Color
- `longitudinalRoughness`: Roughness on longitudinal direction
- `azimuthalRoughness`: Roughness on azimuthal direction
- `ior`: The index of refraction of the hair volume
- `eta`: 1 / ior
- `fresnelApproximation`: Flag that enable Schlick fresnel approximation or not, set to true by default
- `absorptionModel`: Color based or physics based
- `melanin`: The melanin of the hair, 0 means no melanin, which makes the hair white; while 1 means maximum melanin, which makes hair black. Only will be used when the absorption model is HairAbsorptionModel::Physics or HairAbsorptionModel::Physics_Normalized
- `melaninRedness`; Control the redness of the hair. Only will be used when the absorption model is HairAbsorptionModel::Physics or HairAbsorptionModel::Physics_Normalized
- `cuticleAngleInDegrees`: The cuticle angle on top of the hair, the larger angle we have, the R and TRT highlights will be further away from each other. 0 means completely smooth hair on the cuticle.

### Step 3: Evaluate hair BCSDF for direct radiance

#### Step 3.1: Transform to Local Coordinate System

All shadings are done in the local tangent coordinate system. So we need to transfer the view direction vector and light direction vector to tangent space:

```cpp
const float3x3 hairTangentBasis = float3x3(tangentWorld, biTangentWorld, shadingNormal); // TBN
const float3 viewVectorLocal = mul(hairTangentBasis, viewVector);
const float3 lightVectorLocal = mul(hairTangentBasis, vectorToLight);
```

#### Step 3.2: Setup HairInteractionSurface

Provide the incident ray direction, normal and tangent in tangent space:

```cpp
HairInteractionSurface hairInteractionSurface = (HairInteractionSurface) 0;
hairInteractionSurface.incidentRayDirection = viewVectorLocal;
hairInteractionSurface.shadingNormal = float3(0.0f, 0.0f, 1.0f);
hairInteractionSurface.tangent = float3(1.0f, 0.0f, 0.0f);
```

#### Step 3.3: Evaluate the BCSDF

Now we have all we need to evaluate the BCSDF, just simply create the `HairMaterialInteraction` for Chiang BCSDF or `HairMaterialInteractionBcsdf` for the novel Far-Field BCSDF. The "interaction" stores all the variables that needed for calculating the BCSDF, we separate the Chiang and Far-Field because Chaing needs some pre-computation for the cuticle angle. Then we just send it with light and view direction to the `hairChiangBsdfEval` to get the final BCSDF result:

##### Chiang BCSDF:

```cpp
HairMaterialInteraction hairMaterialInteraction = createHairMaterialInteraction(hairMaterialData, hairInteractionSurface);
hairBsdf = RTXCR_HairChiangBsdfEval(hairMaterialInteraction, lightVectorLocal, viewVectorLocal);
```

##### Far-Field BCSDF:
The far-field version BCSDF optionally supports diffuse layer on top of the BCSDF lobe. So we need to send both hair roughness and diffuse tint to the evaluation function. If the game is not using diffuse layer, just send 0 to the evaluation function.

```cpp
HairMaterialInteractionBcsdf hairMaterialInteractionBcsdf = createHairMaterialInteractionBcsdf(hairMaterialData, g_Global.diffuseReflectionTint, g_Global.diffuseReflectionWeight, g_Global.hairRoughness);
RTXCR_HairFarFieldBcsdfEval(hairInteractionSurface, hairMaterialInteractionBcsdf, lightVectorLocal, viewVectorLocal, hairBsdf);
```

### Step 4: Evaluate hair BCSDF for indirect radiance

For indirect radiance, we first sample the BCSDF lobe to get indirect rays:

#### Chiang:
```cpp
HairLobeType lobeType;
HairMaterialInteraction hairMaterialInteraction = createHairMaterialInteraction(hairMaterialData, hairInteractionSurface);
continueTrace = RTXCR_SampleChiangBsdf(hairMaterialInteraction, viewVectorLocal, rand2, sampleDirection, bsdfPdf, bsdfWeight, lobeType);
```

#### Far-Field:
```cpp
const HairMaterialInteractionBcsdf hairMaterialInteractionBcsdf =
    createHairMaterialInteractionBcsdf(hairMaterialData, g_Global.diffuseReflectionTint, g_Global.diffuseReflectionWeight, g_Global.hairRoughness);
const float h = 2.0f * Rand(rngState) - 1.0f;
const float lobeRandom = Rand(rngState);
float3 bsdfSpecular = float3(0.0f, 0.0f, 0.0f);
float3 bsdfDiffuse = float3(0.0f, 0.0f, 0.0f);
continueTrace = RTXCR_SampleFarFieldBcsdf(hairInteractionSurface, hairMaterialInteractionBcsdf, viewVectorLocal, h, lobeRandom, rand2, sampleDirection, bsdfSpecular, bsdfDiffuse, bsdfPdf);
bsdfWeight = bsdfSpecular + bsdfDiffuse;
```

Then calculate the sample PDF and do integration:

```cpp
if (!continueTrace)
{
  return false;
}

bsdfWeight /= bsdfPdf;
throughput *= bsdfWeight;
```

## Reference
[1] A practical and controllable hair and fur model for production path tracing, MJY Chiang, B Bitterli, C Tappan, B Burley, Computer Graphics Forum 35 (2), 275-283 [Paper Link](https://benedikt-bitterli.me/pchfm/)

[2] THE IMPLEMENTATION OF A HAIR SCATTERING MODEL, Matt Pharr | [Paper Link](https://www.pbrt.org/hair.pdf)

[3] Physically based hair shading in unreal, Brian Karis | [Paper Link](https://blog.selfshadow.com/publications/s2016-shading-course/karis/s2016_pbs_epic_hair.pdf)

[4] Strand based Hair Rendering in Frostbite, Sebastian Tafuri | [Paper Link](https://advances.realtimerendering.com/s2019/hair_presentation_final.pdf)

[5] An efficient and practical near and far field fur reflectance model, LQ Yan, HW Jensen, R Ramamoorthi, ACM Transactions on Graphics (TOG) 36 (4), 1-13 | [Paper Link](https://dl.acm.org/doi/pdf/10.1145/3072959.3073600)

[6] A new hair BCSDF for real-time rendering, Eugene d'Eon | [Paper Link](elliptic_hair.pdf)
