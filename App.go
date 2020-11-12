package main

import "C"

import (
	"fmt"
	"gioui.org/app"
	"github.com/tarm/serial"
	"log"
	"time"
	"uSDX/ambEmuLcd"
)

func main() {

	config := &serial.Config{Name: "/dev/ttyACM0", Baud: 500000, ReadTimeout: 10 * time.Millisecond}
	port, err := serial.OpenPort(config)
	if err != nil {
		log.Fatal(err)
	}

	lcdEvents := make(chan interface{}, 100)
	go ambEmuLcd.Run(port, lcdEvents)

	InitControls(port)

	go gui(loop, lcdEvents)

	app.Main()

}

func loop(w *app.Window, lcdEvents chan interface{}) error {
	for {
		select {

		case lcdEvt := <-lcdEvents:
			w.Invalidate()
			switch e := lcdEvt.(type) {
			case *ambEmuLcd.Settled:
				fmt.Printf("%v %s\n", e.Line1Data, e.Line1Data)
				fmt.Printf("%v %s\n", e.Line2Data, e.Line2Data)
				fmt.Printf("%d\n\n", e.CursorPos)
			case ambEmuLcd.Updated:
			}

		case e := <-w.Events():
			stop, evtErr := handleWindowEvent(e)
			if stop {
				return evtErr
			}
		}
	}
}
