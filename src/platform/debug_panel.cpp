#include "platform/debug_panel.h"

#include "platform/demo_common.h"
#include "platform/ui_text.h"

#include <algorithm>

namespace rally::platform {

using namespace demo;

DebugDrawn DebugPanel::draw(Overlay& overlay, const DebugText& text, double now_s) {
    DebugDrawn drawn;
    const UiMetrics& m = overlay.metrics();
    const std::vector<DebugText::Line>& lines = text.lines();
    std::vector<DebugBlock> blocks(static_cast<std::size_t>(text.block_count()));
    for (int b = 0; b < text.block_count(); ++b) {
        DebugBlock& block = blocks[static_cast<std::size_t>(b)];
        block.lines = text.block_size(b);
        for (int i = 0; i < block.lines; ++i) {
            const DebugText::Line& line = lines[static_cast<std::size_t>(text.block_first(b) + i)];
            block.width = std::max(block.width,
                                   line.bars ? axis_bars_width(m) : text_width(line.text, m.font));
        }
    }
    // overlay.layout() es el área máxima; el rectángulo propio de este fotograma aún no está
    // fijado (begin() lo borra) y no influye en el reparto.
    const DebugPlan plan = plan_debug(blocks, overlay.layout());
    pages_ = static_cast<int>(plan.pages.size());
    page_ = std::min(page_, pages_ - 1);
    const DebugPage& page = plan.pages[static_cast<std::size_t>(page_)];
    const int width = hold_.update(page.width, now_s);
    overlay.set_debug_rect(debug_panel_rect(overlay.layout(), page.lines, width));
    overlay.panel(Layer::Debug, kDebugBackground);
    const int x = overlay.content_x(Layer::Debug);
    int y = overlay.content_y(Layer::Debug);
    overlay.text_fit(Layer::Debug, x, y, debug_title(page_, pages_), kTitle);
    y += m.line;
    const int last = y + (page.lines - 1) * m.line;
    for (int b = page.first_block; b < page.first_block + page.block_count; ++b) {
        for (int i = 0; i < text.block_size(b) && y < last; ++i, y += m.line) {
            const DebugText::Line& line = lines[static_cast<std::size_t>(text.block_first(b) + i)];
            if (line.bars) {
                drawn.bars_x = x;
                drawn.bars_y = y;
            } else {
                overlay.text_fit(Layer::Debug, x, y, line.text, line.color);
            }
        }
    }
    return drawn;
}

} // namespace rally::platform
