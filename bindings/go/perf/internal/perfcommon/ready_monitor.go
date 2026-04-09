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
		event, ok, err := monitor.TryRecv()
		if err != nil {
			if IsTransient(err) {
				time.Sleep(50 * time.Millisecond)
				continue
			}
			return err
		}
		if !ok || event == nil {
			time.Sleep(50 * time.Millisecond)
			continue
		}
		readyEvents++
	}
	if readyEvents >= minEvents {
		return nil
	}
	return fmt.Errorf("%s did not become ready", name)
}
