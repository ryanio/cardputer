#include "../ui.h"
#include "../view.h"

// Placeholder. Bankr's agent profiles are public and unauthenticated, so this
// view needs no key: the leaderboard is a fetch anyone can make. Each profile
// carries a token address on Base, which is exactly what a Coral score takes.
namespace {

void draw()
{
	ui::clearBody();
	ui::title("Bankr");
	ui::line(0, "agents by market cap,", ui::DIM);
	ui::line(1, "weekly revenue, and the", ui::DIM);
	ui::line(2, "Coral score of the token", ui::DIM);
	ui::line(4, "phase 2", ui::WARN);
}

const view::View kBankr = {
    .name = "Bankr",
    .source = "BANKR",
    .order = view::ORDER_BANKR,
    .draw = draw,
};

}  // namespace

VIEW_REGISTER(kBankr);
