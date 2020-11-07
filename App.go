package main

import "C"

import (
	"gioui.org/app"
	"gioui.org/f32"
	"gioui.org/font/gofont"
	"gioui.org/io/event"
	"gioui.org/io/system"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/paint"
	. "gioui.org/unit"
	"gioui.org/widget"
	"gioui.org/widget/material"
	"github.com/tarm/serial"
	"image"
	"image/color"
	"log"
	"os"
	"time"
	"uSDX/ambEmuLcd"
)

var (
	theme            = material.NewTheme(gofont.Collection())
	xPixels, yPixels = ambEmuLcd.NumPixels()
	rendPixSize      = float32(4) // This is the RENDERED displaySize of an LCD pixel
	wPixels          = float32(xPixels) * rendPixSize
	hPixels          = float32(yPixels) * rendPixSize
	dm               = float32(6)                                     // display margin
	displaySize      = image.Pt(int(wPixels+2*dm), int(hPixels+2*dm)) // Rendered displaySize of display
)

var (
	leftButton  = new(widget.Clickable)
	ccwButton   = new(widget.Clickable)
	rightButton = new(widget.Clickable)
	cwButton    = new(widget.Clickable)
	midButton   = new(widget.Clickable)
)

func main() {

	config := &serial.Config{Name: "/dev/ttyACM0", Baud: 500000, ReadTimeout: 5 * time.Millisecond}
	port, err := serial.OpenPort(config)
	if err != nil {
		log.Fatal(err)
	}

	lcdActivityChan := make(chan bool, 100)
	go ambEmuLcd.Run(port, lcdActivityChan)
	InitControls(port)

	go gui(lcdActivityChan)

	app.Main()

}

type (
	D = layout.Dimensions
	C = layout.Context
)

func gui(lcdActivityChan chan bool) {

	w := app.NewWindow(
		app.Title("uSDX Controller"),
		app.Size(Px(float32(displaySize.X)), Px(150)),
	)

	if err := loop(w, lcdActivityChan); err != nil {
		log.Fatal(err)
	}

	os.Exit(0)
}

func loop(w *app.Window, lcdActivityChan chan bool) error {
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

func handleWindowEvent(iEvt event.Event) (stop bool, err error) {
	var ops op.Ops
	stop = false
	err = nil

	for leftButton.Clicked() {
		Do(ClickLeftButton)
	}
	for midButton.Clicked() {
		Do(ClickEncoderButton)
	}
	for rightButton.Clicked() {
		Do(ClickRightButton)
	}
	for ccwButton.Clicked() {
		Do(RotateEncoderCounterclockwise)
	}
	for cwButton.Clicked() {
		Do(RotateEncoderClockwise)
	}

	switch evt := iEvt.(type) {

	case system.DestroyEvent:
		stop = true
		err = evt.Err

	case system.FrameEvent:
		ambEmuLcd.UpdatePixels() // This updates the pixel state in the LCD emulator.
		gtx := layout.NewContext(&ops, evt)
		flex := layout.Flex{
			Axis:    layout.Vertical,
			Spacing: layout.SpaceBetween,
		}
		flex.Layout(gtx,
			layout.Rigid(func(gtx C) D { return layoutLcdDisplay(gtx) }),
			layout.Flexed(0.5, func(gtx C) D { return layoutButtons(gtx) }),
		)
		evt.Frame(gtx.Ops)
	}

	return
}

func layoutButtons(gtx C) D {
	inset := layout.UniformInset(Px(10))
	return inset.Layout(gtx, func(gtx C) D {
		flex := layout.Flex{
			Axis:    layout.Horizontal,
			Spacing: layout.SpaceBetween,
		}
		return flex.Layout(gtx,
			layout.Rigid(func(gtx C) D { return material.Button(theme, leftButton, "     ").Layout(gtx) }),
			layout.Rigid(func(gtx C) D { return layoutEncoderButtons(gtx) }),
			layout.Rigid(func(gtx C) D { return material.Button(theme, rightButton, "     ").Layout(gtx) }),
		)
	})
}

func layoutEncoderButtons(gtx C) D {
	flex := layout.Flex{Axis: layout.Horizontal}
	return flex.Layout(gtx,
		layout.Rigid(func(gtx C) D { return material.Button(theme, ccwButton, "<").Layout(gtx) }),
		layout.Rigid(func(gtx C) D { return D{Size: image.Pt(5, 0)} }),
		layout.Rigid(func(gtx C) D { return material.Button(theme, midButton, "     ").Layout(gtx) }),
		layout.Rigid(func(gtx C) D { return D{Size: image.Pt(5, 0)} }),
		layout.Rigid(func(gtx C) D { return material.Button(theme, cwButton, ">").Layout(gtx) }),
	)
}

func layoutLcdDisplay(gtx C) D {

	paint.ColorOp{Color: color.RGBA{R: 0x1f, G: 0x1f, B: 0xff, A: 0xFF}}.Add(gtx.Ops)
	paint.PaintOp{Rect: f32.Rect(0, 0, wPixels+2*dm, hPixels+2*dm)}.Add(gtx.Ops)

	op.Offset(f32.Pt(dm, dm)).Add(gtx.Ops)

	for iX := 0; iX < xPixels; iX++ {
		for iY := 0; iY < yPixels; iY++ {
			var r, g, b uint8
			switch ambEmuLcd.PixelState(iX, iY) {
			case -1:
				continue
			case 0:
				r = 0x00
				g = 0x00
				b = 0xe0
			case 1:
				r = 0xf0
				g = 0xf0
				b = 0xff
			}
			pX0 := float32(iX) * rendPixSize
			pY0 := float32(iY) * rendPixSize
			pixel := f32.Rect(pX0, pY0, pX0+rendPixSize, pY0+rendPixSize)
			paint.ColorOp{Color: color.RGBA{R: r, G: g, B: b, A: 0xFF}}.Add(gtx.Ops)
			paint.PaintOp{Rect: pixel}.Add(gtx.Ops)
		}
	}

	return D{Size: displaySize}
}
