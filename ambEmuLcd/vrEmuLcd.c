/*
 * Troy's HD44780U Lcd Display Emulator
 *
 * Copyright (c) 2020 Troy Schrapel
 *
 * This code is licensed under the MIT license
 *
 * https://github.com/visrealm/VrEmuLcd
 *
 */

#include "vrEmuLcd.h"
#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <ctype.h>

/* PUBLIC CONSTANTS
 * ---------------------------------------- */
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_CLEAR                = 0b00000001;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_HOME                 = 0b00000010;

VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_ENTRY_MODE           = 0b00000100;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_ENTRY_MODE_INCREMENT = 0b00000010;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_ENTRY_MODE_DECREMENT = 0b00000000;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_ENTRY_MODE_SHIFT     = 0b00000001;

VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_DISPLAY              = 0b00001000;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_DISPLAY_ON           = 0b00000100;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_DISPLAY_CURSOR       = 0b00000010;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_DISPLAY_CURSOR_BLINK = 0b00000001;

VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_SHIFT                = 0b00010000;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_SHIFT_CURSOR         = 0b00000000;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_SHIFT_DISPLAY        = 0b00001000;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_SHIFT_LEFT           = 0b00000000;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_SHIFT_RIGHT          = 0b00000100;

VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_FUNCTION             = 0b00100000;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_FUNCTION_LCD_1LINE   = 0b00000000;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_FUNCTION_LCD_2LINE   = 0b00001000;

VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_SET_CGRAM_ADDR       = 0b01000000;
VR_LCD_EMU_DLLEXPORT const byte LCD_CMD_SET_DRAM_ADDR        = 0b10000000;

/* PRIVATE CONSTANTS
 * ---------------------------------------- */
#define CHAR_WIDTH_PX         5
#define CHAR_HEIGHT_PX        8

#define DATA_WIDTH_CHARS_1ROW 0x80 // AMB changed to hex
#define DATA_WIDTH_CHARS_2ROW 0x40 // AMB changed to hex
#define DATA_WIDTH_CHARS_4ROW 0x20 // AMB changed to hex
#define DDRAM_SIZE            0x80 // AMB changed to hex
#define DDRAM_VISIBLE_SIZE    0x80 // AMB changed to hex

#define DISPLAY_MIN_COLS      8
#define DISPLAY_MAX_COLS     40
#define DISPLAY_MIN_ROWS      1
#define DISPLAY_MAX_ROWS      4

#define CGRAM_STORAGE_CHARS  16
#define ROM_FONT_CHARS       (256 - CGRAM_STORAGE_CHARS)

#define CLOCK_TO_MS  (1.0 / (CLOCKS_PER_SEC / 1000.0))

#define CURSOR_MASK  (LCD_CMD_DISPLAY_CURSOR_BLINK | LCD_CMD_DISPLAY_CURSOR)
#define CURSOR_BLINK_PERIOD_MS  350
#define CURSOR_BLINK_CYCLE_MS  (2 * CURSOR_BLINK_PERIOD_MS)


/* font roms */
static const byte fontA00[ROM_FONT_CHARS][CHAR_WIDTH_PX];
static const byte fontA02[ROM_FONT_CHARS][CHAR_WIDTH_PX];

#define DEFAULT_CGRAM_BYTE  0xaa

static int rowOffsets[] = { 0x00, 0x40, 0x14, 0x54 };


/* PRIVATE DATA STRUCTURE
 * ---------------------------------------- */
struct vrEmuLcd_s
{
  // size in characters
  int cols;
  int rows;

  // current state
  byte entryModeFlags;
  byte displayFlags;
  int scrollOffset;

  // ddRam storage
  byte* ddRam;
  byte* ddPtr;
  int dataWidthCols;

  // cgRam storage
  byte cgRam[CGRAM_STORAGE_CHARS][CHAR_HEIGHT_PX];
  byte* cgPtr;

  // which character rom?
  vrEmuLcdCharacterRom characterRom;

  // display pixels
  char* pixels;
  int pixelsWidth;
  int pixelsHeight;
  int numPixels;
};


/*
 * Function:  increment
 * --------------------
 * increments the ddRam pointer of a VrEmuLcd
 *
 * automatically skips to the correct line and
 * rolls back to the start
 */
static void increment(VrEmuLcd* lcd)
{
  ++lcd->ddPtr;

  // find pointer offset from start
  int offset = lcd->ddPtr - lcd->ddRam;

  // 4 row mode's funky addressing scheme
  if (lcd->rows > 2)
  {
    if (offset == 0x28) lcd->ddPtr = lcd->ddRam + 0x40;
    else if (offset == 0x68 || offset >= DDRAM_SIZE) lcd->ddPtr = lcd->ddRam;
  }
  else if (offset >= DDRAM_VISIBLE_SIZE)
  {
    lcd->ddPtr = lcd->ddRam;
  }
}

/*
 * Function:  decrement
 * --------------------
 * decrements the ddRam pointer of a VrEmuLcd
 *
 * automatically skips to the correct line and
 * rolls back to the end
 */
static void decrement(VrEmuLcd* lcd)
{
  --lcd->ddPtr;

  // find pointer offset from start
  int offset = lcd->ddPtr - lcd->ddRam;

  // 4 row mode's funky addressing scheme
  if (lcd->rows > 2)
  {
    if (offset == -1) lcd->ddPtr = lcd->ddRam + 0x67;
    else if (offset == 0x39) lcd->ddPtr = lcd->ddRam  + 0x27;
  }

  if (offset == -1)
  {
    lcd->ddPtr += DDRAM_VISIBLE_SIZE;
  }
  else if (offset >= DDRAM_SIZE)
  {
    lcd->ddPtr = lcd->ddRam;
  }
}

/*
 * Function:  doShiftDdram
 * --------------------
 * shift the cursor or display as required
 * by the current entry mode flags
 */
static void doShift(VrEmuLcd* lcd)
{
  // if we're looking at cgram, shift the cg pointer
  if (lcd->cgPtr)
  {
    if (lcd->entryModeFlags & LCD_CMD_ENTRY_MODE_INCREMENT)
    {
      ++lcd->cgPtr;
      if (lcd->cgPtr >= (byte*)lcd->cgRam + sizeof(lcd->cgRam))
      {
        lcd->cgPtr = (byte*)lcd->cgRam;
      }
    }
    else
    {
      --lcd->cgPtr;
      if (lcd->cgPtr < (byte*)lcd->cgRam)
      {
        lcd->cgPtr = (byte*)lcd->cgRam + sizeof(lcd->cgRam) - 1;
      }
    }
  }
  // otherwise, shift the ddram pointer or scroll offset
  else
  {
    if (lcd->entryModeFlags & LCD_CMD_ENTRY_MODE_SHIFT)
    {
      if (lcd->entryModeFlags & LCD_CMD_ENTRY_MODE_INCREMENT)
      {
        ++lcd->scrollOffset;
      }
      else
      {
        --lcd->scrollOffset;
      }
    }

    if (lcd->entryModeFlags & LCD_CMD_ENTRY_MODE_INCREMENT)
    {
      increment(lcd);
    }
    else
    {
      decrement(lcd);
    }
  }
}


/* Function:  vrEmuLcdNew
 * --------------------
 * create a new LCD
 *
 * cols: number of display columns  (8 to 40)
 * rows: number of display rows (1, 2 or 4)
 * rom:  character rom to load
 */
