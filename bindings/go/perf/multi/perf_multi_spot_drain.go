package main

import (
	"sync/atomic"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func drainMultiSpotSubscriber(
	index int,
	sub multiSpotSubscriber,
	stats *perfcommon.Stats,
	activeAt time.Time,
	msgSize int,
	tracker *multiSpotReadyTracker,
	activeCollect *atomic.Bool,
) bool {
	processed := false
	for {
		message, err := sub.spot.Subscribe(zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				return processed
			}
			perfcommon.Must(err)
		}
		if message == nil {
			return processed
		}
		processed = true
		part, partErr := message.SinglePartOrError()
		if partErr == nil {
			if activeCollect != nil && activeCollect.Load() {
				if stats != nil {
					perfcommon.RecordMessageLatency(stats, activeAt, msgSize, part)
				}
			} else if tracker != nil {
				if _, ok := perfcommon.SentAtFromMessagePhase(part, 64, perfcommon.PhaseWarmup); ok {
					tracker.mark(index)
				}
			}
		}
		_ = message.Close()
	}
}
