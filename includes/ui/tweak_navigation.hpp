#pragma once

#include "optimizer.hpp"

#include <string>
#include <utility>
#include <vector>

struct CarouselLayout {
    std::string text;
    std::vector<std::pair<int, int>> tab_columns;
};

inline CarouselLayout make_carousel(const std::vector<std::string>& labels,
                                    int content_column = 4)
{
    CarouselLayout result{"TWEAKS  ", {}};
    for (size_t index = 0; index < labels.size(); ++index) {
        if (index)
            result.text += " ";
        const int start = content_column + static_cast<int>(result.text.size());
        const std::string tab = " " + labels[index] + " ";
        result.text += tab;
        result.tab_columns.emplace_back(start, start + static_cast<int>(tab.size()));
    }
    return result;
}

inline size_t move_carousel(size_t current, int direction, size_t count)
{
    if (!count)
        return 0;
    if (direction > 0)
        return (current + 1) % count;
    if (direction < 0)
        return (current + count - 1) % count;
    return current;
}

inline size_t carousel_tab_at(const CarouselLayout& carousel, int column)
{
    for (size_t tab = 0; tab < carousel.tab_columns.size(); ++tab) {
        const auto range = carousel.tab_columns[tab];
        if (column >= range.first && column < range.second)
            return tab;
    }
    return carousel.tab_columns.size();
}

inline std::vector<size_t> tweaks_in_category(
    const std::vector<Optimizer::TweakSetting>& settings,
    const std::string& category)
{
    std::vector<size_t> indices;
    for (size_t index = 0; index < settings.size(); ++index)
        if (settings[index].category == category)
            indices.push_back(index);
    return indices;
}

inline size_t move_tweak_cursor(size_t current, int direction, size_t count)
{
    if (count == 0)
        return 0;
    if (direction > 0 && current + 1 < count)
        return current + 1;
    if (direction < 0 && current > 0)
        return current - 1;
    return current < count ? current : count - 1;
}

inline size_t scroll_to_cursor(size_t scroll, size_t cursor, size_t visible)
{
    if (visible == 0)
        visible = 1;
    if (cursor < scroll)
        return cursor;
    if (cursor >= scroll + visible)
        return cursor - visible + 1;
    return scroll;
}
