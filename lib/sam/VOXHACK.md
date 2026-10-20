# SAM in VoxHack

Sebastian Macke's C port of SAM (Software Automatic Mouth, 1982), copied
unchanged from https://github.com/s-macke/SAM (commit a7b36ef, 2020-02-09):
the files in `src/` there, plus its README.

SAM has no open-source licence. Its port is a reverse-engineered version of
commercial software whose owner couldn't be reached (see README.md). VoxHack
includes it accepting that risk, as the port's author describes.

VoxHack uses it in two ways:
- The engine: `src/client/sound/sam_unity.c` compiles these files (not
  `main.c`) as one unit with their globals renamed, and
  `sam_voice.cpp` speaks lines live (sounds named `nhv_sam:...`).
- `sam.exe` (built from all of them by the game's `tools/deploy.sh`), which
  the VoxHack sound editor uses to preview SAM voices.
