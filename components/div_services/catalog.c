#include "div_service.h"
#include <string.h>
static const div_field_t fields_scan[] = {
    {"seconds", "Duration (seconds)", "15"},
    {"channel", "Channel (0 = hop)", "0"},
    {"path", "Capture file (optional)", ""},
    {NULL, NULL, NULL}
};
static const div_field_t fields_monitor[] = {
    {"seconds", "Duration (0 = continuous)", "0"},
    {"channel", "Channel (0 = hop)", "1"},
    {"path", "Capture file (optional)", ""},
    {NULL, NULL, NULL}
};
static const div_field_t fields_txwifi[] = {
    {"ssid", "Network name", "DIV-Lab"},
    {"channel", "Channel", "6"},
    {"count", "Frame count", "10"},
    {"interval", "Interval (ms)", "100"},
    {"bssid", "AP MAC (for selected AP)", ""},
    {"client", "Client MAC", ""},
    {NULL, NULL, NULL}
};
static const div_field_t fields_portal[] = {
    {"ssid", "Access point name", "DIV-Lab"},
    {"password", "Access point password", "div-lab-2026"},
    {"seconds", "Duration (seconds)", "120"},
    {NULL, NULL, NULL}
};
static const div_field_t fields_arp[] = {
    {"ssid", "Wi-Fi name", ""},
    {"password", "Wi-Fi password", ""},
    {"seconds", "Wait (seconds)", "15"},
    {NULL, NULL, NULL}
};
static const div_field_t fields_ble[] = {
    {"seconds", "Duration (seconds)", "15"},
    {"name", "Advertising name", "DIV-Lab"},
    {"data", "Advertising data (hex, optional)", ""},
    {"path", "Save observations (optional)", ""},
    {NULL, NULL, NULL}
};
static const div_field_t fields_nrf[] = {
    {"radio", "Radio (1..3)", "1"},
    {"channel", "Channel (0..125)", "40"},
    {"seconds", "Duration (seconds)", "15"},
    {"address", "ESB address (5 bytes hex)", "E7E7E7E7E7"},
    {"path", "Capture file", "esb/capture.bin"},
    {"payload", "Payload (hex)", "01020304"},
    {NULL, NULL, NULL}
};
static const div_field_t fields_sub[] = {
    {"frequency", "Frequency (MHz)", "433.92"},
    {"seconds", "Duration (seconds)", "15"},
    {"path", "Profile file", "subghz/capture.sub"},
    {"count", "Repeat count", "1"},
    {"mode", "Sequence: brute / debruijn", "brute"},
    {"bits", "Code bits", "12"},
    {"pulse", "Base pulse (microseconds)", "350"},
    {"start", "First code", "0"},
    {"threshold", "Detector threshold (dBm)", "-60"},
    {NULL, NULL, NULL}
};
static const div_field_t fields_ir[] = {
    {"path", "Profile file", "ir/capture.ir"},
    {"seconds", "Capture timeout (seconds)", "15"},
    {"carrier", "Carrier (Hz)", "38000"},
    {"count", "Repeat count", "1"},
    {"code", "NEC code (hex, optional)", ""},
    {NULL, NULL, NULL}
};
static const div_field_t fields_nfc[] = {
    {"path", "Dump file", "nfc/card.bin"},
    {"key", "MIFARE key (6 bytes hex)", "FFFFFFFFFFFF"},
    {"block", "Block / page", "4"},
    {"data", "Write data (hex)", ""},
    {"seconds", "Timeout (seconds)", "15"},
    {NULL, NULL, NULL}
};
static const div_field_t fields_gps[] = {
    {"seconds", "Duration (seconds)", "60"},
    {"path", "WiGLE CSV file", "logs/wardrive.csv"},
    {NULL, NULL, NULL}
};
static const div_field_t fields_file[] = {
    {"path", "SD path", ""},
    {"action", "Action: list / read / mkdir / delete / rename", "list"},
    {"destination", "Rename destination", ""},
    {NULL, NULL, NULL}
};
static const div_field_t fields_firmware[] = {
    {"path", "Application image on SD", "firmware.bin"},
    {NULL, NULL, NULL}
};
static const div_field_t fields_serial[] = {
    {"seconds", "Duration (seconds)", "30"},
    {"baud", "Baud rate", "115200"},
    {"text", "Text to send (optional)", ""},
    {NULL, NULL, NULL}
};
static const div_field_t fields_ducky[] = {
    {"path", "Keyboard script", "ducky/01_notepad_hello.txt"},
    {NULL, NULL, NULL}
};
static const div_field_t fields_settings[] = {
    {"brightness", "Brightness (0..255)", "160"},
    {"dark", "Dark theme (0/1)", "1"},
    {"led", "LED enabled (0/1)", "0"},
    {"background", "Background scan (0/1)", "0"},
    {"accent", "Accent: orange/green/red/cyan/purple/yellow/white (0..6)", "0"},
    {NULL, NULL, NULL}
};
static const div_field_t fields_none[] = {

    {NULL, NULL, NULL}
};
static const div_feature_t features[] = {
    {"wifi_monitor", "Packet Monitor", "Live packet FFT, color waterfall and optional PCAP.", DIV_WIFI, fields_monitor, false},
    {"wifi_scan", "Wi-Fi Scanner", "Discover APs with signal, channel and authentication details.", DIV_WIFI, fields_scan, false},
    {"wifi_beacon", "Beacon Spammer", "Transmit a finite sequence of custom lab beacons.", DIV_WIFI, fields_txwifi, true},
    {"wifi_deauth", "Wi-Fi Deauther", "Use the public Wi-Fi transmit API; firmware may reject deauth frames.", DIV_WIFI, fields_txwifi, true},
    {"wifi_probe", "Probe Request Flood", "Transmit a finite set of probe requests.", DIV_WIFI, fields_txwifi, true},
    {"wifi_detect", "Deauth Detector", "Receive deauthentication and disassociation frames.", DIV_WIFI, fields_scan, false},
    {"wifi_portal", "Captive Portal", "Local AP, DNS redirect and device information portal.", DIV_WIFI, fields_portal, true},
    {"wifi_hidden", "Hidden SSID Revealer", "Correlate beacon, probe-response and association SSIDs passively.", DIV_WIFI, fields_scan, false},
    {"wifi_wps", "WPS Scanner", "Parse WPS vendor information elements.", DIV_WIFI, fields_scan, false},
    {"wifi_arp", "ARP Scanner", "Join the configured network and discover IPv4 neighbours.", DIV_WIFI, fields_arp, false},
    {"wifi_karma", "Karma Attack", "Observe requested SSIDs; AP identity is explicitly configured.", DIV_WIFI, fields_portal, true},
    {"ble_scan", "BLE Scanner", "Discover advertising devices and names.", DIV_BLE, fields_ble, false},
    {"ble_sniff", "BLE Sniffer", "Track advertisements, RSSI, packet counts and last seen.", DIV_BLE, fields_ble, false},
    {"ble_spoof", "BLE Spoofer", "Advertise configured raw data or a local device name.", DIV_BLE, fields_ble, true},
    {"ble_apple", "Sour Apple", "Advertise supplied manufacturer data in a bounded session.", DIV_BLE, fields_ble, true},
    {"ble_airtag_tx", "AirTag Spoofer", "Advertise a supplied Find My compatible payload.", DIV_BLE, fields_ble, true},
    {"ble_airtag_rx", "AirTag Sniffer", "Identify Find My advertisements; identity is not guaranteed.", DIV_BLE, fields_ble, false},
    {"ble_skimmer", "Skimmer Detect", "Report suspicious name signatures; matches are heuristics.", DIV_BLE, fields_ble, false},
    {"ble_jam", "BLE Jammer", "NRF24 carrier test on configured RF channel.", DIV_NRF, fields_nrf, true},
    {"ble_ducky", "BLE Rubber Ducky", "Run a keyboard script after a host connects and subscribes.", DIV_BLE, fields_ducky, true},
    {"nrf_scan", "2.4 GHz Scanner", "Sample NRF24 received-power detector over 126 channels.", DIV_NRF, fields_nrf, false},
    {"nrf_protokill", "Proto Kill", "Bounded carrier test on the configured channel.", DIV_NRF, fields_nrf, true},
    {"nrf_sniff", "ESB Sniffer", "Receive ESB packets on an explicit address and save records.", DIV_NRF, fields_nrf, false},
    {"nrf_replay", "ESB Replay", "Replay records saved by ESB Sniffer.", DIV_NRF, fields_nrf, true},
    {"nrf_mouse_scan", "MouseJack Scan", "Inspect ESB traffic on a supplied receiver address.", DIV_NRF, fields_nrf, false},
    {"nrf_mouse_inject", "MouseJack Inject", "Send a supplied payload to a configured receiver.", DIV_NRF, fields_nrf, true},
    {"sub_capture", "Replay Attack", "Capture raw Sub-GHz pulses to a profile.", DIV_SUB, fields_sub, false},
    {"sub_replay", "Saved Profile", "Load and transmit a saved raw Sub-GHz profile.", DIV_SUB, fields_sub, true},
    {"sub_jam", "Sub-GHz Jammer", "Bounded carrier test at the configured frequency.", DIV_SUB, fields_sub, true},
    {"sub_brute", "De Bruijn / Brute", "Generate fixed-code test sequences from explicit parameters.", DIV_SUB, fields_sub, true},
    {"sub_detect", "Jamming Detector", "Receive-only RSSI and sustained-activity detection.", DIV_SUB, fields_sub, false},
    {"ir_record", "IR Record", "Capture pulse timings and save an IR profile.", DIV_IR, fields_ir, false},
    {"ir_saved", "IR Saved Profiles", "Replay a raw or NEC profile stored on SD.", DIV_IR, fields_ir, true},
    {"ir_universal", "Universal IR Controller", "Send a NEC code or load an imported remote profile.", DIV_IR, fields_ir, true},
    {"nfc_read", "Card Reader", "Read UID, ATQA and SAK from a PN532 tag.", DIV_NFC, fields_nfc, false},
    {"nfc_clone", "Card Clone", "Copy writable data blocks from a dump using the supplied key.", DIV_NFC, fields_nfc, true},
    {"nfc_erase", "Erase", "Clear a selected writable block; sector trailers are protected.", DIV_NFC, fields_nfc, true},
    {"nfc_dump", "Dump", "Read accessible Classic blocks using the supplied key.", DIV_NFC, fields_nfc, false},
    {"nfc_access", "Decode Access", "Validate and decode Classic sector access bits.", DIV_NFC, fields_nfc, false},
    {"nfc_jam", "Jam Reader", "Toggle the local PN532 RF field during a bounded test.", DIV_NFC, fields_nfc, true},
    {"nfc_disrupt", "Tag Disrupt", "Exercise local RF field transitions and tag discovery.", DIV_NFC, fields_nfc, true},
    {"nfc_emulate", "Disrupt Emulate", "PN532 target-mode session with bounded response handling.", DIV_NFC, fields_nfc, true},
    {"gps_wardrive", "Wardriver", "Log GNSS position with Wi-Fi and BLE observations.", DIV_GPS, fields_gps, false},
    {"gps_satellites", "Satellite Scanner", "Read NMEA satellite, fix and signal diagnostics.", DIV_GPS, fields_gps, false},
    {"system_serial", "Serial Monitor", "Display an external UART stream.", DIV_SYSTEM, fields_serial, false},
    {"system_files", "SD File Manager", "Browse, read, rename and delete SD files.", DIV_SYSTEM, fields_file, true},
    {"system_update", "Update Firmware", "Validate and write an application image to the inactive OTA slot.", DIV_SYSTEM, fields_firmware, true},
    {"system_calibrate", "Touch Calibrate", "Four-point touchscreen calibration stored in NVS.", DIV_SYSTEM, fields_none, false},
    {"system_settings", "Settings", "Brightness, theme, LEDs and background scanning.", DIV_SYSTEM, fields_settings, false},
    {"system_about", "About / Diagnostics", "Inspect hardware, memory, storage and firmware version.", DIV_SYSTEM, fields_none, false},
};
const div_feature_t *div_features(size_t *count) { if (count) *count = sizeof(features)/sizeof(features[0]); return features; }
const div_feature_t *div_feature_find(const char *id) {
    if (!id) return NULL;
    for (size_t i=0; i<sizeof(features)/sizeof(features[0]); ++i) if (!strcmp(id,features[i].id)) return &features[i];
    return NULL;
}
const char *div_category_name(div_category_t c) {
    static const char *names[] = {"Wi-Fi","Bluetooth","2.4 GHz","Sub-GHz","Infrared","RFID / NFC","GPS","Device"};
    return c < 8 ? names[c] : "Unknown";
}
