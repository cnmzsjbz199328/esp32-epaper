/**
 * app_state.h  –  Centralized application state for ESP32 Ecosystem
 *
 * Consolidates scattered global state variables into a single, thread-safe
 * structure. Reduces coupling between modules and makes state changes
 * traceable.
 *
 * Usage:
 *   AppState& state = AppState::getInstance();
 *   state.runner_score++;
 *   uint32_t score = state.runner_score;
 */

#ifndef APP_STATE_H
#define APP_STATE_H

#include <Arduino.h>
#include "bsp_types.h"

/**
 * Centralized application state – singleton.
 *
 * All mutable shared state lives here. Modules read/write through
 * the singleton rather than maintaining their own globals.
 *
 * Thread safety note: For fields accessed from both the BLE callback
 * context and the main loop, use the provided lock/unlock methods or
 * mark fields as volatile where atomic access is sufficient.
 */
class AppState {
public:
    static AppState& getInstance() {
        static AppState instance;
        return instance;
    }

    /* ── Demo / Game state ────────────────────────────────── */
    // Moved to UI layer via weak bsp_app_state_json()

    /* ── WiFi provisioning (temporary) ─────────────────────── */
    String temp_wifi_ssid;
    String temp_wifi_pass;

    /* ── BLE state tracking ────────────────────────────────── */
    uint32_t last_state_notify = 0;

    static constexpr size_t SELFTEST_RECORD_CAPACITY = 24;
    bsp_selftest_record_t selftest_records[SELFTEST_RECORD_CAPACITY] = {};
    size_t selftest_record_count = 0;

    /* ── Convenience reset ─────────────────────────────────── */
    void resetGameState() {
    }

private:
    AppState() = default;
    AppState(const AppState&) = delete;
    AppState& operator=(const AppState&) = delete;
};

#endif /* APP_STATE_H */
