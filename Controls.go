package main

import "github.com/tarm/serial"

// These MUST be the values defined by the uSDX controller board:
const (
	ClickLeftButton               = byte(1)
	ClickRightButton              = byte(2)
	ClickEncoderButton            = byte(3)
	RotateEncoderClockwise        = byte(4)
	RotateEncoderCounterclockwise = byte(5)
)

var controlNames = []string{
	"DUMMY",
	"Left Button Click",
	"Right Button Click",
	"Encoder Button Click",
	"Encoder Clockwise Turn",
	"Encoder Counter Turn",
}

var port *serial.Port

func InitControls(p *serial.Port) {
	port = p
}

func Do(action byte) {
	//fmt.Printf("%s\n", controlNames[action])
	_, err := port.Write([]byte{action})
	if err != nil {
		panic(err)
	}
}
