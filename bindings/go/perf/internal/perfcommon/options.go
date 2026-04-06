package perfcommon

import "time"

type RecvMode string

const (
	RecvModeRecv     RecvMode = "recv"
	RecvModeCallback RecvMode = "callback"

	DefaultSocketTimeout = 500 * time.Millisecond
)

func (m RecvMode) String() string {
	return string(m)
}

func ConfigureRoundTripSocket(socket interface {
	SetRecvTimeout(time.Duration) error
	SetSendTimeout(time.Duration) error
}) {
	Must(socket.SetRecvTimeout(DefaultSocketTimeout))
	Must(socket.SetSendTimeout(DefaultSocketTimeout))
}

func ConfigureRecvSocket(socket interface {
	SetRecvTimeout(time.Duration) error
}) {
	Must(socket.SetRecvTimeout(DefaultSocketTimeout))
}

func ConfigureNoDrop(socket interface {
	SetNoDrop(bool) error
}) {
	Must(socket.SetNoDrop(true))
}

func ConfigureSendHWM(socket interface {
	SetSendHWM(int) error
}, hwm int) {
	Must(socket.SetSendHWM(hwm))
}

func ConfigureRecvHWM(socket interface {
	SetRecvHWM(int) error
}, hwm int) {
	Must(socket.SetRecvHWM(hwm))
}