VR_LCD_EMU_DLLEXPORT VrEmuLcd* vrEmuLcdNew(int cols, int rows, vrEmuLcdCharacterRom rom)
{
  // validate display size
  if (cols < DISPLAY_MIN_COLS) cols = DISPLAY_MIN_COLS;
  else if (cols > DISPLAY_MAX_COLS) cols = DISPLAY_MAX_COLS;

  if (rows < DISPLAY_MIN_ROWS) rows = DISPLAY_MIN_ROWS;
  else if (rows > DISPLAY_MAX_ROWS) rows = DISPLAY_MAX_ROWS;
  if (rows == 3) rows = 2;

  // build lcd data structure
  VrEmuLcd* lcd = (VrEmuLcd*)malloc(sizeof(VrEmuLcd));
  if (lcd != NULL)
  {
    lcd->cols = cols;
    lcd->rows = rows;
    lcd->characterRom = rom;

    lcd->ddRam = (byte*)malloc(DDRAM_SIZE);
    lcd->ddPtr = lcd->ddRam;
    lcd->entryModeFlags = LCD_CMD_ENTRY_MODE_INCREMENT;
    lcd->displayFlags = LCD_CMD_DISPLAY_ON; //0x00;  AMB change
    lcd->scrollOffset = 0x00;
    lcd->cgPtr = NULL;
    lcd->pixelsWidth = lcd->cols * (CHAR_WIDTH_PX + 1) - 1;
    lcd->pixelsHeight = lcd->rows * (CHAR_HEIGHT_PX + 1) - 1;
    lcd->numPixels = lcd->pixelsWidth * lcd->pixelsHeight;
    lcd->pixels = (char*)malloc(lcd->numPixels);

    switch (lcd->rows)
    {
      case 1:
        lcd->dataWidthCols = DATA_WIDTH_CHARS_1ROW;
        break;

      case 2:
        lcd->dataWidthCols = DATA_WIDTH_CHARS_2ROW;
        break;

      case 4:
        lcd->dataWidthCols = DATA_WIDTH_CHARS_4ROW;
        break;
    }

    // fill arrays with default data
    if (lcd->ddRam != NULL)
    {
	  memset(lcd->ddRam, ' ', DDRAM_SIZE);
    }

    memset(lcd->cgRam, DEFAULT_CGRAM_BYTE, sizeof(lcd->cgRam));
    memset(lcd->pixels, -1, lcd->numPixels);

    vrEmuLcdUpdatePixels(lcd);
  }
  return lcd;
}

/*
 * Function:  vrEmuLcdDestroy
 * --------------------
 * destroy an LCD
 *
 * lcd: lcd object to destroy / clean up
 */
VR_LCD_EMU_DLLEXPORT void vrEmuLcdDestroy(VrEmuLcd* lcd)
{
  if (lcd)
  {
    free(lcd->ddRam);
    free(lcd->pixels);
    memset(lcd, 0, sizeof(VrEmuLcd));
    free(lcd);
  }
}

/*
 * Function:  vrEmuLcdSendCommand
 * --------------------
 * send a command to the lcd (RS is low)
 *
 * byte: the data (DB0 -> DB7) to send
 */
VR_LCD_EMU_DLLEXPORT void vrEmuLcdSendCommand(VrEmuLcd* lcd, byte command)
{
  if (command & LCD_CMD_SET_DRAM_ADDR)
  {
    // ddram address in remaining 7 bits
    int offset = (command & 0x7f);
    lcd->ddPtr = lcd->ddRam + offset;
    lcd->cgPtr = NULL;
  }
  else if (command & LCD_CMD_SET_CGRAM_ADDR)
  {
    // cgram address in remaining 6 bits
    lcd->cgPtr = (byte*)lcd->cgRam + (command & 0x3f);
  }
  else if (command & LCD_CMD_FUNCTION)
  {
    // ignore
  }
  else if (command & LCD_CMD_SHIFT)
  {
    if (command & LCD_CMD_SHIFT_DISPLAY)
    {
      if (command & LCD_CMD_SHIFT_RIGHT)
      {
        --lcd->scrollOffset;
      }
      else
      {
        ++lcd->scrollOffset;
      }
    }
    else
    {
      if (command & LCD_CMD_SHIFT_RIGHT)
      {
        increment(lcd);
      }
      else
      {
        decrement(lcd);
      }
    }
  }
  else if (command & LCD_CMD_DISPLAY)
  {
    lcd->displayFlags = command;
  }
  else if (command & LCD_CMD_ENTRY_MODE)
  {
    lcd->entryModeFlags = command;
  }
  else if (command & LCD_CMD_HOME)
  {
    lcd->ddPtr = lcd->ddRam;
    lcd->scrollOffset = 0;
  }
  else if (command & LCD_CMD_CLEAR)
  {
    if (lcd->ddRam != NULL)
    {
      memset(lcd->ddRam, ' ', DDRAM_SIZE);
    }
    lcd->ddPtr = lcd->ddRam;
    lcd->scrollOffset = 0;
  }
}

/*
 * Function:  vrEmuLcdWriteByte
 * --------------------
 * write a byte to the lcd (RS is high)
 *
 * byte: the data (DB0 -> DB7) to send
 */
VR_LCD_EMU_DLLEXPORT void vrEmuLcdWriteByte(VrEmuLcd* lcd, byte data)
{
  if (lcd->cgPtr)
  {
    // find row offset
    int row = (lcd->cgPtr - (byte*)lcd->cgRam) % CHAR_HEIGHT_PX;

    // find starting byte for the current character
    byte* startAddr = lcd->cgPtr - row;

    for (int i = 0; i < CHAR_WIDTH_PX; ++i)
    {
      byte bit = data & ((0x01 << (CHAR_WIDTH_PX - 1)) >> i);
      if (bit)
      {
        *(startAddr + i) |= (0x80 >> row);
      }
      else
      {
        *(startAddr + i) &= ~(0x80 >> row);
      }
    }
  }
  else
  {
    *lcd->ddPtr = data;
  }
  doShift(lcd);
}


/*
 * Function:  vrEmuLcdReadByte
 * --------------------
 * read a byte from the lcd (RS is high)
 *
 * returns: the data (DB0 -> DB7) at the current address
 */
 VR_LCD_EMU_DLLEXPORT byte vrEmuLcdReadByte(VrEmuLcd* lcd)
{
  byte data = 0;

  if (lcd->cgPtr)
  {
    // find row offset
    int row = (lcd->cgPtr - (byte*)lcd->cgRam) % 8;

    // find starting byte for the current character
    byte* startAddr = lcd->cgPtr - row;

    for (int i = 0; i < CHAR_WIDTH_PX; ++i)
    {
      if (*(startAddr + i) & (0x80 >> row))
      {
        data |= ((0x01 << (CHAR_WIDTH_PX - 1)) >> i);
      }
    }
  }
  else
  {
    data = *lcd->ddPtr;
  }

  doShift(lcd);

  return data;
}


/* Function:  vrEmuLcdReadAddress
 * --------------------
 * read the current address offset (RS is high, R/W is high)
 *
 * returns: the current address
 */
VR_LCD_EMU_DLLEXPORT byte vrEmuLcdReadAddress(VrEmuLcd* lcd)
{
  if (lcd->cgPtr)
  {
    return (lcd->cgPtr - (byte*)lcd->cgRam) & 0x3f;
  }

  return (lcd->ddPtr - lcd->ddRam) & 0x7f;
}

/*
 * Function:  vrEmuLcdWriteString
 * ----------------------------------------
 * write a string to the lcd
 * iterates over the characters and sends them individually
 *
 * str: the string to write.
 */
