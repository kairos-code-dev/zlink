package main

import (
	"fmt"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func waitForMultiSpotReady(
	publisher *zlink.Spot,
	subs []multiSpotSubscriber,
	pollers []*zlink.Poller,
	msgSize int,
) {
	payload := perfcommon.PreparePayload(msgSize)
	readySeen := make([]bool, len(subs))
	deadline := time.Now().Add(perfcommon.SingleReadyTimeout())
	for time.Now().Before(deadline) {
		perfcommon.StampProbePayload(payload)
		err := publisher.Publish(multiSpotServiceName, multiSpotTopic, zlink.SendFlagsDontWait, perfcommon.NewMessage(payload))
		if err != nil && !perfcommon.IsTransient(err) {
			perfcommon.Must(err)
		}
		if drainMultiSpotReady(subs, pollers, readySeen, msgSize) {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	perfcommon.Must(fmt.Errorf("multi spot perf endpoint ready timed out"))
}

func drainMultiSpotReady(
	subs []multiSpotSubscriber,
	pollers []*zlink.Poller,
	readySeen []bool,
	msgSize int,
) bool {
	for index, sub := range subs {
		if readySeen[index] {
			continue
		}
		event, err := pollers[index].Wait(0)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if event == nil || event.Events&perfcommon.ZLinkPollIn == 0 {
			continue
		}
		for {
			message, err := sub.spot.Subscribe(zlink.RecvFlagsDontWait)
			if err != nil {
				if perfcommon.IsTransient(err) {
					break
				}
				perfcommon.Must(err)
			}
			if message == nil {
				break
			}
			part, err := message.SinglePartOrError()
			if err == nil && part != nil {
				data := part.Data()
				header, ok := perfcommon.DecodeMetricHeader(data)
				if ok &&
					header.RunID == perfcommon.MetricRunID &&
					int(header.MsgSize) == msgSize &&
					header.Phase == perfcommon.PhaseWarmup {
					readySeen[index] = true
				}
			}
			_ = message.Close()
		}
	}
	return allMultiSpotCooldownSeen(readySeen)
}
