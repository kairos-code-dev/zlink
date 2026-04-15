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
	defer publisherNode.Close()
	subscriberNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer subscriberNode.Close()

	publisher, err := publisherNode.Spot()
	perfcommon.Must(err)
	defer publisher.Close()
	subscriber, err := subscriberNode.Spot()
	perfcommon.Must(err)
	defer subscriber.Close()
	serviceName := "bench"

	endpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-spot")
	perfcommon.Must(perfcommon.ConfigureTLSServer(publisherNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(subscriberNode, cfg.transport))
	perfcommon.Must(publisher.SetNoDrop(true))
	perfcommon.Must(publisherNode.Bind(endpoint))
	perfcommon.Must(subscriberNode.ConnectPeer(endpoint))
	perfcommon.Must(subscriber.SetSubscription("bench."))

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(0, cfg.duration)
	perfcommon.Must(subscriber.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	waitForSpotReady(publisher, subscriber)

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(window.StopAt) {
		perfcommon.StampPayload(payload)
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
			perfcommon.StampPayload(payload)
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
		perfcommon.RecordMessageLatency(stats, activeAt, part)
	}
	_ = message.Close()
	return true
}
