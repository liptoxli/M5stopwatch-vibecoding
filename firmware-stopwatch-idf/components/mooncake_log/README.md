# Mooncake Log

只是 [fmt](https://github.com/fmtlib/fmt) 的简单封装

引入后二进制文件通常会增加 300 多 KB 哦

### Basic logging

```cpp
mclog::info("?? {} ..??? 0x{:02X}", 114514, 66);
// [2025-06-06 12.34.56.123] [info] ?? 114514 ..??? 0x42

mclog::info("{}", std::vector<int>{1, 23, 4, 5});
// [2025-06-06 12.34.56.123] [info] [1, 23, 4, 5]

mclog::warn("???");
// [2025-06-06 12.34.56.123] [warn] ???

mclog::warn("{}", "6");
// [2025-06-06 12.34.56.123] [warn] 6

mclog::error("???");
// [2025-06-06 12.34.56.123] [error] ???

mclog::error("{}", "6");
// [2025-06-06 12.34.56.123] [error] 6
```

### Log level

```cpp
mclog::debug("you can't see me now");
// > no shit

mclog::set_level(mclog::level_debug);
mclog::debug("dddddddddddddddeeeeeeeeeebuggggggggggggggggggiiiinnnnnnggg");
// [2025-06-06 12.34.56.123] [debug] dddddddddddddddeeeeeeeeeebuggggggggggggggggggiiiinnnnnnggg
```

### Time format

```cpp
mclog::set_time_format(mclog::time_format_none);
mclog::info("time format: none");
// [info] time format: none

mclog::set_time_format(mclog::time_format_time_only);
mclog::info("time format: time only");
// [12.34.56.123] [info] time format: time only

mclog::set_time_format(mclog::time_format_unix_seconds);
mclog::info("time format: unix seconds");
// [1749184496] [info] time format: unix seconds

mclog::set_time_format(mclog::time_format_unix_milliseconds);
mclog::info("time format: unix milliseconds");
// [1749184496123] [info] time format: unix milliseconds

mclog::set_time_format(mclog::time_format_iso_8601);
mclog::info("time format: iso 8601");
// [2025-06-06T12:34:56.123+0800] [info] time format: iso 8601

mclog::set_time_format(mclog::time_format_full);
mclog::info("time format: full (default)");
// [2025-06-06 12.34.56.123] [info] time format: full (default)
```

### Level format

```cpp
mclog::set_level_format(mclog::level_format_none);
mclog::info("level format: none");
// [2025-06-06 12.34.56.123] [info] level format: none

mclog::set_level_format(mclog::level_format_uppercase);
mclog::info("level format: uppercase");
// [2025-06-06 12.34.56.123] [INFO] level format: uppercase

mclog::set_level_format(mclog::level_format_single_letter);
mclog::info("level format: single letter");
// [2025-06-06 12.34.56.123] [I] level format: single letter

mclog::set_level_format(mclog::level_format_lowercase);
mclog::info("level format: lowercase (default)");
// [2025-06-06 12.34.56.123] [info] level format: lowercase (default)
```

### On log callback

```cpp
mclog::on_log.connect([](mclog::LogLevel_t level, const std::string& msg) {
    fmt::println(">> level: {} msg: {}", static_cast<int>(level), msg);
});

mclog::info("?");
// [info] ?
// >> level: 0 msg: ?

mclog::warn("?");
// [warn] ?
// >> level: 1 msg: ?

mclog::error("?");
// [error] ?
// >> level: 2 msg: ?

mclog::debug("?");
// [debug] ?
// >> level: 3 msg: ?

mclog::on_log.clear();
mclog::info("clear on log callback");
// [2025-06-06 12.34.56.123] [info] clear on log callback
```
