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

	stats := perfcommon.NewStats()
	stopAt := time.Now().Add(cfg.duration)
	activeAt := time.Now()
	perfcommon.Must(subscriber.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	go func() {
		for time.Now().Before(stopAt) {
			message, err := subscriber.Subscribe(zlink.RecvFlagsNone)
			if err != nil {
				continue
			}
			part, err := message.SinglePartOrError()
			if err == nil {
				if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
					stats.Add(sentAt)
				}
			}
			_ = message.Close()
		}
	}()

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(stopAt) {
		perfcommon.StampPayload(payload)
		perfcommon.Must(publisher.Publish("bench.topic", zlink.SendFlagsNone, perfcommon.NewMessage(payload)))
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}
