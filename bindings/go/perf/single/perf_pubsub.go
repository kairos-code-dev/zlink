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
	perfcommon.Must(subscriber.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	perfcommon.PostReadySettle(cfg.pattern)

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(cfg.duration)
	payload := perfcommon.PreparePayload(cfg.msgSize)
	sendDone := make(chan struct{})

	go func() {
		defer close(sendDone)
		for time.Now().Before(window.StopAt) {
			perfcommon.StampWindowPayload(payload, window.ActiveAt)
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
		}
		perfcommon.StampCooldownPayload(payload)
		err := publisher.Publish(
			"bench.topic",
			zlink.SendFlagsNone,
			perfcommon.NewMessage(payload),
		)
		if err != nil && !perfcommon.IsTransient(err) {
			perfcommon.Must(err)
		}
	}()

	for time.Now().Before(window.StopAt) {
		message, err := subscriber.Subscribe(zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if message == nil {
			continue
		}
		part, err := message.SinglePartOrError()
		if err == nil {
			perfcommon.RecordMessageLatency(stats, window.ActiveAt, cfg.msgSize, part)
		}
		_ = message.Close()
	}

	<-sendDone
	idleDrainDeadline := time.Now().Add(perfcommon.SingleIdleDrainDuration())
	for time.Now().Before(idleDrainDeadline) {
		message, err := subscriber.Subscribe(zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if message == nil {
			continue
		}
		_ = message.Close()
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}
