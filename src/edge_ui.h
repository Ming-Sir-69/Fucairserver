#pragma once

struct ImFont;

namespace fucairserver {
namespace edge { class EdgeControl; }

// The "Keyboard and mouse" page, drawn inside the main content window.
void draw_edge_page(edge::EdgeControl& edge, ImFont* bold_font, ImFont* small_font, float top, float width);
}
