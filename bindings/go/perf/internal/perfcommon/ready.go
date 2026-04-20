package perfcommon

import (
	"fmt"
	"time"

	"zlink"
)

type ReadyConfig struct {
	Monitor   *zlink.SocketMonitor
	MinEvents int
	Timeout   time.Duration
	Name      string
	Probe     func() (bool, error)
}

func WaitReady(cfg ReadyConfig) error {
	timeout := cfg.Timeout
	if timeout <= 0 {
		timeout = SingleReadyTimeout()
	}
	name := cfg.Name
	if name == "" {
		name = "perf endpoint"
	}

	if cfg.Monitor != nil {
		return waitMonitorReady(cfg.Monitor, maxReadyEvents(cfg.MinEvents), timeout, name)
	}
	if cfg.Probe == nil {
		return fmt.Errorf("%s ready wait is not configured", name)
	}
	return waitProbeReady(cfg.Probe, timeout, name)
}

func WaitConnected(monitors ...*zlink.SocketMonitor) {
	for _, monitor := range monitors {
		Must(WaitReady(ReadyConfig{
			Monitor:   monitor,
			MinEvents: 1,
			Timeout:   SingleReadyTimeout(),
		}))
	}
}

func maxReadyEvents(minEvents int) int {
	if minEvents > 0 {
		return minEvents
	}
	return 1
}
