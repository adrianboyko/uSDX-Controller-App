package Controls

import "github.com/tarm/serial"

// These MUST be the values defined by the uSDX controller board:
const (
	clickLeftButton               = byte(1)
	clickRightButton              = byte(2)
	clickEncoderButton            = byte(3)
	rotateEncoderClockwise        = byte(4)
	rotateEncoderCounterclockwise = byte(5)
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

func hardwareAction(action byte) {
	//fmt.Printf("%s\n", controlNames[action])
	_, err := port.Write([]byte{action})
	if err != nil {
		panic(err)
	}
}

func InitLowLevelControls(p *serial.Port) {
	port = p
}

func ClickLeftButton()               { hardwareAction(clickLeftButton) }
func ClickRightButton()              { hardwareAction(clickRightButton) }
func ClickEncoderButton()            { hardwareAction(clickEncoderButton) }
func RotateEncoderClockwise()        { hardwareAction(rotateEncoderClockwise) }
func RotateEncoderCounterclockwise() { hardwareAction(rotateEncoderCounterclockwise) }

func RotateEncoder(dir int) {
	if dir > 0 {
		RotateEncoderClockwise()
	} else if dir < 0 {
		RotateEncoderCounterclockwise()
	} else {
		panic("Rotation direction must be nonzero")
	}
}
