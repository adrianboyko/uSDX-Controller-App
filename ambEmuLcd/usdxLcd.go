package ambEmuLcd

// #include "vrEmuLcd.h"
import "C"

const glyphCount = 8
const rowsPerGlyph = 8

var glyph = [glyphCount][rowsPerGlyph]uint8{
	{0b01000, // 1; logo
		0b00100,
		0b01010,
		0b00101,
		0b01010,
		0b00100,
		0b01000,
		0b00000,
	},
	{0b00000, // 2; s-meter, 0 bars
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
	},
	{0b10000, // 3; s-meter, 1 bars
		0b10000,
		0b10000,
		0b10000,
		0b10000,
		0b10000,
		0b10000,
		0b10000,
	},
	{0b10000, // 4; s-meter, 2 bars
		0b10000,
		0b10100,
		0b10100,
		0b10100,
		0b10100,
		0b10100,
		0b10100,
	},
	{0b10000, // 5; s-meter, 3 bars
		0b10000,
		0b10101,
		0b10101,
		0b10101,
		0b10101,
		0b10101,
		0b10101,
	},
	{0b01100, // 6; vfo-a
		0b10010,
		0b11110,
		0b10010,
		0b10010,
		0b00000,
		0b00000,
		0b00000,
	},
	{0b11100, // 7; vfo-b
		0b10010,
		0b11100,
		0b10010,
		0b11100,
		0b00000,
		0b00000,
		0b00000,
	},
	{0b00000, // 8; TBD
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
		0b00000,
	},
}

func createGlyph(lcd AmbEmuLcd, n int, aGlyph [rowsPerGlyph]uint8) {
	C.vrEmuLcdSendCommand(lcd, C.uchar(0x40|(((n+1)&0x7)<<3)))
	for i := 0; i != rowsPerGlyph; i++ {
		C.vrEmuLcdWriteByte(lcd, C.uchar(aGlyph[i]))
	}
}

func InitUsdxGlyphs(lcd AmbEmuLcd) {
	for i := 0; i < glyphCount; i++ {
		createGlyph(lcd, i, glyph[i])
	}
}
