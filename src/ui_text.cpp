#include "ui_text.h"

#include <atomic>
#include <string_view>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fucairserver {
namespace {

struct Entry { const char* en; const char* zh; };

// Written for the Simplified Chinese edition of the product. Apple's
// published Chinese names are used for its features: AirPlay is
// 隔空播放, Screen Mirroring is 屏幕镜像, Control Center is 控制中心.
constexpr Entry kText[] = {
    // Navigation and window
    {"YOUR SPACE", "我的空间"},
    {"Home", "首页"},
    {"Music", "音乐"},
    {"Screen mirroring", "屏幕镜像"},
    {"Settings", "设置"},
    {"About Fucairserver", "关于 Fucairserver"},
    {"Screen mirroring, audio and recording.", "屏幕镜像、音频接收与录制。"},
    {"Built with AirPlay-Windows, UxPlay and Dear ImGui. Thanks to their authors and contributors.",
     "基于 AirPlay-Windows、UxPlay 和 Dear ImGui 构建，感谢各项目的作者与贡献者。"},
    {"AirPlay-Windows: GNU GPL version 3. See the bundled LICENSE.",
     "AirPlay-Windows 采用 GNU GPL 第 3 版授权，详见随附的 LICENSE。"},
    {"Close", "关闭"},
    {"Device connected", "设备已连接"},
    {"Receiver available", "接收器已就绪"},
    {"Waiting for AirPlay", "等待隔空播放连接"},

    // Connection header and pages
    {"Connected device", "已连接的设备"},
    {"Connected · Audio", "已连接 · 音频"},
    {"Connected · Screen mirroring", "已连接 · 屏幕镜像"},
    {"Check firewall", "检查防火墙"},
    {"Disconnect", "断开连接"},
    {"Welcome home.", "欢迎回来。"},
    {"Your music on this PC.", "在这台电脑上听你的音乐。"},
    {"Your screen, live.", "你的屏幕，实时呈现。"},
    {"Connected", "已连接"},
    {"Ready to receive", "等待连接"},
    {"Your device.", "你的设备，"},
    {"A little bigger.", "在大屏上展开。"},
    {"Your music and your screen, right here on this PC.", "音乐和屏幕，都能直接投到这台电脑上。"},
    {"From your Mac, iPhone or iPad.", "支持 Mac、iPhone 和 iPad。"},
    {"SELECT ON YOUR DEVICE", "在设备上选择"},
    {"Receiver settings", "接收器设置"},
    {"Same Wi-Fi network", "连接同一网络"},
    {"Choose your mode", "选择投放方式"},
    {"Select your PC", "选择这台电脑"},
    {"Connect your device and this PC to the same network.", "让设备和这台电脑连接到同一个网络。"},
    {"Use AirPlay for music, or Screen Mirroring from Control Center.",
     "播放音乐用“隔空播放”；投放画面请在“控制中心”中选择“屏幕镜像”。"},
    {"Choose this receiver. The connection starts automatically.", "选中本接收器，连接会自动开始。"},
    {"My PC isn't showing up", "找不到这台电脑？"},
    {"Can't find your PC?", "找不到这台电脑？"},
    {"Close help", "关闭帮助"},
    {"1. Use the same network", "1. 连接同一网络"},
    {"Connect your iPhone or iPad and this PC to the same Wi-Fi network.",
     "让 Mac、iPhone 或 iPad 与这台电脑连接同一个 Wi-Fi。"},
    {"2. Check your VPN", "2. 检查 VPN"},
    {"A VPN can hide local devices. Check that it allows access to your local network.",
     "VPN 可能会屏蔽局域网内的设备，请确认它允许访问本地网络。"},
    {"3. Check Windows Firewall", "3. 检查 Windows 防火墙"},
    {"If a firewall warning is shown, choose Allow on local network.",
     "如果界面上出现防火墙提示，请点击“在局域网中允许”。"},
    {"Then reopen AirPlay on your device.", "然后在设备上重新打开隔空播放。"},
    {"Windows · Local network", "Windows · 局域网"},

    // Firewall
    {"Allow AirPlay through Windows Firewall", "允许隔空播放通过 Windows 防火墙"},
    {"Windows Firewall blocks incoming connections", "Windows 防火墙阻止了传入连接"},
    {"Firewall check unavailable", "无法检查防火墙"},
    {"Allow this app to receive connections from devices on your local network.",
     "允许本应用接收局域网内设备发起的连接。"},
    {"Waiting for Windows...", "正在等待 Windows…"},
    {"Allow on local network", "在局域网中允许"},

    // Music
    {"NOW PLAYING", "正在播放"},
    {"PAUSED", "已暂停"},
    {"Playing from your device on this PC", "正在这台电脑上播放来自你设备的音频"},
    {"Playback position (read-only)", "播放进度（只读）"},
    {"Previous track", "上一首"},
    {"Pause", "暂停"},
    {"Play", "播放"},
    {"Next track", "下一首"},
    {"Sending command...", "正在发送指令…"},
    {"Remote control unavailable", "当前无法遥控播放"},
    {"Command failed or unsupported", "指令未生效，或设备不支持"},

    // Mirroring toolbar
    {"Record", "录制"},
    {"Stop recording", "停止录制"},
    {"Fullscreen", "全屏"},
    {"Hide interface", "隐藏界面"},
    {"Rotation", "旋转"},
    {"Automatic (device)", "自动（跟随设备）"},
    {"Manual: 0 degrees", "手动：0°"},
    {"Manual: 90 degrees", "手动：90°"},
    {"Manual: 180 degrees", "手动：180°"},
    {"Manual: 270 degrees", "手动：270°"},
    {"GPU acceleration enabled.\nPerformance issues? Try CPU decoding in Settings.",
     "已启用 GPU 加速。\n如遇卡顿，可在设置中改用 CPU 解码。"},
    {"CPU decoding selected.\nGPU acceleration is available in Settings.",
     "当前使用 CPU 解码。\n可在设置中开启 GPU 加速。"},

    // Settings
    {"Close settings", "关闭设置"},
    {"Language", "语言"},
    {"Follow system", "跟随系统"},
    {"Appearance", "外观"},
    {"Light", "浅色"},
    {"Dark", "深色"},
    {"Receiver name", "接收器名称"},
    {"Apply name", "应用名称"},
    {"Use a name without dots or backslashes (max. 50 UTF-8 bytes).",
     "名称不能包含句点或反斜杠，最长 50 字节（约 16 个汉字）。"},
    {"Shown in AirPlay on your devices. Saved automatically.",
     "会显示在设备的隔空播放列表中，修改后自动保存。"},
    {"Sound", "声音"},
    {"Receive sound from devices", "接收设备的声音"},
    {"On: when a device connects, its sound plays on this PC.", "开启后，设备连接时声音会在这台电脑上播放。"},
    {"Off: devices keep their own sound output, such as AirPods. Takes effect on the next connection.",
     "关闭后，设备保留自己的声音输出（例如 AirPods），下次连接时生效。"},
    {"Volume %d%%", "音量 %d%%"},
    {"Mute", "静音"},
    {"Volume and mute apply immediately to playback on this PC.", "音量和静音只影响这台电脑上的播放，立即生效。"},
    {"Mirror resolution", "镜像分辨率"},
    {"1920x1080  (Full HD)", "1920×1080（全高清）"},
    {"2560x1440  (QHD, default)", "2560×1440（2K，默认）"},
    {"1920x1920  (square limit)", "1920×1920（方形上限）"},
    {"2560x2560  (square limit)", "2560×2560（方形上限）"},
    {"2868x2868  (square limit)", "2868×2868（方形上限）"},
    {"3840x3840  (square limit)", "3840×3840（方形上限）"},
    {"3840x2160  (4K UHD)", "3840×2160（4K 超高清）"},
    {"Custom: %dx%d", "自定义：%d×%d"},
    {"Custom...", "自定义…"},
    {"Width", "宽"},
    {"Height", "高"},
    {"Range: 320-7680 x 240-4320", "范围：宽 320–7680，高 240–4320"},
    {"Maximum video size. Your device chooses the actual stream resolution.",
     "这是画面尺寸上限，实际分辨率由发送设备决定。"},
    {"Received: %d x %d", "当前接收：%d × %d"},
    {"Applies on next device connection", "下次连接时生效"},
    {"(LiveSettings not wired)", "（设置不可用）"},
    {"Video decoder", "视频解码"},
    {"Use GPU acceleration", "使用 GPU 加速"},
    {"Uses your graphics card when available.", "显卡可用时由显卡解码。"},
    {"Uses your processor to decode video.", "由处理器解码视频。"},
    {"Synchronize with display", "与显示器同步（垂直同步）"},
    {"Sync presents to monitor refresh.", "按显示器刷新率输出画面。"},
    {"Present immediately (lower latency, visible tearing on fast pans).",
     "立即输出画面：延迟更低，但快速移动时可能出现撕裂。"},
    {"Applies live (no reconnect needed)", "立即生效，无需重新连接"},
    {"Recording", "录制"},
    {"Folder", "保存位置"},
    {"Browse...", "浏览…"},
    {"Auto (GPU preferred)", "自动（优先 GPU）"},
    {"GPU only", "仅 GPU"},
    {"CPU only", "仅 CPU"},
    {"H.264 (compatible)", "H.264（兼容性好）"},
    {"H.265 / HEVC (smaller)", "H.265 / HEVC（文件更小）"},
    {"Video codec", "视频编码"},
    {"Encoder", "编码器"},
    {"Bitrate (Mbps)", "码率（Mbps）"},
    {"Lower bitrate = smaller file / more compression", "码率越低，文件越小，压缩痕迹也越明显。"},
    {"Settings apply to the next recording", "设置将在下次录制时生效"},
    {"REC  %02lld:%02lld", "录制中  %02lld:%02lld"},
    {"Frames: %llu", "帧数：%llu"},
    {"Audio: %s", "音频：%s"},
    {"AAC stereo", "AAC 立体声"},
    {"waiting for PCM", "等待音频数据"},
    {"Queue drops: %llu", "队列丢帧：%llu"},
    {"Codec: %s", "编码：%s"},
    {"MP4 video (decoded AirPlay stream)", "保存为 MP4 视频（解码后的隔空播放画面）"},
    {"Start recording", "开始录制"},
    {"File: %s", "文件：%s"},
    {"Advanced", "高级"},
    {"Show diagnostics", "显示诊断信息"},
    {"Auto follows the orientation sent by the device. Manual angles rotate clockwise from the received image.",
     "“自动”跟随设备发送的方向；手动角度在接收画面的基础上顺时针旋转。"},
    {"Received: %s (%d x %d)", "当前接收：%s（%d × %d）"},
    {"Landscape", "横向"},
    {"Portrait", "纵向"},
    {"Square", "方形"},
    {"Applies live to display only; recording is unchanged.", "仅影响显示并立即生效，录制内容不受影响。"},
    {"Frame rate", "帧率"},
    {"Frame rate cap", "帧率上限"},
    {"%d fps", "%d 帧/秒"},
    {"iOS caps mirror at 60 fps regardless of hint; lower values force a deterministic cadence.",
     "发送端最高 60 帧/秒；设得更低可以让画面节奏更稳定。"},
    {"Compression", "压缩"},
    {"Allow HEVC (H.265)", "允许 HEVC（H.265）"},
    {"The device may pick HEVC at high resolution", "高分辨率时，设备可能改用 HEVC"},
    {"Force H.264 (more CPU, more bandwidth)", "强制使用 H.264：占用更多 CPU 和带宽"},
    {"Network", "网络"},
    {"RTSP port advertised through AirPlay", "RTSP 端口通过隔空播放自动广播"},
    {"mDNS   advertising", "mDNS   正在广播"},
    {"Keyboard shortcuts", "快捷键"},
    {"F / F11 / double-click  Toggle fullscreen", "F / F11 / 双击   切换全屏"},
    {"H       Hide / show the interface", "H        隐藏 / 显示界面"},
    {"R       Start / stop recording", "R        开始 / 停止录制"},
    {"Esc     Exit fullscreen / show UI / quit", "Esc      退出全屏 / 显示界面 / 收起到托盘"},
    {"Restore all app preferences to their defaults.", "将所有设置恢复为默认值。"},
    {"Reset all settings", "恢复默认设置"},
    {"Default settings restored.", "已恢复默认设置。"},

    // Diagnostics bar
    {"State: %s", "状态：%s"},
    {"Streaming", "传输中"},
    {"Paused", "已暂停"},
    {"Idle", "空闲"},
    {"Video: %dx%d @ %.1f", "视频：%d×%d @ %.1f"},
    {"Video: -", "视频：-"},
    {"Stage: %dx%d", "画布：%d×%d"},
    {"Renderer: %.0f fps", "渲染：%.0f 帧/秒"},
    {"BW: %.1f Mbps", "带宽：%.1f Mbps"},
    {"Total: %s", "累计：%s"},
    {"Lat: %.1f ms", "延迟：%.1f 毫秒"},
    {"HLS backend: %s", "HLS 后端：%s"},
};

// Fragments of messages composed by the firewall monitor.
constexpr Entry kMessageFragments[] = {
    {"Inbound access is controlled by your administrator or blocked for this network.",
     "传入连接由管理员策略控制，或在当前网络中被阻止。"},
    {"No active physical Ethernet/Wi-Fi interface with a gateway requires LAN access.",
     "当前没有带网关的有线或 Wi-Fi 网卡，无需开放局域网访问。"},
    {"Active LAN interfaces: ", "当前局域网网卡："},
    {"Could not locate this application.", "找不到本应用程序。"},
    {"Permission request cancelled.", "已取消授权请求。"},
    {"Could not request administrator permission.", "无法请求管理员权限。"},
    {"Could not verify the firewall update.", "无法确认防火墙规则是否已更新。"},
    {"Windows did not apply the rule. Check your administrator's firewall policy.",
     "Windows 未能应用该规则，请检查管理员设置的防火墙策略。"},
    {"Could not check Windows Firewall.", "无法检查 Windows 防火墙。"},
    {"Could not identify active physical LAN interfaces.", "无法识别当前使用的局域网网卡。"},
    {"Could not read firewall policy.", "无法读取防火墙策略。"},
    {"Could not read Windows Firewall rules.", "无法读取 Windows 防火墙规则。"},
};

std::atomic<bool> g_chinese{false};

bool system_prefers_chinese() {
#if defined(_WIN32)
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
#else
    return false;
#endif
}

const std::unordered_map<std::string_view, const char*>& table() {
    static const auto map = [] {
        std::unordered_map<std::string_view, const char*> m;
        for (const auto& e : kText) m.emplace(e.en, e.zh);
        return m;
    }();
    return map;
}

} // namespace

