// Window: GLFW window and GL 3.3 context, ImGui setup and fonts, frame presentation and PNG read / write.
#include "window.hpp"
#include "notgroot/ui/style.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <cmath>
#include <cstdio>
#include <fontconfig/fontconfig.h>
#include <iostream>
#include <misc/freetype/imgui_freetype.h>
#include <png.h>
#include <stdexcept>
#include <strings.h>

namespace notgroot::app {
Window::Window(int width, int height, const std::string &titleText, bool hidden) {
    // GLFW and a core-profile GL 3.3 window.
    glfwSetErrorCallback([](int, const char *text) { std::cerr << "GLFW: " << text << '\n'; });
    if (!glfwInit())
        throw std::runtime_error("GLFW initialization failed. OpenGL 3.3 and an X/Wayland display are required.");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);
    window_ = glfwCreateWindow(width, height, titleText.c_str(), nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Cannot create an OpenGL 3.3 window");
    }

    // The size is at 100 %: on a scaled desktop the window opens that size in the desktop's units, or maximized
    // when that would not fit the screen.
    if (!hidden) {
        float scaleX = 1, scaleY = 1;
        glfwGetWindowContentScale(window_, &scaleX, &scaleY);
        int areaX = 0, areaY = 0, areaW = 0, areaH = 0;
        if (GLFWmonitor *monitor = glfwGetPrimaryMonitor())
            glfwGetMonitorWorkarea(monitor, &areaX, &areaY, &areaW, &areaH);
        const int scaledW = int(float(width) * scaleX), scaledH = int(float(height) * scaleY);
        if (areaW > 0 && (scaledW > areaW * 0.92 || scaledH > areaH * 0.92))
            glfwMaximizeWindow(window_);
        else if (scaleX > 1.01f)
            glfwSetWindowSize(window_, scaledW, scaledH);
    }

    // GL context and function loading; drain errors GLEW leaves behind.
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(hidden ? 0 : 1);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
        throw std::runtime_error("OpenGL loading failed");
    while (glGetError() != GL_NO_ERROR) {
    }

    // ImGui context, docking, fonts and the GLFW / OpenGL backends. Keyboard navigation stays off: the canvas and
    // the editor's own shortcuts own the arrows and Tab.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr; // the app saves its layout itself (config directory)
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    loadFonts(1);
    ImGui::StyleColorsDark(); // the application applies its theme
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    backendReady_ = true;
    pinch_ = std::make_unique<Pinch>(window_);
}

float Window::contentScale() const {
    float x = 1, y = 1;
    glfwGetWindowContentScale(window_, &x, &y);
    return std::isfinite(x) && x > 0 ? x : 1;
}

namespace {
// The file fontconfig picks for a family and style (empty if none).
std::string fontFile(const std::string &family, const char *style) {
    std::string file;
    if (family.empty() || !FcInit())
        return file;
    FcPattern *pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>(family.c_str()));
    FcPatternAddString(pattern, FC_STYLE, reinterpret_cast<const FcChar8 *>(style));
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result = FcResultNoMatch;
    if (FcPattern *match = FcFontMatch(nullptr, pattern, &result)) {
        FcChar8 *path = nullptr, *matched = nullptr;
        // Only the family asked for: fontconfig always returns something, often an unrelated fallback.
        if (FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch &&
            FcPatternGetString(match, FC_FAMILY, 0, &matched) == FcResultMatch &&
            strcasecmp(reinterpret_cast<const char *>(matched), family.c_str()) == 0)
            file = reinterpret_cast<const char *>(path);
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    return file;
}
} // namespace

