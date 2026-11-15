// GLFW/GLEW/ImGui context of the editor: one window, the theme's fonts. Adapted from Nereus's viewer host window.
#pragma once
#include "notgroot/ui/theme.hpp"
#include "pinch.hpp"
#include <filesystem>
#include <memory>
#include <imgui.h>
#include <string>
#include <vector>

struct GLFWwindow;

namespace notgroot::app {
// Owns the GLFW window, its GL context and the ImGui context / backends. Main thread only.
class Window {
  public:
    Window(int width, int height, const std::string &title, bool hidden);
    ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    GLFWwindow *handle() const {
        return window_;
    }

    bool closing() const;
    void requestClose();
    void cancelClose(); // a close the application refuses (asks first)
    void setTitle(const std::string &);
    void beginFrame();                               // poll (or wait for) events + ImGui NewFrame
    void present(const std::filesystem::path &shot); // render ImGui, optional PNG of the frame
    void swap();

    // Sleep in beginFrame until input arrives (an editor redraws only when something happens), at most `seconds`.
    void setIdleWait(double seconds) {
        idleWait_ = seconds;
    }

    // The window's icon (dock, task switcher) from PNG files of several sizes; missing files are skipped.
    void setIcon(const std::vector<std::filesystem::path> &pngs);

    // Fonts, valid after loadFonts().
    ImFont *normal = nullptr, *small = nullptr, *title = nullptr, *strong = nullptr, *smallStrong = nullptr;
    ImFont *mono = nullptr; // values in the inspector and on cards (DejaVu Sans Mono)

    // Loads the fonts at the interface scale; between frames only. The theme's font files (in `fontDirectory`) or
    // family (fontconfig name) at its point size replace DejaVu Sans.
    void loadFonts(float ui, const ui::Theme &theme = {}, const std::filesystem::path &fontDirectory = {});
    // The desktop's UI scale for this window (GNOME at 200 %: 2), in window units: 1 on a Retina Mac, whose window
    // units are already points.
    float contentScale() const;
    // Framebuffer pixels per window unit (2 on a Retina Mac, 1 on X11).
    float pixelRatio() const;
    // The touchpad pinch going on, read in beginFrame.
    const Pinch &pinch() const {
        return *pinch_;
    }

  private:
    bool backendReady_ = false;
    double idleWait_ = 0;
    GLFWwindow *window_ = nullptr;
    std::unique_ptr<Pinch> pinch_;
};

// Reads a PNG as RGBA8 rows (top row first); false if it cannot be read.
bool readPng(const std::filesystem::path &, int &width, int &height, std::vector<unsigned char> &rgba);
// Writes RGB8 rows (top row first) as a PNG; throws on failure.
void writePng(const std::filesystem::path &, int width, int height, const std::vector<unsigned char> &rgb);
} // namespace notgroot::app
