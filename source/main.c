// DSLeaf entry point. Placeholder that proves the toolchain works;
// real modules (input, book, layout, render, ui, storage) come later.
#include <nds.h>
#include <stdio.h>

int main(void) {
	// Bottom screen only, matching single-screen mode (docs/decisions.md 005).
	consoleDemoInit();

	iprintf("\n\n      DSLeaf\n\n");
	iprintf(" A better way to read on DS\n\n");
	iprintf(" Press START to exit\n");

	while (pmMainLoop()) {
		swiWaitForVBlank();
		scanKeys();
		if (keysDown() & KEY_START) break;
	}

	return 0;
}
