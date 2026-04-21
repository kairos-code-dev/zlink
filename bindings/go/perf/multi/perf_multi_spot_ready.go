package main

import (
	"sync/atomic"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

type multiSpotReadyTracker struct {
	readySeen  []uint32
	readyCount atomic.Uint32
	signaled   atomic.Uint32
	readyCh    chan struct{}
}

func newMultiSpotReadyTracker(clientCount int) *multiSpotReadyTracker {
	return &multiSpotReadyTracker{
		readySeen: make([]uint32, clientCount),
		readyCh:   make(chan struct{}, 1),
	}
}

func (t *multiSpotReadyTracker) mark(index int) {
	if index < 0 || index >= len(t.readySeen) {
		return
	}
	if !atomic.CompareAndSwapUint32(&t.readySeen[index], 0, 1) {
		return
	}
	readyCount := t.readyCount.Add(1)
	if int(readyCount) != len(t.readySeen) {
		return
	}
	if !t.signaled.CompareAndSwap(0, 1) {
		return
	}
	select {
	case t.readyCh <- struct{}{}:
	default:
	}
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
