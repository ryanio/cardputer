# The ADV keyboard

What is printed on the keys of an M5Stack Cardputer ADV, written down so a hint on screen can name
a key the way the key itself names it. Taken from the product photo Ryan sent on 2026-09-26.

Four rows of fourteen keys. Where a key prints two legends the second is the shifted one.

| Row | Keys, left to right |
|---|---|
| 1 | `esc` (orange, over `` ` `` and `~`), `1 !`, `2 @`, `3 #`, `4 $`, `5 %`, `6 ^`, `7 &`, `8 *`, `9 (`, `0 )`, `_ -`, `= +`, `del` (black, with a backspace arrow) |
| 2 | `tab`, `Q` `W` `E` `R` `T` `Y` `U` `I` `O` `P`, `[ {`, `] }`, `\ \|` |
| 3 | `fn` (orange), `Aa` (blue, shift), `A` `S` `D` `F` `G` `H` `J` `K` `L`, `; :` (orange up arrow), `' "`, `ok` (black, with a return arrow) |
| 4 | `ctrl`, `opt` (green), `alt`, `Z` `X` `C` `V` `B` `N` `M`, `, <` (orange left arrow), `. >` (orange down arrow), `/ ?` (orange right arrow), space (a bracket drawn under nothing) |

The screen's left edge carries a column of five indicators, `Aa` `fn` `ctrl` `opt` `alt`, which
are lamps for modifier state and not keys.

## Naming a key in a hint

Write the key's printed legend in square brackets and `ui` draws it as a keycap: `[ok] picks`,
`[del] goes back`, `[esc] cancels`, `[tab] shows it`, `[fn][esc] backtick`, `[R] refreshes`.

- There is no key called enter. The return key prints `ok`, so a hint says `[ok]`.
- Letters print in capitals, so a hint says `[R]` and `[B]`, not `[r]` and `[b]`.
- `del` is the key at the right end of the top row. It erases and it backs out.
- `esc` is the top left key, printed orange with `` ` `` under it. flint reads the key alone as
  escape, and `fn` with it as a backtick.
- The arrows are the four orange arrows on `;` `.` `,` `/` (up, down, left, right), and flint reads
  those keys as arrows without `fn`. None of them prints a word, so hints say "arrows" or "left
  right" in plain text rather than a keycap that matches nothing on the board.
