#include <app/platform/PlatformWindow.hpp>

#include <algorithm>
#include <cfloat>
#include <optional>
#include <stdexcept>
#include <utility>

#include <TargetConditionals.h>

#if TARGET_OS_OSX

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/CAMetalLayer.h>
#import <mach-o/dyld.h>

#include <filesystem>
#include <unistd.h>

namespace {

    constexpr unsigned short kMacKeyA = 0;
    constexpr unsigned short kMacKeyS = 1;
    constexpr unsigned short kMacKeyD = 2;
    constexpr unsigned short kMacKeyF = 3;
    constexpr unsigned short kMacKeyE = 14;
    constexpr unsigned short kMacKeyR = 15;
    constexpr unsigned short kMacKeyW = 13;
    constexpr unsigned short kMacKeyO = 31;
    constexpr unsigned short kMacKeyP = 35;
    constexpr unsigned short kMacKeyN = 45;
    constexpr unsigned short kMacKeySpace = 49;
    constexpr unsigned short kMacKeyGraveAccent = 50;
    constexpr unsigned short kMacKeyDelete = 51;
    constexpr unsigned short kMacKeyEscape = 53;
    constexpr unsigned short kMacKeyLeftShift = 56;
    constexpr unsigned short kMacKeyLeftControl = 59;
    constexpr unsigned short kMacKeyRightShift = 60;
    constexpr unsigned short kMacKeyRightControl = 62;
    constexpr unsigned short kMacKeyForwardDelete = 117;

    std::optional<scrap::platform::Key> mapMacKey(unsigned short keyCode) {
        switch (keyCode) {
        case kMacKeyEscape:
            return scrap::platform::Key::Escape;
        case kMacKeyGraveAccent:
            return scrap::platform::Key::GraveAccent;
        case kMacKeyDelete:
        case kMacKeyForwardDelete:
            return scrap::platform::Key::DeleteKey;
        case kMacKeyF:
            return scrap::platform::Key::F;
        case kMacKeyE:
            return scrap::platform::Key::E;
        case kMacKeyR:
            return scrap::platform::Key::R;
        case kMacKeyO:
            return scrap::platform::Key::O;
        case kMacKeyN:
            return scrap::platform::Key::N;
        case kMacKeyP:
            return scrap::platform::Key::P;
        case kMacKeySpace:
            return scrap::platform::Key::Space;
        case kMacKeyLeftShift:
            return scrap::platform::Key::LeftShift;
        case kMacKeyRightShift:
            return scrap::platform::Key::RightShift;
        case kMacKeyLeftControl:
            return scrap::platform::Key::LeftControl;
        case kMacKeyRightControl:
            return scrap::platform::Key::RightControl;
        case kMacKeyW:
            return scrap::platform::Key::W;
        case kMacKeyA:
            return scrap::platform::Key::A;
        case kMacKeyS:
            return scrap::platform::Key::S;
        case kMacKeyD:
            return scrap::platform::Key::D;
        default:
            return std::nullopt;
        }
    }

    std::optional<scrap::platform::MouseButton> mapMouseButton(NSInteger buttonNumber) {
        switch (buttonNumber) {
        case 0:
            return scrap::platform::MouseButton::Left;
        case 1:
            return scrap::platform::MouseButton::Right;
        case 2:
            return scrap::platform::MouseButton::Middle;
        default:
            return std::nullopt;
        }
    }

