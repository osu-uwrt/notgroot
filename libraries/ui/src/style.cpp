// Node and key colors.
#include "notgroot/ui/style.hpp"
#include "notgroot/ui/theme.hpp"
#include <algorithm>
#include <array>
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

void dismissible(bool &open) {
    if (ImGui::IsWindowAppearing())
        return; // the click or key that opened it
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        open = false;
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (clicked &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        open = false;
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

namespace {
std::array<float, 3> lab(ImVec4 c) {
    const auto linear = [](float v) { return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f); };
    const float r = linear(c.x), g = linear(c.y), b = linear(c.z);
    // sRGB to XYZ (D65), then to Lab.
    const float x = (.4124f * r + .3576f * g + .1805f * b) / .95047f, y = .2126f * r + .7152f * g + .0722f * b,
                z = (.0193f * r + .1192f * g + .9505f * b) / 1.08883f;
    const auto f = [](float t) { return t > .008856f ? std::cbrt(t) : 7.787f * t + 16.f / 116; };
    return {116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))};
}

// CIE94 (textiles: lightness counts half, so hue sets keys apart first); symmetric, on the mean chroma.
float cie94(const std::array<float, 3> &p, const std::array<float, 3> &q) {
    const float c1 = std::hypot(p[1], p[2]), c2 = std::hypot(q[1], q[2]), c = (c1 + c2) / 2;
    const float dl = p[0] - q[0], dc = c1 - c2, da = p[1] - q[1], db = p[2] - q[2];
    const float dh2 = std::max(0.f, da * da + db * db - dc * dc);
    const float sc = 1 + .048f * c, sh = 1 + .014f * c;
    return std::sqrt((dl / 2) * (dl / 2) + (dc / sc) * (dc / sc) + dh2 / (sh * sh));
}

// Readable key colors for the theme: every 5° of hue at a few strengths, each nudged until it reads on the paper.
const std::vector<std::pair<float, ImVec4>> &keyCandidates() {
    static std::vector<std::pair<float, ImVec4>> candidates;
    static std::string theme;
    static float paperLight = -1;
    const ImVec4 paper = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    if (theme == currentTheme() && paperLight == paper.x + paper.y + paper.z)
        return candidates;
    theme = currentTheme();
    paperLight = paper.x + paper.y + paper.z;
    candidates.clear();
    const bool dark = darkTheme();
    // Strengths of each hue; then only colors that read as a color (not as gray text) and sit in a lightness band
    // that reads on the paper.
    const float tiers[][2] = {{.55f, 1.f}, {.8f, 1.f}, {.45f, .9f}, {.75f, .85f}, {.95f, .95f}};
    const float lightTiers[][2] = {{.75f, .55f}, {1.f, .45f}, {.6f, .45f}, {1.f, .7f}, {.85f, .35f}};
    for (int tier = 0; tier < 5; ++tier)
        for (int degrees = 0; degrees < 360; degrees += 4) {
            float r = 0, g = 0, b = 0;
            const auto &sv = dark ? tiers[tier] : lightTiers[tier];
            ImGui::ColorConvertHSVtoRGB(float(degrees) / 360, sv[0], sv[1], r, g, b);
            ImVec4 color{r, g, b, 1};
            for (int i = 0; i < 6 && contrastRatio(color, paper) < 4.5f; ++i)
                color = mix(color, dark ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1), .2f);
            const auto l = lab(color);
            const bool lit = dark ? l[0] >= 55 && l[0] <= 88 : l[0] >= 25 && l[0] <= 55;
            if (std::hypot(l[1], l[2]) >= 30 && lit && contrastRatio(color, paper) >= 4.5f)
                candidates.push_back({float(degrees + tier * 1000), color});
        }
    return candidates;
}
} // namespace

float colorDistance(ImVec4 a, ImVec4 b) {
    return cie94(lab(a), lab(b));
}

std::map<std::string, ImVec4> keyColors(std::vector<std::string> keys) {
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    const auto &candidates = keyCandidates();
    std::vector<std::array<float, 3>> labs;
    for (const auto &[tag, color] : candidates)
        labs.push_back(lab(color));
    std::vector<bool> taken(candidates.size(), false);
    std::vector<std::size_t> chosen;
    std::map<std::string, ImVec4> colors;
    for (const auto &key : keys) {
        const float preferred = float(hash(key) % 360);
        // How far each candidate is from the colors already given. The farthest set the bar; among the candidates
        // close to it (or far enough anyway), the one nearest the hue the name picks.
        std::vector<float> apart(candidates.size(), 1e9f);
        float farthest = 0;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (taken[i])
                continue;
            for (const auto j : chosen)
                apart[i] = std::min(apart[i], cie94(labs[i], labs[j]));
            farthest = std::max(farthest, apart[i]);
        }
        const float bar = farthest * .98f;
        std::size_t best = 0;
        float bestNear = 1e9f;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (taken[i] || apart[i] < bar)
                continue;
            const float hue = std::fmod(candidates[i].first, 1000.f);
            const float near = std::min(std::abs(hue - preferred), 360 - std::abs(hue - preferred)) +
                               (candidates[i].first >= 1000 ? 30 : 0); // the first strength first
            if (near < bestNear) {
                best = i;
                bestNear = near;
            }
        }
        taken[best] = true;
        chosen.push_back(best);
    }

    // Then each in turn moves to the free color farthest from all the others, a few rounds: picking one at a time
    // leaves the first ones crowded by the later ones.
    const auto distance = [&](std::size_t i, std::size_t j) { return cie94(labs[i], labs[j]); };
    for (int round = 0; round < 4 && chosen.size() > 1; ++round)
        for (std::size_t k = 0; k < chosen.size(); ++k) {
            const auto nearest = [&](std::size_t i) {
                float least = 1e9f;
                for (std::size_t other = 0; other < chosen.size(); ++other)
                    if (other != k)
                        least = std::min(least, distance(i, chosen[other]));
                return least;
            };
            std::size_t best = chosen[k];
            float bestApart = nearest(best);
            for (std::size_t i = 0; i < candidates.size(); ++i)
                if (!taken[i] && nearest(i) > bestApart + .5f) {
                    best = i;
                    bestApart = nearest(i);
                }
            taken[chosen[k]] = false;
            taken[best] = true;
            chosen[k] = best;
        }
    for (std::size_t k = 0; k < keys.size(); ++k)
        colors[keys[k]] = candidates[chosen[k]].second;
    return colors;
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
