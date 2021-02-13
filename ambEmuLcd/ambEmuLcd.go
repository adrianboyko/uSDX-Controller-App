package ambEmuLcd

// #include "vrEmuLcd.h"
import "C"

import (
	"fmt"
	"github.com/tarm/serial"
	"image"
	"unsafe"
)

type PoweredOn struct{}
type PoweredOff struct{}

type Updated struct{}

type Settled struct {
	Line1Data []byte
	Line2Data []byte
	CursorPos image.Point
}

// The microcontroller port pins used for each LCD signal
const RS_BIT = 5
const D7_BIT = 3
const D6_BIT = 2
const D5_BIT = 1
const D4_BIT = 0

const (
	WAITING_FOR_NIBBLE_1 = iota
	WAITING_FOR_NIBBLE_2
)

const (
	CMD_REGISTER  = 0
	DATA_REGISTER = 1
)

var state = WAITING_FOR_NIBBLE_1

var rs, d7, d6, d5, d4 byte

type AmbEmuLcd = *C.VrEmuLcd

var lcd AmbEmuLcd = C.vrEmuLcdNew(C.int(16), C.int(2), C.EmuLcdRomA02)

func cBytesToByteSlice(src *C.byte, start int, len int) []byte {
	return (*(*[1 << 30]byte)(unsafe.Pointer(src)))[start : start+len : start+len]
}

func createSettledEvent() *Settled {
	ddRam := C.vrEmuLcdGetDisplayRam(lcd)
	// Following code assume 2 row, 16 col, unscrolled LCD.
	line1 := cBytesToByteSlice(ddRam, 0x00, 16)
	line2 := cBytesToByteSlice(ddRam, 0x40, 16)

	cCursor := C.vrEmuLcdGetCursorOffset(lcd)
	goCursor := *(*int32)(unsafe.Pointer(&cCursor)) // is defined as int32_t in C code.
	var cursorRow, cursorCol int
	if 0 <= goCursor && goCursor < 0x40 {
		cursorRow = 1
		cursorCol = int(goCursor)
	} else {
		cursorRow = 2
		cursorCol = int(goCursor) - 0x40
	}
	return &Settled{
		Line1Data: line1,
		Line2Data: line2,
		CursorPos: image.Point{X: cursorCol, Y: cursorRow},
	}
}

// NOTE: The following ignores the fact that communication starts in 8bit data mode.
// This works, but only by doing error-recovery from situations that are not actually errors.
// A more sophisticated implementation would take the 8bit startup into account instead of
// assuming only 4bit mode is used.
func interpretByteFromSerial(nibbleByte byte, lcdEvents chan interface{}) {

	nibbleRS := (nibbleByte >> RS_BIT) & 1
	nibbleD7 := (nibbleByte >> D7_BIT) & 1
	nibbleD6 := (nibbleByte >> D6_BIT) & 1
	nibbleD5 := (nibbleByte >> D5_BIT) & 1
	nibbleD4 := (nibbleByte >> D4_BIT) & 1

	switch state {

	case WAITING_FOR_NIBBLE_1:

		// Second nibble should reference same register, so save 1st to compare later.
		rs = nibbleRS

		// Save the first nibble
		d7 = nibbleD7
		d6 = nibbleD6
		d5 = nibbleD5
		d4 = nibbleD4

		state = WAITING_FOR_NIBBLE_2

	case WAITING_FOR_NIBBLE_2:

		d3 := nibbleD7
		d2 := nibbleD6
		d1 := nibbleD5
		d0 := nibbleD4

		fullByte := d7<<7 + d6<<6 + d5<<5 + d4<<4 + d3<<3 + d2<<2 + d1<<1 + d0<<0

		// If RS values differ for two nibbles, it means that we're out of sync.
		rsMismatchErr := rs != nibbleRS

		// If RS is zero then we have a command, but 00000000 is not a vaild command.
		noSuchCommandErr := rs == 0 && fullByte == 0

		if !rsMismatchErr && !noSuchCommandErr {
			// We are probably in sync so proceed normally.
			interpretFullByte(fullByte, lcdEvents)
			state = WAITING_FOR_NIBBLE_1
		} else {
			// We are definitely out of sync, so let's try to get back on track
			//fmt.Printf("%01b %08b ERROR!\n", rs, fullByte)
			rs = nibbleRS
			d7 = nibbleD7
			d6 = nibbleD6
			d5 = nibbleD5
			d4 = nibbleD4
			state = WAITING_FOR_NIBBLE_2
		}
	}
}

func interpretFullByte(b byte, lcdEvents chan interface{}) {
	if rs == 0 && b == 255 { // Power UP signal is not a valid LCD command.
		lcdEvents <- PoweredOn{}
	} else if rs == 0 && b == 254 { // Power DOWN signal is not a valid LCD command.
		lcdEvents <- PoweredOff{}
		C.vrEmuLcdSendCommand(lcd, 0b00000001) // Clear Display
		C.vrEmuLcdSendCommand(lcd, 0b00001100) // Cursor Off
	} else {
		sendFullByteToEmulator(b)
		lcdEvents <- Updated{}
	}
}

func sendFullByteToEmulator(b byte) {
	//fmt.Printf("%01b %08b\n", rs, b)

	switch rs {
	case CMD_REGISTER:
		C.vrEmuLcdSendCommand(lcd, C.byte(b))
	case DATA_REGISTER:
		C.vrEmuLcdWriteByte(lcd, C.byte(b))
	default:
		panic("Bad RS value")
	}
	//C.vrEmuLcdPrintDisplayRam(lcd)
}

func NumPixels() (width int, height int) {
	var cWidth C.int
	var cHeight C.int
	C.vrEmuLcdNumPixels(lcd, &cWidth, &cHeight)
	return int(cWidth), int(cHeight)
}

func PixelState(col int, row int) int {
	return int(C.vrEmuLcdPixelState(lcd, C.int(col), C.int(row)))
}

func UpdatePixels() {
	C.vrEmuLcdUpdatePixels(lcd)
}

func PrintPixels() {
	pixelsWidth, pixelsHeight := NumPixels()
	fmt.Printf("\n")
	for row := 0; row < pixelsHeight; row++ {
		fmt.Printf("   ")
		for col := 0; col < pixelsWidth; col++ {
			switch PixelState(col, row) {
			case 1:
				fmt.Printf("█")
				break
			case 0:
				fmt.Printf(" ")
				break
			case -1:
				fmt.Printf(" ")
				break
			}
		}
		fmt.Printf("\n")
	}
}

func ProcessSerialLcdData(uSdx *serial.Port, lcdEvents chan interface{}) {

	buf := make([]byte, 128)
	serialIsIdle := false

	for {
		bytesRead, _ := uSdx.Read(buf)
		if bytesRead == 0 {
			if !serialIsIdle {
				serialIsIdle = true
				state = WAITING_FOR_NIBBLE_1
				lcdEvents <- createSettledEvent()
			}
		} else {
			for i := 0; i < bytesRead; i++ {
				interpretByteFromSerial(buf[i], lcdEvents)
			}
			serialIsIdle = false
		}
	}
}
