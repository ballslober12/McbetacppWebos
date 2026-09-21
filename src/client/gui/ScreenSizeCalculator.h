#pragma once

#include "java/Type.h"

class Options;

class ScreenSizeCalculator
{
private:
	int_t w = 0;
	int_t h = 0;
public:
	int_t scale = 0;

	// GUI scale of the most recently constructed calculator (display pixels per
	// GUI pixel). The webOS gamepad code uses it to step the cursor by exactly
	// one inventory slot.
	static int_t lastScale;

public:
	ScreenSizeCalculator(const Options &options, int_t width, int_t height);
	int getWidth() const;
	int getHeight() const;
};
