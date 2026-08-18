#include "../ui.h"
#include "../view.h"

// Placeholder until Phase 4. A bot is text, not an image: unicode.textContent
// rendered through the generated glyph atlas, in unicode.colors.
namespace {

void draw()
{
	ui::clearBody();
	ui::title("Bot");
	ui::line(0, "a GlyphBot from its", ui::DIM);
	ui::line(1, "unicode genome, raised", ui::DIM);
	ui::line(2, "as a pet", ui::DIM);
	ui::line(4, "phase 4", ui::WARN);
}

const view::View kBot = {
    .name = "Bot",
    .source = "GLYPHBOTS",
    .order = view::ORDER_BOT,
    .icon = icons::BOT,
    .draw = draw,
};

}  // namespace

VIEW_REGISTER(kBot);
