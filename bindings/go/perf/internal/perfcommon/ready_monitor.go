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
		ch := make(chan result, 1)
		go func() {
			event, err := monitor.Recv()
			ch <- result{event: event, err: err}
		}()
		select {
		case out := <-ch:
			if out.err != nil {
				if IsTransient(out.err) {
					time.Sleep(50 * time.Millisecond)
					continue
				}
				return out.err
			}
			if out.event != nil {
				readyEvents++
			}
		case <-time.After(50 * time.Millisecond):
		}
	}
	if readyEvents >= minEvents {
		return nil
	}
	return fmt.Errorf("%s did not become ready", name)
}
