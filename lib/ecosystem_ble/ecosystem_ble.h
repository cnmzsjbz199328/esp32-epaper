#pragma once

/// Starts the optional common BLE GATT server for a BSP node.
/// @return true if BLE started successfully, false on failure.
bool ecp_ble_begin(void);
bool ecp_ble_connected(void);

/// Publishes periodic state notifications while the common BLE server is active.
void ecp_ble_loop(void);

/// Forces an immediate state notify, bypassing the periodic interval.
/// Call this when a UI-visible state field a peer needs promptly (e.g. a
/// text_prompt appearing/disappearing) changes outside the normal cadence.
/// No-op if no peer is connected.
void ecp_ble_notify_state_now(void);
