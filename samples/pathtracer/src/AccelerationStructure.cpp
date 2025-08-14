/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <nvrhi/utils.h>
#include <donut/app/ApplicationBase.h>

#include "Ui/PathtracerUi.h"
#include "SampleScene.h"
#include "AccelerationStructure.h"
#include "ScopeMarker.h"

AccelerationStructure::AccelerationStructure(nvrhi::IDevice* const device, std::shared_ptr<SampleScene> scene, UIData& ui)
    : m_device(device)
    , m_scene(scene)
    , m_ui(ui)
{
}

void GetMeshBlasDesc(
    const donut::engine::MeshInfo& mesh,
    nvrhi::rt::AccelStructDesc& blasDesc,
    const bool skipTransmissiveMaterials,
    const uint32_t frameIndex,
    const bool isUpdate)
{
    using namespace donut::math;

    blasDesc.isTopLevel = false;
    blasDesc.debugName = mesh.name;

    blasDesc.bottomLevelGeometries.resize(mesh.geometries.size());

    for (uint geometryIndex = 0; geometryIndex < mesh.geometries.size(); ++geometryIndex)
    {
        const auto& geometry = mesh.geometries[geometryIndex];

        nvrhi::rt::GeometryDesc geometryDesc;

        if (mesh.type != MeshType::CurveLinearSweptSpheres)
        {
            auto& triangles = geometryDesc.geometryData.triangles;
            triangles.indexBuffer = mesh.buffers->indexBuffer;
            triangles.indexOffset = (mesh.indexOffset + geometry->indexOffsetInMesh) * sizeof(uint32_t);
            triangles.indexFormat = nvrhi::Format::R32_UINT;
            triangles.indexCount = geometry->numIndices;
            triangles.vertexBuffer = mesh.buffers->vertexBuffer;
            triangles.vertexOffset = (mesh.vertexOffset + geometry->vertexOffsetInMesh) * sizeof(float3) +
                mesh.buffers->getVertexBufferRange(donut::engine::VertexAttribute::Position).byteOffset;
            triangles.vertexFormat = nvrhi::Format::RGB32_FLOAT;
            triangles.vertexStride = sizeof(float3);
            triangles.vertexCount = geometry->numVertices;
            geometryDesc.geometryType = nvrhi::rt::GeometryType::Triangles;
        }
        else
        {
            auto& lss = geometryDesc.geometryData.lss;
            // TODO: Add support for for implicit successive indexing format
            // Until then, the index buffer related fields can stay at default values
            //lss.indexBuffer = mesh.buffers->indexBuffer;
            //lss.indexOffset = (mesh.indexOffset + geometry->indexOffsetInMesh) * sizeof(uint32_t);
            //lss.indexFormat = geometry->numIndices > 0 ? nvrhi::Format::R32_UINT : nvrhi::Format::UNKNOWN;
            //lss.indexStride = sizeof(uint32_t);
            //lss.indexCount = geometry->numIndices;
            lss.vertexBuffer = mesh.buffers->vertexBuffer;
            lss.vertexPositionOffset = (mesh.vertexOffset + geometry->vertexOffsetInMesh) * sizeof(float3) +
                mesh.buffers->getVertexBufferRange(donut::engine::VertexAttribute::Position).byteOffset;
            lss.vertexPositionFormat = nvrhi::Format::RGB32_FLOAT;
            lss.vertexPositionStride = sizeof(float3);
            lss.vertexRadiusOffset = (mesh.vertexOffset + geometry->vertexOffsetInMesh) * sizeof(float) +
                mesh.buffers->getVertexBufferRange(donut::engine::VertexAttribute::CurveRadius).byteOffset;
            lss.vertexRadiusFormat = nvrhi::Format::R32_FLOAT;
            lss.vertexRadiusStride = sizeof(float);
            lss.primitiveCount = geometry->numVertices / 2;
            lss.vertexCount = geometry->numVertices;
            lss.primitiveFormat = nvrhi::rt::GeometryLssPrimitiveFormat::List;
            lss.endcapMode = nvrhi::rt::GeometryLssEndcapMode::None;
            geometryDesc.geometryType = nvrhi::rt::GeometryType::Lss;
        }

        geometryDesc.flags = (geometry->material->domain != donut::engine::MaterialDomain::Opaque)
            ? nvrhi::rt::GeometryFlags::None
            : nvrhi::rt::GeometryFlags::Opaque;

        blasDesc.bottomLevelGeometries[geometryIndex] = geometryDesc;
    }

    if (mesh.isMorphTargetAnimationMesh)
    {
        if (isUpdate)
        {
            blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::AllowUpdate
                | nvrhi::rt::AccelStructBuildFlags::PreferFastTrace
                | nvrhi::rt::AccelStructBuildFlags::PerformUpdate;
        }
        else
        {
            blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::AllowUpdate
                | nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;
        }
    }
    else if (mesh.skinPrototype)
    {
        blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace;
    }
    else
    {
        blasDesc.buildFlags = nvrhi::rt::AccelStructBuildFlags::PreferFastTrace | nvrhi::rt::AccelStructBuildFlags::AllowCompaction;
    }
}

