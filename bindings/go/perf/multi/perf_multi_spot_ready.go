package main

import (
	"sync"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

type multiSpotReadyTracker struct {
	mu        sync.Mutex
	readySeen []bool
	signaled  bool
	readyCh   chan struct{}
}

func newMultiSpotReadyTracker(clientCount int) *multiSpotReadyTracker {
	return &multiSpotReadyTracker{
		readySeen: make([]bool, clientCount),
		readyCh:   make(chan struct{}, 1),
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
	if t.signaled {
		return
	}
	for _, ready := range t.readySeen {
		if !ready {
			return
		}
	}
	t.signaled = true
	select {
	case t.readyCh <- struct{}{}:
	default:
	}
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
	tracker *multiSpotReadyTracker,
) {
	serviceName := "bench"
	payload := perfcommon.PreparePayload(64)
	perfcommon.Must(perfcommon.WaitReady(perfcommon.ReadyConfig{
		Name: "multi spot perf endpoint",
		Start: func() error {
			perfcommon.StampProbePayload(payload)
			err := publisher.Publish(serviceName, "bench.topic", zlink.SendFlagsDontWait, perfcommon.NewMessage(payload))
			if err != nil {
				return err
			}
			return nil
		},
		Ready: tracker.readyCh,
	}))
}
