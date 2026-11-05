// App: window, menus, dockspace and preferences around the editor panels.
#include "app.hpp"
#include "notgroot/ui/panels.hpp"
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;

namespace notgroot::app {
fs::path contentDirectory() {
    if (fs::exists(fs::path(NOTGROOT_CONTENT_DIR) / "themes"))
        return NOTGROOT_CONTENT_DIR;
    return NOTGROOT_INSTALLED_CONTENT_DIR;
}

fs::path configHome() {
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return xdg;
    if (const char *home = std::getenv("HOME"); home && *home)
        return fs::path(home) / ".config";
    return {};
}

App::App(Options options) : opt_(std::move(options)) {
    content_ = contentDirectory();
    if (opt_.persist && !configHome().empty())
        config_ = configHome() / "notgroot";

    const bool hidden = !opt_.screenshot.empty();
    window_ = std::make_unique<Window>(1600, 1000, "notgroot", hidden);
    window_->setIcon({content_ / "icons/notgroot_16.png", content_ / "icons/notgroot_32.png",
                      content_ / "icons/notgroot_48.png", content_ / "icons/notgroot_64.png",
                      content_ / "icons/notgroot_128.png", content_ / "icons/notgroot_256.png"});
    window_->setIdleWait(hidden ? 0 : 0.5);
    loadNodeIcons();

    // Layout: the last session's docking, else the default arrangement built on the first frame.
    if (!config_.empty() && fs::exists(config_ / "layout.ini"))
        ImGui::LoadIniSettingsFromDisk((config_ / "layout.ini").c_str());

    for (const auto &warning : ui::loadThemes(content_ / "themes"))
        std::cerr << "notgroot: theme: " << warning << '\n';
    followNereus_ = opt_.theme.empty() && preference("theme").empty();
    const std::string theme = preferredTheme();
    if (!ui::applyTheme(theme)) {
        std::cerr << "notgroot: unknown theme '" << theme << "'; using timing-board\n";
        if (!ui::applyTheme("timing-board"))
            ui::applyTheme(ui::themes().front().id);
    }

    try {
        if (const auto scale = preference("interface_scale"); !scale.empty() && scale != "auto")
            uiScale_ = std::stof(scale);
        else
            uiScale_ = window_->contentScale();
    } catch (const std::exception &) {
        uiScale_ = window_->contentScale();
    }
    ui::setInterfaceScale(uiScale_);
    window_->loadFonts(uiScale_, ui::currentThemeInfo(), content_ / "fonts");

    // The editor: builtin models, then the trees asked for, else the last ones open.
    std::string error;
    if (!session_.loadBuiltinModels(content_ / "builtin_models.xml", error))
        std::cerr << "notgroot: " << error << '\n';
    session_.quit = [this] { quit(); };
    session_.setChecker(fs::exists(NOTGROOT_CHECK) ? fs::path(NOTGROOT_CHECK) : fs::path(NOTGROOT_INSTALLED_CHECK));
    session_.browse = [this] {
        const auto start = session_.workspace.isOpen() ? session_.workspace.directory() : fs::path();
        if (!picker_.open("Open a trees folder", start, "", {}, true))
            session_.status(picker_.hint(), true);
    };
    picker_.setParent(0);
    session_.horizontal = preference("layout") == "left-right";
    if (!config_.empty() && fs::exists(config_ / "notgroot.yaml"))
        try {
            for (const auto &path : YAML::LoadFile((config_ / "notgroot.yaml").string())["recent"])
                if (fs::exists(path.as<std::string>()))
                    recent_.push_back(path.as<std::string>());
        } catch (const std::exception &) {
        }
    if (!opt_.open.empty())
        openWorkspace(opt_.open);
    else if (!recent_.empty())
        openWorkspace(recent_.front());

    // The getting-started tour on the first start (never in a --no-persist run: screenshots stay as asked).
    session_.tourDone = [this] { savePreference("tour_seen", "yes"); };
    if (!config_.empty() && preference("tour_seen").empty())
        session_.showTour = true;
}

void App::openWorkspace(const fs::path &path) {
    // Edits would be lost, so ask first.
    session_.confirmThen([this, path] { loadWorkspace(path); });
}

void App::loadWorkspace(const fs::path &path) {
    std::string error;
    if (!session_.open(path, error)) {
        session_.status(error, true);
        return;
    }
    // Remembered for next time (most recent first, ten at most).
    const auto dir = session_.workspace.loose() ? fs::absolute(path).string() : session_.workspace.directory().string();
    recent_.erase(std::remove(recent_.begin(), recent_.end(), dir), recent_.end());
    recent_.insert(recent_.begin(), dir);
    if (recent_.size() > 10)
        recent_.resize(10);
    if (config_.empty())
        return;
    try {
        const auto file = config_ / "notgroot.yaml";
        YAML::Node preferences(YAML::NodeType::Map);
        if (fs::exists(file))
            preferences = YAML::LoadFile(file.string());
        preferences["recent"] = recent_;
        fs::create_directories(config_);
        std::ofstream(file) << preferences << '\n';
    } catch (const std::exception &error) {
        std::cerr << "notgroot: recent not saved: " << error.what() << '\n';
    }
}

void App::quit() {
    session_.confirmThen([this] {
        closing_ = true;
        window_->requestClose();
    });
}

void App::updateTitle() {
    std::string title = "notgroot";
    if (session_.workspace.isOpen())
        title = (session_.dirty() ? "• " : "") + session_.workspace.directory().string() +
                (session_.active.empty() ? "" : " — " + session_.active) + " — notgroot";
    if (title != title_) {
        title_ = title;
        window_->setTitle(title);
    }
}

App::~App() {
    savePreference("layout", session_.horizontal ? "left-right" : "");
    if (!config_.empty()) {
        std::error_code error;
        fs::create_directories(config_, error);
        ImGui::SaveIniSettingsToDisk((config_ / "layout.ini").c_str());
    }
    if (logo_)
        glDeleteTextures(1, &logo_);
    if (!icons_.empty())
        glDeleteTextures(GLsizei(icons_.size()), icons_.data());
}

std::string App::preferredTheme() const {
    if (!opt_.theme.empty())
        return opt_.theme;
    if (auto own = preference("theme"); !own.empty())
        return own;
    // Nereus's own choice, so both tools look alike on one desktop.
    if (const auto nereus = configHome() / "nereus/viewer.yaml"; !configHome().empty() && fs::exists(nereus))
        try {
            if (auto theme = YAML::LoadFile(nereus.string())["theme"].as<std::string>(""); !theme.empty())
                return theme;
        } catch (const std::exception &) {
        }
    return "timing-board";
}

std::string App::preference(const char *key) const {
    if (config_.empty() || !fs::exists(config_ / "notgroot.yaml"))
        return {};
    try {
        return YAML::LoadFile((config_ / "notgroot.yaml").string())[key].as<std::string>("");
    } catch (const std::exception &error) {
        std::cerr << "notgroot: ignoring " << config_ / "notgroot.yaml"
                  << ": " << error.what() << '\n';
        return {};
    }
}

// Preferences in notgroot.yaml in the config directory; an empty value removes the key.
void App::savePreference(const char *key, const std::string &value) {
    if (config_.empty())
        return;
    try {
        const auto file = config_ / "notgroot.yaml";
        YAML::Node preferences(YAML::NodeType::Map);
        if (fs::exists(file))
            preferences = YAML::LoadFile(file.string());
        if (value.empty())
            preferences.remove(key);
        else
            preferences[key] = value;
        fs::create_directories(config_);
        std::ofstream(file) << preferences << '\n';
    } catch (const std::exception &error) {
        std::cerr << "notgroot: " << key << " not saved: " << error.what() << '\n';
    }
}

// The theme changes between frames: mid-frame, pushed colors would pop back as the old theme's.
void App::setTheme(const std::string &id, bool follow) {
    pendingTheme_ = id;
    pendingFollow_ = follow;
}

void App::applyPending() {
    if (!pendingTheme_.empty()) {
        const std::string id = std::move(pendingTheme_);
        pendingTheme_.clear();
        followNereus_ = pendingFollow_;
        const auto before = ui::currentThemeInfo();
        if (ui::applyTheme(id)) {
            savePreference("theme", followNereus_ ? "" : id);
            const auto &after = ui::currentThemeInfo();
            if (after.fontFamily != before.fontFamily || after.fontPoints != before.fontPoints ||
                after.fontRegular != before.fontRegular)
                pendingScale_ = uiScale_;
        }
    }
    if (pendingScale_ > 0) {
        uiScale_ = pendingScale_;
        pendingScale_ = 0;
        ui::setInterfaceScale(uiScale_);
        window_->loadFonts(uiScale_, ui::currentThemeInfo(), content_ / "fonts");
    }
}

// The mark (black strokes on clear) as an alpha texture, drawn tinted in the theme's ink.
unsigned App::logoTexture() {
    if (logo_)
        return logo_;
    std::vector<unsigned char> rgba;
    if (!readPng(content_ / "icons/notgroot_mark.png", logoWidth_, logoHeight_, rgba))
        return 0;
    for (std::size_t i = 0; i < rgba.size(); i += 4)
        rgba[i] = rgba[i + 1] = rgba[i + 2] = 255;
    glGenTextures(1, &logo_);
    glBindTexture(GL_TEXTURE_2D, logo_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, logoWidth_, logoHeight_, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    return logo_;
}

// Groot's node icons, white on clear and mipmapped so they stay smooth at every zoom; the canvas tints them.
void App::loadNodeIcons() {
    std::error_code error;
    for (const auto &entry : fs::directory_iterator(content_ / "icons/nodes", error)) {
        int width = 0, height = 0;
        std::vector<unsigned char> rgba;
        if (entry.path().extension() != ".png" || !readPng(entry.path(), width, height, rgba))
            continue;
        unsigned texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        icons_.push_back(texture);
        ui::setNodeIcon(entry.path().stem().string(), ImTextureID(texture));
    }
}

int App::run() {
    while (true) {
        // The window's close button asks first when trees have unsaved edits.
        if (window_->closing() && !closing_) {
            window_->cancelClose();
            quit();
        }
        if (window_->closing() && closing_)
            break;
        applyPending();
        if (!opt_.keys.empty() || !opt_.text.empty())
            replay();
        window_->beginFrame();
        session_.pinch = {window_->pinch().active(), window_->pinch().scale()};
        frame();
        ++frameCount_;
        const bool last = opt_.frames > 0 && frameCount_ >= opt_.frames;
        window_->present(last ? opt_.screenshot : fs::path());
        window_->swap();
        if (last)
            break;
    }
    return 0;
}

namespace {
// "Ctrl+Shift+P" -> the key and its modifiers (keys by ImGui's names: Down, Tab, F2, A…).
bool parseKey(const std::string &text, ImGuiKey &key, ImGuiKeyChord &mods) {
    mods = 0;
    std::string rest = text;
    for (const auto &[name, mod] : {std::pair<const char *, ImGuiKeyChord>{"Ctrl+", ImGuiMod_Ctrl},
                                    {"Shift+", ImGuiMod_Shift},
                                    {"Alt+", ImGuiMod_Alt}})
        while (rest.rfind(name, 0) == 0) {
            mods |= mod;
            rest = rest.substr(std::strlen(name));
        }
    const std::string arrow =
        rest == "Up" || rest == "Down" || rest == "Left" || rest == "Right" ? rest + "Arrow" : rest;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
        if (arrow == ImGui::GetKeyName(ImGuiKey(k))) {
            key = ImGuiKey(k);
            return true;
        }
    return false;
}
} // namespace

// Replays --keys (one press every few frames, after the layout settles; an item starting with ' types the rest),
// then --type.
void App::replay() {
    static std::vector<std::string> keys;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        std::string item;
        for (const char c : opt_.keys + ",")
            if (c == ',') {
                if (!item.empty())
                    keys.push_back(item);
                item.clear();
            } else
                item += c;
    }
    const int step = frameCount_ - 6;
    if (step < 0 || step % 3 != 0)
        return;
    auto &io = ImGui::GetIO();
    const std::size_t index = std::size_t(step / 3);
    if (index < keys.size() && keys[index][0] == '\'') {
        io.AddInputCharactersUTF8(keys[index].c_str() + 1);
    } else if (index < keys.size()) {
        ImGuiKey key = ImGuiKey_None;
        ImGuiKeyChord mods = 0;
        if (!parseKey(keys[index], key, mods)) {
            std::cerr << "notgroot: --keys: unknown key " << keys[index] << '\n';
            return;
        }
        for (const auto mod : {ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt})
            if (mods & mod)
                io.AddKeyEvent(ImGuiKey(mod), true);
        io.AddKeyEvent(key, true);
        pendingRelease_ = {key, mods};
    } else if (index == keys.size() && !opt_.text.empty())
        io.AddInputCharactersUTF8(opt_.text.c_str());
}

void App::frame() {
    // A replayed press lasts one frame.
    if (pendingRelease_.first != ImGuiKey_None) {
        auto &io = ImGui::GetIO();
        io.AddKeyEvent(pendingRelease_.first, false);
        for (const auto mod : {ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt})
            if (pendingRelease_.second & mod)
                io.AddKeyEvent(ImGuiKey(mod), false);
        pendingRelease_ = {ImGuiKey_None, 0};
    }
    // Files picked in the desktop dialog, and files changed on disk (checked once a second).
    if (auto picked = picker_.take())
        openWorkspace(*picked);
    if (ImGui::GetTime() - lastPoll_ > 1) {
        lastPoll_ = ImGui::GetTime();
        session_.pollDisk();
    }
    session_.pollCheck();
    session_.problems(); // keeps the cursor valid after undo / reload

    menuBar();
    statusBar();
    dockspace();
    const auto panel = [&](const char *name, void (*draw)(ui::Session &), ImGuiWindowFlags flags = 0) {
        if (ImGui::Begin(name, nullptr, flags))
            draw(session_);
        ImGui::End();
    };
    panel("Trees", ui::drawExplorer);
    panel("Palette", ui::drawPalette);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    const bool canvas =
        ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (canvas)
        ui::drawCanvas(session_);
    else
        session_.canvasFocused = false;
    ImGui::End();
    panel("Inspector", ui::drawInspector);
    panel("Variables", ui::drawVariables);
    panel("Problems", ui::drawProblems);

    ui::drawFinder(session_);
    ui::drawPicker(session_);
    ui::drawDialogs(session_);
    ui::drawShortcuts(session_);
    ui::drawTour(session_);
    aboutWindow();
    ui::handleShortcuts(session_);
    updateTitle();
}

// The arrangement on first start (or after View > Reset layout): trees and palette on the left, the canvas in
// the middle, inspector and variables on the right, problems along the bottom.
void App::defaultLayout(unsigned dock) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
    ImGuiID center = dock;
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, .17f, nullptr, &center);
    const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, .27f, nullptr, &center);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, .2f, nullptr, &center);
    ImGuiID rightTop = right;
    const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(rightTop, ImGuiDir_Down, .42f, nullptr, &rightTop);
    ImGui::DockBuilderDockWindow("Trees", left);
    ImGui::DockBuilderDockWindow("Palette", left);
    ImGui::DockBuilderDockWindow("Canvas", center);
    ImGui::DockBuilderDockWindow("Inspector", rightTop);
    ImGui::DockBuilderDockWindow("Variables", rightBottom);
    ImGui::DockBuilderDockWindow("Problems", bottom);
    ImGui::DockBuilderFinish(dock);
}

