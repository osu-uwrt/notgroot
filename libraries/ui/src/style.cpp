// Node and key colors.
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include <cmath>

namespace notgroot::ui {
namespace {
ImVec4 hex(unsigned rgb) {
    return {float((rgb >> 16) & 0xff) / 255.f, float((rgb >> 8) & 0xff) / 255.f, float(rgb & 0xff) / 255.f, 1};
}

float luminance(ImVec4 c) {
    return .2126f * c.x + .7152f * c.y + .0722f * c.z;
}

// FNV-1a: the same key gets the same hue in every session.
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
        color = hex(0x4CAF50);
        break;
    case core::Category::Condition:
        color = hex(0x2196F3);
        break;
    case core::Category::Control:
        color = hex(0xFF9800);
        break;
    case core::Category::Decorator:
        color = hex(0x9C27B0);
        break;
    case core::Category::SubTree:
        color = hex(0x00BCD4);
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