void set_ui_language(UiLanguage preference) {
    const bool chinese = preference == UiLanguage::Chinese ||
        (preference == UiLanguage::System && system_prefers_chinese());
    g_chinese.store(chinese, std::memory_order_relaxed);
}

bool ui_chinese() { return g_chinese.load(std::memory_order_relaxed); }

const char* tr(const char* english) {
    if (!english || !ui_chinese()) return english;
    const auto& map = table();
    const auto it = map.find(english);
    return it == map.end() ? english : it->second;
}

std::string tr_message(const std::string& english) {
    if (!ui_chinese()) return english;
    std::string out = english;
    for (const auto& e : kMessageFragments) {
        const std::string_view from = e.en;
        for (auto pos = out.find(from); pos != std::string::npos; pos = out.find(from, pos)) {
            out.replace(pos, from.size(), e.zh);
            pos += std::string_view(e.zh).size();
        }
    }
    // The interface list ends with an English full stop.
    if (out != english && !out.empty() && out.back() == '.') {
        out.pop_back();
        out += "。";
    }
    return out;
}

std::string translated_glyphs() {
    std::string all = "简体中文";
    for (const auto& e : kText) all += e.zh;
    for (const auto& e : kMessageFragments) all += e.zh;
    return all;
}

} // namespace fucairserver
