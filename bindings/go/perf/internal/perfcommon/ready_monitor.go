package perfcommon

import (
	"fmt"
	"time"

	"zlink"
)

func waitMonitorReady(
	monitor *zlink.SocketMonitor,
	minEvents int,
	timeout time.Duration,
	name string,
) error {
	deadline := time.Now().Add(timeout)
	readyEvents := 0
	for time.Now().Before(deadline) && readyEvents < minEvents {
		type result struct {
			event *zlink.MonitorEvent
			err   error
		}
		recvDone := make(chan result, 1)
		go func() {
			event, err := monitor.Recv()
			recvDone <- result{event: event, err: err}
		}()

		select {
		case outcome := <-recvDone:
			if outcome.err == nil {
				readyEvents++
				continue
			}
			if IsTransient(outcome.err) {
				continue
			}
			return outcome.err
		case <-time.After(DefaultSocketTimeout):
		}
	}
	if readyEvents >= minEvents {
		return nil
	}
	return fmt.Errorf("%s did not become ready", name)
}
