/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>

using namespace view;
using namespace uitk::lvgl_cpp;

SelectMenuPage::SelectMenuPage(std::vector<MenuSection> sections) : _sections(std::move(sections))
{
    _pannel = std::make_unique<uitk::lvgl_cpp::Container>(lv_screen_active());
    _pannel->setSize(466, 466);
    _pannel->setBgColor(lv_color_black());
    _pannel->setPadding(0, 72, 0, 0);
    _pannel->setBorderWidth(0);
    _pannel->setRadius(0);
    _pannel->setScrollDir(LV_DIR_VER);
    _pannel->setScrollbarMode(LV_SCROLLBAR_MODE_ACTIVE);

    int cursor_y = 36;

    for (int i = 0; i < _sections.size(); ++i) {
        const auto& section = _sections[i];
        create_selection_label(cursor_y, section.title);
        cursor_y += 28 + 24;

        for (int j = 0; j < section.items.size(); ++j) {
            const auto& item = section.items[j];
            create_item_button(cursor_y, item, i, j);
            cursor_y += 119 + 21;
        }
        cursor_y += 5;
    }
    cursor_y += 20;
}

void SelectMenuPage::update()
{
    if (_pending_section_index >= 0 && _pending_item_index >= 0) {
        if (_pending_section_index < _sections.size()) {
            auto& section = _sections[_pending_section_index];
            if (_pending_item_index < section.items.size()) {
                auto& item = section.items[_pending_item_index];
                if (item.onClick) {
                    item.onClick();
                }
            }
        }
        _pending_section_index = -1;
        _pending_item_index    = -1;
    }
}

void SelectMenuPage::create_selection_label(int y, std::string_view text)
{
    auto label = std::make_unique<uitk::lvgl_cpp::Label>(*_pannel);
    label->setText(text);
    label->setTextFont(&MontserratSemiBold26);
    label->setTextColor(lv_color_hex(0xFFFFFF));
    label->align(LV_ALIGN_TOP_MID, 0, y);
    _labels.push_back(std::move(label));
}

void SelectMenuPage::create_item_button(int y, const MenuItem& item, int section_idx, int item_idx)
{
    auto btn = std::make_unique<uitk::lvgl_cpp::Button>(*_pannel);
    btn->setSize(374, 119);
    btn->align(LV_ALIGN_TOP_MID, 0, y);
    btn->setBgColor(lv_color_hex(0x4C4C4C));
    btn->setBorderWidth(0);
    btn->setShadowWidth(0);
    btn->setRadius(60);

    btn->label().setText(item.label);
    btn->label().setTextFont(&lv_font_montserrat_28);
    btn->label().setTextColor(lv_color_hex(0xFFFFFF));
    btn->label().align(LV_ALIGN_CENTER, 0, 0);
    btn->label().setWidth(294);
    btn->label().setTextAlign(LV_TEXT_ALIGN_CENTER);
    btn->label().setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

    if (item.onClick) {
        btn->onClick().connect([this, section_idx, item_idx]() {
            _pending_section_index = section_idx;
            _pending_item_index    = item_idx;
        });
    }
    _buttons.push_back(std::move(btn));
}
