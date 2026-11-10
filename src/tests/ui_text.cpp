#include "ui_text.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    try {
        using namespace fucairserver;
        set_ui_language(UiLanguage::English);
        require(!ui_chinese(), "English must be selectable");
        require(std::strcmp(tr("Disconnect"), "Disconnect") == 0, "English must return the source text");

        set_ui_language(UiLanguage::Chinese);
        require(ui_chinese(), "Chinese must be selectable");
        require(std::string(tr("Disconnect")) == "断开连接", "Known text must be translated");
        require(std::string(tr("Screen mirroring")) == "屏幕镜像", "Apple feature names must use the official Chinese terms");
        require(std::strcmp(tr("Not in the table"), "Not in the table") == 0, "Unknown text must fall back to English");
        require(std::string(tr("Custom: %dx%d")).find("%d") != std::string::npos, "Format placeholders must survive translation");
        require(tr(nullptr) == nullptr, "Null text must stay null");

        const auto message = tr_message("Inbound access is controlled by your administrator or blocked for this network. Active LAN interfaces: WLAN.");
        require(message.find("当前局域网网卡：WLAN") != std::string::npos, "Composed firewall messages must be translated");
        require(message.find("Active LAN") == std::string::npos, "No English fragment may remain");
        require(message.size() >= 3 && message.compare(message.size() - 3, 3, "。") == 0, "Chinese messages end with a Chinese full stop");

        const auto glyphs = translated_glyphs();
        require(glyphs.find("断开连接") != std::string::npos && glyphs.find("简体中文") != std::string::npos,
                "Glyph text must cover translations and the language menu");

        set_ui_language(UiLanguage::English);
        require(tr_message("Could not read firewall policy.") == "Could not read firewall policy.", "English messages must stay unchanged");
        std::cout << "UI language, fallback, format and glyph checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
