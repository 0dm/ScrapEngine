#include <app/Log.hpp>
#include <app/ScrapEngineApp.hpp>
#include <gpu/Backend.hpp>

static_assert(scrap::gpu::kActiveBackend == scrap::gpu::BackendKind::Metal,
              "ScrapEngine iOS uses Metal.");

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unistd.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

namespace scrap_ios {

    class IOSPlatformView final : public scrap::platform::PlatformView {
      public:
        explicit IOSPlatformView(UIView* view)
            : nativeView(view), metalLayer((CAMetalLayer*)view.layer) {
            updateDrawableSize(view.bounds.size, view.contentScaleFactor);
        }

        void* getMetalLayerHandle() const override {
            return (__bridge void*)metalLayer;
        }

        scrap::platform::FramebufferExtent getFramebufferExtent() const override {
            return framebufferExtent;
        }

        float getContentScaleFactor() const override {
            return contentScaleFactor;
        }

        void pumpEvents() override {
        }

        bool shouldClose() const override {
            return false;
        }

        void requestClose() override {
            inputState.closeRequested = true;
        }

        void setWindowTitle(const std::string&) override {
        }

        void setWindowSize(uint32_t width, uint32_t height) override {
            updateDrawableSize(CGSizeMake(width, height), nativeView.contentScaleFactor);
        }

        scrap::platform::InputState consumeInputState() override {
            scrap::platform::InputState snapshot = inputState;
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
            return snapshot;
        }

        void prepareImGuiFrame(ImGuiIO& io, float deltaTime) override {
            io.DisplaySize = ImVec2(static_cast<float>(nativeView.bounds.size.width),
                                    static_cast<float>(nativeView.bounds.size.height));
            io.DisplayFramebufferScale = ImVec2(contentScaleFactor, contentScaleFactor);
            io.DeltaTime = deltaTime > 0.0f ? deltaTime : (1.0f / 60.0f);

            addKeyEvent(io, ImGuiKey_Escape, scrap::platform::Key::Escape);
            addKeyEvent(io, ImGuiKey_GraveAccent, scrap::platform::Key::GraveAccent);
            addKeyEvent(io, ImGuiKey_Delete, scrap::platform::Key::DeleteKey);
            addKeyEvent(io, ImGuiKey_F, scrap::platform::Key::F);
            addKeyEvent(io, ImGuiKey_E, scrap::platform::Key::E);
            addKeyEvent(io, ImGuiKey_R, scrap::platform::Key::R);
            addKeyEvent(io, ImGuiKey_O, scrap::platform::Key::O);
            addKeyEvent(io, ImGuiKey_N, scrap::platform::Key::N);
            addKeyEvent(io, ImGuiKey_P, scrap::platform::Key::P);
            addKeyEvent(io, ImGuiKey_Space, scrap::platform::Key::Space);
            addKeyEvent(io, ImGuiKey_LeftShift, scrap::platform::Key::LeftShift);
            addKeyEvent(io, ImGuiKey_RightShift, scrap::platform::Key::RightShift);
            addKeyEvent(io, ImGuiKey_LeftCtrl, scrap::platform::Key::LeftControl);
            addKeyEvent(io, ImGuiKey_RightCtrl, scrap::platform::Key::RightControl);
            addKeyEvent(io, ImGuiKey_W, scrap::platform::Key::W);
            addKeyEvent(io, ImGuiKey_A, scrap::platform::Key::A);
            addKeyEvent(io, ImGuiKey_S, scrap::platform::Key::S);
            addKeyEvent(io, ImGuiKey_D, scrap::platform::Key::D);

            io.AddKeyEvent(ImGuiMod_Ctrl, inputState.controlDown);
            io.AddKeyEvent(ImGuiMod_Shift, inputState.shiftDown);
            io.AddKeyEvent(ImGuiMod_Alt, inputState.altDown);
            io.AddKeyEvent(ImGuiMod_Super, inputState.superDown);

            if (cursorCaptured) {
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                io.AddMouseButtonEvent(0, false);
            } else {
                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMousePosEvent(inputState.pointer.x, inputState.pointer.y);
                io.AddMouseButtonEvent(
                    0, inputState.pointer
                           .buttons[static_cast<std::size_t>(scrap::platform::MouseButton::Left)]
                           .down);
            }

            if (!pendingText.empty()) {
                io.AddInputCharactersUTF8(pendingText.c_str());
                pendingText.clear();
            }

            if (pendingBackspace) {
                io.AddKeyEvent(ImGuiKey_Backspace, true);
                io.AddKeyEvent(ImGuiKey_Backspace, false);
                pendingBackspace = false;
            }
        }

