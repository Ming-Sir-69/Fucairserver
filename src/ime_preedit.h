#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace fucairserver {
// Preedit is a display-only value. Only committed SDL_TEXTINPUT edits the
// caller's text buffer, preserving ImGui's normal selection and undo handling.
class ImePreedit {
public:
    struct Preview {
        std::string text;
        std::size_t begin = 0, end = 0, caret = 0;
        std::size_t selection_begin = 0, selection_end = 0;
    };
    bool active() const { return !text_.empty(); }
    void clear() { text_.clear(); cursor_ = selection_ = 0; }
    void update(std::string_view text, int cursor, int selection) {
        text_.assign(text);
        cursor_ = utf8_offset(text_, static_cast<std::size_t>(std::max(0, cursor)));
        selection_ = utf8_offset(std::string_view(text_).substr(cursor_),
                                 static_cast<std::size_t>(std::max(0, selection)));
    }
    Preview preview(std::string_view committed, int cursor, int selection_start, int selection_end) const {
        const auto caret = boundary(committed, cursor);
        Preview result{std::string(committed), caret, caret, caret, caret, caret};
        if (!active()) return result;
        auto first = boundary(committed, selection_start);
        auto last = boundary(committed, selection_end);
        if (first > last) std::swap(first, last);
        if (first == last) first = last = caret;
        result.text = std::string(committed.substr(0, first)) + text_ + std::string(committed.substr(last));
        result.begin = first;
        result.end = first + text_.size();
        result.caret = first + cursor_;
        result.selection_begin = result.caret;
        result.selection_end = result.caret + selection_;
        return result;
    }
private:
    static std::size_t boundary(std::string_view text, int offset) {
        auto byte = std::min(text.size(), static_cast<std::size_t>(std::max(0, offset)));
        while (byte > 0 && byte < text.size() && (static_cast<unsigned char>(text[byte]) & 0xc0) == 0x80) --byte;
        return byte;
    }
    static std::size_t utf8_offset(std::string_view text, std::size_t characters) {
        std::size_t byte = 0;
        while (byte < text.size() && characters-- > 0) {
            ++byte;
            while (byte < text.size() && (static_cast<unsigned char>(text[byte]) & 0xc0) == 0x80) ++byte;
        }
        return byte;
    }
    std::string text_;
    std::size_t cursor_ = 0, selection_ = 0;
};
}
