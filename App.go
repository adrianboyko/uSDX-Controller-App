package main

import "C"

import (
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

func loop(w *app.Window, lcdActivityChan chan interface{}) error {
	for {
		select {

		case <-lcdActivityChan:
			w.Invalidate()
			//Do(ClickRightButton)

		case e := <-w.Events():
			stop, evtErr := handleWindowEvent(e)
			if stop {
				return evtErr
			}
		}
	}
}
