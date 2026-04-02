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
	stopAt := time.Now().Add(cfg.warmup + cfg.duration)
	activeAt := time.Now().Add(cfg.warmup)
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
			if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
				stats.Add(sentAt)
			}
		}))
	}

	waitForSpotReady(publisher, subscriber, cfg.recvMode, ready)

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(stopAt) {
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
			drainSpotOnce(subscriber, stats, activeAt)
		}
	}
	if cfg.recvMode == "recv" {
		drainUntil := time.Now().Add(500 * time.Millisecond)
		for time.Now().Before(drainUntil) {
			if !drainSpotOnce(subscriber, stats, activeAt) {
				time.Sleep(250 * time.Microsecond)
			}
		}
	} else {
		time.Sleep(500 * time.Millisecond)
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitForSpotReady(publisher *zlink.Spot, subscriber *zlink.Spot, recvMode string, ready <-chan struct{}) {
	payload := perfcommon.PreparePayload(64)
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
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
			continue
		}
		if recvMode == "callback" {
			select {
			case <-ready:
				return
			case <-time.After(250 * time.Millisecond):
			}
			continue
		}
		if drainSpotOnce(subscriber, nil, time.Now().Add(24*time.Hour)) {
			return
		}
		time.Sleep(250 * time.Microsecond)
	}
	perfcommon.Must(&spotReadyError{})
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
		if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
			stats.Add(sentAt)
		}
	}
	_ = message.Close()
	return true
}

type spotReadyError struct{}

func (e *spotReadyError) Error() string {
	return "spot perf endpoint did not become ready"
}
