#pragma once
#include <string>

namespace fucairserver {
// Persisted as UiPreferences::language.
enum class UiLanguage { System = 0, Chinese = 1, English = 2 };

// System resolves to Chinese when the Windows display language is Chinese.
void set_ui_language(UiLanguage preference);
bool ui_chinese();

// English source text is the key. Unknown text is returned unchanged.
const char* tr(const char* english);
// Translates known fragments inside composed system messages.
std::string tr_message(const std::string& english);
// Every translated string, for building the glyph atlas.
std::string translated_glyphs();
}
