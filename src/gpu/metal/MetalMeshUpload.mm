#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <gpu/metal/MetalMeshGpu.hpp>
#include <gpu/metal/MetalMeshUpload.hpp>
#include <gpu/metal/VertexLayout.hpp>

#include <app/modeling/Mesh.hpp>

#include <cstring>

namespace scrap::gpu::metal {

    bool uploadMeshToMetal(void* mtlDevice, const scrap::modeling::Mesh& src, MetalMeshGpu& out) {
        id<MTLDevice> device = (__bridge id<MTLDevice>)mtlDevice;
        releaseMetalMeshGpu(out);
        if (!device || !src.isValid() || src.getVertexCount() == 0 || src.getIndexCount() == 0) {
            return false;
        }

        const auto& verts = src.getVertices();
        const auto& idx = src.getIndices();
        const NSUInteger vbSize = verts.size() * sizeof(Vertex);
        const NSUInteger ibSize = idx.size() * sizeof(uint32_t);

        id<MTLBuffer> vb = [device newBufferWithBytes:verts.data()
                                               length:vbSize
                                              options:MTLResourceStorageModeShared];
        id<MTLBuffer> ib = [device newBufferWithBytes:idx.data()
                                               length:ibSize
                                              options:MTLResourceStorageModeShared];
        if (vb == nil || ib == nil) {
            return false;
        }

        out.vertexBuffer = (__bridge_retained void*)vb;
        out.indexBuffer = (__bridge_retained void*)ib;
        out.indexCount = static_cast<uint32_t>(idx.size());
        return true;
    }

    bool syncMetalMeshWithCpuMesh(void* mtlDevicePtr, const scrap::modeling::Mesh& src,
                                  MetalMeshGpu& inOut) {
        id<MTLDevice> device = (__bridge id<MTLDevice>)mtlDevicePtr;
        if (!device || !src.isValid() || src.getVertexCount() == 0 || src.getIndexCount() == 0) {
            return false;
        }
        const auto& verts = src.getVertices();
        const auto& idx = src.getIndices();
        const NSUInteger vbNeed = verts.size() * sizeof(Vertex);
        const NSUInteger ibNeed = idx.size() * sizeof(uint32_t);

        id<MTLBuffer> vb = inOut.vertexBuffer ? (__bridge id<MTLBuffer>)inOut.vertexBuffer : nil;
        id<MTLBuffer> ib = inOut.indexBuffer ? (__bridge id<MTLBuffer>)inOut.indexBuffer : nil;
        if (vb == nil || ib == nil || vb.length != vbNeed || ib.length != ibNeed ||
            inOut.indexCount != static_cast<uint32_t>(idx.size())) {
            return uploadMeshToMetal(mtlDevicePtr, src, inOut);
        }
        std::memcpy([vb contents], verts.data(), vbNeed);
        std::memcpy([ib contents], idx.data(), ibNeed);
        return true;
    }

    void releaseMetalMeshGpu(MetalMeshGpu& mesh) {
        if (mesh.vertexBuffer) {
            CFBridgingRelease(mesh.vertexBuffer);
            mesh.vertexBuffer = nullptr;
        }
        if (mesh.indexBuffer) {
            CFBridgingRelease(mesh.indexBuffer);
            mesh.indexBuffer = nullptr;
        }
        mesh.indexCount = 0;
    }

} // namespace scrap::gpu::metal
