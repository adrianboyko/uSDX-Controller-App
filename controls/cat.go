package controls

import (
	"bufio"
	"fmt"
	"github.com/tarm/serial"
	"log"
	"strings"
)

// Based, in part, on code from https://github.com/threeme3/QCX-SSB.
// threeme3's credits are as follows:
//    CAT support inspired by Charlie Morris, ZL2CTM,
//    Contribution by Alex, PE1EVX,
//    source: http://zl2ctm.blogspot.com/2020/06/digital-modes-transceiver.html?m=1
//
// Emulated radio is documented at:
//    https://www.kenwood.com/i/products/info/amateur/ts_480/pdf/ts_480_pc.pdf

var catSerial *serial.Port = nil

func ProcessSerialFromCat(_catSerial *serial.Port) {
	if _catSerial == nil {
		return
	}
	catSerial = _catSerial
	catReader := bufio.NewReader(catSerial)
	for {
		data, err := catReader.ReadBytes(';')
		if err != nil {
			log.Fatal(err)
		}
		//log.Printf("-> %s", string(data))
		processCatCommand(data)
	}
}

func processCatCommand(catCmd []byte) {

	noParams := catCmd[2] == ';'

	switch string(catCmd[0:2]) {

	case "AI":
		if noParams {
			readAutoInfo()
			return
		} else if catCmd[2] == '0' {
			setAutoInfo(0)
			return
		}

	case "FA":
		if noParams {
			readFrequencyA()
			return
		} else if catCmd[13] == ';' {
			setFrequencyA(catCmd)
			return
		}

	case "ID":
		if noParams {
			readTransceiverId()
			return
		}

	case "IF":
		if noParams {
			readTransceiverStatus()
			return
		}

	case "MD":
		if noParams {
			readOperatingMode()
			return
		}

	case "PS":
		if noParams {
			readPowerOnOffStatus()
			return
		} else if catCmd[2] == '1' {
			setPowerOnOffStatus('1')
			return
		}

	case "RS":
		if noParams {
			readTranceiverStatus()
			return
		}

	case "RX":
		if noParams {
			setReceiveMode()
			return
		}

	case "TX":
		if noParams {
			setTransmitMode(0)
			return
		} else if strings.ContainsAny(string(catCmd[2]), "012") {
			setTransmitMode(catCmd[2])
			return
		}

	case "VX":
		if catCmd[2] != ';' {
			setVoxStatus(catCmd[2])
			return
		}

	}
	respond("?;")

}

func respond(response string) {
	//log.Printf("<- %s", response)
	_, _ = catSerial.Write([]byte(response))
}

func readFrequencyA() {
	respond(fmt.Sprintf("FA00%09d;", currentFrequencyA))
}

func setFrequencyA(catCmd []byte) {
	SetFrequency(string(catCmd[2:13]))
}

func add(sb *strings.Builder, addition string) {
	sb.WriteString(addition)
}

func readTransceiverStatus() {
	var sb strings.Builder

	add(&sb, "IF")
	add(&sb, fmt.Sprintf("00%09d", currentFrequencyA))
	add(&sb, "     ") // P2   Always 5 spaces for TS-480
	add(&sb, "+0000") // P3   RIT/XIT frequency in Hz
	add(&sb, "0")     // P4   0: RIT OFF, 1: RIT ON
	add(&sb, "0")     // P5   0: XIT OFF, 1: XIT ON
	add(&sb, "0")     // P6   Memory channel bank number
	add(&sb, "00")    // P7   Memory channel number
	add(&sb, "0")     // P8   0:RX, 1:TX  Why does QCX-SSB always have 0?
	add(&sb, "2")     // P9   Operating Mode. Why does QCX-SSB always have 2?
	add(&sb, "0")     // P10  "See FR and FT commands"?
	add(&sb, "0")     // P11  Scan status
	add(&sb, "0")     // P12  0: Simplex Operation, 1: Split operation
	add(&sb, "0")     // P13  0: OFF, 1: TONE, 2: CTCSS
	add(&sb, "00")    // P14  Tone number, refer to the TN anc CN commands
	add(&sb, " ")     // P15  Always a space for TS-480
	add(&sb, ";")

	respond(sb.String())
}

func readAutoInfo() {
	respond("AI0;") // OFF
}

func setAutoInfo(p1 byte) {
	respond("AI0;")
}

func readOperatingMode() {
	respond("MD2;") // TODO: Why does QCX-SSB always return USB?
}

func setReceiveMode() {
	EndPushToTalk()
	//respond("RX0;") // REVIEW: Why does a set command have a response?
}

func setTransmitMode(p1 byte) {
	StartPushToTalk()
}

func readTranceiverStatus() {
	respond("RS0;")
}

func setVoxStatus(p1 byte) {
	// TODO: Why was QCX-SSB implementation answering with given byte?
	// respond(fmt.Sprintf("VX%c;", p1))
}

func readTransceiverId() {
	respond("ID020;")
}

func readPowerOnOffStatus() {
	respond("PS1;")
}

func setPowerOnOffStatus(p1 byte) {
	// REVIEW: No implementation provided in QCX-SSB
}
