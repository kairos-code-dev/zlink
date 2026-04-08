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

	endpoint := perfcommon.UniqueTCPEndpoint("perf-spot")
	perfcommon.Must(publisher.SetNoDrop(true))
	perfcommon.Must(publisherNode.Bind(endpoint))
	perfcommon.Must(subscriberNode.ConnectPeer(endpoint))
	perfcommon.Must(subscriber.SetSubscription("bench."))

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(0, cfg.duration)
	ready := make(chan struct{}, 1)

	if cfg.recvMode == "callback" {
		perfcommon.Must(subscriber.OnSubscribe(func(message *zlink.TopicMessage) {
			defer message.Close()
			select {
			case ready <- struct{}{}:
			default:
			}
			part, err := message.SinglePartOrError()
			if err != nil {
				return
			}
			perfcommon.RecordMessageLatency(stats, window.ActiveAt, part)
		}))
	}

	waitForSpotReady(publisher, subscriber, cfg.recvMode, ready)

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(window.StopAt) {
		perfcommon.StampPayload(payload)
		result, err := publisher.TryPublish("bench.topic", perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if result != zlink.SendResultSent {
			time.Sleep(250 * time.Microsecond)
		}
		if cfg.recvMode == "recv" {
			drainSpotOnce(subscriber, stats, window.ActiveAt)
		}
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitForSpotReady(publisher *zlink.Spot, subscriber *zlink.Spot, recvMode string, ready <-chan struct{}) {
	payload := perfcommon.PreparePayload(64)
	perfcommon.Must(perfcommon.WaitReady(perfcommon.ReadyConfig{
		Name: "spot perf endpoint",
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
			if recvMode == "callback" {
				select {
				case <-ready:
					return true, nil
				case <-time.After(250 * time.Millisecond):
				}
				return false, nil
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
	message, ok, err := subscriber.TrySubscribe()
	if err != nil {
		if perfcommon.IsTransient(err) {
			return false
		}
		perfcommon.Must(err)
	}
	if !ok || message == nil {
		return false
	}
	part, err := message.SinglePartOrError()
	if err == nil && stats != nil {
		perfcommon.RecordMessageLatency(stats, activeAt, part)
	}
	_ = message.Close()
	return true
}
