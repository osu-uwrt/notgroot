// Pinch: XInput 2.4 gesture events on a second X connection. GLFW's own connection has XInput 2.0, and the server
// won't move a client from 2.0 to 2.4, so the gestures are selected and read here instead.
#include "pinch.hpp"
#ifdef NOTGROOT_PINCH
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/extensions/XInput2.h>
#include <atomic>
#include <chrono>
#include <poll.h>
#include <thread>

namespace notgroot::app {
struct Pinch::Impl {
    Display *display = nullptr;
    int opcode = 0;
    std::thread watcher;
    std::atomic<bool> stop{false}, posted{false};
    bool ending = false; // an end came in the same batch as updates: it applies next frame
};

Pinch::Pinch(GLFWwindow *window) {
    // Only on X11 (XWayland included), with the server's XInput at 2.4 or later.
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    if (glfwGetPlatform() != GLFW_PLATFORM_X11)
        return;
#endif
    const Window target = glfwGetX11Window(window);
    if (!glfwGetX11Display() || !target)
        return;
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return;
    int opcode = 0, event = 0, error = 0, major = 2, minor = 4;
    if (!XQueryExtension(display, "XInputExtension", &opcode, &event, &error) ||
        XIQueryVersion(display, &major, &minor) != Success || major < 2 || (major == 2 && minor < 4)) {
        XCloseDisplay(display);
        return;
    }
    unsigned char bits[XIMaskLen(XI_LASTEVENT)] = {};
    XISetMask(bits, XI_GesturePinchBegin);
    XISetMask(bits, XI_GesturePinchUpdate);
    XISetMask(bits, XI_GesturePinchEnd);
    XIEventMask mask{XIAllMasterDevices, int(sizeof(bits)), bits};
    XISelectEvents(display, target, &mask, 1);
    XFlush(display);

    impl_ = std::make_unique<Impl>();
    impl_->display = display;
    impl_->opcode = opcode;
    // The editor sleeps in glfwWaitEvents between inputs: wake it when a gesture comes in on this connection.
    impl_->watcher = std::thread([impl = impl_.get(), fd = ConnectionNumber(display)] {
        while (!impl->stop) {
            pollfd waiting{fd, POLLIN, 0};
            if (impl->posted || ::poll(&waiting, 1, 100) <= 0) {
                if (impl->posted)
                    std::this_thread::sleep_for(std::chrono::milliseconds(4));
                continue;
            }
            impl->posted = true;
            glfwPostEmptyEvent();
        }
    });
}

Pinch::~Pinch() {
    if (!impl_)
        return;
    impl_->stop = true;
    impl_->watcher.join();
    XCloseDisplay(impl_->display);
}

void Pinch::poll() {
    if (!impl_)
        return;
    // A pinch that ended along with updates in one batch still shows for a frame.
    if (impl_->ending) {
        impl_->ending = false;
        active_ = false;
        scale_ = 1;
    }
    bool updated = false;
    while (XPending(impl_->display)) {
        XEvent event;
        XNextEvent(impl_->display, &event);
        auto &cookie = event.xcookie;
        if (cookie.type != GenericEvent || cookie.extension != impl_->opcode || !XGetEventData(impl_->display, &cookie))
            continue;
        const auto *pinch = static_cast<const XIGesturePinchEvent *>(cookie.data);
        if (cookie.evtype == XI_GesturePinchBegin || cookie.evtype == XI_GesturePinchUpdate) {
            active_ = updated = true;
            scale_ = float(pinch->scale);
            impl_->ending = false;
        } else if (cookie.evtype == XI_GesturePinchEnd) {
            if (updated)
                impl_->ending = true;
            else {
                active_ = false;
                scale_ = 1;
            }
        }
        XFreeEventData(impl_->display, &cookie);
    }
    impl_->posted = false;
}
} // namespace notgroot::app
#else
// Built without XInput 2.4 headers: no pinches.
namespace notgroot::app {
struct Pinch::Impl {};
Pinch::Pinch(GLFWwindow *) {
}
Pinch::~Pinch() = default;
void Pinch::poll() {
}
} // namespace notgroot::app
#endif
