// AMB copied this from src/os/signal/internal/pty/pty.go
// TODO: Replace this with https://github.com/creack/pty

package pty

/*
 #define _XOPEN_SOURCE 600
 #include <fcntl.h>
 #include <stdlib.h>
 #include <unistd.h>
 #include <termios.h>
*/
import "C"

import (
	"fmt"
	"os"
	"syscall"
)

// Copyright 2017 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

type PtyError struct {
	FuncName    string
	ErrorString string
	Errno       syscall.Errno
}

func ptyError(name string, err error) *PtyError {
	return &PtyError{name, err.Error(), err.(syscall.Errno)}
}

func (e *PtyError) Error() string {
	return fmt.Sprintf("%s: %s", e.FuncName, e.ErrorString)
}

func (e *PtyError) Unwrap() error { return e.Errno }

// Open returns a control pty and the name of the linked process tty.
func Open() (pty *os.File, processTTY string, err error) {
	m, err := C.posix_openpt(C.O_RDWR)
	if err != nil {
		return nil, "", ptyError("posix_openpt", err)
	}
	if _, err := C.grantpt(m); err != nil {
		C.close(m)
		return nil, "", ptyError("grantpt", err)
	}
	if _, err := C.unlockpt(m); err != nil {
		C.close(m)
		return nil, "", ptyError("unlockpt", err)
	}

	// var tio C.struct_termios
	// C.tcgetattr(m, &tio)
	// tio.c_lflag ^= C.ECHO
	// C.tcsetattr(m, C.TCSANOW|C.TCSAFLUSH, &tio)

	processTTY = C.GoString(C.ptsname(m))
	return os.NewFile(uintptr(m), "pty"), processTTY, nil
}
