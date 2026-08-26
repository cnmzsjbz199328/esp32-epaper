/**
 * logger.h  –  Unified logging macros for ESP32 Ecosystem
 *
 * Replaces scattered Serial.println() calls with level-aware logging.
 * Uses ESP32's built-in log level system (CORE_DEBUG_LEVEL build flag).
 *
 * Usage:
 *   LOG_I("MODULE", "message %d", value);   // Info
 *   LOG_W("MODULE", "warning: %s", str);     // Warning  
 *   LOG_E("MODULE", "error code: %d", err);  // Error
 *   LOG_D("MODULE", "debug data: 0x%X", v);  // Debug
 *   LOG_V("MODULE", "verbose trace");         // Verbose
 *
 * Log levels are controlled at compile time by -DCORE_DEBUG_LEVEL=N
 *   0=None, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Verbose
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

/* ── Level definitions ─────────────────────────────────────── */
#define LOG_LEVEL_NONE    0
#define LOG_LEVEL_ERROR   1
#define LOG_LEVEL_WARN    2
#define LOG_LEVEL_INFO    3
#define LOG_LEVEL_DEBUG   4
#define LOG_LEVEL_VERBOSE 5

#ifndef CORE_DEBUG_LEVEL
#define CORE_DEBUG_LEVEL LOG_LEVEL_INFO
#endif

/* ── Timestamp helper ──────────────────────────────────────── */
#define LOG_TIMESTAMP()  (millis())

/* ── Core logging macro ────────────────────────────────────── */
#define LOG_FORMAT(level, tag, fmt) \
    "[%6lu][" level "][" tag "] " fmt "\n"

#define LOG_PRINT(level_char, tag, fmt, ...) \
    Serial.printf(LOG_FORMAT(level_char, tag, fmt), LOG_TIMESTAMP(), ##__VA_ARGS__)

/* ── Level-gated macros ────────────────────────────────────── */
#if CORE_DEBUG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_E(tag, fmt, ...) LOG_PRINT("E", tag, fmt, ##__VA_ARGS__)
#else
#define LOG_E(tag, fmt, ...) do {} while(0)
#endif

#if CORE_DEBUG_LEVEL >= LOG_LEVEL_WARN
#define LOG_W(tag, fmt, ...) LOG_PRINT("W", tag, fmt, ##__VA_ARGS__)
#else
#define LOG_W(tag, fmt, ...) do {} while(0)
#endif

#if CORE_DEBUG_LEVEL >= LOG_LEVEL_INFO
#define LOG_I(tag, fmt, ...) LOG_PRINT("I", tag, fmt, ##__VA_ARGS__)
#else
#define LOG_I(tag, fmt, ...) do {} while(0)
#endif

#if CORE_DEBUG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_D(tag, fmt, ...) LOG_PRINT("D", tag, fmt, ##__VA_ARGS__)
#else
#define LOG_D(tag, fmt, ...) do {} while(0)
#endif

#if CORE_DEBUG_LEVEL >= LOG_LEVEL_VERBOSE
#define LOG_V(tag, fmt, ...) LOG_PRINT("V", tag, fmt, ##__VA_ARGS__)
#else
#define LOG_V(tag, fmt, ...) do {} while(0)
#endif

#endif /* LOGGER_H */
