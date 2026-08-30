# Analysis scripts

Ghidra scripts that derive the numbers in `src/builds/steam_offsets.cpp` -
function RVAs, call-site return addresses, struct offsets and reflection
bitfield positions - from a local install of the game. They exist so those
numbers can be re-checked rather than taken on trust, and so the next patched
build can be added to the registry the same way.

They run against your own copy of the game, headless: import the shipping exe
into a Ghidra project, then run a script over it with `analyzeHeadless`,
pointing `-process` at the imported program, `-scriptPath` at this directory
and `-postScript` at the script you want.

Each script writes to `.lab/ghidra/<name>.txt`, relative to the directory
`analyzeHeadless` was launched from, so launch it from the repository root.
That directory is gitignored and stays that way: it holds decompiler and
disassembler output, which belongs to the game's authors and is not ours to
publish.

What lands in this repository is the result: numbers, and prose explaining how
each was established. No game code, no assets, no binaries, nothing lifted out
of the executable.
