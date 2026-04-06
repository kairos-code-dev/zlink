package main

import (
	"time"

	"zlink/perf/internal/perfcommon"
)

func drainMultiSpotOnce(
	subs []multiSpotSubscriber,
	stats *perfcommon.Stats,
	activeAt time.Time,
	tracker *multiSpotReadyTracker,
) int {
	processed := 0
	for index, sub := range subs {
		message, ok, err := sub.spot.TrySubscribe()
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if !ok || message == nil {
			continue
		}
		processed++
		part, err := message.SinglePartOrError()
		if err == nil && stats != nil {
			perfcommon.RecordMessageLatency(stats, activeAt, part)
		}
		if tracker != nil {
			tracker.mark(index)
		}
		_ = message.Close()
	}
	return processed
}