void App::dockspace() {
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImGuiID dock = ImGui::GetID("notgroot_dock");
    static bool reset = false;
    if (!ImGui::DockBuilderGetNode(dock) || ImGui::DockBuilderGetNode(dock)->IsLeafNode() || resetLayout_) {
        defaultLayout(dock);
        resetLayout_ = false;
        reset = true;
    }
    ImGui::DockSpaceOverViewport(dock, viewport);
    if (reset) { // the trees panel in front of the palette
        ImGui::SetWindowFocus("Trees");
        reset = false;
    }
}

void App::statusBar() {
    const float height = ImGui::GetFrameHeight();
    ui::beginSurface(ui::Surface::Board);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ui::palette().bar);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui::palette().bar);
    const bool shown = ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down, height,
                                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                                       ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleColor(2);
    if (!shown) {
        ImGui::End();
        ui::endSurface();
        return;
    }
    if (ImGui::BeginMenuBar()) {
        const bool fresh = ImGui::GetTime() - session_.statusTime < 8;
        if (fresh && !session_.statusText.empty())
            ImGui::TextColored(session_.statusError ? ui::palette().error : ui::palette().text, "%s",
                               session_.statusText.c_str());
        else
            ImGui::TextDisabled("Ctrl+P find · Ctrl+Shift+P commands · Tab add child · Enter edit · Space fold / "
                                "open subtree · F8 next problem");
        // Right side: problem counts and the layout direction.
        int errors = 0, warnings = 0;
        for (const auto &problem : session_.problems()) {
            errors += problem.severity == core::Severity::Error;
            warnings += problem.severity == core::Severity::Warning;
        }
        char counts[96];
        std::snprintf(counts, sizeof(counts), "%s%d errors  %d warnings  %s",
                      session_.checking() ? "checking with BehaviorTree.CPP…  " : "", errors, warnings,
                      session_.horizontal ? "left-right" : "top-down");
        const float width = ImGui::CalcTextSize(counts).x + ImGui::GetStyle().ItemSpacing.x * 2;
        ImGui::SameLine(ImGui::GetWindowWidth() - width);
        ImGui::TextColored(errors ? ui::palette().error : ui::palette().muted, "%s", counts);
        ImGui::EndMenuBar();
    }
    ImGui::End();
    ui::endSurface();
}

