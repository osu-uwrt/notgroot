// Node and key colors.
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace notgroot::ui {
namespace {
ImVec4 hex(unsigned rgb) {
    return {float((rgb >> 16) & 0xff) / 255.f, float((rgb >> 8) & 0xff) / 255.f, float(rgb & 0xff) / 255.f, 1};
}

float luminance(ImVec4 c) {
    return .2126f * c.x + .7152f * c.y + .0722f * c.z;
}

// FNV-1a: the same key gets the same hue in every session.
std::vector<ImFont *> titleFonts, bodyFonts;
std::map<std::string, ImTextureID> icons;

std::size_t levelOf(float zoom) {
    const auto &levels = zoomLevels();
    std::size_t best = 0;
    for (std::size_t i = 1; i < levels.size(); ++i)
        if (std::abs(levels[i] - zoom) < std::abs(levels[best] - zoom))
            best = i;
    return best;
}

unsigned hash(const std::string &text) {
    unsigned h = 2166136261u;
    for (const char c : text)
        h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
    return h;
}
} // namespace

bool darkTheme() {
    return luminance(ImGui::GetStyle().Colors[ImGuiCol_WindowBg]) < .4f;
}

ImVec4 withAlpha(ImVec4 c, float a) {
    return {c.x, c.y, c.z, a};
}

ImVec4 mix(ImVec4 a, ImVec4 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

ImVec4 categoryColor(core::Category category) {
    ImVec4 color;
    switch (category) {
    case core::Category::Action:
        color = hex(0x90A4AE);
        break;
    case core::Category::Condition:
        color = hex(0x26A69A);
        break;
    case core::Category::Control:
        color = hex(0xFF9800);
        break;
    case core::Category::Decorator:
        color = hex(0x9C27B0);
        break;
    case core::Category::SubTree:
        color = hex(0xA1887F);
        break;
    case core::Category::Unknown:
        color = hex(0x757575);
        break;
    }
    // Deeper on light paper so the bands keep their weight next to dark text.
    return darkTheme() ? color : mix(color, {0, 0, 0, 1}, .12f);
}

ImVec4 rootColor() {
    return darkTheme() ? hex(0xF44336) : hex(0xD32F2F);
}

ImVec4 subtreeColor(const std::string &text) {
    if (text.size() != 7 || text[0] != '#')
        return categoryColor(core::Category::SubTree);
    unsigned rgb = 0;
    try {
        rgb = unsigned(std::stoul(text.substr(1), nullptr, 16));
    } catch (const std::exception &) {
        return categoryColor(core::Category::SubTree);
    }
    return hex(rgb);
}

Accent nodeAccent(const std::string &id, core::Category category) {
    struct Style {
        const char *id;
        unsigned rgb; // 0: no accent color
        const char *icon;
    };
    // Groot's caption colors and icons; BT.CPP 4 names (SequenceStar is SequenceWithMemory).
    static const Style styles[] = {
        {"Sequence", 0xff99bb, "sequence"},
        {"SequenceWithMemory", 0xffaacc, "sequence_star"},
        {"ReactiveSequence", 0xff77aa, "sequence_reactive"},
        {"Fallback", 0x77ccff, "fallback"},
        {"ReactiveFallback", 0x44aadd, "fallback_reactive"},
        {"AlwaysSuccess", 0x00ff00, nullptr},
        {"ForceSuccess", 0x00ff00, nullptr},
        {"AlwaysFailure", 0xff2222, nullptr},
        {"ForceFailure", 0xff2222, nullptr},
        {"Inverter", 0xffee77, "not_equal"},
        {"Repeat", 0xffee77, "repeat"},
        {"RetryUntilSuccessful", 0xffee77, "retry"},
        {"Timeout", 0xffee77, "timeout"},
        {"SetBlackboard", 0, "edit_list"},
        {"SetBlackboardV3", 0, "edit_list"},
    };
    Accent accent;
    for (const auto &style : styles)
        if (id == style.id) {
            accent.icon = style.icon;
            if (style.rgb)
                accent.color = hex(style.rgb), accent.colored = true;
            return accent;
        }
    // The rest by category: conditions green, decorators yellow, calls the subtree icon.
    if (category == core::Category::Condition)
        accent.color = hex(0x44bb44), accent.colored = true;
    else if (category == core::Category::Decorator)
        accent.color = hex(0xffee77), accent.colored = true;
    else if (category == core::Category::SubTree)
        accent.icon = "subtree";
    return accent;
}

ImVec4 readableOn(ImVec4 color, ImVec4 paper) {
    const bool lighten = luminance(paper) < .4f;
    for (int i = 0; i < 8 && contrastRatio(color, paper) < 4.5f; ++i)
        color = mix(color, lighten ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1), .2f);
    return color;
}

void setNodeIcon(const std::string &name, ImTextureID texture) {
    icons[name] = texture;
}

ImTextureID nodeIcon(const char *name) {
    const auto found = name ? icons.find(name) : icons.end();
    return found == icons.end() ? ImTextureID() : found->second;
}

ImVec4 inkOnCategory(core::Category category) {
    return inkOn(categoryColor(category), {1, 1, 1, 1}, 3.f);
}

ImVec4 keyColor(const std::string &key) {
    const float hue = float(hash(key) % 360) / 360.f;
    float r = 0, g = 0, b = 0;
    // Bright on dark paper, deep on light; nudged until it reads on the card body.
    const bool dark = darkTheme();
    ImGui::ColorConvertHSVtoRGB(hue, dark ? .5f : .75f, dark ? .95f : .55f, r, g, b);
    ImVec4 color{r, g, b, 1};
    const ImVec4 paper = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    for (int i = 0; i < 6 && contrastRatio(color, paper) < 4.5f; ++i)
        color = mix(color, dark ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1), .2f);
    return color;
}

