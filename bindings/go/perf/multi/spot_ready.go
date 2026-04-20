package main

import (
	"sync"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

type multiSpotReadyTracker struct {
	mu        sync.Mutex
	readySeen []bool
}

func newMultiSpotReadyTracker(clientCount int) *multiSpotReadyTracker {
	return &multiSpotReadyTracker{
		readySeen: make([]bool, clientCount),
	}
}

func (t *multiSpotReadyTracker) mark(index int) {
	t.mu.Lock()
	t.markLocked(index)
	t.mu.Unlock()
}

func (t *multiSpotReadyTracker) markLocked(index int) {
	if index < 0 || index >= len(t.readySeen) {
		return
	}
	t.readySeen[index] = true
}

func (t *multiSpotReadyTracker) countReady() int {
	t.mu.Lock()
	defer t.mu.Unlock()
	count := 0
	for _, ready := range t.readySeen {
		if ready {
			count++
		}
	}
	return count
}

func waitForMultiSpotReady(
	publisher *zlink.Spot,
	subs []multiSpotSubscriber,
	tracker *multiSpotReadyTracker,
) {
	serviceName := "bench"
	payload := perfcommon.PreparePayload(64)
	perfcommon.Must(perfcommon.WaitReady(perfcommon.ReadyConfig{
		Name: "multi spot perf endpoint",
		Probe: func() (bool, error) {
			perfcommon.StampProbePayload(payload)
			err := publisher.Publish(serviceName, "bench.topic", zlink.SendFlagsDontWait, perfcommon.NewMessage(payload))
			if err != nil {
				if perfcommon.IsTransient(err) {
					return false, nil
				}
				return false, err
			}
				_ = drainMultiSpotOnce(subs, nil, time.Now().Add(24*time.Hour), tracker)
				return tracker.countReady() >= len(subs), nil
			},
		}))
}
