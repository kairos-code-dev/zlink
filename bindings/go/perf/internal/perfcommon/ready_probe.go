package perfcommon

import (
	"fmt"
	"time"
)

func waitProbeReady(probe func() (bool, error), timeout time.Duration, name string) error {
	deadline := time.NewTimer(timeout)
	defer deadline.Stop()
	ticker := time.NewTicker(10 * time.Millisecond)
	defer ticker.Stop()

	for {
		ready, err := probe()
		if err != nil {
			return err
		}
		if ready {
			return nil
		}
		select {
		case <-deadline.C:
			return fmt.Errorf("%s did not become ready", name)
		case <-ticker.C:
		}
	}
}