    ImGuiKey mapImGuiKey(scrap::platform::Key key) {
        switch (key) {
        case scrap::platform::Key::Escape:
            return ImGuiKey_Escape;
        case scrap::platform::Key::GraveAccent:
            return ImGuiKey_GraveAccent;
        case scrap::platform::Key::DeleteKey:
            return ImGuiKey_Delete;
        case scrap::platform::Key::F:
            return ImGuiKey_F;
        case scrap::platform::Key::E:
            return ImGuiKey_E;
        case scrap::platform::Key::R:
            return ImGuiKey_R;
        case scrap::platform::Key::O:
            return ImGuiKey_O;
        case scrap::platform::Key::N:
            return ImGuiKey_N;
        case scrap::platform::Key::P:
            return ImGuiKey_P;
        case scrap::platform::Key::Space:
            return ImGuiKey_Space;
        case scrap::platform::Key::LeftShift:
            return ImGuiKey_LeftShift;
        case scrap::platform::Key::RightShift:
            return ImGuiKey_RightShift;
        case scrap::platform::Key::LeftControl:
            return ImGuiKey_LeftCtrl;
        case scrap::platform::Key::RightControl:
            return ImGuiKey_RightCtrl;
        case scrap::platform::Key::W:
            return ImGuiKey_W;
        case scrap::platform::Key::A:
            return ImGuiKey_A;
        case scrap::platform::Key::S:
            return ImGuiKey_S;
        case scrap::platform::Key::D:
            return ImGuiKey_D;
        default:
            return ImGuiKey_None;
        }
    }

    void resetTransientInput(scrap::platform::InputState& inputState) {
        inputState.keyPressed.fill(false);
        inputState.keyReleased.fill(false);
        inputState.pointer.deltaX = 0.0f;
        inputState.pointer.deltaY = 0.0f;
        inputState.pointer.scrollX = 0.0f;
        inputState.pointer.scrollY = 0.0f;
        for (auto& button : inputState.pointer.buttons) {
            button.pressed = false;
            button.released = false;
        }
        inputState.droppedPaths.clear();
        inputState.framebufferResized = false;
        inputState.toggleCursorCaptureRequested = false;
        inputState.closeRequested = false;
    }

} // namespace

namespace scrap::platform {

