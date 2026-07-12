// SPDX-FileCopyrightText: 2026 Kenta IDA <fuga@fugafuga.org>
// SPDX-License-Identifier: BSL-1.0
//
// No-op implementation of the BLE settings-service public API, built in place
// of config_service.cpp / gatt_settings.cpp / dis.cpp / crypto.cpp when
// Bluetooth is disabled (CONFIG_BT_ENABLED=n — see this component's
// CMakeLists). Every entry point becomes a no-op: start() reports success
// without bringing up NimBLE, ble_connected() is always false, and the
// sink/getter registrations are dropped. The rest of the firmware links and
// runs unchanged; provisioning and live settings go through the Wi-Fi HTTP
// service instead. Device configuration itself is unaffected — config_store.cpp
// still provides load()/save() and is always built.

#include "config_service/config_service.hpp"
#include "config_service/config_store.hpp"

namespace stackchan::config {

// load() is a thin forwarder to the NVS-backed store (config_store.cpp, always
// built) — it must stay functional with BT off so the device keeps reading its
// saved Wi-Fi / API-key / settings. The real config_service.cpp defines the
// same one-liner; here we reproduce it since that file isn't compiled.
DeviceConfig load() { return store::load(); }

tl::expected<void, Error> start(const DeviceConfig& /*current*/) { return {}; }

void notify_wifi_connected(bool /*connected*/) {}
void notify_battery(int /*millivolts*/, int /*milliamps*/, int /*percent*/) {}
void set_board_kind(std::uint8_t /*kind*/) {}
bool ble_connected() { return false; }

void set_audio_stream_sink(const AudioStreamSink* /*sink*/) {}
void set_face_config_sink(FaceConfigSink /*sink*/) {}
void set_lt_config_sink(LtConfigSink /*sink*/) {}
void set_servo_range_mode_sink(ServoRangeModeSink /*sink*/) {}
void set_servo_positions_getter(ServoPositionsGetter /*getter*/) {}
void set_audio_metrics_getter(AudioMetricsJsonGetter /*getter*/) {}
void set_led_state_getter(LedStateGetter /*getter*/) {}
void set_led_state_sink(LedStateSink /*sink*/) {}
void set_mic_lip_gain_getter(MicLipGainGetter /*getter*/) {}
void set_mic_lip_gain_sink(MicLipGainSink /*sink*/) {}
void set_speaker_volume_getter(SpeakerVolumeGetter /*getter*/) {}
void set_speaker_volume_sink(SpeakerVolumeSink /*sink*/) {}
void set_jtts_say_kana_sink(JttsSayKanaSink /*sink*/) {}
void set_settings_hooks(const SettingsHooks& /*hooks*/) {}
void set_avatar_bytecode_sink(AvatarBytecodeSink /*sink*/) {}

} // namespace stackchan::config
