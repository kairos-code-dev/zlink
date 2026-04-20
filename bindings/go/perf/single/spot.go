package main

import (
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runSpot(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	publisherNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	discovery, err := ctx.Discovery(zlink.ServiceTypeSpot, "bench")
	perfcommon.Must(err)
	defer discovery.Close()
	perfcommon.Must(publisherNode.AttachDiscovery(discovery))

	publisher, err := publisherNode.Spot()
	perfcommon.Must(err)
	defer publisher.Close()
	subscriber := publisher
	serviceName := "bench"

	perfcommon.Must(publisher.SetNoDrop(true))
	perfcommon.Must(subscriber.SetSubscription("bench."))

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(cfg.warmup, cfg.duration)
	perfcommon.Must(subscriber.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	waitForSpotReady(publisher, subscriber)
	perfcommon.PostReadySettle(cfg.pattern)

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(window.StopAt) {
		perfcommon.StampWindowPayload(payload, window.ActiveAt)
		err := publisher.Publish(serviceName, "bench.topic", zlink.SendFlagsDontWait, perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		drainSpotOnce(subscriber, stats, window.ActiveAt)
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitForSpotReady(publisher *zlink.Spot, subscriber *zlink.Spot) {
	serviceName := "bench"
	payload := perfcommon.PreparePayload(64)
	perfcommon.Must(perfcommon.WaitReady(perfcommon.ReadyConfig{
		Name: "spot perf endpoint",
		Probe: func() (bool, error) {
			perfcommon.StampProbePayload(payload)
			err := publisher.Publish(serviceName, "bench.topic", zlink.SendFlagsDontWait, perfcommon.NewMessage(payload))
			if err != nil {
				if perfcommon.IsTransient(err) {
					return false, nil
				}
				return false, err
			}
			if drainSpotOnce(subscriber, nil, time.Now().Add(24*time.Hour)) {
				return true, nil
			}
			time.Sleep(250 * time.Microsecond)
			return false, nil
		},
	}))
}

func drainSpotOnce(subscriber *zlink.Spot, stats *perfcommon.Stats, activeAt time.Time) bool {
	message, err := subscriber.Subscribe(zlink.RecvFlagsDontWait)
	if err != nil {
		if perfcommon.IsTransient(err) {
			return false
		}
		perfcommon.Must(err)
	}
	if message == nil {
		return false
	}
	part, err := message.SinglePartOrError()
	if err == nil && stats != nil {
		perfcommon.RecordMessageLatency(stats, activeAt, len(part.Data()), part)
	}
	_ = message.Close()
	return true
}