    void setWorkingDirectoryToExecutableFolder() {
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        if (size == 0) {
            return;
        }
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            return;
        }
        std::error_code ec;
        const std::filesystem::path exeDir =
            std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str())).parent_path();
        if (!exeDir.empty()) {
            std::filesystem::current_path(exeDir, ec);
        }
    }

    struct PlatformWindowImpl {
        NSWindow* window = nil;
        NSView* contentView = nil;
        id delegate = nil;
        InputState inputState{};
        FramebufferExtent framebufferExtent{};
        float contentScaleFactor = 1.0f;
        bool shouldCloseWindow = false;
        bool cursorCaptured = false;
        bool cursorHidden = false;
        bool hasPointerLocation = false;
        NSPoint lastPointerLocation = NSZeroPoint;
        std::string pendingText;
        bool pendingBackspace = false;

        void updateModifierState(NSEventModifierFlags flags) {
            inputState.shiftDown = (flags & NSEventModifierFlagShift) != 0;
            inputState.controlDown = (flags & NSEventModifierFlagControl) != 0;
            inputState.altDown = (flags & NSEventModifierFlagOption) != 0;
            inputState.superDown = (flags & NSEventModifierFlagCommand) != 0;
            inputState.keys[keyIndex(Key::LeftShift)] = inputState.shiftDown;
            inputState.keys[keyIndex(Key::RightShift)] = inputState.shiftDown;
            inputState.keys[keyIndex(Key::LeftControl)] = inputState.controlDown;
            inputState.keys[keyIndex(Key::RightControl)] = inputState.controlDown;
        }

        void updateDrawableSize(NSView* view) {
            auto* layer = (CAMetalLayer*)view.layer;
            NSScreen* screen = view.window.screen != nil ? view.window.screen : NSScreen.mainScreen;
            contentScaleFactor =
                static_cast<float>(screen != nil ? screen.backingScaleFactor : 1.0);
            layer.contentsScale = contentScaleFactor;
            const NSSize viewSize = view.bounds.size;
            layer.drawableSize = CGSizeMake(viewSize.width * contentScaleFactor,
                                            viewSize.height * contentScaleFactor);
            framebufferExtent = {static_cast<uint32_t>(std::max(0.0, layer.drawableSize.width)),
                                 static_cast<uint32_t>(std::max(0.0, layer.drawableSize.height))};
            inputState.framebufferResized = true;
        }

        void setMouseButton(MouseButton button, bool pressed) {
            auto& state = inputState.pointer.buttons[static_cast<std::size_t>(button)];
            if (pressed != state.down) {
                if (pressed) {
                    state.pressed = true;
                } else {
                    state.released = true;
                }
            }
            state.down = pressed;
        }

        void handlePointerEvent(NSEvent* event, bool useRelativeDeltas) {
            updateModifierState(event.modifierFlags);
            const NSPoint pointInView = [contentView convertPoint:event.locationInWindow
                                                         fromView:nil];
            inputState.pointer.x = static_cast<float>(pointInView.x);
            inputState.pointer.y = static_cast<float>(pointInView.y);

            // NSEvent deltas use window coordinates (matches our flipped view: +Y = down).
            // ScrapEngineApp / EditorCamera negate deltaY again for pitch ("mouse down" -> look down),
            // same as the non-captured path (deltaY = y - lastY). Do not negate here or pitch inverts.
            if (cursorCaptured && useRelativeDeltas) {
                inputState.pointer.deltaX += static_cast<float>(event.deltaX);
                inputState.pointer.deltaY += static_cast<float>(event.deltaY);
                return;
            }

            if (!hasPointerLocation) {
                lastPointerLocation = pointInView;
                hasPointerLocation = true;
            }

            inputState.pointer.deltaX += static_cast<float>(pointInView.x - lastPointerLocation.x);
            inputState.pointer.deltaY += static_cast<float>(pointInView.y - lastPointerLocation.y);
            lastPointerLocation = pointInView;
        }

        // GLFW-style apps get consistent client-space coords whenever the window is focused; AppKit
        // can leave our cached pointer stale until the first move event after key/focus changes.
        void syncPointerPositionFromSystemCursor() {
            if (contentView == nil || contentView.window == nil) {
                return;
            }
            const NSPoint screenPt = [NSEvent mouseLocation];
            const NSPoint winPt = [contentView.window convertPointFromScreen:screenPt];
            const NSPoint local = [contentView convertPoint:winPt fromView:nil];
            inputState.pointer.x = static_cast<float>(local.x);
            inputState.pointer.y = static_cast<float>(local.y);
            lastPointerLocation = local;
            hasPointerLocation = true;
        }
    };

} // namespace scrap::platform

@interface ScrapPlatformWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) scrap::platform::PlatformWindowImpl* impl;
@end

@implementation ScrapPlatformWindowDelegate

- (BOOL)windowShouldClose:(id)sender {
    (void)sender;
    self.impl->shouldCloseWindow = true;
    self.impl->inputState.closeRequested = true;
    return YES;
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
    (void)notification;
    NSWindow* w = self.impl->window;
    if (w == nil || self.impl->contentView == nil) {
        return;
    }
    [w makeFirstResponder:self.impl->contentView];
    self.impl->syncPointerPositionFromSystemCursor();
}

- (void)windowDidDeminiaturize:(NSNotification*)notification {
    [self windowDidBecomeKey:notification];
}

@end

@interface ScrapPlatformContentView : NSView <NSDraggingDestination>
@property(nonatomic, assign) scrap::platform::PlatformWindowImpl* impl;
@end

@implementation ScrapPlatformContentView {
    NSTrackingArea* _trackingArea;
}

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    (void)event;
    return YES;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_trackingArea != nil) {
        [self removeTrackingArea:_trackingArea];
    }
    _trackingArea = [[NSTrackingArea alloc]
        initWithRect:self.bounds
             options:NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect |
                     NSTrackingEnabledDuringMouseDrag
               owner:self
            userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    if (self.window != nil) {
        [self.window setAcceptsMouseMovedEvents:YES];
        [self.window makeFirstResponder:self];
        self.impl->updateDrawableSize(self);
    }
}