        void setCursorCaptured(bool captured) override {
            cursorCaptured = captured;
            if (captured) {
                auto& leftButton =
                    inputState.pointer
                        .buttons[static_cast<std::size_t>(scrap::platform::MouseButton::Left)];
                leftButton.down = false;
                leftButton.pressed = false;
                leftButton.released = false;
            }
        }

        void updateDrawableSize(CGSize size, CGFloat scale) {
            contentScaleFactor = scale > 0.0 ? static_cast<float>(scale) : 1.0f;
            metalLayer.contentsScale = contentScaleFactor;
            metalLayer.drawableSize =
                CGSizeMake(size.width * contentScaleFactor, size.height * contentScaleFactor);
            framebufferExtent = {
                static_cast<uint32_t>(std::max(0.0, metalLayer.drawableSize.width)),
                static_cast<uint32_t>(std::max(0.0, metalLayer.drawableSize.height)),
            };
            inputState.framebufferResized = true;
        }

        void requestCursorCaptureToggle() {
            inputState.toggleCursorCaptureRequested = true;
        }

        void appendText(NSString* text) {
            if (text.length == 0) {
                return;
            }

            pendingText += std::string(text.UTF8String);
        }

        void appendBackspace() {
            pendingBackspace = true;
        }

        void handleTouches(NSSet<UITouch*>* touches, UIView* view, UITouchPhase phase) {
            UITouch* touch = touches.anyObject;
            if (touch == nil) {
                return;
            }

            const CGPoint location = [touch locationInView:view];
            if (!hasPointerLocation) {
                lastPointerLocation = location;
                hasPointerLocation = true;
            }

            inputState.pointer.deltaX += static_cast<float>(location.x - lastPointerLocation.x);
            inputState.pointer.deltaY += static_cast<float>(location.y - lastPointerLocation.y);
            inputState.pointer.x = static_cast<float>(location.x);
            inputState.pointer.y = static_cast<float>(location.y);
            lastPointerLocation = location;

            switch (phase) {
            case UITouchPhaseBegan:
                inputState.pointer
                    .buttons[static_cast<std::size_t>(scrap::platform::MouseButton::Left)]
                    .down = true;
                inputState.pointer
                    .buttons[static_cast<std::size_t>(scrap::platform::MouseButton::Left)]
                    .pressed = true;
                break;
            case UITouchPhaseMoved:
            case UITouchPhaseStationary:
                inputState.pointer
                    .buttons[static_cast<std::size_t>(scrap::platform::MouseButton::Left)]
                    .down = true;
                break;
            case UITouchPhaseEnded:
            case UITouchPhaseCancelled:
                inputState.pointer
                    .buttons[static_cast<std::size_t>(scrap::platform::MouseButton::Left)]
                    .released =
                    inputState.pointer
                        .buttons[static_cast<std::size_t>(scrap::platform::MouseButton::Left)]
                        .down;
                inputState.pointer
                    .buttons[static_cast<std::size_t>(scrap::platform::MouseButton::Left)]
                    .down = false;
                hasPointerLocation = false;
                break;
            default:
                break;
            }
        }

        void setKey(UIKeyboardHIDUsage usage, bool pressed) {
            const auto key = mapKey(usage);
            if (!key.has_value()) {
                return;
            }

            const std::size_t index = scrap::platform::keyIndex(*key);
            if (pressed != inputState.keys[index]) {
                if (pressed) {
                    inputState.keyPressed[index] = true;
                } else {
                    inputState.keyReleased[index] = true;
                }
            }
            inputState.keys[index] = pressed;
            inputState.shiftDown =
                inputState.keys[scrap::platform::keyIndex(scrap::platform::Key::LeftShift)] ||
                inputState.keys[scrap::platform::keyIndex(scrap::platform::Key::RightShift)];
            inputState.controlDown =
                inputState.keys[scrap::platform::keyIndex(scrap::platform::Key::LeftControl)] ||
                inputState.keys[scrap::platform::keyIndex(scrap::platform::Key::RightControl)];
        }

