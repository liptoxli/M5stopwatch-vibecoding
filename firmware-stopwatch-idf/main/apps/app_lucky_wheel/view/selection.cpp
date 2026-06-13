/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>

using namespace view;
using namespace uitk::lvgl_cpp;

namespace {

constexpr int _panel_size      = 466;
constexpr int _selector_width  = 374;
constexpr int _selector_height = 200;
constexpr int _ok_width        = 374;
constexpr int _ok_height       = 130;

constexpr uint32_t _bg_color                = 0x3B3B3B;
constexpr uint32_t _selector_color          = 0x4C4C4C;
constexpr uint32_t _selector_selected_color = 0x737373;
constexpr uint32_t _title_color             = 0xFFFFFF;
constexpr uint32_t _selector_text_color     = 0xFFFFFF;
constexpr uint32_t _ok_color                = 0x4AD78C;
constexpr uint32_t _ok_text_color           = 0x0F5831;

}  // namespace

void SelectionView::init(lv_obj_t* parent)
{
    _panel = std::make_unique<Container>(parent);
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setSize(_panel_size, _panel_size);
    _panel->setRadius(0);
    _panel->setBorderWidth(0);
    _panel->setPaddingAll(0);
    _panel->setBgColor(lv_color_hex(_bg_color));
    _panel->setBgOpa(LV_OPA_COVER);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _title_label = std::make_unique<Label>(_panel->get());
    _title_label->align(LV_ALIGN_TOP_MID, 0, 75);
    _title_label->setText("Number of Options");
    _title_label->setTextFont(&MontserratSemiBold26);
    _title_label->setTextColor(lv_color_hex(_title_color));

    _selector = std::make_unique<Roller>(_panel->get());
    _selector->align(LV_ALIGN_CENTER, 0, -13);
    _selector->setSize(_selector_width, _selector_height);
    _selector->setOptions("2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18", LV_ROLLER_MODE_NORMAL);
    _selector->setVisibleRowCount(5);
    _selector->setSelected(0, LV_ANIM_OFF);
    lv_obj_set_style_radius(_selector->get(), 52, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_selector->get(), lv_color_hex(_selector_color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_selector->get(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_selector->get(), 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(_selector->get(), &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(_selector->get(), lv_color_hex(_selector_text_color), LV_PART_MAIN);
    lv_obj_set_style_text_align(_selector->get(), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(_selector->get(), 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_selector->get(), lv_color_hex(_selector_selected_color), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(_selector->get(), LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_font(_selector->get(), &lv_font_montserrat_28, LV_PART_SELECTED);
    lv_obj_set_style_text_color(_selector->get(), lv_color_hex(_selector_text_color), LV_PART_SELECTED);
    lv_obj_set_style_text_align(_selector->get(), LV_TEXT_ALIGN_CENTER, LV_PART_SELECTED);
    lv_obj_set_style_border_width(_selector->get(), 0, LV_PART_SELECTED);

    _ok_button = std::make_unique<Button>(_panel->get());
    _ok_button->align(LV_ALIGN_CENTER, 0, 175);
    _ok_button->setSize(_ok_width, _ok_height);
    _ok_button->setRadius(77);
    _ok_button->setBorderWidth(0);
    _ok_button->setShadowWidth(0);
    _ok_button->setBgColor(lv_color_hex(_ok_color));
    _ok_button->label().setText("OK");
    _ok_button->label().setTextFont(&lv_font_montserrat_28);
    _ok_button->label().setTextColor(lv_color_hex(_ok_text_color));
    _ok_button->label().align(LV_ALIGN_CENTER, 0, 0);
    _ok_button->onClick().connect([this]() {
        _confirmed_option_count = selectedOptionCount();
        _is_confirmed           = true;
    });
}

int SelectionView::selectedOptionCount() const
{
    if (_selector == nullptr) {
        return 2;
    }

    return static_cast<int>(_selector->getSelected()) + 2;
}