- (void)layout {
    [super layout];
    self.impl->updateDrawableSize(self);
}

- (void)mouseDown:(NSEvent*)event {
    if (self.window != nil) {
        [self.window makeFirstResponder:self];
    }
    self.impl->handlePointerEvent(event, false);
    self.impl->setMouseButton(scrap::platform::MouseButton::Left, true);
}

- (void)mouseUp:(NSEvent*)event {
    self.impl->handlePointerEvent(event, false);
    self.impl->setMouseButton(scrap::platform::MouseButton::Left, false);
}

- (void)rightMouseDown:(NSEvent*)event {
    if (self.window != nil) {
        [self.window makeFirstResponder:self];
    }
    self.impl->handlePointerEvent(event, false);
    self.impl->setMouseButton(scrap::platform::MouseButton::Right, true);
}

- (void)rightMouseUp:(NSEvent*)event {
    self.impl->handlePointerEvent(event, false);
    self.impl->setMouseButton(scrap::platform::MouseButton::Right, false);
}

- (void)otherMouseDown:(NSEvent*)event {
    if (self.window != nil) {
        [self.window makeFirstResponder:self];
    }
    self.impl->handlePointerEvent(event, false);
    if (auto button = mapMouseButton(event.buttonNumber); button.has_value()) {
        self.impl->setMouseButton(*button, true);
    }
}

- (void)otherMouseUp:(NSEvent*)event {
    self.impl->handlePointerEvent(event, false);
    if (auto button = mapMouseButton(event.buttonNumber); button.has_value()) {
        self.impl->setMouseButton(*button, false);
    }
}

- (void)mouseMoved:(NSEvent*)event {
    self.impl->handlePointerEvent(event, true);
}

- (void)mouseDragged:(NSEvent*)event {
    self.impl->handlePointerEvent(event, true);
}

- (void)rightMouseDragged:(NSEvent*)event {
    self.impl->handlePointerEvent(event, true);
}

- (void)otherMouseDragged:(NSEvent*)event {
    self.impl->handlePointerEvent(event, true);
}

- (void)scrollWheel:(NSEvent*)event {
    self.impl->handlePointerEvent(event, false);
    self.impl->inputState.pointer.scrollX += static_cast<float>(event.scrollingDeltaX);
    self.impl->inputState.pointer.scrollY += static_cast<float>(event.scrollingDeltaY);
}

- (void)keyDown:(NSEvent*)event {
    self.impl->updateModifierState(event.modifierFlags);
    if (auto key = mapMacKey(event.keyCode); key.has_value()) {
        if (!self.impl->inputState.keys[scrap::platform::keyIndex(*key)]) {
            self.impl->inputState.keyPressed[scrap::platform::keyIndex(*key)] = true;
        }
        self.impl->inputState.keys[scrap::platform::keyIndex(*key)] = true;
    }

    NSString* characters = event.charactersIgnoringModifiers;
    if (event.keyCode == kMacKeyDelete || event.keyCode == kMacKeyForwardDelete) {
        self.impl->pendingBackspace = true;
    } else if (characters.length > 0 && !event.isARepeat &&
               (event.modifierFlags & NSEventModifierFlagCommand) == 0 &&
               (event.modifierFlags & NSEventModifierFlagControl) == 0) {
        self.impl->pendingText += std::string(characters.UTF8String);
    }
}

- (void)keyUp:(NSEvent*)event {
    self.impl->updateModifierState(event.modifierFlags);
    if (auto key = mapMacKey(event.keyCode); key.has_value()) {
        if (self.impl->inputState.keys[scrap::platform::keyIndex(*key)]) {
            self.impl->inputState.keyReleased[scrap::platform::keyIndex(*key)] = true;
        }
        self.impl->inputState.keys[scrap::platform::keyIndex(*key)] = false;
    }
}

