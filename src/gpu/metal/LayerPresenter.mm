#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <gpu/metal/LayerPresenter.hpp>

#include <imgui.h>
#include <imgui_impl_metal.h>

#include <stdexcept>

namespace scrap::gpu::metal {

    struct LayerPresenter::Impl {
        CAMetalLayer* layer = nil;
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> queue = nil;
        bool imguiMetalInited = false;
        id<MTLTexture> depthTexture = nil;
        NSUInteger depthW = 0;
        NSUInteger depthH = 0;
    };

    LayerPresenter::LayerPresenter() : pImpl(std::make_unique<Impl>()) {
    }

    LayerPresenter::~LayerPresenter() {
        shutdown();
    }

    void LayerPresenter::init(void* cametalLayerPtr, uint32_t width, uint32_t height,
                              float contentScale) {
        if (cametalLayerPtr == nullptr) {
            throw std::runtime_error("LayerPresenter: null Metal layer");
        }
        CAMetalLayer* layer = (__bridge CAMetalLayer*)cametalLayerPtr;
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (dev == nil) {
            throw std::runtime_error("LayerPresenter: MTLCreateSystemDefaultDevice returned nil");
        }
        pImpl->layer = layer;
        pImpl->device = dev;
        pImpl->queue = [dev newCommandQueue];
        if (pImpl->queue == nil) {
            throw std::runtime_error("LayerPresenter: failed to create command queue");
        }
        layer.device = dev;
        // Match Vulkan swapchain (eB8G8R8A8Srgb): linear shader output is encoded for display. Plain Unorm
        // leaves linear values in the buffer and looks too dark (PBR + ImGui) vs the Vulkan build.
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
        layer.framebufferOnly = YES;
        setDrawableSize(width, height, contentScale);
    }

    void LayerPresenter::setDrawableSize(uint32_t width, uint32_t height, float contentScale) {
        if (pImpl->layer == nil) {
            return;
        }
        const CGFloat scale = contentScale > 0.0f ? static_cast<CGFloat>(contentScale) : 1.0;
        const CGFloat w = static_cast<CGFloat>(width) * scale;
        const CGFloat h = static_cast<CGFloat>(height) * scale;
        if (w > 0 && h > 0) {
            pImpl->layer.drawableSize = CGSizeMake(w, h);
        }
    }

    void LayerPresenter::setDrawableSizePixels(uint32_t widthPx, uint32_t heightPx) {
        if (pImpl->layer == nil || widthPx == 0 || heightPx == 0) {
            return;
        }
        pImpl->layer.drawableSize =
            CGSizeMake(static_cast<CGFloat>(widthPx), static_cast<CGFloat>(heightPx));
    }

    void LayerPresenter::presentClear(float r, float g, float b, float a) {
        if (pImpl->layer == nil || pImpl->queue == nil) {
            return;
        }
        id<CAMetalDrawable> drawable = [pImpl->layer nextDrawable];
        if (drawable == nil) {
            return;
        }

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(r, g, b, a);

        id<MTLCommandBuffer> cmdBuf = [pImpl->queue commandBuffer];
        id<MTLRenderCommandEncoder> enc = [cmdBuf renderCommandEncoderWithDescriptor:pass];
        [enc endEncoding];
        [cmdBuf presentDrawable:drawable];
        [cmdBuf commit];
    }

    void LayerPresenter::initDearImGui() {
        if (pImpl->device == nil) {
            throw std::runtime_error("LayerPresenter::initDearImGui: device not ready");
        }
        if (!ImGui_ImplMetal_Init(pImpl->device)) {
            throw std::runtime_error("ImGui_ImplMetal_Init failed");
        }
        if (!ImGui_ImplMetal_CreateFontsTexture(pImpl->device)) {
            ImGui_ImplMetal_Shutdown();
            throw std::runtime_error("ImGui_ImplMetal_CreateFontsTexture failed");
        }
        pImpl->imguiMetalInited = true;
    }

    void LayerPresenter::ensureDepthTexture(std::uint32_t w, std::uint32_t h) {
        if (w == 0 || h == 0 || pImpl->device == nil) {
            return;
        }
        const NSUInteger uw = static_cast<NSUInteger>(w);
        const NSUInteger uh = static_cast<NSUInteger>(h);
        if (pImpl->depthTexture != nil && pImpl->depthW == uw && pImpl->depthH == uh) {
            return;
        }
        pImpl->depthTexture = nil;
        MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                               width:uw
                                                              height:uh
                                                           mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget;
        desc.storageMode = MTLStorageModePrivate;
        pImpl->depthTexture = [pImpl->device newTextureWithDescriptor:desc];
        pImpl->depthW = uw;
        pImpl->depthH = uh;
    }

    void* LayerPresenter::mtlDevice() const {
        return (__bridge void*)pImpl->device;
    }

    std::uint32_t LayerPresenter::drawablePixelFormat() const {
        if (pImpl->layer == nil) {
            return static_cast<std::uint32_t>(MTLPixelFormatBGRA8Unorm_sRGB);
        }
        return static_cast<std::uint32_t>(pImpl->layer.pixelFormat);
    }

    void LayerPresenter::shutdownDearImGui() {
        if (!pImpl->imguiMetalInited) {
            return;
        }
        ImGui_ImplMetal_Shutdown();
        pImpl->imguiMetalInited = false;
    }

    void LayerPresenter::renderDearImGuiFrame(
        scrap::platform::PlatformView& view, float deltaTime, const std::function<void()>& buildUi,
        const std::function<void(void* mtlRenderEncoder, std::uint32_t drawableWidthPx,
                                 std::uint32_t drawableHeightPx)>& encodeBeforeImGui) {
        if (!pImpl->imguiMetalInited || pImpl->layer == nil || pImpl->queue == nil) {
            return;
        }
        id<CAMetalDrawable> drawable = [pImpl->layer nextDrawable];
        if (drawable == nil) {
            return;
        }

        const NSUInteger dw = drawable.texture.width;
        const NSUInteger dh = drawable.texture.height;
        ensureDepthTexture(static_cast<std::uint32_t>(dw), static_cast<std::uint32_t>(dh));

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0.08, 0.09, 0.14, 1.0);

        if (pImpl->depthTexture != nil) {
            pass.depthAttachment.texture = pImpl->depthTexture;
            pass.depthAttachment.loadAction = MTLLoadActionClear;
            pass.depthAttachment.storeAction = MTLStoreActionStore;
            pass.depthAttachment.clearDepth = 1.0;
        }

        id<MTLCommandBuffer> cmdBuf = [pImpl->queue commandBuffer];
        id<MTLRenderCommandEncoder> enc = [cmdBuf renderCommandEncoderWithDescriptor:pass];

        if (encodeBeforeImGui) {
            encodeBeforeImGui((__bridge void*)enc, static_cast<std::uint32_t>(dw),
                              static_cast<std::uint32_t>(dh));
        }

        ImGuiIO& io = ImGui::GetIO();
        view.prepareImGuiFrame(io, deltaTime);
        ImGui_ImplMetal_NewFrame(pass);
        ImGui::NewFrame();

        if (buildUi) {
            buildUi();
        }

        ImGui::Render();
        ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmdBuf, enc);

        [enc endEncoding];
        [cmdBuf presentDrawable:drawable];
        [cmdBuf commit];
    }

    void LayerPresenter::shutdown() {
        if (!pImpl) {
            return;
        }
        pImpl->depthTexture = nil;
        pImpl->depthW = 0;
        pImpl->depthH = 0;
        pImpl->queue = nil;
        pImpl->device = nil;
        pImpl->layer = nil;
    }

} // namespace scrap::gpu::metal