VR_LCD_EMU_DLLEXPORT void vrEmuLcdWriteString(VrEmuLcd* lcd, const char* str)
{
  const char* ddPtr = str;
  while (*ddPtr != '\0')
  {
    vrEmuLcdWriteByte(lcd, *ddPtr);
    ++ddPtr;
  }
}

/*
 * Function:  vrEmuLcdCharBits
 * ----------------------------------------
 * return a character's pixel data
 *
 * pixel data consists of 5 bytes where each is
 * a vertical row of bits for the character
 *
 * c: character index
 *    0 - 15   cgram
 *    16 - 255 rom
 */
VR_LCD_EMU_DLLEXPORT const byte* vrEmuLcdCharBits(VrEmuLcd* lcd, byte c)
{
  if (c < CGRAM_STORAGE_CHARS)
  {
    return lcd->cgRam[c];
  }

  const int characterRomIndex = c - CGRAM_STORAGE_CHARS;

  switch (lcd->characterRom)
  {
    case EmuLcdRomA00:
      return fontA00[characterRomIndex];

    case EmuLcdRomA02:
    default:
      return fontA02[characterRomIndex];
  }
}

/*
 * Function:  vrEmuLcdGetDataOffset
 * ----------------------------------------
 * return the character offset in ddram for a given
 * row and column.
 *
 * can be used to set the current cursor address
 */
VR_LCD_EMU_DLLEXPORT int vrEmuLcdGetDataOffset(VrEmuLcd* lcd, int row, int col)
{
  // adjust for display scroll offset
  if (row >= lcd->rows) row = lcd->rows - 1;

  while (lcd->scrollOffset < 0)
  {
    lcd->scrollOffset += lcd->dataWidthCols;
  }

  int dataCol = (col + lcd->scrollOffset) % lcd->dataWidthCols;
  int rowOffset = row * lcd->dataWidthCols;

  if (lcd->rows > 2)
  {
    rowOffset = rowOffsets[row];
  }

  return rowOffset + dataCol;
}

/*
 * Function:  vrEmuLcdUpdatePixels
 * ----------------------------------------
 * updates the display's pixel data
 * changes are only reflected in the pixel data when this function is called
 */
VR_LCD_EMU_DLLEXPORT void vrEmuLcdUpdatePixels(VrEmuLcd* lcd)
{
  // determine cursor blink state
  int cursorOn = lcd->displayFlags & CURSOR_MASK;
  if (lcd->displayFlags & LCD_CMD_DISPLAY_CURSOR_BLINK)
  {
    if (((int)(clock() * CLOCK_TO_MS) % CURSOR_BLINK_CYCLE_MS)
          < CURSOR_BLINK_PERIOD_MS)
    {
      cursorOn &= ~LCD_CMD_DISPLAY_CURSOR_BLINK;
    }
  }

  int displayOn = lcd->displayFlags & LCD_CMD_DISPLAY_ON;

  // /cycle through each row of the display
  for (int row = 0; row < lcd->rows; ++row)
  {
    for (int col = 0; col < lcd->cols; ++col)
    {
      // find top-left pixel for the current display character position
      char* charTopLeft = lcd->pixels + (row * (CHAR_HEIGHT_PX + 1) * lcd->pixelsWidth) + col * (CHAR_WIDTH_PX + 1);

      // find current character in ddram
      int offset = vrEmuLcdGetDataOffset(lcd, row, col);
      byte* ddPtr = lcd->ddRam + offset;

      // only draw cursor if the data pointer is pointing at this character
      int drawCursor = cursorOn && (ddPtr == lcd->ddPtr);

      // get the character data (bits) for the current character
      const byte* bits = vrEmuLcdCharBits(lcd, *ddPtr);

      // apply its bits to the pixel data
      for (int y = 0; y < CHAR_HEIGHT_PX; ++y)
      {
        // set pixel pointer
        char* pixel = charTopLeft + y * lcd->pixelsWidth;
        for (int x = 0; x < CHAR_WIDTH_PX; ++x)
        {
          // is the display on?
          if (!displayOn)
          {
            *pixel = 0;
            continue;
          }

          // set the pixel data from the character bits
          // Note: The ROM fonts are defined wrong, shifted one pixel too low. Code added to compensate.
          int glyphShift = (*ddPtr > 15) ? 1 : 0;
          *pixel = (bits[x]<<glyphShift & (0x80 >> y)) ? 1 : 0;

          // override with cursor data if appropriate
          if (drawCursor)
          {
            if ((cursorOn & LCD_CMD_DISPLAY_CURSOR_BLINK) ||
               ((cursorOn & LCD_CMD_DISPLAY_CURSOR) && y == CHAR_HEIGHT_PX - 1))
            {
              *pixel = 1;
            }
          }

          // next pixel
          ++pixel;
        }
      }
    }
  }
}

/*
 * Function:  vrEmuLcdNumPixels
 * ----------------------------------------
 * get the number of pixels for the entire display
 */
VR_LCD_EMU_DLLEXPORT void vrEmuLcdNumPixels(VrEmuLcd* lcd, int* cols, int* rows)
{
  if (cols) *cols = vrEmuLcdNumPixelsX(lcd);
  if (rows) *rows = vrEmuLcdNumPixelsY(lcd);
}

/*
 * Function:  vrEmuLcdNumPixelsX
 * ----------------------------------------
 * returns: number of horizontal pixels in the display
 */
VR_LCD_EMU_DLLEXPORT int vrEmuLcdNumPixelsX(VrEmuLcd* lcd)
{
  return lcd->pixelsWidth;
}

/*
 * Function:  vrEmuLcdNumPixelsY
 * ----------------------------------------
 * returns: number of vertical pixels in the display
 */
VR_LCD_EMU_DLLEXPORT int vrEmuLcdNumPixelsY(VrEmuLcd* lcd)
{
  return lcd->pixelsHeight;
}

/*
 * Function:  char vrEmuLcdPixelState
 * ----------------------------------------
 * returns: pixel state at the given location
 *
 * -1 = no pixel (character borders)  // REVIEW: This is also the out-of-bounds error result?
 *  0 = pixel off
 *  1 = pixel on
 *
 */
VR_LCD_EMU_DLLEXPORT char vrEmuLcdPixelState(VrEmuLcd* lcd, int x, int y)
{
  int offset = y * lcd->pixelsWidth + x;
  if (offset >= 0 && offset < lcd->numPixels)
    return lcd->pixels[offset];
  return -1;
}


VR_LCD_EMU_DLLEXPORT void vrEmuLcdPrintDisplayRam(VrEmuLcd* lcd)
{
  for (int i = 0; i < DDRAM_SIZE; i++)
  {
    byte c = lcd->ddRam[i];
    if (lcd->ddRam+i == lcd->ddPtr) printf("_");
    else if (c < 16) printf(".");
    else printf("%c", c);
  }
  printf("\n");
}



