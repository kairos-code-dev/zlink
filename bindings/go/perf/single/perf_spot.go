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
	perfcommon.ApplySingleHWM(publisher)
	perfcommon.ApplySingleHWM(subscriber)
	endpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-spot")
	perfcommon.Must(publisherNode.Bind(endpoint))
	perfcommon.Must(subscriberNode.ConnectPeer(endpoint))
	perfcommon.Must(publisher.SetNoDrop(true))
	perfcommon.Must(subscriber.SetSubscription("bench."))
	perfcommon.ApplySingleBenchmarkSocketOptions(publisher, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(subscriber, cfg.transport)
	perfcommon.Must(subscriber.SetRecvTimeout(perfcommon.SingleRecvTimeout()))

	stats := perfcommon.NewStats()
	var window perfcommon.BenchmarkWindow
	var readySeen atomic.Bool
	var activeCollect atomic.Bool
	readySignal := make(chan struct{}, 1)

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
			readySignal,
		)
	}))

	waitForSpotReady(publisher, cfg.msgSize, readySignal)
	perfcommon.PostReadySettle(cfg.pattern)
	window = perfcommon.NewBenchmarkWindow(cfg.duration)
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
			readySignal,
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
	readySignal chan<- struct{},
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
			} else if _, ok := perfcommon.SentAtFromMessagePhase(part, msgSize, perfcommon.PhaseWarmup); ok {
				if readySeen.CompareAndSwap(false, true) {
					select {
					case readySignal <- struct{}{}:
					default:
					}
				}
			}
		}
		_ = message.Close()
	}
}

func waitForSpotReady(publisher *zlink.Spot, msgSize int, ready <-chan struct{}) {
	payload := perfcommon.PreparePayload(msgSize)
	perfcommon.Must(perfcommon.WaitReady(perfcommon.ReadyConfig{
		Name: "spot perf endpoint",
		Start: func() error {
			perfcommon.StampProbePayload(payload)
			err := publisher.Publish(singleSpotServiceName, singleSpotTopic, zlink.SendFlagsNone, perfcommon.NewMessage(payload))
			if err != nil {
				return err
			}
			return nil
		},
		Ready: ready,
	}))
}
