package main

import "C"

import (
	"gioui.org/app"
	"github.com/tarm/serial"
	"log"
	"time"
	"uSDX/ambEmuLcd"
	"uSDX/controls"
)

const uSdxDev = "/dev/serial/by-id/usb-Arduino_LLC_Arduino_Nano_Every_ACB4982851514746334C2020FF0E2053-if00"

func main() {

	lcdEvents := make(chan interface{}, 100)

	go gui(loop, lcdEvents)

	uSdxConfig := &serial.Config{Name: uSdxDev, Baud: 500000, ReadTimeout: 50 * time.Millisecond}
	uSdxPort, uSdxErr := serial.OpenPort(uSdxConfig)
	if uSdxErr != nil {
		log.Fatal(uSdxErr)
	}
	controls.InitLowLevelControls(uSdxPort)
	controls.ForceRefresh()
	go ambEmuLcd.ProcessSerialLcdData(uSdxPort, lcdEvents)

	// TODO: Use ProcessPtyCat where Pty is available (e.g. Linux/Mac), else ProcessSerialCat (e.g. Windows)
	go controls.ProcessPtyCat()

	app.Main()

}

func loop(w *app.Window, lcdEvents chan interface{}) error {
	for {
		select {

		case lcdEvt := <-lcdEvents:
			w.Invalidate()
			switch e := lcdEvt.(type) {
			case *ambEmuLcd.Settled:
				controls.HandleSettledEvent(e)
			case ambEmuLcd.Updated:
				// Nothing yet. Might not use.
			case ambEmuLcd.PoweredOn:
				controls.InitHighLevelControls()
			case ambEmuLcd.PoweredOff:
				// Nothing yet. Might not use.
			}

		case e := <-w.Events():
			stop, evtErr := handleWindowEvent(e)
			if stop {
				return evtErr
			}
		}
	}
}
