// Touchpad pinches, which GLFW 3.3 doesn't report: XInput 2.4 gesture events from the X server (XWayland passes the
// compositor's on), read on a connection of their own.
#pragma once
#include <memory>

struct GLFWwindow;

namespace notgroot::app {
// Main thread only, except a watcher thread that wakes the event wait when a gesture arrives. Does nothing where
// there is no X11 display with XInput 2.4.
class Pinch {
  public:
    explicit Pinch(GLFWwindow *);
    ~Pinch();
    Pinch(const Pinch &) = delete;
    Pinch &operator=(const Pinch &) = delete;

    void poll(); // reads the gestures that arrived (once a frame)
    bool active() const {
        return active_;
    }
    float scale() const { // since the pinch began
        return scale_;
    }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool active_ = false;
    float scale_ = 1;
};
} // namespace notgroot::app
