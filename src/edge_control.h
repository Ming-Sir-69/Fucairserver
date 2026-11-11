#pragma once

// Edge control, PC side: a paired Mac pushes its pointer past the screen edge
// facing this PC and its keyboard and mouse then drive this PC. Also shares
// the clipboard (text and images) with the Mac, both toggled per device.
//
// Protocol (TCP, port 7021, advertised as _fucair-edge._tcp):
//  Pairing, numeric comparison as in Bluetooth Secure Simple Pairing:
//   Mac -> "FPAR2" mac_pub[32] mac_id[16] name_len name
//   PC  -> "FPAR2" pc_pub[32] pc_id[16] commit[32] name_len name
//          commit = SHA-256("fucair-commit" || pc_pub || pc_nonce || mac_pub)
//   Mac -> mac_nonce[16];  PC -> pc_nonce[16]  (Mac checks the commitment)
//   Both show code = SHA-256("fucair-code" || mac_pub || pc_pub || nonces) mod 10^6
//   and ask their user. Each side sends 1 byte (1 = accepted) and reads the other's.
//   key = SHA-256("fucair-edge-pair-v2" || X25519 shared || mac_pub || pc_pub || nonces)
//   Mac -> HMAC(key,"mac-confirm");  PC -> HMAC(key,"pc-confirm")
//  Session: Mac -> "FEDG2" mac_id[16] random[16]
//           PC  -> "FEDG2" random[16]  or "FEDGX" reason (1 unknown Mac, 2 turned off)
//   session key = SHA-256(paired key || Mac random || PC random); frames are
//   [u32 big-endian length][12-byte nonce][AES-256-GCM ciphertext + tag].

#include "edge_keys.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fucairserver::edge {

struct Config {
    bool enabled = true;     // accept control from paired Macs
    bool clipboard = true;   // share the clipboard with paired Macs
    KeyConfig keys;
    bool operator==(const Config& o) const { return enabled == o.enabled && clipboard == o.clipboard && keys == o.keys; }
};

struct Device {
    std::string id;           // hex
    std::string name;
    bool control = true;      // may drive this PC
    bool clipboard = true;    // shares the clipboard
    bool online = false;
    bool controlling = false;
};

struct Status {
    bool listening = false;
    std::string problem;      // English; shown through tr()
    std::vector<Device> devices;
};

class EdgeControl {
public:
    EdgeControl();
    ~EdgeControl();
    EdgeControl(const EdgeControl&) = delete;
    EdgeControl& operator=(const EdgeControl&) = delete;

    // dir holds edge-control.ini and the protected device keys.
    bool start(const std::filesystem::path& dir, uint16_t port = 7021);
    void stop();

    Config config() const;
    void set_config(const Config& config);
    Status status() const;
    void set_device(const std::string& id, bool control, bool clipboard);
    void remove_device(const std::string& id);

    std::string pc_id() const;   // hex, advertised in the Bonjour TXT record
    uint16_t port() const;
    // The AirPlay receiver name; the Mac pauses edge control while mirroring to it.
    void set_receiver_name(const std::string& name);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fucairserver::edge