- (void)flagsChanged:(NSEvent*)event {
    const bool newShiftDown = (event.modifierFlags & NSEventModifierFlagShift) != 0;
    const bool newControlDown = (event.modifierFlags & NSEventModifierFlagControl) != 0;

    const bool previousLeftShift =
        self.impl->inputState.keys[scrap::platform::keyIndex(scrap::platform::Key::LeftShift)];
    const bool previousRightShift =
        self.impl->inputState.keys[scrap::platform::keyIndex(scrap::platform::Key::RightShift)];
    const bool previousLeftControl =
        self.impl->inputState.keys[scrap::platform::keyIndex(scrap::platform::Key::LeftControl)];
    const bool previousRightControl =
        self.impl->inputState.keys[scrap::platform::keyIndex(scrap::platform::Key::RightControl)];

    self.impl->updateModifierState(event.modifierFlags);

    if (!previousLeftShift && newShiftDown) {
        self.impl->inputState
            .keyPressed[scrap::platform::keyIndex(scrap::platform::Key::LeftShift)] = true;
        self.impl->inputState
            .keyPressed[scrap::platform::keyIndex(scrap::platform::Key::RightShift)] = true;
    } else if ((previousLeftShift || previousRightShift) && !newShiftDown) {
        self.impl->inputState
            .keyReleased[scrap::platform::keyIndex(scrap::platform::Key::LeftShift)] = true;
        self.impl->inputState
            .keyReleased[scrap::platform::keyIndex(scrap::platform::Key::RightShift)] = true;
    }

    if (!previousLeftControl && newControlDown) {
        self.impl->inputState
            .keyPressed[scrap::platform::keyIndex(scrap::platform::Key::LeftControl)] = true;
        self.impl->inputState
            .keyPressed[scrap::platform::keyIndex(scrap::platform::Key::RightControl)] = true;
    } else if ((previousLeftControl || previousRightControl) && !newControlDown) {
        self.impl->inputState
            .keyReleased[scrap::platform::keyIndex(scrap::platform::Key::LeftControl)] = true;
        self.impl->inputState
            .keyReleased[scrap::platform::keyIndex(scrap::platform::Key::RightControl)] = true;
    }
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    (void)sender;
    return NSDragOperationCopy;
}

- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender {
    (void)sender;
    return YES;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSPasteboard* pasteboard = sender.draggingPasteboard;
    NSArray<NSURL*>* urls =
        [pasteboard readObjectsForClasses:@[ NSURL.class ]
                                  options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    for (NSURL* url in urls) {
        if (url.fileURL) {
            self.impl->inputState.droppedPaths.push_back(std::string(url.path.UTF8String));
        }
    }
    return YES;
}

@end

namespace scrap::platform {

    PlatformWindow::PlatformWindow(std::unique_ptr<PlatformWindowImpl> impl)
        : impl(std::move(impl)) {
    }
    PlatformWindow::~PlatformWindow() = default;

    std::unique_ptr<PlatformWindow> createPlatformWindow(
        const PlatformWindowCreateInfo& createInfo) {
        setWorkingDirectoryToExecutableFolder();
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];

        auto impl = std::make_unique<PlatformWindowImpl>();

        const NSRect frame = NSMakeRect(0.0, 0.0, static_cast<CGFloat>(createInfo.width),
                                        static_cast<CGFloat>(createInfo.height));
        NSWindowStyleMask styleMask =
            NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
        if (createInfo.resizable) {
            styleMask |= NSWindowStyleMaskResizable;
        }

        impl->window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:styleMask
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
        if (impl->window == nil) {
            throw std::runtime_error("Failed to create native macOS window");
        }

        impl->contentView = [[ScrapPlatformContentView alloc] initWithFrame:frame];
        auto* contentView = static_cast<ScrapPlatformContentView*>(impl->contentView);
        contentView.impl = impl.get();
        contentView.wantsLayer = YES;
        contentView.layer = [CAMetalLayer layer];
        CAMetalLayer* rootMetalLayer = (CAMetalLayer*)contentView.layer;
        rootMetalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
        rootMetalLayer.framebufferOnly = NO;
        if (@available(macOS 12.0, *)) {
            // Allow presents faster than display refresh (main loop is uncapped; avoids locking FPS to vsync).
            rootMetalLayer.displaySyncEnabled = NO;
        }

        if (createInfo.acceptFileDrops) {
            [contentView registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
        }

        impl->window.title = [NSString stringWithUTF8String:createInfo.title.c_str()];
        impl->window.contentView = impl->contentView;
        impl->delegate = [ScrapPlatformWindowDelegate new];
        static_cast<ScrapPlatformWindowDelegate*>(impl->delegate).impl = impl.get();
        impl->window.delegate = impl->delegate;
        [impl->window center];
        [impl->window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        return std::unique_ptr<PlatformWindow>(new PlatformWindow(std::move(impl)));
    }

    void* PlatformWindow::getMetalLayerHandle() const {
        return (__bridge void*)impl->contentView.layer;
    }

    FramebufferExtent PlatformWindow::getFramebufferExtent() const {
        return impl->framebufferExtent;
    }

    float PlatformWindow::getContentScaleFactor() const {
        return impl->contentScaleFactor;
    }

    void PlatformWindow::pumpEvents() {
        for (;;) {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantPast]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event == nil) {
                break;
            }
            [NSApp sendEvent:event];
        }
        [NSApp updateWindows];
    }

    bool PlatformWindow::shouldClose() const {
        return impl->shouldCloseWindow;
    }

    void PlatformWindow::requestClose() {
        impl->shouldCloseWindow = true;
        impl->inputState.closeRequested = true;
        [impl->window close];
    }

    void PlatformWindow::setWindowTitle(const std::string& title) {
        impl->window.title = [NSString stringWithUTF8String:title.c_str()];
    }

    void PlatformWindow::setWindowSize(uint32_t width, uint32_t height) {
        if (impl->window == nil || impl->contentView == nil) {
            return;
        }

        [impl->window
            setContentSize:NSMakeSize(static_cast<CGFloat>(width), static_cast<CGFloat>(height))];
        [impl->contentView layoutSubtreeIfNeeded];
        impl->updateDrawableSize(impl->contentView);
    }

    InputState PlatformWindow::consumeInputState() {
        InputState snapshot = impl->inputState;
        resetTransientInput(impl->inputState);
        return snapshot;
    }

    void PlatformWindow::prepareImGuiFrame(ImGuiIO& io, float deltaTime) {
        io.DisplaySize = ImVec2(static_cast<float>(impl->contentView.bounds.size.width),
                                static_cast<float>(impl->contentView.bounds.size.height));
        io.DisplayFramebufferScale = ImVec2(impl->contentScaleFactor, impl->contentScaleFactor);
        io.DeltaTime = deltaTime > 0.0f ? deltaTime : (1.0f / 60.0f);
        io.BackendPlatformName = "scrap_macos";

        for (std::size_t i = 0; i < keyIndex(Key::Count); ++i) {
            const ImGuiKey imguiKey = mapImGuiKey(static_cast<Key>(i));
            if (imguiKey != ImGuiKey_None) {
                io.AddKeyEvent(imguiKey, impl->inputState.keys[i]);
            }
        }

        io.AddKeyEvent(ImGuiMod_Ctrl, impl->inputState.controlDown);
        io.AddKeyEvent(ImGuiMod_Shift, impl->inputState.shiftDown);
        io.AddKeyEvent(ImGuiMod_Alt, impl->inputState.altDown);
        io.AddKeyEvent(ImGuiMod_Super, impl->inputState.superDown);

        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        if (impl->cursorCaptured) {
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        } else {
            io.AddMousePosEvent(impl->inputState.pointer.x, impl->inputState.pointer.y);
        }
        io.AddMouseButtonEvent(
            0, impl->inputState.pointer.buttons[static_cast<std::size_t>(MouseButton::Left)].down);
        io.AddMouseButtonEvent(
            1, impl->inputState.pointer.buttons[static_cast<std::size_t>(MouseButton::Right)].down);
        io.AddMouseButtonEvent(
            2,
            impl->inputState.pointer.buttons[static_cast<std::size_t>(MouseButton::Middle)].down);
        io.AddMouseWheelEvent(impl->inputState.pointer.scrollX, impl->inputState.pointer.scrollY);

        if (!impl->pendingText.empty()) {
            io.AddInputCharactersUTF8(impl->pendingText.c_str());
            impl->pendingText.clear();
        }

        if (impl->pendingBackspace) {
            io.AddKeyEvent(ImGuiKey_Backspace, true);
            io.AddKeyEvent(ImGuiKey_Backspace, false);
            impl->pendingBackspace = false;
        }
    }

    void PlatformWindow::setCursorCaptured(bool captured) {
        if (impl->cursorCaptured == captured) {
            return;
        }

        impl->cursorCaptured = captured;
        impl->hasPointerLocation = false;
        if (captured) {
            if (!impl->cursorHidden) {
                CGDisplayHideCursor(kCGDirectMainDisplay);
                impl->cursorHidden = true;
            }
            CGAssociateMouseAndMouseCursorPosition(false);
        } else {
            CGAssociateMouseAndMouseCursorPosition(true);
            if (impl->cursorHidden) {
                CGDisplayShowCursor(kCGDirectMainDisplay);
                impl->cursorHidden = false;
            }
        }
    }

    void PlatformWindow::activateWindowForInput() {
        if (impl->window == nil || impl->contentView == nil) {
            return;
        }
        [NSApp activateIgnoringOtherApps:YES];
        [impl->window makeKeyAndOrderFront:nil];
        [impl->window makeFirstResponder:impl->contentView];
        impl->syncPointerPositionFromSystemCursor();
    }

    bool PlatformWindow::isWindowKey() const {
        return impl->window != nil && [impl->window isKeyWindow];
    }

} // namespace scrap::platform

