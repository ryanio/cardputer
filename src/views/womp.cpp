#include <ArduinoJson.h>

#include "../jpeg.h"
#include "../net.h"
#include "../store.h"
#include "../ui.h"
#include "../view.h"

// A photo somebody took inside Voxels, on the panel.
//
// The one view where the payload does not fit in RAM. A womp is around 128KB
// of JPEG and this device has 320KB in total with the TLS stack already inside
// it, so nothing here ever holds an image: the decoder pulls from the socket
// as it goes and pushes pixels straight at the panel, scaling on the way. That
// is what net::get with a sink is for, and it is the reason the sink takes a
// Stream rather than a buffer.
//
// Browsing goes backwards by walking ids down. The list endpoint honours limit
// and ignores offset, so asking for the next page returns the same three womps
// forever, but any single id resolves and the ids are sequential.
namespace {

constexpr const char *NEWEST_URL = "https://www.voxels.com/api/womps.json?limit=1";
constexpr const char *WOMP_URL = "https://www.voxels.com/api/womps/%ld.json";

constexpr const char *ID_KEY = "womp.id";
constexpr int CAPTION_H = 21;

// Two captions rather than two screens. The panel holds the only copy of the
// picture, so leaving it and coming back would mean fetching 148KB again: the
// caption strip can be repainted for nothing, a second page of it cannot.
constexpr int CAPTIONS = 2;
enum class State : uint8_t { Idle, Waiting, Ready, Failed };
enum class Job : uint8_t { None, Newest, Womp, Image };

int caption = 0;
State state = State::Idle;
Job job = Job::None;
bool waitingShown = false;
int status = 0;

long id = 0;
long newest = 0;
char author[32] = {0};
char parcel[40] = {0};
char island[24] = {0};
char coords[32] = {0};
char created[24] = {0};
char imageUrl[192] = {0};

// The panel holds the only copy of the picture. Nothing can repaint it without
// fetching it again, so the view tracks whether it is up there and never
// clears the screen out from under it for no reason.
bool painted = false;
size_t imageBytes = 0;
uint32_t imageMs = 0;

// ------------------------------------------------------------------ helpers

void copyField(JsonVariantConst value, char *out, size_t n, const char *fallback = "")
{
	ui::asciify(value.is<const char *>() ? value.as<const char *>() : fallback, out, n);
}

// 2026-08-18T00:08:46.530Z reads as 2026-08-18 on a panel this size.
void readDate(JsonVariantConst value, char *out, size_t n)
{
	copyField(value, out, n);
	for (size_t i = 0; i < n && out[i] != '\0'; i++) {
		if (out[i] == 'T') {
			out[i] = '\0';
			break;
		}
	}
}

// --------------------------------------------------------------------- fetch

void want(Job next)
{
	job = next;
	waitingShown = false;
	state = State::Waiting;
	view::repaint();
}

void readWomp(JsonObjectConst w)
{
	id = w["id"] | id;
	copyField(w["author"]["name"], author, sizeof(author), "anonymous");
	copyField(w["parcel_address"], parcel, sizeof(parcel));
	copyField(w["parcel_island"], island, sizeof(island));
	copyField(w["coords"], coords, sizeof(coords));
	readDate(w["created_at"], created, sizeof(created));
	snprintf(imageUrl, sizeof(imageUrl), "%s", w["image_url"] | "");
	store::setInt(ID_KEY, (int32_t)id);
}

// The whole read API answers {"success":true,...} and a failed lookup can come
// back as success:false with a 200, so the flag is what decides, not the status.
bool succeeded(JsonDocument &doc)
{
	return doc["success"].is<bool>() ? doc["success"].as<bool>() : true;
}

void fetchNewest()
{
	JsonDocument filter;
	filter["success"] = true;
	JsonObject w = filter["womps"][0].to<JsonObject>();
	w["id"] = true;
	w["author"]["name"] = true;
	w["image_url"] = true;
	w["coords"] = true;
	w["parcel_address"] = true;
	w["parcel_island"] = true;
	w["created_at"] = true;

	JsonDocument doc;
	const net::Result r = net::getJson(NEWEST_URL, doc, &filter);
	status = r.status;
	if (!r.ok() || !succeeded(doc) || doc["womps"].as<JsonArrayConst>().size() == 0) {
		state = State::Failed;
		return;
	}

	readWomp(doc["womps"][0]);
	newest = id;
	want(Job::Image);
}

void fetchWomp()
{
	char url[96];
	snprintf(url, sizeof(url), WOMP_URL, id);

	JsonDocument filter;
	filter["success"] = true;
	JsonObject w = filter["womp"].to<JsonObject>();
	w["id"] = true;
	w["author"]["name"] = true;
	w["image_url"] = true;
	w["coords"] = true;
	w["parcel_address"] = true;
	w["parcel_island"] = true;
	w["created_at"] = true;

	JsonDocument doc;
	const net::Result r = net::getJson(url, doc, &filter);
	status = r.status;
	if (!r.ok() || !succeeded(doc) || doc["womp"].isNull()) {
		state = State::Failed;
		return;
	}

	readWomp(doc["womp"]);
	want(Job::Image);
}

void fetchImage()
{
	if (imageUrl[0] == '\0') {
		state = State::Failed;
		return;
	}

	painted = false;
	ui::gfx().fillScreen(ui::BG);

	// The decode happens inside here, while the body is still arriving, which
	// is the whole reason net::get hands the sink a Stream rather than a
	// buffer. See src/jpeg.cpp for what the device actually does with it.
	const net::Result r = net::get(imageUrl, [&](Stream &body, int length) {
		painted = jpeg::draw(body, length);
		return painted;
	});

	status = r.status;
	imageBytes = r.bytes;
	imageMs = r.ms;
	if (!painted) {
		state = State::Failed;
		return;
	}
	state = State::Ready;
}

void open(long next)
{
	if (next < 1) {
		return;
	}
	id = next;
	want(Job::Womp);
}

// ---------------------------------------------------------------------- draw

// Drawn over the bottom of the photo rather than beside it, because the panel
// is the frame and 21 rows of letterbox would be a waste of it. Only these 21
// rows are ever repainted, which is what makes a second page free.
void drawCaption()
{
	M5GFX &g = ui::gfx();
	const int top = ui::H - CAPTION_H;
	g.fillRect(0, top, ui::W, CAPTION_H, ui::BG);
	g.drawFastHLine(0, top, ui::W, ui::RULE);

	char left[64];
	char right[32];
	if (caption == 0) {
		snprintf(left, sizeof(left), "%s", parcel[0] == '\0' ? "somewhere" : parcel);
		snprintf(right, sizeof(right), "%s", island);
	} else {
		snprintf(left, sizeof(left), "%s", coords[0] == '\0' ? "no coordinates" : coords);
		snprintf(right, sizeof(right), "#%ld", id);
	}
	ui::small(3, top + 3, left, ui::FG);
	ui::small(ui::W - 3 - (int)strlen(right) * 6, top + 3, right, ui::DIM);

	if (caption == 0) {
		snprintf(left, sizeof(left), "%s  %s", author, created);
	} else {
		snprintf(left, sizeof(left), "%uKB decoded in %ums", (unsigned)(imageBytes / 1024),
		         (unsigned)imageMs);
	}
	ui::small(3, top + 12, left, ui::DIM);
	// A full screen view carries its own source name, and this is the corner
	// the photo can spare.
	ui::small(ui::W - 3 - 6 * 6, top + 12, "VOXELS", ui::RULE);
}

void draw()
{
	if (state == State::Waiting) {
		ui::clearAll(ui::BG);
		char text[48];
		if (job == Job::Image) {
			snprintf(text, sizeof(text), "womp %ld", id);
			ui::message("decoding", text);
		} else {
			ui::message("reading", job == Job::Newest ? "the newest womp" : "a womp");
		}
		ui::spinner(ui::W / 2, 100);
		waitingShown = true;
		return;
	}
	if (state == State::Failed) {
		ui::clearAll(ui::BG);
		char text[48];
		snprintf(text, sizeof(text), "womp %ld", id);
		ui::message(text, net::statusText(status), ui::WARN);
		ui::lineAt(108, "up and down walk the ids", ui::DIM, textdatum_t::top_center);
		return;
	}
	if (state == State::Idle) {
		ui::clearAll(ui::BG);
		ui::message("voxels", net::online() ? "loading the newest" : "needs wifi");
		return;
	}

	// The photo is already on the panel: repainting it would mean fetching
	// 148KB again, so only the caption is drawn here.
	if (!painted) {
		want(Job::Image);
		return;
	}
	drawCaption();
}

// ---------------------------------------------------------------- lifecycle

void enter()
{
	caption = 0;
	painted = false;
	if (!net::online()) {
		state = State::Idle;
		return;
	}
	const long saved = (long)store::getInt(ID_KEY, 0);
	if (saved > 0) {
		id = saved;
		want(Job::Womp);
	} else {
		want(Job::Newest);
	}
}

void leave()
{
	job = Job::None;
	painted = false;
}

void tick()
{
	if (job == Job::None) {
		if (state == State::Idle && net::online()) {
			want(Job::Newest);
		}
		return;
	}
	if (!waitingShown) {
		return;
	}
	const Job running = job;
	job = Job::None;
	waitingShown = false;
	switch (running) {
		case Job::Newest:
			fetchNewest();
			break;
		case Job::Womp:
			fetchWomp();
			break;
		case Job::Image:
			fetchImage();
			break;
		default:
			break;
	}
	view::repaint();
}

// ---------------------------------------------------------------------- keys

bool key(const view::Key &k)
{
	if (state == State::Waiting) {
		return true;
	}

	if (k.ch == 'n' || k.ch == 'N') {
		want(Job::Newest);
		return true;
	}
	if (k.up) {
		open(id + 1);  // ids climb towards the newest
		return true;
	}
	if (k.down) {
		open(id - 1);
		return true;
	}
	if (k.enter || k.right || k.left) {
		caption = (caption + 1) % CAPTIONS;
		view::repaint();
		return true;
	}
	return false;
}

const view::View kWomp = {
    .name = "Womp",
    .source = "VOXELS",
    .order = view::ORDER_WOMP,
    .icon = icons::CAMERA,
    // The picture is the point, so it takes the panel and the caption sits over
    // its bottom edge carrying the source name.
    .fullScreen = true,
    .enter = enter,
    .leave = leave,
    .draw = draw,
    .tick = tick,
    .key = key,
};

}  // namespace

VIEW_REGISTER(kWomp);