// A00 (Japanese) character set.
// skip first 16 characters reserved for CGRAM
static const byte fontA00[ROM_FONT_CHARS][CHAR_WIDTH_PX] = {
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  16 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  17 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  18 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  19 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  20 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  21 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  22 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  23 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  24 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  25 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  26 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  27 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  28 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  29 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  30 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  31 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, //  32 -
  {0x00, 0x00, 0xf2, 0x00, 0x00}, //  33 - !
  {0x00, 0xe0, 0x00, 0xe0, 0x00}, //  34 - "
  {0x28, 0xfe, 0x28, 0xfe, 0x28}, //  35 - #
  {0x24, 0x54, 0xfe, 0x54, 0x48}, //  36 - $
  {0xc4, 0xc8, 0x10, 0x26, 0x46}, //  37 - %
  {0x6c, 0x92, 0xaa, 0x44, 0x0a}, //  38 - &
  {0x00, 0xa0, 0xc0, 0x00, 0x00}, //  39 - '
  {0x00, 0x38, 0x44, 0x82, 0x00}, //  40 - (
  {0x00, 0x82, 0x44, 0x38, 0x00}, //  41 - )
  {0x28, 0x10, 0x7c, 0x10, 0x28}, //  42 - *
  {0x10, 0x10, 0x7c, 0x10, 0x10}, //  43 - +
  {0x00, 0x0a, 0x0c, 0x00, 0x00}, //  44 - ,
  {0x10, 0x10, 0x10, 0x10, 0x10}, //  45 - -
  {0x00, 0x06, 0x06, 0x00, 0x00}, //  46 - .
  {0x04, 0x08, 0x10, 0x20, 0x40}, //  47 - /
  {0x7c, 0x8a, 0x92, 0xa2, 0x7c}, //  48 - 0
  {0x00, 0x42, 0xfe, 0x02, 0x00}, //  49 - 1
  {0x42, 0x86, 0x8a, 0x92, 0x62}, //  50 - 2
  {0x84, 0x82, 0xa2, 0xd2, 0x8c}, //  51 - 3
  {0x18, 0x28, 0x48, 0xfe, 0x08}, //  52 - 4
  {0xe4, 0xa2, 0xa2, 0xa2, 0x9c}, //  53 - 5
  {0x3c, 0x52, 0x92, 0x92, 0x0c}, //  54 - 6
  {0x80, 0x8e, 0x90, 0xa0, 0xc0}, //  55 - 7
  {0x6c, 0x92, 0x92, 0x92, 0x6c}, //  56 - 8
  {0x60, 0x92, 0x92, 0x94, 0x78}, //  57 - 9
  {0x00, 0x6c, 0x6c, 0x00, 0x00}, //  58 - :
  {0x00, 0x6a, 0x6c, 0x00, 0x00}, //  59 - ;
  {0x10, 0x28, 0x44, 0x82, 0x00}, //  60 - <
  {0x28, 0x28, 0x28, 0x28, 0x28}, //  61 - =
  {0x00, 0x82, 0x44, 0x28, 0x10}, //  62 - >
  {0x40, 0x80, 0x8a, 0x90, 0x60}, //  63 - ?
  {0x4c, 0x92, 0x9e, 0x82, 0x7c}, //  64 - @
  {0x7e, 0x90, 0x90, 0x90, 0x7e}, //  65 - A
  {0xfe, 0x92, 0x92, 0x92, 0x6c}, //  66 - B
  {0x7c, 0x82, 0x82, 0x82, 0x44}, //  67 - C
  {0xfe, 0x82, 0x82, 0x44, 0x38}, //  68 - D
  {0xfe, 0x92, 0x92, 0x92, 0x82}, //  69 - E
  {0xfe, 0x90, 0x90, 0x90, 0x80}, //  70 - F
  {0x7c, 0x82, 0x92, 0x92, 0x5e}, //  71 - G
  {0xfe, 0x10, 0x10, 0x10, 0xfe}, //  72 - H
  {0x00, 0x82, 0xfe, 0x82, 0x00}, //  73 - I
  {0x04, 0x82, 0x82, 0xfc, 0x00}, //  74 - J
  {0xfe, 0x10, 0x28, 0x44, 0x82}, //  75 - K
  {0xfe, 0x02, 0x02, 0x02, 0x02}, //  76 - L
  {0xfe, 0x40, 0x30, 0x40, 0xfe}, //  77 - M
  {0xfe, 0x20, 0x10, 0x08, 0xfe}, //  78 - N
  {0x7c, 0x82, 0x82, 0x82, 0x7c}, //  79 - O
  {0xfe, 0x90, 0x90, 0x90, 0x60}, //  80 - P
  {0x7c, 0x82, 0x8a, 0x84, 0x7a}, //  81 - Q
  {0xfe, 0x90, 0x98, 0x94, 0x62}, //  82 - R
  {0x62, 0x92, 0x92, 0x92, 0x8c}, //  83 - S
  {0x80, 0x80, 0xfe, 0x80, 0x80}, //  84 - T
  {0xfc, 0x02, 0x02, 0x02, 0xfc}, //  85 - U
  {0xf8, 0x04, 0x02, 0x04, 0xf8}, //  86 - V
  {0xfc, 0x02, 0x1c, 0x02, 0xfc}, //  87 - W
  {0xc6, 0x28, 0x10, 0x28, 0xc6}, //  88 - X
  {0xe0, 0x10, 0x0e, 0x10, 0xe0}, //  89 - Y
  {0x86, 0x8a, 0x92, 0xa2, 0xc2}, //  90 - Z
  {0x00, 0xfe, 0x82, 0x82, 0x00}, //  91 - [
  {0xa8, 0x68, 0x3e, 0x68, 0xa8}, //  92 - fwd slash
  {0x00, 0x82, 0x82, 0xfe, 0x00}, //  93 - ]
  {0x20, 0x40, 0x80, 0x40, 0x20}, //  94 - ^
  {0x02, 0x02, 0x02, 0x02, 0x02}, //  95 - _
  {0x00, 0x80, 0x40, 0x20, 0x00}, //  96 - `
  {0x04, 0x2a, 0x2a, 0x2a, 0x1e}, //  97 - a
  {0xfe, 0x12, 0x22, 0x22, 0x1c}, //  98 - b
  {0x1c, 0x22, 0x22, 0x22, 0x04}, //  99 - c
  {0x1c, 0x22, 0x22, 0x12, 0xfe}, // 100 - d
  {0x1c, 0x2a, 0x2a, 0x2a, 0x18}, // 101 - e
  {0x10, 0x7e, 0x90, 0x80, 0x40}, // 102 - f
  {0x30, 0x4a, 0x4a, 0x4a, 0x7c}, // 103 - g
  {0xfe, 0x10, 0x20, 0x20, 0x1e}, // 104 - h
  {0x00, 0x22, 0xbe, 0x02, 0x00}, // 105 - i
  {0x04, 0x02, 0x22, 0xbc, 0x00}, // 106 - j
  {0xfe, 0x08, 0x14, 0x22, 0x00}, // 107 - k
  {0x02, 0x82, 0xfe, 0x02, 0x02}, // 108 - l
  {0x3e, 0x20, 0x18, 0x20, 0x1e}, // 109 - m
  {0x3e, 0x10, 0x20, 0x20, 0x1e}, // 110 - n
  {0x1c, 0x22, 0x22, 0x22, 0x1c}, // 111 - o
  {0x3e, 0x28, 0x28, 0x28, 0x10}, // 112 - p
  {0x10, 0x28, 0x28, 0x18, 0x3e}, // 113 - q
  {0x3e, 0x10, 0x20, 0x20, 0x10}, // 114 - r
  {0x12, 0x2a, 0x2a, 0x2a, 0x04}, // 115 - s
  {0x20, 0xfc, 0x22, 0x02, 0x04}, // 116 - t
  {0x3c, 0x02, 0x02, 0x04, 0x3e}, // 117 - u
  {0x38, 0x04, 0x02, 0x04, 0x38}, // 118 - v
  {0x3c, 0x02, 0x0c, 0x02, 0x3c}, // 119 - w
  {0x22, 0x14, 0x08, 0x14, 0x22}, // 120 - x
  {0x30, 0x0a, 0x0a, 0x0a, 0x3c}, // 121 - y
  {0x22, 0x26, 0x2a, 0x32, 0x22}, // 122 - z
  {0x00, 0x10, 0x6c, 0x82, 0x00}, // 123 - {
  {0x00, 0x00, 0xfe, 0x00, 0x00}, // 124 - |
  {0x00, 0x82, 0x6c, 0x10, 0x00}, // 125 - }
  {0x10, 0x10, 0x54, 0x38, 0x10}, // 126 - ~
  {0x10, 0x38, 0x54, 0x10, 0x10}, // 127 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 128 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 129 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 130 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 131 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 132 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 133 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 134 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 135 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 136 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 137 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 138 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 139 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 140 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 141 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 142 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 143 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 144 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 145 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 146 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 147 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 148 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 149 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 150 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 151 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 152 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 153 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 154 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 155 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 156 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 157 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 158 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 159 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 160 -
  {0x0e, 0x0a, 0x0e, 0x00, 0x00}, // 161 -
  {0x00, 0x00, 0xf0, 0x80, 0x80}, // 162 -
  {0x02, 0x02, 0x1e, 0x00, 0x00}, // 163 -
  {0x08, 0x04, 0x02, 0x00, 0x00}, // 164 -
  {0x00, 0x18, 0x18, 0x00, 0x00}, // 165 -
  {0x50, 0x50, 0x52, 0x54, 0x78}, // 166 -
  {0x20, 0x22, 0x2c, 0x28, 0x30}, // 167 -
  {0x04, 0x08, 0x1e, 0x20, 0x00}, // 168 -
  {0x18, 0x12, 0x32, 0x12, 0x1c}, // 169 -
  {0x12, 0x12, 0x1e, 0x12, 0x12}, // 170 -
  {0x12, 0x14, 0x18, 0x3e, 0x10}, // 171 -
  {0x10, 0x3e, 0x10, 0x14, 0x18}, // 172 -
  {0x02, 0x12, 0x12, 0x1e, 0x02}, // 173 -
  {0x2a, 0x2a, 0x2a, 0x3e, 0x00}, // 174 -
  {0x18, 0x00, 0x1a, 0x02, 0x1c}, // 175 -
  {0x10, 0x10, 0x10, 0x10, 0x10}, // 176 -
  {0x80, 0x82, 0xbc, 0x90, 0xe0}, // 177 -
  {0x08, 0x10, 0x3e, 0x40, 0x80}, // 178 -
  {0x70, 0x40, 0xc2, 0x44, 0x78}, // 179 -
  {0x42, 0x42, 0x7e, 0x42, 0x42}, // 180 -
  {0x44, 0x48, 0x50, 0xfe, 0x40}, // 181 -
  {0x42, 0xfc, 0x40, 0x42, 0x7c}, // 182 -
  {0x50, 0x50, 0xfe, 0x50, 0x50}, // 183 -
  {0x10, 0x62, 0x42, 0x44, 0x78}, // 184 -
  {0x20, 0xc0, 0x42, 0x7c, 0x40}, // 185 -
  {0x42, 0x42, 0x42, 0x42, 0x7e}, // 186 -
  {0x40, 0xf2, 0x44, 0xf8, 0x40}, // 187 -
  {0x52, 0x52, 0x02, 0x04, 0x38}, // 188 -
  {0x42, 0x44, 0x48, 0x54, 0x62}, // 189 -
  {0x40, 0xfc, 0x42, 0x52, 0x62}, // 190 -
  {0x60, 0x12, 0x02, 0x04, 0x78}, // 191 -
  {0x10, 0x62, 0x52, 0x4c, 0x78}, // 192 -
  {0x50, 0x52, 0x7c, 0x90, 0x10}, // 193 -
  {0x70, 0x00, 0x72, 0x04, 0x78}, // 194 -
  {0x20, 0xa2, 0xbc, 0xa0, 0x20}, // 195 -
  {0x00, 0xfe, 0x10, 0x08, 0x00}, // 196 -
  {0x22, 0x24, 0xf8, 0x20, 0x20}, // 197 -
  {0x02, 0x42, 0x42, 0x42, 0x02}, // 198 -
  {0x42, 0x54, 0x48, 0x54, 0x60}, // 199 -
  {0x44, 0x48, 0xde, 0x68, 0x44}, // 200 -
  {0x00, 0x02, 0x04, 0xf8, 0x00}, // 201 -
  {0x1e, 0x00, 0x40, 0x20, 0x1e}, // 202 -
  {0xfc, 0x22, 0x22, 0x22, 0x22}, // 203 -
  {0x40, 0x42, 0x42, 0x44, 0x78}, // 204 -
  {0x20, 0x40, 0x20, 0x10, 0x0c}, // 205 -
  {0x4c, 0x40, 0xfe, 0x40, 0x4c}, // 206 -
  {0x40, 0x48, 0x44, 0x4a, 0x70}, // 207 -
  {0x00, 0x54, 0x54, 0x54, 0x02}, // 208 -
  {0x1c, 0x24, 0x44, 0x04, 0x0e}, // 209 -
  {0x02, 0x14, 0x08, 0x14, 0x60}, // 210 -
  {0x50, 0x7c, 0x52, 0x52, 0x52}, // 211 -
  {0x20, 0xfe, 0x20, 0x28, 0x30}, // 212 -
  {0x02, 0x42, 0x42, 0x7e, 0x02}, // 213 -
  {0x52, 0x52, 0x52, 0x52, 0x7e}, // 214 -
  {0x20, 0xa0, 0xa2, 0xa4, 0x38}, // 215 -
  {0xf0, 0x02, 0x04, 0xf8, 0x00}, // 216 -
  {0x3e, 0x00, 0x7e, 0x02, 0x0c}, // 217 -
  {0x7e, 0x02, 0x04, 0x08, 0x10}, // 218 -
  {0x7e, 0x42, 0x42, 0x42, 0x7e}, // 219 -
  {0x70, 0x40, 0x42, 0x44, 0x78}, // 220 -
  {0x42, 0x42, 0x02, 0x04, 0x18}, // 221 -
  {0x40, 0x20, 0x80, 0x40, 0x00}, // 222 -
  {0xe0, 0xa0, 0xe0, 0x00, 0x00}, // 223 -
  {0x1c, 0x22, 0x12, 0x0c, 0x32}, // 224 -
  {0x04, 0xaa, 0x2a, 0xaa, 0x1e}, // 225 -
  {0x1f, 0x2a, 0x2a, 0x2a, 0x14}, // 226 -
  {0x14, 0x2a, 0x2a, 0x22, 0x04}, // 227 -
  {0x3f, 0x02, 0x02, 0x04, 0x3e}, // 228 -
  {0x1c, 0x22, 0x32, 0x2a, 0x24}, // 229 -
  {0x0f, 0x12, 0x22, 0x22, 0x1c}, // 230 -
  {0x1c, 0x22, 0x22, 0x22, 0x3f}, // 231 -
  {0x04, 0x02, 0x3c, 0x20, 0x20}, // 232 -
  {0x20, 0x20, 0x00, 0x70, 0x00}, // 233 -
  {0x00, 0x00, 0x20, 0xbf, 0x00}, // 234 -
  {0x50, 0x20, 0x50, 0x00, 0x00}, // 235 -
  {0x18, 0x24, 0x7e, 0x24, 0x08}, // 236 -
  {0x28, 0xfe, 0x2a, 0x02, 0x02}, // 237 -
  {0x3e, 0x90, 0xa0, 0xa0, 0x1e}, // 238 -
  {0x1c, 0xa2, 0x22, 0xa2, 0x1c}, // 239 -
  {0x3f, 0x12, 0x22, 0x22, 0x1c}, // 240 -
  {0x1c, 0x22, 0x22, 0x12, 0x3f}, // 241 -
  {0x3c, 0x52, 0x52, 0x52, 0x3c}, // 242 -
  {0x0c, 0x14, 0x08, 0x14, 0x18}, // 243 -
  {0x1a, 0x26, 0x20, 0x26, 0x1a}, // 244 -
  {0x3c, 0x82, 0x02, 0x84, 0x3e}, // 245 -
  {0xc6, 0xaa, 0x92, 0x82, 0x82}, // 246 -
  {0x22, 0x3c, 0x20, 0x3e, 0x22}, // 247 -
  {0xa2, 0x94, 0x88, 0x94, 0xa2}, // 248 -
  {0x3c, 0x02, 0x02, 0x02, 0x3f}, // 249 -
  {0x28, 0x28, 0x3e, 0x28, 0x48}, // 250 -
  {0x22, 0x3c, 0x28, 0x28, 0x2e}, // 251 -
  {0x3e, 0x28, 0x38, 0x28, 0x3e}, // 252 -
  {0x08, 0x08, 0x2a, 0x08, 0x08}, // 253 -
  {0x00, 0x00, 0x00, 0x00, 0x00}, // 254 -
  {0xff, 0xff, 0xff, 0xff, 0xff}  // 255 -
};


