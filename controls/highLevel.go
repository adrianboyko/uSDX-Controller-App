package controls

import (
	"fmt"
	"image"
	"strconv"
	"strings"
	"time"
	"uSDX/ambEmuLcd"
)

var line1 []byte
var line2 []byte
var cursor image.Point

var currentFrequencyA int64 = 0 // Must be MMkkkHHH format, with leading zeros.

var settledEvents chan *ambEmuLcd.Settled = nil

var uSdxSettingNames []string // The settings on the menu, in order.

func InitHighLevelControls() {
	//uSdxSettingNames = nil
	//asyncGatherUsdxSettings()
}

func indexOfSettingName(name string) (found bool, index int) {
	for i, v := range uSdxSettingNames {
		if v == name {
			return true, i
		}
	}
	return false, 999
}

func menuLineToTrimmedString(line []byte) string {
	sLine1 := strings.TrimLeft(string(line), " 0123456789.")
	return strings.TrimSpace(sLine1)
}

func asyncGatherUsdxSettings() {
	go func() {

		time.Sleep(time.Second)
		settledEvents = make(chan *ambEmuLcd.Settled, 100)

		lastLine1Seen := ""
		ClickLeftButton()
		for {
			e := <-settledEvents
			sLine1 := menuLineToTrimmedString(e.Line1Data)
			if sLine1 == lastLine1Seen {
				break
			}
			uSdxSettingNames = append(uSdxSettingNames, sLine1)
			RotateEncoderClockwise()
			lastLine1Seen = sLine1
		}
		settledEvents = nil
		ClickRightButton()
		fmt.Printf("%v\n", uSdxSettingNames)
	}()
}

func HandleSettledEvent(e *ambEmuLcd.Settled) {
	line1 = e.Line1Data
	line2 = e.Line2Data
	cursor = e.CursorPos

	if line2[0] == 6 { // Custom character 6 is used to indicate VFO A
		hzStr := string(line2[1:11]) + "0" // uSDX doesn't display "ones" position, so suffix a "0"
		hzStr = strings.ReplaceAll(hzStr, ",", "")
		hzStr = strings.ReplaceAll(hzStr, " ", "")
		currentFrequencyA, _ = strconv.ParseInt(hzStr, 10, 64)
	}

	if settledEvents != nil {
		settledEvents <- e
	}
}

func asyncSetParameter(nameToSet, val string) {
	settledEvents = make(chan *ambEmuLcd.Settled, 100)
	go func() {

		// Enter menu and find out where we are in it
		ClickLeftButton()
		e := <-settledEvents
		currentSettingName := menuLineToTrimmedString(e.Line1Data)

		// Calculate how far we need to move, and in wich direction
		found1, currentIndex := indexOfSettingName(currentSettingName)
		found2, targetIndex := indexOfSettingName(nameToSet)
		if !found1 || !found2 {
			ClickRightButton()
			return
		}
		diff := targetIndex - currentIndex
		dir := 1
		if diff < 0 {
			dir = -1
			diff *= -1
		}

		// Move to the target setting
		for n := 0; n < diff; n++ {
			RotateEncoder(dir)
			//<-settledEvents
			time.Sleep(50 * time.Millisecond)
		}

		// Edit the target setting's value
		ClickLeftButton()

		settledEvents = nil
		ClickRightButton()
		ClickRightButton()
	}()
}

func SkipToFreqDigit(sought int) {
	go func() {
		delta := sought - cursor.X
		if delta < 0 {
			delta = 9 + delta // "9" is the number of digits/commas in the frequency display
		}
		for i := 0; i < delta; i++ {
			ClickEncoderButton()
			time.Sleep(50 * time.Millisecond)
		}
	}()
}

var digitPositionMap = []byte{99, 0, 1, 99, 2, 3, 4, 99, 5, 6}
var mostRecentHzStr = ""

const uSdrFreqChars = 9 // including leading spaces and commas

func SetFrequency(hzStr string) {
	settledEvents = make(chan *ambEmuLcd.Settled, 100)
	go func() {

		// Setting frequency is idempotent.
		if hzStr == mostRecentHzStr {
			return
		}
		mostRecentHzStr = hzStr

		hzStr = strings.TrimLeft(hzStr, "0")
		daHzStr := hzStr[0 : len(hzStr)-1]
		daHzStr = fmt.Sprintf("%07s", daHzStr)
		//fmt.Println(daHzStr)

		startX := cursor.X
		for i := startX; i < startX+uSdrFreqChars; i++ { // cycle through the display's frequency characters
			iMod := ((i - 1) % uSdrFreqChars) + 1
			currChar := line2[iMod]
			if currChar != ',' {
				currDigit := currChar - '0'
				if currChar == ' ' { // The display shows leading zeros as spaces.
					currDigit = 0
				}
				targetDigit := daHzStr[digitPositionMap[iMod]] - '0'
				dir := 1
				delta := int(targetDigit) - int(currDigit)
				if delta < 0 {
					dir = -1
					delta = -1 * delta
				}
				//log.Printf("index:%d curr:%d targ:%d delta:%d", iMod, currDigit, targetDigit, delta)
				for n := 0; n < delta; n++ {
					RotateEncoder(dir)
					<-settledEvents
				}

			}
			ClickEncoderButton()
			<-settledEvents
		}
	}()
}
