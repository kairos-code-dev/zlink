package main

import (
	"sync"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

type multiSpotReadyTracker struct {
	mu        sync.Mutex
	readyCh   chan int
	readySeen []bool
}

func newMultiSpotReadyTracker(clientCount int) *multiSpotReadyTracker {
	return &multiSpotReadyTracker{
		readyCh:   make(chan int, clientCount),
		readySeen: make([]bool, clientCount),
	}
}

func (t *multiSpotReadyTracker) signal(index int) {
	t.mu.Lock()
	t.markLocked(index)
	t.mu.Unlock()
	select {
	case t.readyCh <- index:
	default:
	}
}

func (t *multiSpotReadyTracker) drainSignals() {
	for {
		select {
		case index := <-t.readyCh:
			t.mu.Lock()
			t.markLocked(index)
			t.mu.Unlock()
		default:
			return
		}
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
	recvMode string,
	tracker *multiSpotReadyTracker,
) {
	switch recvMode {
	case "callback":
		waitForMultiSpotCallbackReady(publisher, len(subs), tracker)
	default:
		waitForMultiSpotRecvReady(publisher, subs, tracker)
	}
}

func waitForMultiSpotCallbackReady(
	publisher *zlink.Spot,
	expected int,
	tracker *multiSpotReadyTracker,
) {
	payload := perfcommon.PreparePayload(64)
	perfcommon.Must(perfcommon.WaitReady(perfcommon.ReadyConfig{
		Name: "multi spot perf endpoint",
		Probe: func() (bool, error) {
			perfcommon.StampPayload(payload)
			result, err := publisher.TryPublish("bench.topic", perfcommon.NewMessage(payload))
			if err != nil {
				if perfcommon.IsTransient(err) {
					return false, nil
				}
				return false, err
			}
			if result != zlink.SendResultSent {
				time.Sleep(250 * time.Microsecond)
				return false, nil
			}
			tracker.drainSignals()
			return tracker.countReady() >= expected, nil
		},
	}))
}

func waitForMultiSpotRecvReady(
	publisher *zlink.Spot,
	subs []multiSpotSubscriber,
	tracker *multiSpotReadyTracker,
) {
	payload := perfcommon.PreparePayload(64)
	perfcommon.Must(perfcommon.WaitReady(perfcommon.ReadyConfig{
		Name: "multi spot perf endpoint",
		Probe: func() (bool, error) {
			perfcommon.StampPayload(payload)
			result, err := publisher.TryPublish("bench.topic", perfcommon.NewMessage(payload))
			if err != nil {
				if perfcommon.IsTransient(err) {
					return false, nil
				}
				return false, err
			}
			if result != zlink.SendResultSent {
				time.Sleep(250 * time.Microsecond)
				return false, nil
			}
			_ = drainMultiSpotOnce(subs, nil, time.Now().Add(24*time.Hour), tracker)
			time.Sleep(250 * time.Microsecond)
			return tracker.countReady() >= len(subs), nil
		},
	}))
}
