#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Coral's read of a token, and the two screens it is always shown through.
//
// This exists because CLAUDE.md makes one rule about it: anything showing a
// Coral score shows its caveats and the Coral name. A rule with two
// implementations is a rule that will be half kept, so both views that show a
// score go through here and the caveat is drawn by the same function that
// draws the number.
//
// Bankr scores the token behind an agent. Reef makes a game of guessing the
// number before it is revealed. The payloads differ in where the score comes
// from, so there are two ways to fill a Score in and one way to draw it.
namespace coral {

constexpr int MAX_BULLETS = 5;

struct Score {
	// chain:address, the same shape Coral's own tokenId takes. A view compares
	// it to know whether the score it holds is the one on screen.
	char id[80];
	int value = -1;
	float confidence = 0.0f;
	char verdict[16] = {0};
	char confidenceLabel[12] = {0};
	char headline[128] = {0};
	char bullets[MAX_BULLETS][64] = {{0}};
	int bulletCount = 0;
	char caveat[128] = {0};

	bool holds(const char *chain, const char *address) const;
};

// GET score/{chain}/{address}. A full lookup that self rate limits at the
// source and took seconds every time it was probed, so it belongs behind a
// keypress and a waiting screen, never on a timer. Returns a net::Result
// status: 200, or one of the ERR_ values.
int fetch(const char *chain, const char *address, Score &out);

// The same fields out of a payload that already carries them, which is what
// guess/daily returns: one fetch a day and the answer rides along with the
// clues, so a round plays with the radio off.
void read(JsonVariantConst answer, const char *chain, const char *address, Score &out);

// The filter that keeps a score fetch down to what these screens read.
void filter(JsonDocument &into);

// Green for organic, red for manufactured, amber for anything else including
// unknown and whatever Coral names next.
uint16_t verdictColor(const char *verdict);

// Page one: the number, the verdict, the confidence and the headline. `title`
// names what was scored, and the Coral name is drawn whatever it says.
void drawRead(const Score &s, const char *subject);

// Page two: the five bullets, each one a fact and a reading of it.
void drawWhy(const Score &s);

// Drawn by both pages, low on the panel under a rule. Public because a view
// that shows a score its own way still owes the caveat.
void drawCaveat(const Score &s);

}  // namespace coral
