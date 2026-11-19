/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_launch_page.h"

#include "animation/launcher_carousel_layout.h"
#include "animation/ui_launcher_animation.h"
#include "cp0_enum_cast.h"
#include "launcher_platform.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>

namespace {

using launcher_carousel_layout::Slot;
using launcher_carousel_layout::kSlots;

enum class HomeSliderMetric : lv_coord_t {
    Width = 160,
    Height = 8,
    OffsetY = 65,
};

static_assert(UILaunchPage::kPageSlider == launcher_carousel_layout::kElementCount);
static_assert(UILaunchPage::kLauncherCarouselElementCount ==
              launcher_carousel_layout::kElementCount + 1);

// LVGL's software scaler limits the transformed image to
// ((source_size - 1) * scale) >> 8.  The usual floor(source/target) scale
// can therefore leave the final destination row/column untouched.  Round up
// against the last source pixel; the clip object trims the resulting edge.
void compensate_image_scale(lv_obj_t *image)
{
    if (!image) return;
    lv_obj_update_layout(image);
    const int32_t source_width = lv_image_get_src_width(image);
    const int32_t source_height = lv_image_get_src_height(image);
    const int32_t target_width = lv_obj_get_width(image);
    const int32_t target_height = lv_obj_get_height(image);
    if (source_width <= 1 || source_height <= 1 || target_width <= 1 || target_height <= 1)
        return;

    constexpr uint32_t scale_unit = LV_SCALE_NONE;
    const uint32_t scale_x = static_cast<uint32_t>(
        (static_cast<uint64_t>(target_width - 1) * scale_unit + source_width - 2) /
        (source_width - 1));
    const uint32_t scale_y = static_cast<uint32_t>(
        (static_cast<uint64_t>(target_height - 1) * scale_unit + source_height - 2) /
        (source_height - 1));
    // Keep manual compensation centered when the image grows during a
    // carousel transition by explicitly fixing the source pivot.
    lv_image_set_pivot(image, source_width / 2, source_height / 2);
    lv_image_set_scale_x(image, scale_x);
    lv_image_set_scale_y(image, scale_y);
}

lv_obj_t *create_page_slider(lv_obj_t *parent, size_t page_count, size_t selected_page)
{
    lv_obj_t *slider = lv_slider_create(parent);
    if (!slider) return nullptr;

    lv_obj_remove_style_all(slider);
    lv_obj_set_size(
        slider,
        CP0_ENUM_CAST(lv_coord_t, HomeSliderMetric::Width),
        CP0_ENUM_CAST(lv_coord_t, HomeSliderMetric::Height));
    lv_obj_set_pos(
        slider, 0, CP0_ENUM_CAST(lv_coord_t, HomeSliderMetric::OffsetY));
    lv_obj_set_align(slider, LV_ALIGN_CENTER);
    lv_obj_clear_flag(
        slider, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                           LV_OBJ_FLAG_CLICK_FOCUSABLE |
                                           LV_OBJ_FLAG_SCROLLABLE |
                                           LV_OBJ_FLAG_SCROLL_ON_FOCUS));

    lv_obj_set_style_bg_color(slider, lv_color_hex(0x212021), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_MAIN);
    // Keep the 8px knob inside the 160px track at both end values. The
    // main background remains dark across the full slider width.
    lv_obj_set_style_pad_left(slider, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_right(slider, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x212021), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xF2C94C), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
    const size_t max_page = page_count > 0 ? page_count - 1 : 0;
    lv_slider_set_range(slider, 0, static_cast<int32_t>(max_page));
    lv_slider_set_value(slider, static_cast<int32_t>(selected_page), LV_ANIM_OFF);
    return slider;
}

