package perfcommon

import (
	"fmt"
	"time"
)

func waitProbeReady(probe func() (bool, error), timeout time.Duration, name string) error {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		ready, err := probe()
		if err != nil {
			return err
		}
		if ready {
			return nil
		}
	}
	return fmt.Errorf("%s did not become ready", name)
}
