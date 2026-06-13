/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace badge::config_ap {

struct SlotState {
    std::size_t slot = 0;
    bool hasImage    = false;
    bool isActive    = false;
};

struct BadgeState {
    std::size_t slotCount  = 0;
    std::size_t activeSlot = 0;
    std::vector<SlotState> slots;
};

struct ImageData {
    std::string contentType;
    std::vector<uint8_t> data;
};

struct UploadRequest {
    std::size_t slot = 0;
    std::string fileName;
    std::string contentType;
    std::vector<uint8_t> data;
};

using UploadHandler    = std::function<bool(const UploadRequest&, std::string&)>;
using StateHandler     = std::function<BadgeState()>;
using ImageHandler     = std::function<bool(std::size_t, ImageData&, std::string&)>;
using SetActiveHandler = std::function<bool(std::size_t, std::string&)>;
using DeleteHandler    = std::function<bool(std::size_t, std::string&)>;

struct Callbacks {
    UploadHandler onUpload;
    StateHandler onGetState;
    ImageHandler onGetImage;
    SetActiveHandler onSetActive;
    DeleteHandler onDelete;
};

bool run(const std::function<void(std::string_view)>& onLog, const Callbacks& callbacks);

}  // namespace badge::config_ap