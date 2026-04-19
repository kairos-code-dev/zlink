package main

import (
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runPubSub(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	publisher, err := ctx.XPubSocket()
	perfcommon.Must(err)
	defer publisher.Close()
	subscriber, err := ctx.SubSocket()
	perfcommon.Must(err)
	defer subscriber.Close()

	pubMon := perfcommon.OpenMonitor(publisher)
	defer pubMon.Close()
	subMon := perfcommon.OpenMonitor(subscriber)
	defer subMon.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(publisher, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(subscriber, cfg.transport))
	perfcommon.ApplySingleHWM(publisher)
	perfcommon.ApplySingleHWM(subscriber)
	endpoint := perfcommon.BindAndResolveEndpoint(publisher, cfg.transport, "perf-pubsub")
	perfcommon.Must(subscriber.Connect(endpoint))
	perfcommon.ApplySingleBenchmarkSocketOptions(publisher, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(subscriber, cfg.transport)
	perfcommon.WaitConnected(pubMon, subMon)

	perfcommon.Must(subscriber.SetSubscription("bench."))
	_, err = publisher.ReceiveSubscriptionEvent(zlink.RecvFlagsNone)
	perfcommon.Must(err)
	perfcommon.Must(subscriber.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(0, cfg.duration)
	payload := perfcommon.PreparePayload(cfg.msgSize)

	for time.Now().Before(window.StopAt) {
		perfcommon.StampPayload(payload)
		err := publisher.Publish(
			"bench.topic",
			zlink.SendFlagsNone,
			perfcommon.NewMessage(payload),
		)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}

		received, err := subscriber.Subscribe(zlink.RecvFlagsNone)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if received == nil {
			continue
		}
		part, err := received.SinglePartOrError()
		if err == nil {
			perfcommon.RecordMessageLatency(stats, window.ActiveAt, part)
		}
		_ = received.Close()
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}
