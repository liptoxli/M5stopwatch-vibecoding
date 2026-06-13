/**
 * @file mooncake_log.cpp
 * @author Forairaaaaa
 * @brief
 * @version 0.1
 * @date 2024-09-24
 *
 * @copyright Copyright (c) 2024
 *
 */
#include "mooncake_log.h"
#include <chrono>
#include <ctime>
#include <functional>
#include <vector>

#define COLOR_INFO  fg(fmt::terminal_color::green)
#define COLOR_WARN  fg(fmt::terminal_color::yellow)
#define COLOR_ERROR fg(fmt::terminal_color::red)
#define COLOR_DEBUG fg(fmt::terminal_color::blue)

mclog::Signal<mclog::LogLevel_t, const std::string&> mclog::on_log;

static mclog::Settings_t _settings;

mclog::LogLevel_t mclog::internal::get_log_level()
{
    return _settings.log_level;
}

void mclog::set_level(LogLevel_t level)
{
    _settings.log_level = level;
}

void mclog::set_time_format(TimeFormat_t format)
{
    _settings.time_format = format;
}

void mclog::set_level_format(LevelFormat_t format)
{
    _settings.level_format = format;
}

void mclog::internal::print_tag_time()
{
    if (_settings.time_format == TimeFormat_t::time_format_none) {
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    switch (_settings.time_format) {
        case TimeFormat_t::time_format_none:
            break;

        case TimeFormat_t::time_format_full: {
            auto tm_result = std::localtime(&now_c);
            fmt::print("[{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}] ",
                       tm_result->tm_year + 1900, // 年份
                       tm_result->tm_mon + 1,     // 月
                       tm_result->tm_mday,        // 日
                       tm_result->tm_hour,        // 时
                       tm_result->tm_min,         // 分
                       tm_result->tm_sec,         // 秒
                       milliseconds.count());     // 毫秒
            break;
        }

        case TimeFormat_t::time_format_time_only: {
            auto tm_result = std::localtime(&now_c);
            fmt::print("[{:02}:{:02}:{:02}.{:03}] ",
                       tm_result->tm_hour,    // 时
                       tm_result->tm_min,     // 分
                       tm_result->tm_sec,     // 秒
                       milliseconds.count()); // 毫秒
            break;
        }

        case TimeFormat_t::time_format_unix_seconds:
            fmt::print("[{}] ", std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
            break;

        case TimeFormat_t::time_format_unix_milliseconds:
            fmt::print("[{}] ", std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
            break;

        case TimeFormat_t::time_format_iso_8601: {
            auto tm_result = std::localtime(&now_c);
            char tz_buf[16] = {0};
            std::strftime(tz_buf, sizeof(tz_buf), "%z", tm_result);
            fmt::print("[{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}{}] ", tm_result->tm_year + 1900,
                       tm_result->tm_mon + 1, tm_result->tm_mday, tm_result->tm_hour, tm_result->tm_min,
                       tm_result->tm_sec, milliseconds.count(), tz_buf);
            break;
        }
    }
}

void mclog::internal::print_tag_level(const LogLevel_t& level)
{
    if (_settings.level_format == LevelFormat_t::level_format_none) {
        return;
    }

    fmt::print("[");

    switch (level) {
        case LogLevel_t::level_info: {
            switch (_settings.level_format) {
                case LevelFormat_t::level_format_none:
                    break;
                case LevelFormat_t::level_format_lowercase:
                    fmt::print(COLOR_INFO, "info");
                    break;
                case LevelFormat_t::level_format_uppercase:
                    fmt::print(COLOR_INFO, "INFO");
                    break;
                case LevelFormat_t::level_format_single_letter:
                    fmt::print(COLOR_INFO, "I");
                    break;
            }
            break;
        }
        case LogLevel_t::level_warn: {
            switch (_settings.level_format) {
                case LevelFormat_t::level_format_none:
                    break;
                case LevelFormat_t::level_format_lowercase:
                    fmt::print(COLOR_WARN, "warn");
                    break;
                case LevelFormat_t::level_format_uppercase:
                    fmt::print(COLOR_WARN, "WARN");
                    break;
                case LevelFormat_t::level_format_single_letter:
                    fmt::print(COLOR_WARN, "W");
                    break;
            }
            break;
        }
        case LogLevel_t::level_error: {
            switch (_settings.level_format) {
                case LevelFormat_t::level_format_none:
                    break;
                case LevelFormat_t::level_format_lowercase:
                    fmt::print(COLOR_ERROR, "error");
                    break;
                case LevelFormat_t::level_format_uppercase:
                    fmt::print(COLOR_ERROR, "ERROR");
                    break;
                case LevelFormat_t::level_format_single_letter:
                    fmt::print(COLOR_ERROR, "E");
                    break;
            }
            break;
        }
        case LogLevel_t::level_debug: {
            switch (_settings.level_format) {
                case LevelFormat_t::level_format_none:
                    break;
                case LevelFormat_t::level_format_lowercase:
                    fmt::print(COLOR_DEBUG, "debug");
                    break;
                case LevelFormat_t::level_format_uppercase:
                    fmt::print(COLOR_DEBUG, "DEBUG");
                    break;
                case LevelFormat_t::level_format_single_letter:
                    fmt::print(COLOR_DEBUG, "D");
                    break;
            }
            break;
        }
    }

    fmt::print("] ");
}
