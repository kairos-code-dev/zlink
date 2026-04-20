package main

import (
	"sync/atomic"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

const singleSpotServiceName = "perf-spot-svc"
const singleSpotTopic = "bench.topic"

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

	perfcommon.Must(perfcommon.ConfigureTLSServer(publisherNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(subscriberNode, cfg.transport))
	endpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-spot")
	perfcommon.Must(publisherNode.Bind(endpoint))
	perfcommon.Must(subscriberNode.ConnectPeer(endpoint))
	perfcommon.Must(publisher.SetNoDrop(true))
	perfcommon.Must(subscriber.SetSubscription("bench."))
	perfcommon.Must(subscriber.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(cfg.duration)
	var readySeen atomic.Bool
	var activeCollect atomic.Bool

	perfcommon.Must(subscriber.OnDispatchEvent(func(event zlink.SpotDispatchEvent) {
		if event != zlink.SpotDispatchEventSubscribeReadable {
			return
		}
		_ = drainSingleSpotReadable(
			subscriber,
			stats,
			window.ActiveAt,
			cfg.msgSize,
			&activeCollect,
			&readySeen,
		)
	}))

	waitForSpotReady(publisher, cfg.msgSize, &readySeen)
	perfcommon.PostReadySettle(cfg.pattern)
	activeCollect.Store(true)

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(window.StopAt) {
		perfcommon.StampWindowPayload(payload, window.ActiveAt)
		err := publisher.Publish(singleSpotServiceName, singleSpotTopic, zlink.SendFlagsNone, perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
	}

	activeCollect.Store(false)
	perfcommon.StampCooldownPayload(payload)
	err = publisher.Publish(singleSpotServiceName, singleSpotTopic, zlink.SendFlagsNone, perfcommon.NewMessage(payload))
	if err != nil && !perfcommon.IsTransient(err) {
		perfcommon.Must(err)
	}
	idleDrainDeadline := time.Now().Add(perfcommon.SingleIdleDrainDuration())
	for time.Now().Before(idleDrainDeadline) {
		if !drainSingleSpotReadable(
			subscriber,
			nil,
			window.ActiveAt,
			cfg.msgSize,
			&activeCollect,
			&readySeen,
		) {
			continue
		}
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func drainSingleSpotReadable(
	subscriber *zlink.Spot,
	stats *perfcommon.Stats,
	activeAt time.Time,
	msgSize int,
	activeCollect *atomic.Bool,
	readySeen *atomic.Bool,
) bool {
	processed := false
	for {
		message, err := subscriber.Subscribe(zlink.RecvFlagsDontWait)
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
		part, err := message.SinglePartOrError()
		if err == nil {
			if activeCollect.Load() {
				if stats != nil {
					perfcommon.RecordMessageLatency(stats, activeAt, msgSize, part)
				}
			} else if _, ok := perfcommon.SentAtFromMessage(part, msgSize); ok {
				readySeen.Store(true)
			}
		}
		_ = message.Close()
	}
}

func waitForSpotReady(publisher *zlink.Spot, msgSize int, readySeen *atomic.Bool) {
	payload := perfcommon.PreparePayload(msgSize)
	perfcommon.Must(perfcommon.WaitReady(perfcommon.ReadyConfig{
		Name: "spot perf endpoint",
		Probe: func() (bool, error) {
			perfcommon.StampProbePayload(payload)
			err := publisher.Publish(singleSpotServiceName, singleSpotTopic, zlink.SendFlagsNone, perfcommon.NewMessage(payload))
			if err != nil {
				if perfcommon.IsTransient(err) {
					return false, nil
				}
				return false, err
			}
			return readySeen.Load(), nil
		},
	}))
}
