// Pinch on macOS: the trackpad's magnify events, which GLFW doesn't pass on, read with a local NSEvent monitor
// (it runs inside glfwWaitEvents/glfwPollEvents on the main thread, so no watcher is needed).
#include "pinch.hpp"
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#import <AppKit/AppKit.h>

namespace notgroot::app {
struct Pinch::Impl {
    id monitor = nil;
    bool active = false, ending = false;
    float scale = 1;
};

Pinch::Pinch(GLFWwindow *window) {
    NSWindow *target = glfwGetCocoaWindow(window);
    if (!target)
        return;
    impl_ = std::make_unique<Impl>();
    Impl *impl = impl_.get();
    NSEvent * (^handler)(NSEvent *) = ^NSEvent *(NSEvent *event) {
        if (event.window != target)
            return event;
        if (event.phase & NSEventPhaseBegan) {
            impl->ending = false;
            impl->scale = 1;
        }
        // magnification is this event's change; scale is the product since the pinch began
        impl->scale *= float(1 + event.magnification);
        impl->active = true;
        if (event.phase & (NSEventPhaseEnded | NSEventPhaseCancelled))
            impl->ending = true;
        return event;
    };
    impl->monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMagnify handler:handler];
    [impl->monitor retain];
}

Pinch::~Pinch() {
    if (!impl_ || !impl_->monitor)
        return;
    [NSEvent removeMonitor:impl_->monitor];
    [impl_->monitor release];
}

void Pinch::poll() {
    if (!impl_)
        return;
    active_ = impl_->active;
    scale_ = impl_->scale;
    // an end shows for one frame, like the X11 path
    if (impl_->ending) {
        impl_->ending = false;
        impl_->active = false;
        impl_->scale = 1;
    }
}
} // namespace notgroot::app
