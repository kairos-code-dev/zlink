package main

import (
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func drainMultiSpotOnce(
	subs []multiSpotSubscriber,
	stats *perfcommon.Stats,
	activeAt time.Time,
	msgSize int,
	tracker *multiSpotReadyTracker,
) int {
	processed := 0
	for index, sub := range subs {
		message, err := sub.spot.Subscribe(zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if message == nil {
			continue
		}
		processed++
		part, err := message.SinglePartOrError()
		if err == nil && stats != nil {
			perfcommon.RecordMessageLatency(stats, activeAt, msgSize, part)
		}
		if tracker != nil {
			tracker.mark(index)
		}
		_ = message.Close()
	}
	return processed
}
