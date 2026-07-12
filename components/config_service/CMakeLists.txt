# The BLE settings GATT service (config_service.cpp / gatt_settings.cpp /
# dis.cpp) and its X25519+AES-GCM channel (crypto.cpp) are only built when
# Bluetooth is enabled. With CONFIG_BT_ENABLED=n they are replaced by
# config_service_stub.cpp — no-op definitions of the public API — so the rest
# of the firmware links unchanged, the NimBLE controller is never linked, and
# its internal DRAM stays free for the Wi-Fi/TLS stack. Device config storage
# (config_store / settings_registry / staged_config) and OTA (ota.cpp) are
# transport-agnostic and always built.
set(srcs
    "config_store.cpp"
    "settings_registry.cpp"
    "staged_config.cpp"
    "ota.cpp")
set(priv_reqs
    nvs_flash esp_system esp_event esp_timer mbedtls esp_app_format
    app_update json esp_partition esp_netif avatar_vm)

if(CONFIG_BT_ENABLED)
    list(APPEND srcs "config_service.cpp" "gatt_settings.cpp" "crypto.cpp" "dis.cpp")
    list(APPEND priv_reqs bt)
else()
    list(APPEND srcs "config_service_stub.cpp")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES tl_expected
    PRIV_REQUIRES ${priv_reqs})
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
