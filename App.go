package main

import "C"

import (
	"gioui.org/app"
	"github.com/tarm/serial"
	"log"
	"time"
	"uSDX/Controls"
	"uSDX/ambEmuLcd"
)

func main() {

	uSdxConfig := &serial.Config{Name: "/dev/ttyACM0", Baud: 500000, ReadTimeout: 1 * time.Millisecond}
	uSdxPort, uSdxErr := serial.OpenPort(uSdxConfig)
	if uSdxErr != nil {
		log.Fatal(uSdxErr)
	}

	catConfig := &serial.Config{Name: "/dev/ttyS21", Baud: 9600}
	catPort, catErr := serial.OpenPort(catConfig)
	if catErr != nil {
		log.Fatal(catErr)
	}

	lcdEvents := make(chan interface{}, 100)

	go gui(loop, lcdEvents)

	Controls.InitLowLevelControls(uSdxPort)
	Controls.ResetTheUsdx()

	go ProcessSerialFromUsdx(uSdxPort, lcdEvents)
	go Controls.ProcessSerialFromCat(catPort)

	app.Main()

}

func loop(w *app.Window, lcdEvents chan interface{}) error {
	for {
		select {

		case lcdEvt := <-lcdEvents:
			w.Invalidate()
			switch e := lcdEvt.(type) {
			case *ambEmuLcd.Settled:
				Controls.HandleSettledEvent(e)
			case ambEmuLcd.Updated:
				// Nothing yet. Might not use.
			case ambEmuLcd.PoweredOn:
				Controls.InitHighLevelControls()
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
