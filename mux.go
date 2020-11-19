package main

import (
	"github.com/tarm/serial"
	"uSDX/ambEmuLcd"
)

func ProcessSerialFromUsdx(uSdx *serial.Port, lcdEvents chan interface{}) {

	buf := make([]byte, 128)
	serialIsIdle := false

	for {
		bytesRead, _ := uSdx.Read(buf)
		if bytesRead == 0 {
			if !serialIsIdle {
				serialIsIdle = true
				ambEmuLcd.NoteSerialIsIdle(lcdEvents)
			}
		} else {
			for i := 0; i < bytesRead; i++ {
				b := buf[i]
				if b>>7 == 1 {
					// cat.InterpretByte()
				} else {
					ambEmuLcd.InterpretByte(buf[i], lcdEvents)
				}
			}
			serialIsIdle = false
		}
	}
}