void AccelerationStructure::CreateAccelerationStructures(nvrhi::CommandListHandle commandList, const uint32_t frameIndex)
{
    assert(!m_rebuildAS || !m_updateAS);

    ScopedMarker scopedMarker(commandList, "BLAS Updates");

    for (const auto& mesh : m_scene->GetNativeScene()->GetSceneGraph()->GetMeshes())
    {
        if ((m_updateAS && !mesh->isMorphTargetAnimationMesh) ||
            mesh->buffers->hasAttribute(donut::engine::VertexAttribute::JointWeights))
        {
            // skip when:
            // 1. The skinning prototypes
            // 2. Static Mesh request update
            continue;
        }

        nvrhi::rt::AccelStructDesc blasDesc;
        GetMeshBlasDesc(*mesh, blasDesc, !m_ui.enableTransmission, frameIndex, m_updateAS);

        if (m_rebuildAS || !mesh->isMorphTargetAnimationMesh || !mesh->accelStruct)
        {
            const nvrhi::rt::AccelStructHandle accelStruct = m_device->createAccelStruct(blasDesc);
            if (!mesh->skinPrototype)
            {
                nvrhi::utils::BuildBottomLevelAccelStruct(commandList, accelStruct, blasDesc);
            }
            mesh->accelStruct = accelStruct;
        }
        else
        {
            nvrhi::utils::BuildBottomLevelAccelStruct(commandList, mesh->accelStruct, blasDesc);
        }
    }

    size_t tlasInstanceCount = m_scene->GetNativeScene()->GetSceneGraph()->GetMeshInstances().size();

    if (!m_tlas || tlasInstanceCount > m_tlas->getDesc().topLevelMaxInstances)
    {
        nvrhi::rt::AccelStructDesc tlasDesc;
        tlasDesc.isTopLevel = true;
        tlasDesc.topLevelMaxInstances = tlasInstanceCount;
        tlasDesc.debugName = "Top Level Acceleration Struct";
        m_tlas = m_device->createAccelStruct(tlasDesc);
    }
}

void AccelerationStructure::BuildTLAS(nvrhi::CommandListHandle commandList)
{
    {
        ScopedMarker scopedMarker(commandList, "Skinned BLAS Updates");

        // Transition all the buffers to their necessary states before building the BLAS'es to allow BLAS batching
        for (const auto& skinnedInstance : m_scene->GetNativeScene()->GetSceneGraph()->GetSkinnedMeshInstances())
        {
            commandList->setAccelStructState(skinnedInstance->GetMesh()->accelStruct, nvrhi::ResourceStates::AccelStructWrite);
            commandList->setBufferState(skinnedInstance->GetMesh()->buffers->vertexBuffer, nvrhi::ResourceStates::AccelStructBuildInput);
        }
        commandList->commitBarriers();

        // Build BLAS instances
        for (const auto& skinnedInstance : m_scene->GetNativeScene()->GetSceneGraph()->GetSkinnedMeshInstances())
        {
            nvrhi::rt::AccelStructDesc blasDesc;
            blasDesc.debugName = "Bottom Level Acceleration Struct";
            GetMeshBlasDesc(*skinnedInstance->GetMesh(), blasDesc, !m_ui.enableTransmission, 0, false);

            nvrhi::utils::BuildBottomLevelAccelStruct(commandList, skinnedInstance->GetMesh()->accelStruct, blasDesc);
        }
    }

    std::vector<nvrhi::rt::InstanceDesc> instances;
    for (const auto& instance : m_scene->GetNativeScene()->GetSceneGraph()->GetMeshInstances())
    {
        nvrhi::rt::InstanceDesc instanceDesc;
        instanceDesc.bottomLevelAS = instance->GetMesh()->accelStruct;
        assert(instanceDesc.bottomLevelAS);
        const float3& emissiveColor = instance->GetMesh()->geometries[0]->material->emissiveColor;
        const float roughness = instance->GetMesh()->geometries[0]->material->roughness;
        if (roughness == 0)
        {
            instanceDesc.instanceMask = 4;
        }
        else if (!m_ui.showEmissiveSurfaces && (emissiveColor.x > 0.0f || emissiveColor.y > 0.0f || emissiveColor.z > 0.0f))
        {
            instanceDesc.instanceMask = 2;
        }
        else
        {
            instanceDesc.instanceMask = 1;
        }
        instanceDesc.instanceID = instance->GetInstanceIndex();

        if (instance->GetMesh()->type == MeshType::CurveDisjointOrthogonalTriangleStrips)
        {
            instanceDesc.setFlags(nvrhi::rt::InstanceFlags::TriangleCullDisable);
        }

        auto node = instance->GetNode();
        assert(node);
        dm::affineToColumnMajor(node->GetLocalToWorldTransformFloat(), instanceDesc.transform);

        instances.push_back(instanceDesc);
    }

    // Compact acceleration structures that are tagged for compaction and have finished executing the original build
    commandList->compactBottomLevelAccelStructs();

    ScopedMarker scopedMarker(commandList, "TLAS Update");
    commandList->buildTopLevelAccelStruct(m_tlas, instances.data(), instances.size());
}