// A02 (European) character set.
// skip first 16 characters reserved for CGRAM
static const byte fontA02[ROM_FONT_CHARS][CHAR_WIDTH_PX] = {
 {0x00, 0x7f, 0x3e, 0x1c, 0x08}, //  16 -
 {0x08, 0x1c, 0x3e, 0x7f, 0x00}, //  17 -
 {0x30, 0x50, 0x00, 0x30, 0x50}, //  18 -
 {0x50, 0x60, 0x00, 0x50, 0x60}, //  19 -
 {0x11, 0x33, 0x77, 0x33, 0x11}, //  20 -
 {0x44, 0x66, 0x77, 0x66, 0x44}, //  21 -
 {0x1c, 0x3e, 0x3e, 0x3e, 0x1c}, //  22 -
 {0x04, 0x0e, 0x15, 0x04, 0x7c}, //  23 -
 {0x10, 0x20, 0x7f, 0x20, 0x10}, //  24 -
 {0x04, 0x02, 0x7f, 0x02, 0x04}, //  25 -
 {0x08, 0x08, 0x2a, 0x1c, 0x08}, //  26 -
 {0x08, 0x1c, 0x2a, 0x08, 0x08}, //  27 -
 {0x01, 0x11, 0x29, 0x45, 0x01}, //  28 -
 {0x01, 0x45, 0x29, 0x11, 0x01}, //  29 -
 {0x02, 0x0e, 0x3e, 0x0e, 0x02}, //  30 -
 {0x20, 0x38, 0x3e, 0x38, 0x20}, //  31 -
 {0x00, 0x00, 0x00, 0x00, 0x00}, //  32 -
 {0x00, 0x00, 0x79, 0x00, 0x00}, //  33 - !
 {0x00, 0x70, 0x00, 0x70, 0x00}, //  34 - "
 {0x14, 0x7f, 0x14, 0x7f, 0x14}, //  35 - #
 {0x12, 0x2a, 0x7f, 0x2a, 0x24}, //  36 - $
 {0x62, 0x64, 0x08, 0x13, 0x23}, //  37 - %
 {0x36, 0x49, 0x55, 0x22, 0x05}, //  38 - &
 {0x00, 0x50, 0x60, 0x00, 0x00}, //  39 - '
 {0x00, 0x1c, 0x22, 0x41, 0x00}, //  40 - (
 {0x00, 0x41, 0x22, 0x1c, 0x00}, //  41 - )
 {0x14, 0x08, 0x3e, 0x08, 0x14}, //  42 - *
 {0x08, 0x08, 0x3e, 0x08, 0x08}, //  43 - +
 {0x00, 0x05, 0x06, 0x00, 0x00}, //  44 - ,
 {0x08, 0x08, 0x08, 0x08, 0x08}, //  45 - -
 {0x00, 0x03, 0x03, 0x00, 0x00}, //  46 - .
 {0x02, 0x04, 0x08, 0x10, 0x20}, //  47 - /
 {0x3e, 0x45, 0x49, 0x51, 0x3e}, //  48 - 0
 {0x00, 0x21, 0x7f, 0x01, 0x00}, //  49 - 1
 {0x21, 0x43, 0x45, 0x49, 0x31}, //  50 - 2
 {0x42, 0x41, 0x51, 0x69, 0x46}, //  51 - 3
 {0x0c, 0x14, 0x24, 0x7f, 0x04}, //  52 - 4
 {0x72, 0x51, 0x51, 0x51, 0x4e}, //  53 - 5
 {0x1e, 0x29, 0x49, 0x49, 0x06}, //  54 - 6
 {0x40, 0x47, 0x48, 0x50, 0x60}, //  55 - 7
 {0x36, 0x49, 0x49, 0x49, 0x36}, //  56 - 8
 {0x30, 0x49, 0x49, 0x4a, 0x3c}, //  57 - 9
 {0x00, 0x36, 0x36, 0x00, 0x00}, //  58 - :
 {0x00, 0x35, 0x36, 0x00, 0x00}, //  59 - ;
 {0x08, 0x14, 0x22, 0x41, 0x00}, //  60 - <
 {0x14, 0x14, 0x14, 0x14, 0x14}, //  61 - =
 {0x00, 0x41, 0x22, 0x14, 0x08}, //  62 - >
 {0x20, 0x40, 0x45, 0x48, 0x30}, //  63 - ?
 {0x26, 0x49, 0x4f, 0x41, 0x3e}, //  64 - @
 {0x1f, 0x24, 0x44, 0x24, 0x1f}, //  65 - A
 {0x7f, 0x49, 0x49, 0x49, 0x36}, //  66 - B
 {0x3e, 0x41, 0x41, 0x41, 0x22}, //  67 - C
 {0x7f, 0x41, 0x41, 0x22, 0x1c}, //  68 - D
 {0x7f, 0x49, 0x49, 0x49, 0x41}, //  69 - E
 {0x7f, 0x48, 0x48, 0x48, 0x40}, //  70 - F
 {0x3e, 0x41, 0x49, 0x49, 0x2f}, //  71 - G
 {0x7f, 0x08, 0x08, 0x08, 0x7f}, //  72 - H
 {0x00, 0x41, 0x7f, 0x41, 0x00}, //  73 - I
 {0x02, 0x41, 0x41, 0x7e, 0x00}, //  74 - J
 {0x7f, 0x08, 0x14, 0x22, 0x41}, //  75 - K
 {0x7f, 0x01, 0x01, 0x01, 0x01}, //  76 - L
 {0x7f, 0x20, 0x18, 0x20, 0x7f}, //  77 - M
 {0x7f, 0x10, 0x08, 0x04, 0x7f}, //  78 - N
 {0x3e, 0x41, 0x41, 0x41, 0x3e}, //  79 - O
 {0x7f, 0x48, 0x48, 0x48, 0x30}, //  80 - P
 {0x3e, 0x41, 0x45, 0x42, 0x3d}, //  81 - Q
 {0x7f, 0x48, 0x4c, 0x4a, 0x31}, //  82 - R
 {0x31, 0x49, 0x49, 0x49, 0x46}, //  83 - S
 {0x40, 0x40, 0x7f, 0x40, 0x40}, //  84 - T
 {0x7e, 0x01, 0x01, 0x01, 0x7e}, //  85 - U
 {0x7c, 0x02, 0x01, 0x02, 0x7c}, //  86 - V
 {0x7e, 0x01, 0x0e, 0x01, 0x7e}, //  87 - W
 {0x63, 0x14, 0x08, 0x14, 0x63}, //  88 - X
 {0x70, 0x08, 0x07, 0x08, 0x70}, //  89 - Y
 {0x43, 0x45, 0x49, 0x51, 0x61}, //  90 - Z
 {0x00, 0x7f, 0x41, 0x41, 0x00}, //  91 - [
 {0x20, 0x10, 0x08, 0x04, 0x02}, //  92 - fwd slash
 {0x00, 0x41, 0x41, 0x7f, 0x00}, //  93 - ]
 {0x10, 0x20, 0x40, 0x20, 0x10}, //  94 - ^
 {0x01, 0x01, 0x01, 0x01, 0x01}, //  95 - _
 {0x00, 0x40, 0x20, 0x10, 0x00}, //  96 - `
 {0x02, 0x15, 0x15, 0x15, 0x0f}, //  97 - a
 {0x7f, 0x09, 0x11, 0x11, 0x0e}, //  98 - b
 {0x0e, 0x11, 0x11, 0x11, 0x02}, //  99 - c
 {0x0e, 0x11, 0x11, 0x09, 0x7f}, // 100 - d
 {0x0e, 0x15, 0x15, 0x15, 0x0c}, // 101 - e
 {0x08, 0x3f, 0x48, 0x40, 0x20}, // 102 - f
 {0x18, 0x25, 0x25, 0x25, 0x3e}, // 103 - g
 {0x7f, 0x08, 0x10, 0x10, 0x0f}, // 104 - h
 {0x00, 0x09, 0x5f, 0x01, 0x00}, // 105 - i
 {0x02, 0x01, 0x11, 0x5e, 0x00}, // 106 - j
 {0x7f, 0x04, 0x0a, 0x11, 0x00}, // 107 - k
 {0x01, 0x41, 0x7f, 0x01, 0x01}, // 108 - l
 {0x1f, 0x10, 0x0c, 0x10, 0x0f}, // 109 - m
 {0x1f, 0x08, 0x10, 0x10, 0x0f}, // 110 - n
 {0x0e, 0x11, 0x11, 0x11, 0x0e}, // 111 - o
 {0x1f, 0x14, 0x14, 0x14, 0x08}, // 112 - p
 {0x08, 0x14, 0x14, 0x0c, 0x1f}, // 113 - q
 {0x1f, 0x08, 0x10, 0x10, 0x08}, // 114 - r
 {0x09, 0x15, 0x15, 0x15, 0x02}, // 115 - s
 {0x10, 0x7e, 0x11, 0x01, 0x02}, // 116 - t
 {0x1e, 0x01, 0x01, 0x02, 0x1f}, // 117 - u
 {0x1c, 0x02, 0x01, 0x02, 0x1c}, // 118 - v
 {0x1e, 0x01, 0x06, 0x01, 0x1e}, // 119 - w
 {0x11, 0x0a, 0x04, 0x0a, 0x11}, // 120 - x
 {0x18, 0x05, 0x05, 0x05, 0x1e}, // 121 - y
 {0x11, 0x13, 0x15, 0x19, 0x11}, // 122 - z
 {0x00, 0x08, 0x36, 0x41, 0x00}, // 123 - {
 {0x00, 0x00, 0x7f, 0x00, 0x00}, // 124 - |
 {0x00, 0x41, 0x36, 0x08, 0x00}, // 125 - }
 {0x04, 0x08, 0x08, 0x04, 0x08}, // 126 - ~
 {0x1e, 0x22, 0x42, 0x22, 0x1e}, // 127 -
 {0x7f, 0x49, 0x49, 0x49, 0x66}, // 128 -
 {0x0f, 0x94, 0xe4, 0x84, 0xff}, // 129 -
 {0x77, 0x08, 0x7f, 0x08, 0x77}, // 130 -
 {0x41, 0x41, 0x49, 0x49, 0x36}, // 131 -
 {0x7f, 0x04, 0x08, 0x10, 0x7f}, // 132 -
 {0x3f, 0x84, 0x48, 0x90, 0x3f}, // 133 -
 {0x02, 0x41, 0x7e, 0x40, 0x7f}, // 134 -
 {0x7f, 0x40, 0x40, 0x40, 0x7f}, // 135 -
 {0x71, 0x0a, 0x04, 0x08, 0x70}, // 136 -
 {0x7e, 0x02, 0x02, 0x02, 0x7f}, // 137 -
 {0x70, 0x08, 0x08, 0x08, 0x7f}, // 138 -
 {0x3f, 0x01, 0x3f, 0x01, 0x3f}, // 139 -
 {0x7e, 0x02, 0x7e, 0x02, 0x7f}, // 140 -
 {0x40, 0x7f, 0x09, 0x09, 0x06}, // 141 -
 {0x7f, 0x09, 0x06, 0x00, 0x7f}, // 142 -
 {0x22, 0x49, 0x51, 0x49, 0x3e}, // 143 -
 {0x0e, 0x11, 0x09, 0x06, 0x19}, // 144 -
 {0x03, 0x03, 0x7f, 0x20, 0x18}, // 145 -
 {0x7f, 0x40, 0x40, 0x40, 0x60}, // 146 -
 {0x11, 0x1e, 0x10, 0x1f, 0x11}, // 147 -
 {0x63, 0x55, 0x49, 0x41, 0x41}, // 148 -
 {0x0e, 0x11, 0x11, 0x1e, 0x10}, // 149 -
 {0x06, 0x06, 0xfc, 0xa3, 0x7f}, // 150 -
 {0x08, 0x10, 0x1e, 0x11, 0x20}, // 151 -
 {0x04, 0x3c, 0x7e, 0x3c, 0x04}, // 152 -
 {0x3e, 0x49, 0x49, 0x49, 0x3e}, // 153 -
 {0x1d, 0x23, 0x20, 0x23, 0x1d}, // 154 -
 {0x06, 0x29, 0x51, 0x49, 0x26}, // 155 -
 {0x0c, 0x14, 0x08, 0x14, 0x18}, // 156 -
 {0x1c, 0x3e, 0x1f, 0x3e, 0x1c}, // 157 -
 {0x0a, 0x15, 0x15, 0x11, 0x02}, // 158 -
 {0x3f, 0x40, 0x40, 0x40, 0x3f}, // 159 -
 {0x7f, 0x7f, 0x00, 0x7f, 0x7f}, // 160 -
 {0x00, 0x00, 0x4f, 0x00, 0x00}, // 161 - ¡
 {0x1c, 0x22, 0x7f, 0x22, 0x04}, // 162 - ¢
 {0x09, 0x3e, 0x49, 0x41, 0x02}, // 163 - £
 {0x22, 0x1c, 0x14, 0x1c, 0x22}, // 164 - ¤
 {0x54, 0x34, 0x1f, 0x34, 0x54}, // 165 - ¥
 {0x00, 0x00, 0x77, 0x00, 0x00}, // 166 - ¦
 {0x02, 0x29, 0x55, 0x4a, 0x20}, // 167 - §
 {0x0a, 0x09, 0x3e, 0x48, 0x28}, // 168 - ¨
 {0x7f, 0x41, 0x5d, 0x49, 0x7f}, // 169 - ©
 {0x09, 0x55, 0x55, 0x55, 0x3d}, // 170 - ª
 {0x08, 0x14, 0x2a, 0x14, 0x22}, // 171 - «
 {0x7f, 0x08, 0x3e, 0x41, 0x3e}, // 172 - ¬
 {0x31, 0x4a, 0x4c, 0x48, 0x7f}, // 173 - ­
 {0x7f, 0x41, 0x53, 0x45, 0x7f}, // 174 - ®
 {0x00, 0x30, 0x50, 0x00, 0x00}, // 175 - ¯
 {0x70, 0x88, 0x88, 0x70, 0x00}, // 176 - °
 {0x11, 0x11, 0x7d, 0x11, 0x11}, // 177 - ±
 {0x48, 0x98, 0xa8, 0x48, 0x00}, // 178 - ²
 {0x88, 0xa8, 0xa8, 0x50, 0x00}, // 179 - ³
 {0xfe, 0xa0, 0xa4, 0x4f, 0x05}, // 180 - ´
 {0x7f, 0x04, 0x04, 0x08, 0x7c}, // 181 - µ
 {0x30, 0x48, 0x48, 0x7f, 0x7f}, // 182 - ¶
 {0x00, 0x0c, 0x0c, 0x00, 0x00}, // 183 - ·
 {0x0e, 0x11, 0x06, 0x11, 0x0e}, // 184 - ¸
 {0x48, 0xf8, 0x08, 0x00, 0x00}, // 185 - ¹
 {0x39, 0x45, 0x45, 0x45, 0x39}, // 186 - º
 {0x22, 0x14, 0x2a, 0x14, 0x08}, // 187 - »
 {0xe8, 0x16, 0x2a, 0x5f, 0x82}, // 188 - ¼
 {0xe8, 0x10, 0x29, 0x53, 0x8d}, // 189 - ½
 {0xa8, 0xf8, 0x06, 0x0a, 0x1f}, // 190 - ¾
 {0x06, 0x09, 0x51, 0x01, 0x02}, // 191 - ¿
 {0x0f, 0x94, 0x64, 0x14, 0x0f}, // 192 - À
 {0x0f, 0x14, 0x64, 0x94, 0x0f}, // 193 - Á
 {0x0f, 0x54, 0x94, 0x54, 0x0f}, // 194 - Â
 {0x4f, 0x94, 0x94, 0x54, 0x8f}, // 195 - Ã
 {0x0f, 0x94, 0x24, 0x94, 0x0f}, // 196 - Ä
 {0x0f, 0x54, 0xa4, 0x54, 0x0f}, // 197 - Å
 {0x1f, 0x24, 0x7f, 0x49, 0x49}, // 198 - Æ
 {0x78, 0x84, 0x85, 0x87, 0x48}, // 199 - Ç
 {0x1f, 0x95, 0x55, 0x15, 0x11}, // 200 - È
 {0x1f, 0x15, 0x55, 0x95, 0x11}, // 201 - É
 {0x1f, 0x55, 0x95, 0x55, 0x11}, // 202 - Ê
 {0x1f, 0x55, 0x15, 0x55, 0x11}, // 203 - Ë
 {0x00, 0x91, 0x5f, 0x11, 0x00}, // 204 - Ì
 {0x00, 0x11, 0x5f, 0x91, 0x00}, // 205 - Í
 {0x00, 0x51, 0x9f, 0x51, 0x00}, // 206 - Î
 {0x00, 0x51, 0x1f, 0x51, 0x00}, // 207 - Ï
 {0x08, 0x7f, 0x49, 0x41, 0x3e}, // 208 - Ð
 {0x5f, 0x88, 0x84, 0x42, 0x9f}, // 209 - Ñ
 {0x1e, 0xa1, 0x61, 0x21, 0x1e}, // 210 - Ò
 {0x1e, 0x21, 0x61, 0xa1, 0x1e}, // 211 - Ó
 {0x0e, 0x51, 0x91, 0x51, 0x0e}, // 212 - Ô
 {0x4e, 0x91, 0x91, 0x51, 0x8e}, // 213 - Õ
 {0x1e, 0xa1, 0x21, 0xa1, 0x1e}, // 214 - Ö
 {0x22, 0x14, 0x08, 0x14, 0x22}, // 215 - ×
 {0x08, 0x55, 0x7f, 0x55, 0x08}, // 216 - Ø
 {0x3e, 0x81, 0x41, 0x01, 0x3e}, // 217 - Ù
 {0x3e, 0x01, 0x41, 0x81, 0x3e}, // 218 - Ú
 {0x1e, 0x41, 0x81, 0x41, 0x1e}, // 219 - Û
 {0x3e, 0x81, 0x01, 0x81, 0x3e}, // 220 - Ü
 {0x20, 0x10, 0x4f, 0x90, 0x20}, // 221 - Ý
 {0x81, 0xff, 0x25, 0x24, 0x18}, // 222 - Þ
 {0x01, 0x3e, 0x49, 0x49, 0x36}, // 223 - ß
 {0x02, 0x95, 0x55, 0x15, 0x0f}, // 224 - à
 {0x02, 0x15, 0x55, 0x95, 0x0f}, // 225 - á
 {0x02, 0x55, 0x95, 0x55, 0x0f}, // 226 - â
 {0x42, 0x95, 0x95, 0x55, 0x8f}, // 227 - ã
 {0x02, 0x55, 0x15, 0x55, 0x0f}, // 228 - ä
 {0x02, 0x55, 0xb5, 0x55, 0x0f}, // 229 - å
 {0x26, 0x29, 0x1e, 0x29, 0x1a}, // 230 - æ
 {0x18, 0x25, 0x27, 0x24, 0x08}, // 231 - ç
 {0x0e, 0x95, 0x55, 0x15, 0x0c}, // 232 - è
 {0x0e, 0x15, 0x55, 0x95, 0x0c}, // 233 - é
 {0x0e, 0x55, 0x95, 0x55, 0x0c}, // 234 - ê
 {0x0e, 0x55, 0x15, 0x55, 0x0c}, // 235 - ë
 {0x00, 0x89, 0x5f, 0x01, 0x00}, // 236 - ì
 {0x00, 0x09, 0x5f, 0x81, 0x00}, // 237 - í
 {0x00, 0x49, 0x9f, 0x41, 0x00}, // 238 - î
 {0x00, 0x49, 0x1f, 0x41, 0x00}, // 239 - ï
 {0x52, 0x25, 0x55, 0x0d, 0x06}, // 240 - ð
 {0x5f, 0x88, 0x90, 0x50, 0x8f}, // 241 - ñ
 {0x0e, 0x91, 0x51, 0x11, 0x0e}, // 242 - ò
 {0x0e, 0x11, 0x51, 0x91, 0x0e}, // 243 - ó
 {0x06, 0x29, 0x49, 0x29, 0x06}, // 244 - ô
 {0x26, 0x49, 0x49, 0x29, 0x46}, // 245 - õ
 {0x0e, 0x51, 0x11, 0x51, 0x0e}, // 246 - ö
 {0x08, 0x08, 0x2a, 0x08, 0x08}, // 247 - ÷
 {0x08, 0x15, 0x3e, 0x54, 0x08}, // 248 - ø
 {0x1e, 0x81, 0x41, 0x02, 0x1f}, // 249 - ù
 {0x1e, 0x01, 0x41, 0x82, 0x1f}, // 250 - ú
 {0x1e, 0x41, 0x81, 0x42, 0x1f}, // 251 - û
 {0x1e, 0x41, 0x01, 0x42, 0x1f}, // 252 - ü
 {0x18, 0x05, 0x45, 0x85, 0x1e}, // 253 - ý
 {0x00, 0x41, 0x7f, 0x15, 0x08}, // 254 - þ
 {0x18, 0x45, 0x05, 0x45, 0x1e}, // 255 - ÿ
};
