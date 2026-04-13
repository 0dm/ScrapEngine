#import <Metal/Metal.h>

#include <gpu/metal/MetalUnlitRenderer.hpp>
#include <gpu/metal/VertexLayout.hpp>

#include <cstring>
#include <string>

namespace scrap::gpu::metal {

    namespace {

        constexpr const char* kUnlitMsl = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
  float3 position [[attribute(0)]];
  float3 normal [[attribute(1)]];
  float2 texCoord [[attribute(2)]];
  float3 color [[attribute(3)]];
  float4 tangent [[attribute(4)]];
};

struct VsOut {
  float4 position [[position]];
  float3 color;
};

vertex VsOut unlit_vertex(VertexIn in [[stage_in]], constant float4x4& mvp [[buffer(1)]]) {
  VsOut o;
  o.position = mvp * float4(in.position, 1.0);
  o.color = in.color;
  return o;
}

fragment float4 unlit_fragment(VsOut in [[stage_in]]) {
  return float4(in.color, 1.0);
}
)msl";

        MTLVertexDescriptor* makeVertexDesc() {
            MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
            using V = Vertex;
            NSUInteger stride = sizeof(V);
            vd.attributes[0].format = MTLVertexFormatFloat3;
            vd.attributes[0].offset = offsetof(V, position);
            vd.attributes[0].bufferIndex = 0;
            vd.attributes[1].format = MTLVertexFormatFloat3;
            vd.attributes[1].offset = offsetof(V, normal);
            vd.attributes[1].bufferIndex = 0;
            vd.attributes[2].format = MTLVertexFormatFloat2;
            vd.attributes[2].offset = offsetof(V, texCoords);
            vd.attributes[2].bufferIndex = 0;
            vd.attributes[3].format = MTLVertexFormatFloat3;
            vd.attributes[3].offset = offsetof(V, color);
            vd.attributes[3].bufferIndex = 0;
            vd.attributes[4].format = MTLVertexFormatFloat4;
            vd.attributes[4].offset = offsetof(V, tangent);
            vd.attributes[4].bufferIndex = 0;
            vd.layouts[0].stride = stride;
            vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
            vd.layouts[0].stepRate = 1;
            return vd;
        }

    } // namespace

    struct MetalUnlitRenderer::Impl {
        id<MTLDevice> device = nil;
        id<MTLRenderPipelineState> pipeline = nil;
        id<MTLDepthStencilState> depthState = nil;
    };

    MetalUnlitRenderer::MetalUnlitRenderer() : pImpl(std::make_unique<Impl>()) {
    }

    MetalUnlitRenderer::~MetalUnlitRenderer() = default;

    void MetalUnlitRenderer::init(void* mtlDevice, std::uint32_t pixelFormatRaw) {
        pImpl->device = (__bridge id<MTLDevice>)mtlDevice;
        pImpl->pipeline = nil;
        pImpl->depthState = nil;
        if (pImpl->device == nil) {
            return;
        }

        NSError* err = nil;
        NSString* src = [NSString stringWithUTF8String:kUnlitMsl];
        id<MTLLibrary> lib = [pImpl->device newLibraryWithSource:src options:nil error:&err];
        if (lib == nil) {
            return;
        }

        id<MTLFunction> vs = [lib newFunctionWithName:@"unlit_vertex"];
        id<MTLFunction> fs = [lib newFunctionWithName:@"unlit_fragment"];
        if (vs == nil || fs == nil) {
            return;
        }

        MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
        pd.vertexFunction = vs;
        pd.fragmentFunction = fs;
        pd.vertexDescriptor = makeVertexDesc();
        pd.colorAttachments[0].pixelFormat = static_cast<MTLPixelFormat>(pixelFormatRaw);
        pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        pImpl->pipeline = [pImpl->device newRenderPipelineStateWithDescriptor:pd error:&err];
        if (pImpl->pipeline == nil) {
            return;
        }

        MTLDepthStencilDescriptor* ds = [[MTLDepthStencilDescriptor alloc] init];
        ds.depthCompareFunction = MTLCompareFunctionLess;
        ds.depthWriteEnabled = YES;
        pImpl->depthState = [pImpl->device newDepthStencilStateWithDescriptor:ds];
    }

    void MetalUnlitRenderer::draw(void* mtlRenderEncoder, float viewportWidth, float viewportHeight,
                                  const glm::mat4& viewProj,
                                  const std::vector<MetalMeshGpu>& meshes,
                                  const std::vector<glm::mat4>& modelMatrices) {
        if (!pImpl || !pImpl->pipeline || !pImpl->depthState || mtlRenderEncoder == nullptr) {
            return;
        }
        id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)mtlRenderEncoder;
        if (viewportWidth <= 0.0f || viewportHeight <= 0.0f) {
            return;
        }

        MTLViewport vp{};
        vp.originX = 0;
        vp.originY = 0;
        vp.width = viewportWidth;
        vp.height = viewportHeight;
        vp.znear = 0.0;
        vp.zfar = 1.0;
        [enc setViewport:vp];

        [enc setRenderPipelineState:pImpl->pipeline];
        [enc setDepthStencilState:pImpl->depthState];
        [enc setFrontFacingWinding:MTLWindingCounterClockwise];
        [enc setCullMode:MTLCullModeBack];

        const size_t n = std::min(meshes.size(), modelMatrices.size());
        for (size_t i = 0; i < n; ++i) {
            const MetalMeshGpu& m = meshes[i];
            if (m.indexCount == 0 || m.vertexBuffer == nullptr || m.indexBuffer == nullptr) {
                continue;
            }
            id<MTLBuffer> vb = (__bridge id<MTLBuffer>)m.vertexBuffer;
            id<MTLBuffer> ib = (__bridge id<MTLBuffer>)m.indexBuffer;
            glm::mat4 mvp = viewProj * modelMatrices[i];
            [enc setVertexBuffer:vb offset:0 atIndex:0];
            [enc setVertexBytes:&mvp length:sizeof(mvp) atIndex:1];
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:m.indexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:ib
                     indexBufferOffset:0];
        }
    }

} // namespace scrap::gpu::metal