      private:
        static std::optional<scrap::platform::Key> mapKey(UIKeyboardHIDUsage usage) {
            switch (usage) {
            case UIKeyboardHIDUsageKeyboardEscape:
                return scrap::platform::Key::Escape;
            case UIKeyboardHIDUsageKeyboardGraveAccentAndTilde:
                return scrap::platform::Key::GraveAccent;
            case UIKeyboardHIDUsageKeyboardDeleteOrBackspace:
                return scrap::platform::Key::DeleteKey;
            case UIKeyboardHIDUsageKeyboardF:
                return scrap::platform::Key::F;
            case UIKeyboardHIDUsageKeyboardE:
                return scrap::platform::Key::E;
            case UIKeyboardHIDUsageKeyboardR:
                return scrap::platform::Key::R;
            case UIKeyboardHIDUsageKeyboardO:
                return scrap::platform::Key::O;
            case UIKeyboardHIDUsageKeyboardN:
                return scrap::platform::Key::N;
            case UIKeyboardHIDUsageKeyboardP:
                return scrap::platform::Key::P;
            case UIKeyboardHIDUsageKeyboardSpacebar:
                return scrap::platform::Key::Space;
            case UIKeyboardHIDUsageKeyboardLeftShift:
                return scrap::platform::Key::LeftShift;
            case UIKeyboardHIDUsageKeyboardRightShift:
                return scrap::platform::Key::RightShift;
            case UIKeyboardHIDUsageKeyboardLeftControl:
                return scrap::platform::Key::LeftControl;
            case UIKeyboardHIDUsageKeyboardRightControl:
                return scrap::platform::Key::RightControl;
            case UIKeyboardHIDUsageKeyboardW:
                return scrap::platform::Key::W;
            case UIKeyboardHIDUsageKeyboardA:
                return scrap::platform::Key::A;
            case UIKeyboardHIDUsageKeyboardS:
                return scrap::platform::Key::S;
            case UIKeyboardHIDUsageKeyboardD:
                return scrap::platform::Key::D;
            default:
                return std::nullopt;
            }
        }

        void addKeyEvent(ImGuiIO& io, ImGuiKey imguiKey, scrap::platform::Key platformKey) {
            io.AddKeyEvent(imguiKey, inputState.keys[scrap::platform::keyIndex(platformKey)]);
        }

        UIView* nativeView = nil;
        CAMetalLayer* metalLayer = nil;
        scrap::platform::InputState inputState{};
        scrap::platform::FramebufferExtent framebufferExtent{};
        float contentScaleFactor = 1.0f;
        bool cursorCaptured = false;
        bool pendingBackspace = false;
        bool hasPointerLocation = false;
        CGPoint lastPointerLocation{0, 0};
        std::string pendingText;
    };

} // namespace scrap_ios

@interface ScrapMetalView : UIView <UIKeyInput> {
  @public
    scrap_ios::IOSPlatformView* platformBridge;
}
@end

@implementation ScrapMetalView

+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (!self) {
        return nil;
    }

    self.multipleTouchEnabled = YES;
    self.opaque = YES;
    self.backgroundColor = UIColor.blackColor;

    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    layer.device = MTLCreateSystemDefaultDevice();
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
    layer.framebufferOnly = NO;
    layer.contentsScale = UIScreen.mainScreen.scale;

    UITapGestureRecognizer* toggleCaptureGesture =
        [[UITapGestureRecognizer alloc] initWithTarget:self
                                                action:@selector(handleCursorCaptureToggle:)];
    toggleCaptureGesture.numberOfTouchesRequired = 2;
    toggleCaptureGesture.numberOfTapsRequired = 2;
    [self addGestureRecognizer:toggleCaptureGesture];

    return self;
}

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (BOOL)hasText {
    return YES;
}

- (void)didMoveToWindow {
    [super didMoveToWindow];
    if (self.window != nil) {
        [self becomeFirstResponder];
    }
}

- (void)layoutSubviews {
    [super layoutSubviews];
    if (platformBridge != nullptr) {
        platformBridge->updateDrawableSize(self.bounds.size, self.contentScaleFactor);
    }
}

- (void)insertText:(NSString*)text {
    if (platformBridge != nullptr) {
        platformBridge->appendText(text);
    }
}

- (void)deleteBackward {
    if (platformBridge != nullptr) {
        platformBridge->appendBackspace();
    }
}