lv_obj_t *create_carousel_card(lv_obj_t *parent, const Slot &slot,
                               lv_coord_t radius, uint32_t border_color,
                               bool clickable)
{
    lv_obj_t *card = lv_obj_create(parent);
    if (!card) return nullptr;
    lv_obj_set_size(card, slot.width, slot.height);
    lv_obj_set_pos(card, slot.x, slot.y);
    lv_obj_set_align(card, LV_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    if (!clickable)
        lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
    if (slot.hidden)
        lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(card, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(border_color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_post(card, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    return card;
}

lv_obj_t *create_carousel_title(lv_obj_t *parent, const Slot &slot,
                                lv_coord_t width, const char *text)
{
    lv_obj_t *title = lv_label_create(parent);
    if (!title) return nullptr;
    lv_obj_set_width(title, width);
    lv_obj_set_height(title, LV_SIZE_CONTENT);
    lv_obj_set_pos(title, slot.x, slot.y);
    lv_obj_set_align(title, LV_ALIGN_CENTER);
    lv_label_set_text(title, text);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(
        title,
        launcher_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    // Font first: DOT needs a one-line height, which comes from the font.
    launcher_home_animation::apply_title_overflow(title, false);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(title, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (slot.hidden)
        lv_obj_add_flag(title, LV_OBJ_FLAG_HIDDEN);
    return title;
}

lv_obj_t *create_carousel_arrow(lv_obj_t *parent, lv_coord_t x, const char *asset)
{
    lv_obj_t *button = lv_btn_create(parent);
    if (!button) return nullptr;
    lv_obj_set_size(button, 17, 23);
    lv_obj_set_pos(button, x, -4);
    lv_obj_set_align(button, LV_ALIGN_CENTER);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(button, launcher_platform::path_c(asset), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(button, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    return button;
}

} // namespace

void UILaunchPage::set_page_slider_range(size_t page_count, size_t selected_page)
{
    lv_obj_t *slider = carousel_elements_[kPageSlider];
    if (!slider) return;
    const size_t max_page = page_count > 0 ? page_count - 1 : 0;
    lv_slider_set_range(slider, 0, static_cast<int32_t>(max_page));
    set_page_slider_position(selected_page);
}

void UILaunchPage::set_page_slider_position(size_t page)
{
    lv_obj_t *slider = carousel_elements_[kPageSlider];
    if (!slider) return;
    const int32_t max_page = lv_slider_get_max_value(slider);
    const int32_t value = page > static_cast<size_t>(max_page)
        ? max_page : static_cast<int32_t>(page);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
}

void UILaunchPage::set_carousel_element_clickable(size_t element, bool clickable)
{
    if (element >= carousel_elements_.size() || !carousel_elements_[element]) return;
    if (clickable)
        lv_obj_add_flag(carousel_elements_[element], LV_OBJ_FLAG_CLICKABLE);
    else
        lv_obj_clear_flag(carousel_elements_[element], LV_OBJ_FLAG_CLICKABLE);
}

void UILaunchPage::set_panel_icon(lv_obj_t *panel, const std::string &src)
{
    if (!panel)
        return;

    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *clip = lv_obj_get_child(panel, 0);
    lv_obj_t *image = clip ? lv_obj_get_child(clip, 0) : nullptr;
    if (src.empty()) {
        if (image && lv_obj_check_type(image, &lv_image_class))
            lv_image_set_src(image, nullptr);
        return;
    }
    if (!clip || !image || !lv_obj_check_type(image, &lv_image_class)) {
        clip = lv_obj_create(panel);
        if (!clip) return;
        lv_obj_set_size(clip, LV_PCT(100), LV_PCT(100));
        lv_obj_set_align(clip, LV_ALIGN_CENTER);
        lv_obj_clear_flag(clip, LV_OBJ_FLAG_SCROLLABLE);
        image = lv_image_create(clip);
        if (!image) return;
        lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
        lv_obj_set_align(image, LV_ALIGN_CENTER);
        // The pool already provides a buffer sized for the card content. Use
        // CENTER so the scale is supplied by compensate_image_scale rather
        // than LVGL's floor-based STRETCH transform.
        lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CENTER);
    }

    // Clip the image in a child container. LVGL applies clip_corner to an
    // object's children, not to the object's own draw operation.
    const auto selector = LV_PART_MAIN | LV_STATE_DEFAULT;
    const lv_coord_t card_radius = lv_obj_get_style_radius(panel, LV_PART_MAIN);
    const lv_coord_t card_border = lv_obj_get_style_border_width(panel, LV_PART_MAIN);
    lv_obj_set_style_radius(clip, std::max<lv_coord_t>(0, card_radius - card_border), selector);
    lv_obj_set_style_clip_corner(clip, true, selector);
    lv_obj_set_style_bg_opa(clip, LV_OPA_TRANSP, selector);
    lv_obj_set_style_border_width(clip, 0, selector);
    lv_obj_set_style_pad_all(clip, 0, selector);

    // Match the decoded buffer to the card content size. The far slots are
    // hidden while they enter the carousel, so use the side-card size for
    // them and avoid a second transparent-edge resample when they appear.
    const lv_coord_t source_card_size = lv_obj_get_width(panel) >= 100 ? 100 : 80;
    const uint32_t image_size = static_cast<uint32_t>(std::max<lv_coord_t>(
        1, source_card_size - 2 * card_border));
    lv_image_set_src(image, home_icon_pool_.find(src, image_size));
    compensate_image_scale(image);
}

void UILaunchPage::update_carousel_slot(size_t slot, const char *title, const std::string &icon)
{
    update_carousel_item(panel(slot), label(slot), title, icon);
}

void UILaunchPage::update_carousel_item(lv_obj_t *panel, lv_obj_t *label,
                                        const char *title, const std::string &icon)
{
    if (label) {
        const char *next = title ? title : "";
        const char *current = lv_label_get_text(label);
        // DOT mode rewrites the widget text with "...". Only restart the
        // selected bounce when the source name itself changed.
        if (!current || std::strcmp(current, next) != 0)
            lv_label_set_text(label, next);
    }
    set_panel_icon(panel, icon);
}

static std::string layout_pref_path()
{
    const char *home = std::getenv("HOME");
    std::string base = (home && home[0]) ? home : "/home/dead";
    return base + "/.config/applaunch/home_layout";
}

void UILaunchPage::load_home_layout_pref()
{
    if (layout_pref_loaded_) return;
    layout_pref_loaded_ = true;
    std::ifstream in(layout_pref_path());
    std::string val;
    if (in >> val) {
        grid_mode_ = (val != "carousel");
    }
}

void UILaunchPage::save_home_layout_pref() const
{
    const char *home = std::getenv("HOME");
    std::string dir = std::string((home && home[0]) ? home : "/home/dead") + "/.config/applaunch";
    mkdir(dir.c_str(), 0755);
    std::ofstream out(layout_pref_path(), std::ios::trunc);
    if (out) {
        out << (grid_mode_ ? "grid" : "carousel") << "\n";
    }
}

void UILaunchPage::toggle_home_layout()
{
    grid_mode_ = !grid_mode_;
    save_home_layout_pref();
    create_app_container(content_container());
    refresh_carousel();
}

void UILaunchPage::create_screen()
{
    load_home_layout_pref();
    if (grid_mode_) {
        if (!carousel_container_)
            create_app_container(content_container());
    } else {
        const bool complete = carousel_container_ && left_arrow_button_ && right_arrow_button_ &&
            std::all_of(carousel_elements_.begin(), carousel_elements_.end(),
                        [](lv_obj_t *element) { return element != nullptr; });
        if (!complete)
            create_app_container(content_container());
    }
}

void UILaunchPage::create_app_container(lv_obj_t *parent)
{
    if (!parent)
        return;
    load_home_layout_pref();
    if (grid_mode_)
        create_grid_container(parent);
    else
        create_carousel_container(parent);
}

void UILaunchPage::create_grid_container(lv_obj_t *parent)
{

    lv_obj_t *container = lv_obj_create(parent);
    if (!container) return;
    lv_obj_set_size(container, 320, 150);
    lv_obj_set_align(container, LV_ALIGN_CENTER);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(container, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(container, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(container, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(container, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_column(container, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(container, 6, LV_PART_MAIN);
    lv_obj_set_style_width(container, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(container, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_top(container, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_bottom(container, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x00F0FF), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(container, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(container, 2, LV_PART_SCROLLBAR);
    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    *carousel_alive_ = false;
    if (carousel_container_) lv_obj_delete(carousel_container_);
    carousel_alive_ = std::make_shared<bool>(true);
    carousel_container_ = container;
    carousel_elements_.fill(nullptr);
    left_arrow_button_ = nullptr;
    right_arrow_button_ = nullptr;
    grid_tiles_.clear();

    lv_obj_add_event_cb(carousel_container_, on_owned_obj_deleted, LV_EVENT_DELETE, this);
    if (!home_key_registered_ && screen()) {
        lv_obj_add_event_cb(screen(), on_home_key, (lv_event_code_t)LV_EVENT_KEYBOARD, this);
        home_key_registered_ = true;
    }

    rebuild_grid();
}

void UILaunchPage::create_carousel_container(lv_obj_t *parent)
{
    if (!parent)
        return;

    lv_obj_t *container = lv_obj_create(parent);
    if (!container) return;
    lv_obj_set_size(container, 320, 150);
    lv_obj_set_align(container, LV_ALIGN_CENTER);
    lv_obj_clear_flag(container, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);

    std::array<lv_obj_t *, kLauncherCarouselElementCount> elements = {};

    const size_t page_count = launch_ ? launch_->app_count() : 0;
    const size_t selected_page = launch_ ? launch_->current_app_index() : 0;
    elements[kPageSlider] = create_page_slider(container, page_count, selected_page);

    elements[kTitleCenter] = create_carousel_title(container, kSlots[kTitleCenter], 100, "CLI");
    elements[kTitleRight] = create_carousel_title(container, kSlots[kTitleRight], 80, "GAME");
    elements[kTitleLeft] = create_carousel_title(container, kSlots[kTitleLeft], 80, "STORE");

    elements[kCardLeft] = create_carousel_card(
        container, kSlots[kCardLeft], launcher_carousel_layout::kSideCardRadius,
        0x222222, false);
    elements[kCardCenter] = create_carousel_card(
        container, kSlots[kCardCenter], launcher_carousel_layout::kCenterCardRadius,
        0x444444, true);
    elements[kCardRight] = create_carousel_card(
        container, kSlots[kCardRight], launcher_carousel_layout::kSideCardRadius,
        0x222222, false);
    elements[kCardFarRight] = create_carousel_card(
        container, kSlots[kCardFarRight], launcher_carousel_layout::kSideCardRadius,
        0x333333, false);
    elements[kCardFarLeft] = create_carousel_card(
        container, kSlots[kCardFarLeft], launcher_carousel_layout::kSideCardRadius,
        0x333333, false);

    lv_obj_t *left_arrow = create_carousel_arrow(container, -151, "carousel_left_arrow.png");
    lv_obj_t *right_arrow = create_carousel_arrow(container, 150, "carousel_right_arrow.png");

    elements[kTitleFarLeft] = create_carousel_title(container, kSlots[kTitleFarLeft], 61, "one");
    elements[kTitleFarRight] = create_carousel_title(container, kSlots[kTitleFarRight], 61, "three");

    const bool complete = left_arrow && right_arrow &&
        std::all_of(elements.begin(), elements.end(), [](lv_obj_t *element) { return element != nullptr; });
    if (!complete) {
        lv_obj_delete(container);
        return;
    }
    *carousel_alive_ = false;
    if (carousel_container_) lv_obj_delete(carousel_container_);
    carousel_alive_ = std::make_shared<bool>(true);
    carousel_container_ = container;
    carousel_elements_ = elements;
    left_arrow_button_ = left_arrow;
    right_arrow_button_ = right_arrow;
    grid_tiles_.clear();

    lv_obj_add_event_cb(carousel_elements_[kCardLeft], on_app_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(carousel_elements_[kCardCenter], on_app_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(carousel_elements_[kCardRight], on_app_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(carousel_elements_[kCardFarRight], on_app_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(carousel_elements_[kCardFarLeft], on_app_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(left_arrow_button_, on_left_arrow_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(right_arrow_button_, on_right_arrow_clicked, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(carousel_container_, on_owned_obj_deleted, LV_EVENT_DELETE, this);
    for (lv_obj_t *element : carousel_elements_)
        lv_obj_add_event_cb(element, on_owned_obj_deleted, LV_EVENT_DELETE, this);
    lv_obj_add_event_cb(left_arrow_button_, on_owned_obj_deleted, LV_EVENT_DELETE, this);
    lv_obj_add_event_cb(right_arrow_button_, on_owned_obj_deleted, LV_EVENT_DELETE, this);
    launcher_home_animation::apply_title_overflow(carousel_elements_[kTitleCenter], true);
    if (!home_key_registered_ && screen()) {
        lv_obj_add_event_cb(screen(), on_home_key, (lv_event_code_t)LV_EVENT_KEYBOARD, this);
        home_key_registered_ = true;
    }
}

void UILaunchPage::rebuild_grid()
{
    if (!carousel_container_ || !launch_)
        return;

    grid_tiles_.clear();
    lv_obj_clean(carousel_container_);

    const size_t count = launch_->app_count();
    for (size_t i = 0; i < count; ++i) {
        const app *item = launch_->app_at_index(static_cast<int>(i));
        if (!item) continue;

        lv_obj_t *tile = lv_obj_create(carousel_container_);
        if (!tile) continue;
        lv_obj_set_size(tile, 94, 66);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(tile, 8, LV_PART_MAIN);
        lv_obj_set_style_bg_color(tile, lv_color_hex(0x10141E), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(tile, lv_color_hex(0x263042), LV_PART_MAIN);
        lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);

        lv_obj_t *clip = lv_obj_create(tile);
        if (clip) {
            lv_obj_set_size(clip, 40, 40);
            lv_obj_align(clip, LV_ALIGN_TOP_MID, 0, 4);
            lv_obj_clear_flag(clip, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
            lv_obj_set_style_radius(clip, 8, LV_PART_MAIN);
            lv_obj_set_style_clip_corner(clip, true, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(clip, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(clip, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(clip, 0, LV_PART_MAIN);

            lv_obj_t *image = lv_image_create(clip);
            if (image) {
                lv_obj_set_size(image, 40, 40);
                lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);
                lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CENTER);
                lv_image_set_src(image, home_icon_pool_.find(item->Icon, 80));
                compensate_image_scale(image);
            }
        }

        lv_obj_t *label = lv_label_create(tile);
        if (label) {
            lv_obj_set_width(label, 86);
            lv_obj_set_height(label, LV_SIZE_CONTENT);
            lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -4);
            lv_label_set_text(label, item->Name.c_str());
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_font(
                label,
                launcher_fonts().get("Montserrat-Bold.ttf", 12, LV_FREETYPE_FONT_STYLE_BOLD),
                LV_PART_MAIN);
            lv_obj_set_style_text_color(label, lv_color_hex(0xB8C4D8), LV_PART_MAIN);
        }

        lv_obj_add_event_cb(tile, on_grid_tile_clicked, LV_EVENT_CLICKED, this);
        grid_tiles_.push_back(tile);
    }

    update_grid_selection(true);
}

void UILaunchPage::update_grid_selection(bool scroll_into_view)
{
    if (!launch_ || grid_tiles_.empty())
        return;

    const size_t selected = launch_->current_app_index();
    for (size_t i = 0; i < grid_tiles_.size(); ++i) {
        lv_obj_t *tile = grid_tiles_[i];
        if (!tile) continue;
        const bool is_sel = (i == selected);
        lv_obj_set_style_border_width(tile, is_sel ? 2 : 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(
            tile, lv_color_hex(is_sel ? 0x00F0FF : 0x263042), LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            tile, lv_color_hex(is_sel ? 0x161F30 : 0x10141E), LV_PART_MAIN);

        if (lv_obj_t *label = lv_obj_get_child(tile, 1)) {
            lv_obj_set_style_text_color(
                label, lv_color_hex(is_sel ? 0xFFFFFF : 0xB8C4D8), LV_PART_MAIN);
            launcher_home_animation::apply_title_overflow(label, is_sel);
        }
    }

    if (scroll_into_view && selected < grid_tiles_.size() && grid_tiles_[selected]) {
        lv_obj_scroll_to_view(grid_tiles_[selected], LV_ANIM_ON);
    }
}

void UILaunchPage::move_grid_selection(int delta_row, int delta_col)
{
    if (!launch_ || grid_tiles_.empty())
        return;

    const size_t count = grid_tiles_.size();
    const int cur = static_cast<int>(launch_->current_app_index());
    int next = cur + delta_row * 3 + delta_col;
    if (next < 0) next = 0;
    if (next >= static_cast<int>(count)) next = static_cast<int>(count) - 1;
    if (next == cur) return;

    launch_->select_app_index(static_cast<size_t>(next));
    update_grid_selection(true);
}

void UILaunchPage::on_grid_tile_clicked(lv_event_t *event) noexcept
{
    if (!event) return;
    auto *self = static_cast<UILaunchPage *>(lv_event_get_user_data(event));
    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (!self || !self->launch_ || !target) return;

    for (size_t i = 0; i < self->grid_tiles_.size(); ++i) {
        if (self->grid_tiles_[i] == target) {
            if (self->launch_->current_app_index() == i) {
                self->launch_selected_app();
            } else {
                self->launch_->select_app_index(i);
                self->update_grid_selection(true);
            }
            break;
        }
    }
}
