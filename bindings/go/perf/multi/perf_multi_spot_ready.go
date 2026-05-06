package main

import (
	"fmt"
	"time"

	"zlink/perf/internal/perfcommon"
)

func waitForMultiSpotReady(subs []multiSpotSubscriber) {
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(deadline) {
		if len(subs) > 0 {
			time.Sleep(perfcommon.MultiSpotReadySettleDuration())
			time.Sleep(perfcommon.MultiSpotControlSettleDuration())
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	perfcommon.Must(fmt.Errorf("multi spot perf endpoint ready timed out"))
}
