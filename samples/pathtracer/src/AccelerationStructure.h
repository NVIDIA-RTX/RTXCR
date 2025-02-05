/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <memory>
#include <nvrhi/nvrhi.h>

class SampleScene;
struct UIData;

class AccelerationStructure
{
public:
    AccelerationStructure(nvrhi::IDevice* const device, std::shared_ptr<SampleScene> scene, UIData& ui);
    ~AccelerationStructure() = default;

    void CreateAccelerationStructures(nvrhi::CommandListHandle commandList, const uint32_t frameIndex);
    void BuildTLAS(nvrhi::CommandListHandle commandList);

    // Force rebuild the AS, ignore the update AS commands
    inline void SetRebuildAS(const bool rebuildAS)
    {
        m_rebuildAS = rebuildAS;
        m_updateAS = false;
    }

    inline void SetUpdateAS(const bool updateAS)
    {
        // Don't perform updating when we need to rebuild the whole AS
        m_updateAS = !m_rebuildAS ? updateAS : false;
    }

    inline void ClearTLAS() { m_tlas = nullptr; }

    inline const nvrhi::rt::AccelStructHandle GetTLAS() const { return m_tlas; }
    inline const bool IsRebuildAS() const { return m_rebuildAS; }
    inline const bool IsUpdateAS() const { return m_updateAS; }
private:
    nvrhi::IDevice* const m_device;

    std::shared_ptr<SampleScene> m_scene;

    nvrhi::rt::AccelStructHandle m_tlas;
    bool m_rebuildAS;
    bool m_updateAS;

    UIData& m_ui;
};
