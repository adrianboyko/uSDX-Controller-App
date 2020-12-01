package main

import (
	"gioui.org/app"
	"gioui.org/f32"
	"gioui.org/font/gofont"
	"gioui.org/io/event"
	"gioui.org/io/pointer"
	"gioui.org/io/system"
	"gioui.org/layout"
	"gioui.org/op"
	"gioui.org/op/paint"
	. "gioui.org/unit"
	"gioui.org/widget"
	"gioui.org/widget/material"
	"image"
	"image/color"
	"log"
	"os"
	"uSDX/ambEmuLcd"
	"uSDX/controls"
)

type (
	D = layout.Dimensions
	C = layout.Context
)

var (
	theme            = material.NewTheme(gofont.Collection())
	xPixels, yPixels = ambEmuLcd.NumPixels()
	rendPixSize      = float32(4) // This is the RENDERED displaySize of an LCD pixel
	wPixels          = float32(xPixels) * rendPixSize
	hPixels          = float32(yPixels) * rendPixSize
	dm               = float32(6)                                     // display margin
	displaySize      = image.Pt(int(wPixels+2*dm), int(hPixels+2*dm)) // Rendered size of display
)

var (
	leftButton  = new(widget.Clickable)
	ccwButton   = new(widget.Clickable)
	rightButton = new(widget.Clickable)
	cwButton    = new(widget.Clickable)
	midButton   = new(widget.Clickable)
)

func gui(loop func(*app.Window, chan interface{}) error, lcdEvents chan interface{}) {

	w := app.NewWindow(
		app.Title("uSDX Controller"),
		app.Size(Px(float32(displaySize.X)), Px(150)),
	)

	if err := loop(w, lcdEvents); err != nil {
		log.Fatal(err)
	}

	os.Exit(0)
}

var scrollCount uint32 = 0

func handleWindowEvent(iEvt event.Event) (stop bool, err error) {
	var ops op.Ops
	stop = false
	err = nil

	if iEvt != nil {
		switch e := iEvt.(type) {
		case pointer.Event:
			if e.Type == pointer.Scroll {
				scrollCount += 1
				if scrollCount%2 == 0 {
					if e.Scroll.Y > 0 {
						controls.RotateEncoderCounterclockwise()
					} else {
						controls.RotateEncoderClockwise()
					}
				}
			}
		}
	}
	for leftButton.Clicked() {
		controls.ClickLeftButton()
	}
	for midButton.Clicked() {
		controls.ClickEncoderButton()
	}
	for rightButton.Clicked() {
		controls.ClickRightButton()
	}
	for ccwButton.Clicked() {
		controls.RotateEncoderCounterclockwise()
	}
	for cwButton.Clicked() {
		controls.RotateEncoderClockwise()
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

	case pointer.Event:
		handlePointerEvent(evt)
	}

	return
}

func guiPtToLcdCharPt(guiPt f32.Point) (onLcd bool, lcdCharPt image.Point) {
	lcdPixPt := image.Point{X: int((guiPt.X - dm) / rendPixSize), Y: int(((guiPt.Y - dm) / rendPixSize))}
	lcdCharPt = image.Point{X: lcdPixPt.X / 6, Y: int(lcdPixPt.Y / 9)}
	if lcdCharPt.X <= 15 && lcdCharPt.Y <= 1 {
		onLcd = true
	} else {
		onLcd = false
	}
	return
}

func handlePointerEvent(evt pointer.Event) {
	switch evt.Type {
	case pointer.Press:
		if overLcd, charPt := guiPtToLcdCharPt(evt.Position); overLcd {
			if charPt.Y == 1 && charPt.X >= 1 && charPt.X <= 9 {
				controls.SkipToFreqDigit(charPt.X)
			}
			if charPt.Y == 1 && charPt.X >= 11 && charPt.X <= 13 {
				controls.ClickRightButton()
			}
		}
	}
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
