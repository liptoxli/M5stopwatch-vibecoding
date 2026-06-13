/**
 * @file mc_log.h
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2024-09-24
 *
 * @copyright Copyright (c) 2024
 *
 */
#pragma once
#include "fmt/base.h"
#include "fmt/color.h"
#include "fmt/format.h"
#include "fmt/ranges.h"
#include "fmt/chrono.h"
#include "mooncake_log_signal.h"
#include <string_view>

namespace mclog {

enum LogLevel_t {
    level_debug = 0,
    level_info,
    level_warn,
    level_error,
};

enum TimeFormat_t {
    time_format_none = 0,
    time_format_full,
    time_format_time_only,
    time_format_unix_seconds,
    time_format_unix_milliseconds,
    time_format_iso_8601,
};

enum LevelFormat_t {
    level_format_none = 0,
    level_format_lowercase,
    level_format_uppercase,
    level_format_single_letter,
};

struct Settings_t {
    LogLevel_t log_level = level_info;
    TimeFormat_t time_format = time_format_full;
    LevelFormat_t level_format = level_format_lowercase;
};

namespace internal {
void print_tag_time();
void print_tag_level(const LogLevel_t& level);
LogLevel_t get_log_level();
inline bool should_i_go(const LogLevel_t& level)
{
    return level < get_log_level();
}
} // namespace internal

/**
 * @brief On log signal.
 *
 */
extern Signal<LogLevel_t, const std::string&> on_log;

/**
 * @brief Set logging level.
 *
 * @param level
 */
void set_level(LogLevel_t level);

/**
 * @brief Set time format.
 *
 * @param format
 */
void set_time_format(TimeFormat_t format);

/**
 * @brief Set level format.
 *
 * @param format
 */
void set_level_format(LevelFormat_t format);

/**
 * @brief Log info
 *
 * @tparam Args
 * @param args
 */
template <typename... Args>
void info(fmt::format_string<Args...> fmt, Args&&... args)
{
    if (internal::should_i_go(level_info)) {
        return;
    }

    internal::print_tag_time();
    internal::print_tag_level(level_info);

    auto formatted_msg = fmt::format(fmt, std::forward<Args>(args)...);
    fmt::print("{}\n", formatted_msg);

    on_log.emit(level_info, formatted_msg);
}

/**
 * @brief Log warning
 *
 * @tparam Args
 * @param args
 */
template <typename... Args>
void warn(fmt::format_string<Args...> fmt, Args&&... args)
{
    if (internal::should_i_go(level_warn)) {
        return;
    }

    internal::print_tag_time();
    internal::print_tag_level(level_warn);

    auto formatted_msg = fmt::format(fmt, std::forward<Args>(args)...);
    fmt::print("{}\n", formatted_msg);

    on_log.emit(level_warn, formatted_msg);
}

/**
 * @brief Log error
 *
 * @tparam Args
 * @param args
 */
template <typename... Args>
void error(fmt::format_string<Args...> fmt, Args&&... args)
{
    if (internal::should_i_go(level_error)) {
        return;
    }

    internal::print_tag_time();
    internal::print_tag_level(level_error);

    auto formatted_msg = fmt::format(fmt, std::forward<Args>(args)...);
    fmt::print("{}\n", formatted_msg);

    on_log.emit(level_error, formatted_msg);
}

/**
 * @brief Log debug
 *
 * @tparam Args
 * @param args
 */
template <typename... Args>
void debug(fmt::format_string<Args...> fmt, Args&&... args)
{
    if (internal::should_i_go(level_debug)) {
        return;
    }

    internal::print_tag_time();
    internal::print_tag_level(level_debug);

    auto formatted_msg = fmt::format(fmt, std::forward<Args>(args)...);
    fmt::print("{}\n", formatted_msg);

    on_log.emit(level_debug, formatted_msg);
}

/**
 * @brief Log info with extra tag
 *
 * @tparam Args
 * @param tag  Extra tag to prepend
 * @param args Message formatting arguments
 */
template <typename... Args>
void tagInfo(std::string_view tag, fmt::format_string<Args...> fmt, Args&&... args)
{
    if (internal::should_i_go(level_info)) {
        return;
    }

    internal::print_tag_time();
    internal::print_tag_level(level_info);

    auto formatted_msg = fmt::format("[{}] {}", tag, fmt::format(fmt, std::forward<Args>(args)...));
    fmt::print("{}\n", formatted_msg);

    on_log.emit(level_info, formatted_msg);
}

/**
 * @brief Log warning with extra tag
 *
 * @tparam Args
 * @param tag  Extra tag to prepend
 * @param args Message formatting arguments
 */
template <typename... Args>
void tagWarn(std::string_view tag, fmt::format_string<Args...> fmt, Args&&... args)
{
    if (internal::should_i_go(level_warn)) {
        return;
    }

    internal::print_tag_time();
    internal::print_tag_level(level_warn);

    auto formatted_msg = fmt::format("[{}] {}", tag, fmt::format(fmt, std::forward<Args>(args)...));
    fmt::print("{}\n", formatted_msg);

    on_log.emit(level_warn, formatted_msg);
}

/**
 * @brief Log error with extra tag
 *
 * @tparam Args
 * @param tag  Extra tag to prepend
 * @param args Message formatting arguments
 */
template <typename... Args>
void tagError(std::string_view tag, fmt::format_string<Args...> fmt, Args&&... args)
{
    if (internal::should_i_go(level_error)) {
        return;
    }

    internal::print_tag_time();
    internal::print_tag_level(level_error);

    auto formatted_msg = fmt::format("[{}] {}", tag, fmt::format(fmt, std::forward<Args>(args)...));
    fmt::print("{}\n", formatted_msg);

    on_log.emit(level_error, formatted_msg);
}

/**
 * @brief Log debug with extra tag
 *
 * @tparam Args
 * @param tag  Extra tag to prepend
 * @param args Message formatting arguments
 */
template <typename... Args>
void tagDebug(std::string_view tag, fmt::format_string<Args...> fmt, Args&&... args)
{
    if (internal::should_i_go(level_debug)) {
        return;
    }

    internal::print_tag_time();
    internal::print_tag_level(level_debug);

    auto formatted_msg = fmt::format("[{}] {}", tag, fmt::format(fmt, std::forward<Args>(args)...));
    fmt::print("{}\n", formatted_msg);

    on_log.emit(level_debug, formatted_msg);
}

} // namespace mclog