- (void)handleCursorCaptureToggle:(UITapGestureRecognizer*)gesture {
    if (gesture.state == UIGestureRecognizerStateRecognized && platformBridge != nullptr) {
        platformBridge->requestCursorCaptureToggle();
    }
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [super touchesBegan:touches withEvent:event];
    if (platformBridge != nullptr) {
        platformBridge->handleTouches(touches, self, UITouchPhaseBegan);
    }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [super touchesMoved:touches withEvent:event];
    if (platformBridge != nullptr) {
        platformBridge->handleTouches(touches, self, UITouchPhaseMoved);
    }
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [super touchesEnded:touches withEvent:event];
    if (platformBridge != nullptr) {
        platformBridge->handleTouches(touches, self, UITouchPhaseEnded);
    }
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [super touchesCancelled:touches withEvent:event];
    if (platformBridge != nullptr) {
        platformBridge->handleTouches(touches, self, UITouchPhaseCancelled);
    }
}

- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    [super pressesBegan:presses withEvent:event];
    if (platformBridge == nullptr) {
        return;
    }

    for (UIPress* press in presses) {
        if (press.key != nil) {
            platformBridge->setKey(press.key.keyCode, true);
        }
    }
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    [super pressesEnded:presses withEvent:event];
    if (platformBridge == nullptr) {
        return;
    }

    for (UIPress* press in presses) {
        if (press.key != nil) {
            platformBridge->setKey(press.key.keyCode, false);
        }
    }
}

- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
    [super pressesCancelled:presses withEvent:event];
    if (platformBridge == nullptr) {
        return;
    }

    for (UIPress* press in presses) {
        if (press.key != nil) {
            platformBridge->setKey(press.key.keyCode, false);
        }
    }
}

@end

@interface ScrapViewController : UIViewController
@end

@implementation ScrapViewController {
    std::shared_ptr<scrap_ios::IOSPlatformView> _platformView;
    std::unique_ptr<scrap::ScrapEngineApp> _app;
    CADisplayLink* _displayLink;
    CFTimeInterval _lastTimestamp;
    BOOL _failed;
}

- (void)loadView {
    self.view = [[ScrapMetalView alloc] initWithFrame:UIScreen.mainScreen.bounds];
}

- (void)viewDidLoad {
    [super viewDidLoad];

    scrap::Log::init();

    auto* metalView = (ScrapMetalView*)self.view;
    _platformView = std::make_shared<scrap_ios::IOSPlatformView>(metalView);
    metalView->platformBridge = _platformView.get();
    [self.view setNeedsLayout];
    [self.view layoutIfNeeded];

    _app = std::make_unique<scrap::ScrapEngineApp>();

    try {
        const scrap::platform::FramebufferExtent extent = _platformView->getFramebufferExtent();
        _app->initialize(*_platformView, extent.width > 0 ? extent.width : 1280u,
                         extent.height > 0 ? extent.height : 720u);
    } catch (const std::exception& error) {
        NSLog(@"ScrapEngine initialization failed: %s", error.what());
        _failed = YES;
    }
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if (_failed || _displayLink != nil) {
        return;
    }

    _lastTimestamp = 0.0;
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(step:)];
    [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSDefaultRunLoopMode];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [_displayLink invalidate];
    _displayLink = nil;
}

- (void)dealloc {
    [_displayLink invalidate];
    _displayLink = nil;
    if (_app) {
        _app->shutdown();
    }
    scrap::Log::shutdown();
}

- (void)step:(CADisplayLink*)displayLink {
    if (_failed || !_app) {
        return;
    }

    const float deltaTime = _lastTimestamp > 0.0
                                ? static_cast<float>(displayLink.timestamp - _lastTimestamp)
                                : (1.0f / 60.0f);
    _lastTimestamp = displayLink.timestamp;

    try {
        _app->tick(deltaTime);
    } catch (const std::exception& error) {
        NSLog(@"ScrapEngine frame failed: %s", error.what());
        _failed = YES;
        [_displayLink invalidate];
        _displayLink = nil;
    }
}

@end

@interface ScrapAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation ScrapAppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    (void)application;
    (void)launchOptions;

    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [ScrapViewController new];
    [self.window makeKeyAndVisible];
    return YES;
}

@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
        const char* bundlePath = NSBundle.mainBundle.resourcePath.UTF8String;
        if (bundlePath != nullptr) {
            chdir(bundlePath);
        }

        return UIApplicationMain(argc, argv, nil, NSStringFromClass([ScrapAppDelegate class]));
    }
}
