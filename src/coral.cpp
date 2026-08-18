#include "coral.h"

#include <string.h>

#include "net.h"
#include "ui.h"

namespace coral {

namespace {

constexpr const char *SCORE_URL = "https://api.0xcoral.com/api/v1/score/%s/%s";
constexpr int CAVEAT_RULE_Y = 96;
constexpr int CAVEAT_Y = 101;
constexpr const char *CAVEAT_FALLBACK = "Not a price target, audit, or trading recommendation.";

void copyField(JsonVariantConst value, char *out, size_t n, const char *fallback = "")
{
	ui::asciify(value.is<const char *>() ? value.as<const char *>() : fallback, out, n);
}

void identify(Score &s, const char *chain, const char *address)
{
	snprintf(s.id, sizeof(s.id), "%s:%s", chain, address);
}

// One bullet. They arrive as a fact and a reading of it split by a middle dot,
// which ui::asciify leaves as a bar, so the fact keeps full contrast and the
// reading steps back.
void drawBullet(int y, const char *bullet)
{
	const char *split = strchr(bullet, '|');
	if (split == nullptr) {
		ui::small(3, y, bullet, ui::FG);
		return;
	}
	char head[64];
	size_t n = (size_t)(split - bullet);
	if (n >= sizeof(head)) {
		n = sizeof(head) - 1;
	}
	memcpy(head, bullet, n);
	head[n] = '\0';
	ui::small(3, y, head, ui::FG);
	ui::small(3 + (int)n * 6, y, split + 1, ui::DIM);
}

}  // namespace

bool Score::holds(const char *chain, const char *address) const
{
	char wanted[sizeof(id)];
	snprintf(wanted, sizeof(wanted), "%s:%s", chain, address);
	return value >= 0 && strcmp(id, wanted) == 0;
}

void filter(JsonDocument &into)
{
	into["score"] = true;
	into["verdict"] = true;
	into["confidence"] = true;
	into["confidenceLabel"] = true;
	JsonObject explanation = into["explanation"].to<JsonObject>();
	explanation["headline"] = true;
	explanation["bullets"] = true;
	explanation["caveats"] = true;
}

void read(JsonVariantConst answer, const char *chain, const char *address, Score &out)
{
	identify(out, chain, address);
	out.value = answer["score"] | -1;
	out.confidence = answer["confidence"] | 0.0f;
	copyField(answer["verdict"], out.verdict, sizeof(out.verdict), "unknown");
	copyField(answer["confidenceLabel"], out.confidenceLabel, sizeof(out.confidenceLabel));
	copyField(answer["explanation"]["headline"], out.headline, sizeof(out.headline));

	out.bulletCount = 0;
	for (JsonVariantConst b : answer["explanation"]["bullets"].as<JsonArrayConst>()) {
		if (out.bulletCount >= MAX_BULLETS) {
			break;
		}
		copyField(b, out.bullets[out.bulletCount], sizeof(out.bullets[0]));
		out.bulletCount++;
	}

	// Coral sends its caveats as an array and repeats them in a response
	// header. They are not decoration, so one is always on screen behind a
	// score, and a payload that somehow carries none still gets the sentence.
	JsonArrayConst caveats = answer["explanation"]["caveats"].as<JsonArrayConst>();
	copyField(caveats.isNull() ? JsonVariantConst() : caveats[0], out.caveat, sizeof(out.caveat),
	          CAVEAT_FALLBACK);
	if (out.caveat[0] == '\0') {
		snprintf(out.caveat, sizeof(out.caveat), "%s", CAVEAT_FALLBACK);
	}
}

int fetch(const char *chain, const char *address, Score &out)
{
	char url[160];
	snprintf(url, sizeof(url), SCORE_URL, chain, address);

	JsonDocument wanted;
	filter(wanted);

	JsonDocument doc;
	const net::Result r = net::getJson(url, doc, &wanted);
	if (!r.ok()) {
		out.value = -1;
		return r.status;
	}
	read(doc.as<JsonVariantConst>(), chain, address, out);
	return r.status;
}

uint16_t verdictColor(const char *verdict)
{
	if (strcmp(verdict, "organic") == 0) {
		return ui::GOOD;
	}
	if (strcmp(verdict, "manufactured") == 0 || strcmp(verdict, "suspicious") == 0) {
		return ui::BAD;
	}
	return ui::WARN;
}

void drawCaveat(const Score &s)
{
	char lines[2][ui::WRAP_MAX];
	const int count = ui::wrap(s.caveat[0] == '\0' ? CAVEAT_FALLBACK : s.caveat, 39, lines, 2);
	ui::gfx().drawFastHLine(0, CAVEAT_RULE_Y, ui::W, ui::RULE);
	for (int i = 0; i < count; i++) {
		ui::small(3, CAVEAT_Y + i * 10, lines[i], ui::DIM);
	}
}

void drawRead(const Score &s, const char *subject)
{
	char text[64];
	snprintf(text, sizeof(text), "Coral on %s", subject);
	ui::title(text, ui::CORAL);

	snprintf(text, sizeof(text), "%d", s.value);
	ui::bigNumber(text, verdictColor(s.verdict), s.verdict, ui::TITLE_H + 2);

	snprintf(text, sizeof(text), "confidence %s, %.2f", s.confidenceLabel, s.confidence);
	ui::small(3, 64, text, ui::DIM);

	char lines[2][ui::WRAP_MAX];
	const int count = ui::wrap(s.headline, 39, lines, 2);
	for (int i = 0; i < count; i++) {
		ui::small(3, 76 + i * 10, lines[i], ui::FG);
	}

	drawCaveat(s);
}

void drawWhy(const Score &s)
{
	ui::title("what Coral read", ui::CORAL);
	for (int i = 0; i < s.bulletCount; i++) {
		drawBullet(ui::TITLE_H + 3 + i * 14, s.bullets[i]);
	}
	drawCaveat(s);
}

}  // namespace coral
