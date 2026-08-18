#include "../ui.h"
#include "../view.h"

// Placeholder until Phase 6. One round per ET day, fetched once and played
// with the radio off. Every reveal carries the caveats and the Coral name.
namespace {

void draw()
{
	ui::clearBody();
	ui::title("Reef");
	ui::line(0, "the daily Coral Score", ui::DIM);
	ui::line(1, "guessing round", ui::DIM);
	ui::line(3, "phase 6", ui::WARN);
	ui::line(4, "scores are not advice", ui::DIM);
}

const view::View kReef = {
    .name = "Reef",
    .source = "CORAL",
    .order = view::ORDER_REEF,
    .draw = draw,
};

}  // namespace

VIEW_REGISTER(kReef);
