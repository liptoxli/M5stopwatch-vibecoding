/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <apps/app_codex/codex_config.h>
#include <assets/assets.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <hal/utils/settings/settings.h>
#include <mooncake_log.h>
#include <ssid_manager.h>
#include <wifi_manager.h>
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

namespace {

constexpr const char* kTag = "Setup-Wifi";
constexpr const char* kSystemSettingsNs = "system";
constexpr const char* kCodexSettingsNs = "codex";
constexpr const char* kWifiEnabledKey = "wifi_enabled";
constexpr const char* kWifiQuotaFallbackKey = "wifi_quota_fallback_enabled";

constexpr int kPanelSize = 466;
constexpr uint32_t kBgColor = 0x000000;
constexpr uint32_t kTextColor = 0xFFFFFF;
constexpr uint32_t kTextDimColor = 0x9A9A9A;
constexpr uint32_t kPanelColor = 0x343434;
constexpr uint32_t kButtonColor = 0x4C4C4C;
constexpr uint32_t kOkColor = 0x4AD78C;
constexpr uint32_t kOkTextColor = 0x0F5831;

constexpr const char* kCharOptions =
    "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz\n"
    "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL\nM\nN\nO\nP\nQ\nR\nS\nT\nU\nV\nW\nX\nY\nZ\n"
    "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n"
    " \n-\n_\n.\n@\n#\n!\n?\n$\n%\n&\n*\n+\n=\n/\n:";

constexpr const char* kLetterOptions =
    "a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz\n"
    "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\nL\nM\nN\nO\nP\nQ\nR\nS\nT\nU\nV\nW\nX\nY\nZ";

constexpr const char* kSymbolOptions =
    "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n"
    "-\n_\n.\n@\n#\n!\n?\n$\n%\n&\n*\n+\n=\n/\n:\n \n~\n,\n;";

struct WifiNetworkOption {
    std::string ssid;
    int rssi = 0;
    bool secure = false;
    bool saved = false;
    int savedIndex = -1;
};

void apply_selector_style(Roller& roller)
{
    lv_obj_set_style_radius(roller.get(), 52, LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller.get(), lv_color_hex(kPanelColor), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(roller.get(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(roller.get(), 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(roller.get(), &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(roller.get(), lv_color_hex(kTextColor), LV_PART_MAIN);
    lv_obj_set_style_text_align(roller.get(), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(roller.get(), 24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(roller.get(), lv_color_hex(0x696969), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(roller.get(), LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_font(roller.get(), &lv_font_montserrat_28, LV_PART_SELECTED);
    lv_obj_set_style_text_color(roller.get(), lv_color_hex(kTextColor), LV_PART_SELECTED);
    lv_obj_set_style_text_align(roller.get(), LV_TEXT_ALIGN_CENTER, LV_PART_SELECTED);
    lv_obj_set_style_border_width(roller.get(), 0, LV_PART_SELECTED);
}

void apply_button_style(Button& button, uint32_t bgColor, uint32_t textColor, int width, int height)
{
    button.setSize(width, height);
    button.setRadius(height / 2);
    button.setBorderWidth(0);
    button.setShadowWidth(0);
    button.setBgColor(lv_color_hex(bgColor));
    button.label().setTextFont(&lv_font_montserrat_22);
    button.label().setTextColor(lv_color_hex(textColor));
    button.label().align(LV_ALIGN_CENTER, 0, 0);
}

std::string masked_text(size_t len)
{
    if (len == 0) {
        return "-";
    }
    return std::string(len, '*');
}

std::vector<WifiNetworkOption> scan_wifi_networks()
{
    auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsInitialized()) {
        WifiManagerConfig config;
        config.ssid_prefix = codex_config::kWifiPortalPrefix;
        config.language = codex_config::kLanguage;
        wifi.Initialize(config);
    }

    wifi.StopStation();

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_start() != ESP_OK) {
        mclog::tagWarn(kTag, "wifi scan start failed");
        return {};
    }

    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;
    esp_err_t scan_ret = esp_wifi_scan_start(&scan_config, true);

    std::vector<WifiNetworkOption> networks;
    if (scan_ret == ESP_OK) {
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        std::vector<wifi_ap_record_t> records(ap_count);
        if (ap_count > 0 && esp_wifi_scan_get_ap_records(&ap_count, records.data()) == ESP_OK) {
            records.resize(ap_count);
            std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) { return a.rssi > b.rssi; });

            for (const auto& record : records) {
                const char* ssid = reinterpret_cast<const char*>(record.ssid);
                if (ssid == nullptr || ssid[0] == '\0') {
                    continue;
                }

                auto exists = std::find_if(networks.begin(), networks.end(), [ssid](const auto& option) {
                    return option.ssid == ssid;
                });
                if (exists != networks.end()) {
                    continue;
                }

                networks.push_back({
                    .ssid = ssid,
                    .rssi = record.rssi,
                    .secure = record.authmode != WIFI_AUTH_OPEN,
                });
            }
        }
    } else {
        mclog::tagWarn(kTag, "wifi scan failed: {}", esp_err_to_name(scan_ret));
    }

    esp_wifi_scan_stop();
    esp_wifi_stop();
    return networks;
}

void ensure_wifi_initialized()
{
    auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsInitialized()) {
        WifiManagerConfig config;
        config.ssid_prefix = codex_config::kWifiPortalPrefix;
        config.language = codex_config::kLanguage;
        wifi.Initialize(config);
    }
}

void seed_known_wifi_profiles()
{
    ensure_wifi_initialized();
    auto& ssids = SsidManager::GetInstance();
    for (size_t i = 0; i < codex_config::kKnownWifiProfileCount; ++i) {
        const auto& profile = codex_config::kKnownWifiProfiles[i];
        ssids.AddSsid(profile.ssid, profile.password);
    }
    const auto& saved_ssids = ssids.GetSsidList();
    for (int i = 0; i < saved_ssids.size(); ++i) {
        const bool prefer_default = std::any_of(std::begin(codex_config::kKnownWifiProfiles),
                                                std::end(codex_config::kKnownWifiProfiles),
                                                [&](const auto& profile) {
                                                    return profile.preferDefault && saved_ssids[i].ssid == profile.ssid;
                                                });
        if (prefer_default) {
            ssids.SetDefaultSsid(i);
            break;
        }
    }
}

void set_wifi_enabled_setting(bool enabled)
{
    Settings settings(kSystemSettingsNs, true);
    settings.SetBool(kWifiEnabledKey, enabled);
}

bool get_wifi_enabled_setting()
{
    Settings settings(kSystemSettingsNs, false);
    return settings.GetBool(kWifiEnabledKey, codex_config::kDefaultWifiEnabled);
}

void set_codex_wifi_quota_setting(bool enabled)
{
    Settings settings(kCodexSettingsNs, true);
    settings.SetBool(kWifiQuotaFallbackKey, enabled);
}

bool get_codex_wifi_quota_setting()
{
    Settings settings(kCodexSettingsNs, false);
    return get_wifi_enabled_setting() &&
           settings.GetBool(kWifiQuotaFallbackKey, codex_config::kDefaultWifiQuotaFallbackEnabled);
}

void stop_wifi_station()
{
    auto& wifi = WifiManager::GetInstance();
    if (wifi.IsInitialized()) {
        wifi.StopConfigAp();
        wifi.StopStation();
    }
}

}  // namespace

class WifiConnectWorker::WifiConnectView {
public:
    enum class PasswordRoller {
        Letters,
        Symbols,
    };

    enum class Stage {
        Scan,
        Password,
        Connecting,
        Done,
    };

    enum class Action {
        None,
        Add,
        Delete,
        Select,
        Refresh,
        Off,
        ToggleCodexQuota,
        Connect,
        Done,
    };

    WifiConnectView()
    {
        _panel = std::make_unique<Container>(lv_screen_active());
        _panel->align(LV_ALIGN_CENTER, 0, 0);
        _panel->setSize(kPanelSize, kPanelSize);
        _panel->setRadius(0);
        _panel->setBorderWidth(0);
        _panel->setPaddingAll(0);
        _panel->setBgColor(lv_color_hex(kBgColor));
        _panel->setBgOpa(LV_OPA_COVER);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _title = std::make_unique<Label>(_panel->get());
        _title->align(LV_ALIGN_TOP_MID, 0, 28);
        _title->setTextFont(&MontserratSemiBold26);
        _title->setTextColor(lv_color_hex(kTextColor));

        _value = std::make_unique<Label>(_panel->get());
        _value->align(LV_ALIGN_TOP_MID, 0, 66);
        _value->setWidth(400);
        _value->setTextFont(&lv_font_montserrat_22);
        _value->setTextColor(lv_color_hex(kTextDimColor));
        _value->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _value->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

        _codex_quota_row = std::make_unique<Container>(_panel->get());
        _codex_quota_row->setSize(360, 54);
        _codex_quota_row->align(LV_ALIGN_TOP_MID, 0, 96);
        _codex_quota_row->setBgColor(lv_color_hex(0x151515));
        _codex_quota_row->setBgOpa(LV_OPA_COVER);
        _codex_quota_row->setBorderWidth(0);
        _codex_quota_row->setRadius(27);
        _codex_quota_row->setPaddingAll(0);
        _codex_quota_row->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _codex_quota_label = std::make_unique<Label>(_codex_quota_row->get());
        _codex_quota_label->setText("Codex Quota");
        _codex_quota_label->setTextFont(&lv_font_montserrat_22);
        _codex_quota_label->setTextColor(lv_color_hex(kTextColor));
        _codex_quota_label->align(LV_ALIGN_LEFT_MID, 24, 0);

        _codex_quota_switch = std::make_unique<Switch>(_codex_quota_row->get());
        _codex_quota_switch->setSize(76, 40);
        _codex_quota_switch->align(LV_ALIGN_RIGHT_MID, -22, 0);
        _codex_quota_switch->setValue(get_codex_wifi_quota_setting());
        _codex_quota_switch->setBgColor(lv_color_hex(0x3A3A3A), LV_PART_MAIN);
        _codex_quota_switch->setBgOpa(LV_OPA_COVER, LV_PART_MAIN);
        _codex_quota_switch->setBorderWidth(0, LV_PART_MAIN);
        _codex_quota_switch->setRadius(LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(_codex_quota_switch->get(), lv_color_hex(0xFFFFFF), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(_codex_quota_switch->get(), LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_border_width(_codex_quota_switch->get(), 0, LV_PART_KNOB);
        lv_obj_set_style_radius(_codex_quota_switch->get(), LV_RADIUS_CIRCLE, LV_PART_KNOB);
        _codex_quota_switch->setBgColor(lv_color_hex(kOkColor), LV_PART_INDICATOR | LV_STATE_CHECKED);
        _codex_quota_switch->setBgOpa(LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
        _codex_quota_switch->setBorderWidth(0, LV_PART_INDICATOR | LV_STATE_CHECKED);
        _codex_quota_switch->onValueChanged().connect([this](bool enabled) {
            if (_syncing_codex_quota_switch) {
                return;
            }
            _codex_quota_enabled = enabled;
            _pending_action = Action::ToggleCodexQuota;
        });

        _password_card = std::make_unique<Container>(_panel->get());
        _password_card->align(LV_ALIGN_TOP_MID, 0, 98);
        _password_card->setSize(400, 56);
        _password_card->setRadius(28);
        _password_card->setBorderWidth(1);
        _password_card->setBorderColor(lv_color_hex(0x666666));
        _password_card->setBgColor(lv_color_hex(0x1E1E1E));
        _password_card->setBgOpa(LV_OPA_COVER);
        _password_card->setPaddingAll(0);
        _password_card->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _password_label = std::make_unique<Label>(_password_card->get());
        _password_label->setWidth(360);
        _password_label->setTextFont(&lv_font_montserrat_24);
        _password_label->setTextColor(lv_color_hex(kTextColor));
        _password_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _password_label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        _password_label->align(LV_ALIGN_CENTER, 0, 0);

        _roller = std::make_unique<Roller>(_panel->get());
        _roller->align(LV_ALIGN_CENTER, 0, -22);
        _roller->setSize(120, 190);
        _roller->setOptions(kCharOptions, LV_ROLLER_MODE_INFINITE);
        _roller->setVisibleRowCount(5);
        apply_selector_style(*_roller);

        _letter_roller = std::make_unique<Roller>(_panel->get());
        _letter_roller->align(LV_ALIGN_CENTER, -82, 36);
        _letter_roller->setSize(132, 146);
        _letter_roller->setOptions(kLetterOptions, LV_ROLLER_MODE_INFINITE);
        _letter_roller->setVisibleRowCount(3);
        apply_selector_style(*_letter_roller);

        _symbol_roller = std::make_unique<Roller>(_panel->get());
        _symbol_roller->align(LV_ALIGN_CENTER, 82, 36);
        _symbol_roller->setSize(132, 146);
        _symbol_roller->setOptions(kSymbolOptions, LV_ROLLER_MODE_INFINITE);
        _symbol_roller->setVisibleRowCount(3);
        apply_selector_style(*_symbol_roller);

        _add_button = std::make_unique<Button>(_panel->get());
        _add_button->align(LV_ALIGN_BOTTOM_MID, -122, -38);
        apply_button_style(*_add_button, kButtonColor, kTextColor, 116, 56);
        _add_button->label().setText("Add");
        _add_button->onClick().connect([this]() {
            _pending_action = _stage == Stage::Scan ? Action::Refresh : Action::Add;
        });

        _delete_button = std::make_unique<Button>(_panel->get());
        _delete_button->align(LV_ALIGN_BOTTOM_MID, 122, -38);
        apply_button_style(*_delete_button, kButtonColor, kTextColor, 116, 56);
        _delete_button->label().setText("Delete");
        _delete_button->onClick().connect([this]() {
            _pending_action = _stage == Stage::Scan ? Action::Off : Action::Delete;
        });

        _next_button = std::make_unique<Button>(_panel->get());
        _next_button->align(LV_ALIGN_BOTTOM_MID, 0, -38);
        apply_button_style(*_next_button, kOkColor, kOkTextColor, 116, 56);
        _next_button->onClick().connect([this]() {
            if (_stage == Stage::Scan) {
                _pending_action = Action::Select;
            } else if (_stage == Stage::Password) {
                _pending_action = Action::Connect;
            } else if (_stage == Stage::Done) {
                _pending_action = Action::Done;
            }
        });

        _status = std::make_unique<Label>(_panel->get());
        _status->align(LV_ALIGN_BOTTOM_MID, 0, -8);
        _status->setWidth(340);
        _status->setTextFont(&lv_font_montserrat_16);
        _status->setTextColor(lv_color_hex(kTextDimColor));
        _status->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _status->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);

        updateText();
    }

    void setScanning()
    {
        _stage = Stage::Scan;
        _network_options = {"Scanning..."};
        _networks.clear();
        if (_roller) {
            _roller->align(LV_ALIGN_CENTER, 0, 22);
            _roller->setSize(360, 168);
            _roller->setOptions(_network_options, LV_ROLLER_MODE_NORMAL);
            _roller->setVisibleRowCount(4);
            _roller->setSelected(0);
        }
        setStatus("Scanning Wi-Fi...");
        updateText();
    }

    bool codexQuotaEnabled() const
    {
        return _codex_quota_enabled;
    }

    void setCodexQuotaEnabled(bool enabled)
    {
        _codex_quota_enabled = enabled;
        if (_codex_quota_switch) {
            _syncing_codex_quota_switch = true;
            _codex_quota_switch->setValue(enabled);
            _syncing_codex_quota_switch = false;
        }
    }

    void setNetworks(std::vector<WifiNetworkOption> networks)
    {
        _stage = Stage::Scan;
        _networks = std::move(networks);
        _network_options.clear();

        const auto& saved_list = SsidManager::GetInstance().GetSsidList();
        for (auto& network : _networks) {
            for (int i = 0; i < static_cast<int>(saved_list.size()); ++i) {
                if (saved_list[i].ssid == network.ssid) {
                    network.saved = true;
                    network.savedIndex = i;
                    break;
                }
            }
        }

        for (const auto& network : _networks) {
            char line[96] = {};
            snprintf(line,
                     sizeof(line),
                     "%s  %ddBm%s%s",
                     network.ssid.c_str(),
                     network.rssi,
                     network.saved ? " saved" : "",
                     network.secure ? "" : " open");
            _network_options.emplace_back(line);
        }
        if (_network_options.empty()) {
            _network_options.emplace_back("No Wi-Fi found");
        }

        if (_roller) {
            _roller->align(LV_ALIGN_CENTER, 0, 22);
            _roller->setSize(360, 168);
            _roller->setOptions(_network_options, LV_ROLLER_MODE_NORMAL);
            _roller->setVisibleRowCount(4);
            _roller->setSelected(0);
        }

        char status[64] = {};
        snprintf(status, sizeof(status), "%d network%s found", static_cast<int>(_networks.size()), _networks.size() == 1 ? "" : "s");
        setStatus(status);
        updateText();
    }

    Action consumeAction()
    {
        Action action = _pending_action;
        _pending_action = Action::None;
        return action;
    }

    void handleAction(Action action)
    {
        if (action == Action::Add) {
            appendSelectedChar(_active_password_roller == PasswordRoller::Letters ? _letter_roller.get()
                                                                                   : _symbol_roller.get());
        } else if (action == Action::Delete) {
            if (!_password.empty()) {
                _password.pop_back();
            }
        } else if (action == Action::Select) {
            const uint32_t selected = _roller ? _roller->getSelected() : 0;
            if (_networks.empty() || selected >= _networks.size()) {
                setStatus("Refresh and select Wi-Fi");
            } else {
                _ssid = _networks[selected].ssid;
                if (_networks[selected].saved && _networks[selected].savedIndex >= 0) {
                    const auto& saved = SsidManager::GetInstance().GetSsidList()[_networks[selected].savedIndex];
                    _password = saved.password;
                    _stage = Stage::Connecting;
                    setStatus("Connecting saved Wi-Fi...");
                } else {
                    _stage = Stage::Password;
                    _password.clear();
                    _active_password_roller = PasswordRoller::Letters;
                    setStatus("Enter password");
                }
            }
        } else if (action == Action::Connect) {
            if (_ssid.empty()) {
                _stage = Stage::Scan;
                setStatus("Select Wi-Fi first");
            } else {
                _stage = Stage::Connecting;
                setStatus("Connecting...");
            }
        } else if (action == Action::ToggleCodexQuota) {
            setStatus(_codex_quota_enabled ? "Codex quota uses Wi-Fi" : "Codex quota Wi-Fi off");
        } else if (action == Action::Done) {
            _stage = Stage::Done;
        }

        updateText();
    }

    void handleHardwareInput()
    {
        if (_stage != Stage::Password) {
            return;
        }

        if (GetHAL().btnA.wasPressed()) {
            _active_password_roller = PasswordRoller::Letters;
            setStatus("Left: letters");
        } else if (GetHAL().btnB.wasPressed()) {
            _active_password_roller = PasswordRoller::Symbols;
            setStatus("Right: numbers/symbols");
        }

        if (GetHAL().btnA.wasHold()) {
            _active_password_roller = PasswordRoller::Letters;
            appendSelectedChar(_letter_roller.get());
            setStatus("Added letter");
        } else if (GetHAL().btnB.wasHold()) {
            _active_password_roller = PasswordRoller::Symbols;
            appendSelectedChar(_symbol_roller.get());
            setStatus("Added symbol");
        }

        updatePasswordRollerStyle();
        updateText();
    }

    void setStatus(const char* text)
    {
        if (_status) {
            _status->setText(text);
        }
    }

    Stage stage() const
    {
        return _stage;
    }

    void returnToPassword(const char* status)
    {
        _stage = Stage::Password;
        setStatus(status);
        updateText();
    }

    const std::string& ssid() const
    {
        return _ssid;
    }

    const std::string& password() const
    {
        return _password;
    }

    int selectedSavedIndex() const
    {
        const uint32_t selected = _roller ? _roller->getSelected() : 0;
        if (selected >= _networks.size()) {
            return -1;
        }
        return _networks[selected].savedIndex;
    }

private:
    void appendSelectedChar(Roller* roller)
    {
        if (roller == nullptr) {
            return;
        }

        auto selected = roller->getSelectedStr(8);
        if (selected.empty()) {
            return;
        }

        if (_password.size() >= 64) {
            setStatus("Password max 64 chars");
            return;
        }
        _password += selected[0];
    }

    void updatePasswordRollerStyle()
    {
        if (!_letter_roller || !_symbol_roller) {
            return;
        }

        const bool letters_active = _active_password_roller == PasswordRoller::Letters;
        lv_obj_set_style_border_width(_letter_roller->get(), letters_active ? 3 : 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(_letter_roller->get(), lv_color_hex(kOkColor), LV_PART_MAIN);
        lv_obj_set_style_border_width(_symbol_roller->get(), letters_active ? 0 : 3, LV_PART_MAIN);
        lv_obj_set_style_border_color(_symbol_roller->get(), lv_color_hex(kOkColor), LV_PART_MAIN);
    }

    void updateText()
    {
        if (!_title || !_value || !_password_card || !_password_label || !_next_button || !_roller ||
            !_letter_roller || !_symbol_roller || !_add_button || !_delete_button || !_codex_quota_row) {
            return;
        }

        const bool scan = _stage == Stage::Scan;
        const bool editing = _stage == Stage::Password;
        _roller->setHidden(!(scan || editing));
        _roller->setHidden(!scan);
        _letter_roller->setHidden(!editing);
        _symbol_roller->setHidden(!editing);
        _password_card->setHidden(!editing);
        _codex_quota_row->setHidden(!scan);
        _add_button->setHidden(!(scan || editing));
        _delete_button->setHidden(!(scan || editing));

        if (scan) {
            _title->setText("Wi-Fi");
            _value->setText("Select network");
            _add_button->label().setText("Refresh");
            _delete_button->label().setText("Off");
            _next_button->label().setText("Select");
            _next_button->setHidden(false);
        } else if (_stage == Stage::Password) {
            _title->setText("Password");
            _value->setText(_ssid.c_str());
            _password_display = masked_text(_password.size());
            _password_label->setText(_password_display.c_str());
            _add_button->label().setText("A Add");
            _delete_button->label().setText("Delete");
            _next_button->label().setText("Connect");
            _next_button->setHidden(false);
            updatePasswordRollerStyle();
        } else if (_stage == Stage::Connecting) {
            _title->setText("Wi-Fi");
            _value->setText(_ssid.c_str());
            _next_button->setHidden(true);
        } else {
            _title->setText("Wi-Fi");
            _value->setText("Done");
            _next_button->label().setText("OK");
            _next_button->setHidden(false);
        }
    }

    std::unique_ptr<Container> _panel;
    std::unique_ptr<Label> _title;
    std::unique_ptr<Label> _value;
    std::unique_ptr<Container> _codex_quota_row;
    std::unique_ptr<Label> _codex_quota_label;
    std::unique_ptr<Switch> _codex_quota_switch;
    std::unique_ptr<Container> _password_card;
    std::unique_ptr<Label> _password_label;
    std::unique_ptr<Roller> _roller;
    std::unique_ptr<Roller> _letter_roller;
    std::unique_ptr<Roller> _symbol_roller;
    std::unique_ptr<Button> _add_button;
    std::unique_ptr<Button> _delete_button;
    std::unique_ptr<Button> _next_button;
    std::unique_ptr<Label> _status;

    Stage _stage = Stage::Scan;
    Action _pending_action = Action::None;
    std::vector<WifiNetworkOption> _networks;
    std::vector<std::string> _network_options;
    std::string _ssid;
    std::string _password;
    std::string _password_display;
    PasswordRoller _active_password_roller = PasswordRoller::Letters;
    bool _codex_quota_enabled = get_codex_wifi_quota_setting();
    bool _syncing_codex_quota_switch = false;
};

WifiConnectWorker::WifiConnectWorker()
{
    mclog::tagInfo(kTag, "start wifi connect worker");
    seed_known_wifi_profiles();
    _view = std::make_unique<WifiConnectView>();
    _scan_requested = true;
}

void WifiConnectWorker::update()
{
    if (consumeGlobalExitShortcut()) {
        _connecting = false;
        return;
    }

    if (!_view) {
        _is_done = true;
        return;
    }

    _view->handleHardwareInput();

    const auto action = _view->consumeAction();
    if (action == WifiConnectView::Action::Refresh) {
        set_wifi_enabled_setting(true);
        set_codex_wifi_quota_setting(true);
        _view->setCodexQuotaEnabled(true);
        _view->setScanning();
        _scan_requested = true;
        return;
    }
    if (action == WifiConnectView::Action::Off) {
        stop_wifi_station();
        set_wifi_enabled_setting(false);
        set_codex_wifi_quota_setting(false);
        _view->setCodexQuotaEnabled(false);
        _connecting = false;
        _view->setStatus("Wi-Fi off");
        _view->handleAction(WifiConnectView::Action::Done);
        return;
    }
    if (action == WifiConnectView::Action::ToggleCodexQuota) {
        if (_view->codexQuotaEnabled()) {
            set_wifi_enabled_setting(true);
            set_codex_wifi_quota_setting(true);
            ensure_wifi_initialized();
            WifiManager::GetInstance().StartStation();
            _view->setStatus("Wi-Fi quota on");
        } else {
            stop_wifi_station();
            set_wifi_enabled_setting(false);
            set_codex_wifi_quota_setting(false);
            _view->setStatus("Wi-Fi and quota off");
        }
        _view->handleAction(action);
        return;
    }

    if (_scan_requested) {
        _scan_requested = false;
        _view->setScanning();
        _view->setNetworks(scan_wifi_networks());
        return;
    }

    if (action != WifiConnectView::Action::None) {
        _view->handleAction(action);
        if (action == WifiConnectView::Action::Done) {
            _is_done = true;
            return;
        }
    }

    if (!_connecting && _view->stage() == WifiConnectView::Stage::Connecting) {
        _connecting = true;
        _connect_start_tick = GetHAL().millis();
        set_wifi_enabled_setting(true);
        set_codex_wifi_quota_setting(true);
        _view->setCodexQuotaEnabled(true);

        const int saved_index = _view->selectedSavedIndex();
        if (saved_index >= 0) {
            SsidManager::GetInstance().SetDefaultSsid(saved_index);
        } else {
            SsidManager::GetInstance().AddSsid(_view->ssid(), _view->password());
        }

        auto& wifi = WifiManager::GetInstance();
        WifiManagerConfig config;
        config.ssid_prefix = codex_config::kWifiPortalPrefix;
        config.language = codex_config::kLanguage;
        wifi.Initialize(config);
        wifi.StopStation();
        wifi.StartStation();
    }

    if (_connecting) {
        auto& wifi = WifiManager::GetInstance();
        if (wifi.IsConnected()) {
            _view->setStatus("Connected");
            _view->handleAction(WifiConnectView::Action::Done);
            _connecting = false;
            return;
        }

        if (GetHAL().millis() - _connect_start_tick > 15000) {
            _view->returnToPassword("Failed. Check SSID/password");
            _connecting = false;
        }
    }

    if (!_connecting && _view->stage() == WifiConnectView::Stage::Done) {
        if (_done_tick == 0) {
            _done_tick = GetHAL().millis();
        }
        if (GetHAL().millis() - _done_tick > 1200) {
            _done_tick = 0;
            _is_done = true;
        }
    }
}

WifiConnectWorker::~WifiConnectWorker()
{
}
