#include "../ui.h"
#include "../view.h"

// Placeholder until Phase 3. Owned by the womp agent from Stage 2 on. The
// JPEGs run about 128KB, so the decode streams to the panel block by block.
namespace {

void draw()
{
	ui::clearBody();
	ui::title("Womp");
	ui::line(0, "the newest in world photo", ui::DIM);
	ui::line(1, "as a desk frame, with", ui::DIM);
	ui::line(2, "photographer and parcel", ui::DIM);
	ui::line(4, "phase 3", ui::WARN);
}

const view::View kWomp = {
    .name = "Womp",
    .source = "VOXELS",
    .order = view::ORDER_WOMP,
    .icon = icons::CAMERA,
    .draw = draw,
};

}  // namespace

VIEW_REGISTER(kWomp);