#else

namespace scrap::platform {

    void setWorkingDirectoryToExecutableFolder() {
    }

    PlatformWindow::PlatformWindow(std::unique_ptr<PlatformWindowImpl> impl)
        : impl(std::move(impl)) {
    }
    PlatformWindow::~PlatformWindow() = default;

    std::unique_ptr<PlatformWindow> createPlatformWindow(const PlatformWindowCreateInfo&) {
        throw std::runtime_error(
            "Native platform windows are only implemented for macOS in this build");
    }

    void* PlatformWindow::getMetalLayerHandle() const {
        return nullptr;
    }
    FramebufferExtent PlatformWindow::getFramebufferExtent() const {
        return {};
    }
    float PlatformWindow::getContentScaleFactor() const {
        return 1.0f;
    }
    void PlatformWindow::pumpEvents() {
    }
    bool PlatformWindow::shouldClose() const {
        return true;
    }
    void PlatformWindow::requestClose() {
    }
    void PlatformWindow::setWindowTitle(const std::string&) {
    }
    void PlatformWindow::setWindowSize(uint32_t, uint32_t) {
    }
    InputState PlatformWindow::consumeInputState() {
        return {};
    }
    void PlatformWindow::prepareImGuiFrame(ImGuiIO&, float) {
    }
    void PlatformWindow::setCursorCaptured(bool) {
    }
    void PlatformWindow::activateWindowForInput() {
    }

    bool PlatformWindow::isWindowKey() const {
        return true;
    }

} // namespace scrap::platform

#endif
