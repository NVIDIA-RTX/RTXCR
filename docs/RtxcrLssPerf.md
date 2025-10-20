# LSS (Linear Swept Spheres) — Performance Analysis

**Summary**
Linear Swept Spheres (LSS) provide an efficient, compact representation for curve primitives. LSS reduces BVH update cost and trace cost while dramatically cutting memory compared to DOTS and traditional triangle polytube representations. On 4th-generation RT Cores, hardware ray–LSS intersection further accelerates ray traversal, yielding higher frame rates and lower working set.

---

## Test Configuration

- Scene: hair system only (13.7M primitives)
- Renderer: path tracing only (DLSS-FG, DLSS-RR, denoising/upsampling, and post-processing passes disabled)
- GPU: RTX 5090
- API: DirectX 12
- Resolution: 3840×2160 (4K)
- Measurement: 300 warmup frames + 500 measured frames per representation. Reported values are mean ms/frame and FPS.
- Video memory sampled each test run.

---

## Results

| Representation | Video Memory | Frame Time (ms) | FPS |
|----------------|--------------:|----------------:|----:|
| **LSS**        | 2450 MB       | 4.94            | 202.5 |
| **DOTS**       | 3388 MB       | 6.37            | 157.0 |
| **PolyTube**   | 3971 MB       | 7.13            | 140.2 |

---

## Analysis

Compared to **DOTS**, LSS:
- Reduces memory usage by **27.7%**
- Improves performance by **22.5%**

Compared to **traditional PolyTube**, LSS:
- Reduces memory usage by **38.3%**
- Improves performance by **30.7%**

---

## Conclusion

For hair or curve-heavy workloads, LSS offers substantial memory savings and performance gains on modern RTX hardware.
It is recommended when memory footprint and BVH update/trace performance are primary constraints.