void App::menuBar() {
    // The bars are the theme's board (Nereus's menu and command bars), dropdowns included.
    ui::beginSurface(ui::Surface::Board);
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ui::palette().bar);
    const bool open = ImGui::BeginMainMenuBar();
    ImGui::PopStyleColor();
    if (!open) {
        ui::endSurface();
        return;
    }
    const auto commands = [&](const char *group) {
        for (const auto &command : session_.commands)
            if (command.group == group && !ui::Session::alias(command) &&
                ImGui::MenuItem(command.label.c_str(), ui::chordName(command.chord).c_str(), false,
                                !command.enabled || command.enabled()))
                command.run();
    };
    if (ImGui::BeginMenu("File")) {
        commands("File");
        if (ImGui::BeginMenu("Open recent", !recent_.empty())) {
            for (const auto &path : std::vector<std::string>(recent_))
                if (ImGui::MenuItem(path.c_str()))
                    openWorkspace(path);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    for (const char *group : {"Edit", "Select", "Node", "Tree"})
        if (ImGui::BeginMenu(group)) {
            commands(group);
            ImGui::EndMenu();
        }
    if (ImGui::BeginMenu("View")) {
        commands("View");
        ImGui::Separator();
        if (ImGui::BeginMenu("Theme")) {
            if (ImGui::MenuItem("Follow Nereus", nullptr, followNereus_)) {
                followNereus_ = true;
                savePreference("theme", "");
                setTheme(preferredTheme(), true);
            }
            ImGui::Separator();
            for (const auto &theme : ui::themes()) {
                if (ImGui::MenuItem(theme.label.c_str(), nullptr, theme.id == ui::currentTheme()))
                    setTheme(theme.id, false);
                if (ImGui::IsItemHovered() && !theme.description.empty())
                    ImGui::SetTooltip("%s", theme.description.c_str());
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Interface scale")) {
            for (float scale : {0.f, .8f, .9f, 1.f, 1.1f, 1.25f, 1.5f, 1.75f, 2.f}) {
                char label[32];
                if (scale == 0)
                    std::snprintf(label, sizeof(label), "Match the desktop");
                else
                    std::snprintf(label, sizeof(label), "%.0f %%", double(scale * 100));
                if (ImGui::MenuItem(label)) {
                    pendingScale_ = scale > 0 ? scale : window_->contentScale();
                    savePreference("interface_scale", scale > 0 ? std::to_string(scale) : "auto");
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Reset panel layout"))
            resetLayout_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        commands("Help");
        if (ImGui::MenuItem("About notgroot"))
            showAbout_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
    ui::endSurface();
}

void App::aboutWindow() {
    if (!showAbout_)
        return;
    ImGui::SetNextWindowSize(ui::ui(ImVec2(420, 0)), ImGuiCond_Appearing);
    if (ImGui::Begin("About notgroot", &showAbout_, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ui::dismissible(showAbout_);
        if (const unsigned texture = logoTexture()) {
            const float height = ui::ui(72), width = height * float(logoWidth_) / float(logoHeight_);
            ImGui::ImageWithBg(ImTextureID(texture), {width, height}, {0, 0}, {1, 1}, {0, 0, 0, 0}, ui::palette().text);
        }
        ImGui::PushFont(ui::typeRamp().number);
        ImGui::TextUnformatted("notgroot");
        ImGui::PopFont();
        ImGui::TextDisabled("version " NOTGROOT_VERSION);
        ui::emptyState("Anything but Groot: a keyboard-first editor for BehaviorTree.CPP 4 trees. Logo after Groot's "
                       "tree icon (MIT, Fundacio Eurecat).");
    }
    ImGui::End();
}
} // namespace notgroot::app
