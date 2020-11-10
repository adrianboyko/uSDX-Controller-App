package ambEmuLcd

// #cgo CFLAGS: -I "$SRCDIR/display/"
// #include "vrEmuLcd.h"
import "C"

import (
	"fmt"
	"github.com/tarm/serial"
	"unsafe"
)

type Updated struct{}

type Settled struct {
	line1 []byte
	line2 []byte
}

// The microcontroller port pins used for each LCD signal
const RS_PIN = 4
const D7_PIN = 5
const D6_PIN = 2
const D5_PIN = 1
const D4_PIN = 0

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

func Run(s *serial.Port, lcdEvents chan interface{}) {

	buf := make([]byte, 128)
	serialIsIdle := false

	for {
		bytesRead, _ := s.Read(buf)
		if bytesRead == 0 {
			state = WAITING_FOR_NIBBLE_1
			if !serialIsIdle {
				serialIsIdle = true
				ddRam := C.vrEmuLcdGetDisplayRam(lcd)
				// Next two lines assume 16x2 LCD and unscrolled LCD window.
				line1 := cBytesToByteSlice(ddRam, 0x00, 16)
				line2 := cBytesToByteSlice(ddRam, 0x40, 16)
				lcdEvents <- Settled{line1, line2}
			}
		} else {
			for i := 0; i < bytesRead; i++ {
				interpretNibbleByte(buf[i], lcdEvents)
				serialIsIdle = false
			}
		}
	}
}

func interpretNibbleByte(nibbleByte byte, lcdEvents chan interface{}) {

	nibbleRS := (nibbleByte >> RS_PIN) & 1
	nibbleD7 := (nibbleByte >> D7_PIN) & 1
	nibbleD6 := (nibbleByte >> D6_PIN) & 1
	nibbleD5 := (nibbleByte >> D5_PIN) & 1
	nibbleD4 := (nibbleByte >> D4_PIN) & 1

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

		if rs == nibbleRS { // This checks that we're in correct nibble sync
			// We are probably in sync so proceed normally.
			sendFullByte(d7<<7 + d6<<6 + d5<<5 + d4<<4 + d3<<3 + d2<<2 + d1<<1 + d0<<0)
			state = WAITING_FOR_NIBBLE_1
			lcdEvents <- Updated{}
		} else {
			// We are definitely out of sync, so let's try to get back on track
			d7 = nibbleD7
			d6 = nibbleD6
			d5 = nibbleD5
			d4 = nibbleD4
			state = WAITING_FOR_NIBBLE_2
		}
	}
}

func sendFullByte(b byte) {
	//fmt.Printf("%01b %08b ", rs, b)
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