ImVec4 directionColor(core::PortDirection direction) {
    const bool dark = darkTheme();
    switch (direction) {
    case core::PortDirection::Output:
        return dark ? hex(0xFFB74D) : hex(0xB45309);
    case core::PortDirection::InOut:
        return palette().accent;
    case core::PortDirection::Input:
        break;
    }
    return dark ? hex(0x81C784) : hex(0x2E7D32);
}

const char *directionBadge(core::PortDirection direction) {
    switch (direction) {
    case core::PortDirection::Output:
        return "OUT";
    case core::PortDirection::InOut:
        return "IO";
    case core::PortDirection::Input:
        break;
    }
    return "IN";
}

const std::vector<float> &zoomLevels() {
    static const std::vector<float> levels{.3f, .4f, .5f, .6f, .7f, .8f, .9f, 1, 1.15f, 1.3f, 1.5f, 1.75f, 2, 2.5f};
    return levels;
}

float nearestZoom(float zoom) {
    return zoomLevels()[levelOf(zoom)];
}

float stepZoom(float zoom, int steps) {
    const auto &levels = zoomLevels();
    const long at = std::clamp(long(levelOf(zoom)) + steps, 0l, long(levels.size()) - 1);
    return levels[std::size_t(at)];
}

void setCanvasFonts(std::vector<ImFont *> titles, std::vector<ImFont *> bodies) {
    titleFonts = std::move(titles);
    bodyFonts = std::move(bodies);
}

ImFont *canvasTitleFont(float zoom) {
    const auto at = levelOf(zoom);
    return at < titleFonts.size() ? titleFonts[at] : nullptr;
}

ImFont *canvasBodyFont(float zoom) {
    const auto at = levelOf(zoom);
    return at < bodyFonts.size() ? bodyFonts[at] : nullptr;
}

ImVec4 severityColor(int severity) {
    switch (severity) {
    case 0:
        return palette().error;
    case 1:
        return palette().warn;
    default:
        return palette().muted;
    }
}
} // namespace notgroot::ui
