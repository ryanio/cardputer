#include "../ui.h"
#include "../view.h"

// Placeholder until Phase 1 lands. Owned by the gas agent from Stage 2 on.
namespace {

void draw()
{
	ui::clearBody();
	ui::title("Gas");
	ui::line(0, "base fee and three tiers,", ui::DIM);
	ui::line(1, "24h sparkline, congestion", ui::DIM);
	ui::line(2, "banding, threshold alarm", ui::DIM);
	ui::line(4, "phase 1", ui::WARN);
}

const view::View kGas = {
    .name = "Gas",
    .source = "GWEI",
    .order = view::ORDER_GAS,
    .icon = icons::FUEL,
    .draw = draw,
};

}  // namespace

VIEW_REGISTER(kGas);