// Font choice: the theme's shipped files, else its fontconfig family, else DejaVu Sans; the ImGui default font
// when none of those exists.
void Window::loadFonts(float ui, const ui::Theme &theme, const std::filesystem::path &fontDirectory) {
    auto &io = ImGui::GetIO();
    io.Fonts->Clear();
    std::filesystem::path font = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                          bold = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                          monoFile = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", figures;
    float body = 15; // pixels at 100 %; the other sizes keep their ratios to it
    const auto shipped = [&](const std::string &name) {
        return name.empty() ? std::filesystem::path() : fontDirectory / name;
    };

    if (!theme.fontRegular.empty() && std::filesystem::exists(shipped(theme.fontRegular))) {
        font = shipped(theme.fontRegular);
        bold = std::filesystem::exists(shipped(theme.fontStrong)) ? shipped(theme.fontStrong) : font;
        if (std::filesystem::exists(shipped(theme.fontFigures)))
            figures = shipped(theme.fontFigures);
        if (theme.fontPoints > 0)
            body = theme.fontPoints * 96.f / 72.f;
    } else if (const auto regular = fontFile(theme.fontFamily, "Regular"); !regular.empty()) {
        font = regular;
        const auto heavy = fontFile(theme.fontFamily, "Bold");
        bold = heavy.empty() ? regular : heavy;
        if (theme.fontPoints > 0)
            body = theme.fontPoints * 96.f / 72.f; // points to pixels at 96 dpi
    }

    // Every size scales with the body size (k) and the interface scale. FreeType with light hinting keeps small text
    // crisp; the canvas gets its card fonts again at every zoom step.
    const float k = body / 15;
    // Latin-1, general punctuation (dashes, ellipsis) and arrows
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2010, 0x203A, 0x2190, 0x21FF, 0x25A0, 0x25FF, 0};
    // ASCII, the middle dot, the times sign (a run's "ran 3×"), the ellipsis and the bullet: all the cards draw
    static const ImWchar cardRanges[] = {0x0020, 0x007E, 0x00B7, 0x00B7, 0x00D7, 0x00D7, 0x2022, 0x2026, 0};
    std::vector<ImFont *> cardTitles, cardBodies;
    if (std::filesystem::exists(font)) {
        ImFontConfig config;
        config.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
        const auto add = [&](const std::filesystem::path &file, float size, const ImWchar *glyphs = nullptr) {
            return io.Fonts->AddFontFromFileTTF(file.c_str(), size, &config, glyphs ? glyphs : ranges);
        };
        normal = add(font, 15 * k * ui);
        const auto heavy = std::filesystem::exists(bold) ? bold : font;
        strong = add(heavy, 15 * k * ui);
        smallStrong = add(heavy, 12 * k * ui);
        small = add(font, 12 * k * ui);
        title = add(figures.empty() ? heavy : figures, (figures.empty() ? 21 : 23) * k * ui);
        mono = std::filesystem::exists(monoFile) ? add(monoFile, 13 * k * ui) : normal;
        for (const float zoom : ui::zoomLevels()) {
            cardTitles.push_back(zoom == 1 ? strong : add(heavy, 15 * k * ui * zoom, cardRanges));
            cardBodies.push_back(zoom == 1 ? small : add(font, 12 * k * ui * zoom, cardRanges));
        }
    } else
        normal = small = title = strong = smallStrong = mono = io.Fonts->AddFontDefault();
    ui::setCanvasFonts(cardTitles, cardBodies);

    io.FontDefault = normal;
    ui::setTypeRamp({strong, title, small, smallStrong});
    io.Fonts->Build();
    if (backendReady_) { // a running editor: replace the GPU copy of the atlas
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }
}

Window::~Window() {
    pinch_.reset(); // its watcher posts to GLFW
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (window_)
        glfwDestroyWindow(window_);
    glfwTerminate();
}

bool Window::closing() const {
    return glfwWindowShouldClose(window_) != 0;
}

void Window::requestClose() {
    glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

void Window::cancelClose() {
    glfwSetWindowShouldClose(window_, GLFW_FALSE);
}

void Window::setTitle(const std::string &text) {
    glfwSetWindowTitle(window_, text.c_str());
}

void Window::beginFrame() {
    if (idleWait_ > 0)
        glfwWaitEventsTimeout(idleWait_);
    else
        glfwPollEvents();
    pinch_->poll();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Window::present(const std::filesystem::path &shot) {
    ImGui::Render();
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    const ImVec4 paper = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    glClearColor(paper.x, paper.y, paper.z, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Screenshot: read the back buffer (rows bottom-up) and flip to top-down for the PNG.
    if (!shot.empty()) {
        std::vector<unsigned char> pixels(std::size_t(w) * std::size_t(h) * 3), flipped(pixels.size());
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        for (int y = 0; y < h; ++y)
            std::copy_n(pixels.data() + std::size_t(h - 1 - y) * std::size_t(w) * 3, std::size_t(w) * 3,
                        flipped.data() + std::size_t(y) * std::size_t(w) * 3);
        writePng(shot, w, h, flipped);
    }
}

void Window::swap() {
    glfwSwapBuffers(window_);
}

// `pixels` owns the decoded images until glfwSetWindowIcon has copied them.
void Window::setIcon(const std::vector<std::filesystem::path> &pngs) {
    std::vector<std::vector<unsigned char>> pixels;
    std::vector<GLFWimage> images;
    pixels.reserve(pngs.size());
    for (const auto &path : pngs) {
        int width = 0, height = 0;
        std::vector<unsigned char> rgba;
        if (!readPng(path, width, height, rgba))
            continue;
        pixels.push_back(std::move(rgba));
        images.push_back({width, height, pixels.back().data()});
    }
    if (!images.empty())
        glfwSetWindowIcon(window_, int(images.size()), images.data());
}

// libpng simplified API, converting any PNG to RGBA8.
bool readPng(const std::filesystem::path &path, int &width, int &height, std::vector<unsigned char> &rgba) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path.c_str()))
        return false;
    image.format = PNG_FORMAT_RGBA;
    rgba.resize(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, rgba.data(), 0, nullptr)) {
        png_image_free(&image);
        return false;
    }
    width = int(image.width);
    height = int(image.height);
    return true;
}

void writePng(const std::filesystem::path &path, int width, int height, const std::vector<unsigned char> &rgb) {
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    FILE *file = std::fopen(path.c_str(), "wb");
    if (!file)
        throw std::runtime_error("cannot write screenshot: " + path.string());

    // libpng reports errors by longjmp to this setjmp.
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(file);
        throw std::runtime_error("cannot encode screenshot: " + path.string());
    }

    png_init_io(png, file);
    png_set_IHDR(png, info, png_uint_32(width), png_uint_32(height), 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < height; ++y)
        png_write_row(png, const_cast<png_bytep>(rgb.data() + std::size_t(y) * std::size_t(width) * 3));
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(file);
}
} // namespace notgroot::app
