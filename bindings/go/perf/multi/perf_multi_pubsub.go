package main

import (
	"fmt"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runMultiPubSub(cfg multiConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	publisher, err := ctx.XPubSocket()
	perfcommon.Must(err)
	defer publisher.Close()
	perfcommon.Must(publisher.SetNoDrop(perfcommon.EnvEnabled("PERF_MULTI_PUBSUB_XPUB_NODROP", true)))
	perfcommon.ApplyMultiHWM(publisher, cfg.pattern)
	pubMon := perfcommon.OpenMonitor(publisher)
	defer pubMon.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(publisher, cfg.transport))
	perfcommon.ApplyMultiBenchmarkSocketOptions(publisher, cfg.transport)
	endpoint := perfcommon.BindAndResolveEndpoint(publisher, cfg.transport, "perf-multi-pubsub")

	stats := perfcommon.NewStats()
	var window perfcommon.BenchmarkWindow

	subs := make([]*zlink.SubSocket, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		sub, err := ctx.SubSocket()
		if err != nil {
			perfcommon.Must(fmt.Errorf("multi pubsub create sub socket[%d]: %w", i, err))
		}
		subs = append(subs, sub)
		perfcommon.Must(perfcommon.ConfigureTLSClient(sub, cfg.transport))
		perfcommon.ApplyMultiHWM(sub, cfg.pattern)
		perfcommon.ApplyMultiBenchmarkSocketOptions(sub, cfg.transport)
		subMon := perfcommon.OpenMonitor(sub)
		if err := sub.Connect(endpoint); err != nil {
			perfcommon.Must(fmt.Errorf("multi pubsub connect sub[%d]: %w", i, err))
		}
		perfcommon.WaitConnectedWithTimeout(perfcommon.MultiReadyTimeout(), pubMon, subMon)
		_ = subMon.Close()
		if err := sub.SetSubscription("bench."); err != nil {
			perfcommon.Must(fmt.Errorf("multi pubsub subscribe[%d]: %w", i, err))
		}

	}
	defer func() {
		for _, sub := range subs {
			_ = sub.Close()
		}
	}()
	window = perfcommon.NewBenchmarkWindow(cfg.duration)

	payload := perfcommon.PreparePayload(cfg.msgSize)
	recvDone := make(chan struct{})
	go func() {
		defer close(recvDone)
		for time.Now().Before(window.StopAt) {
			if !drainMultiPubSubAvailable(subs, stats, cfg.msgSize, window.ActiveAt, window.StopAt) {
				continue
			}
		}
	}()
	for time.Now().Before(window.StopAt) {
		perfcommon.StampWindowPayload(payload, window.ActiveAt)
		msg, err := zlink.NewMessage(payload)
		if err != nil {
			perfcommon.Must(fmt.Errorf(
				"multi pubsub create payload message size=%d clients=%d transport=%s: %w",
				cfg.msgSize,
				cfg.clients,
				cfg.transport,
				err,
			))
		}
		err = publisher.Publish("bench.topic", zlink.SendFlagsDontWait, msg)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(fmt.Errorf(
				"multi pubsub publish size=%d clients=%d transport=%s: %w",
				cfg.msgSize,
				cfg.clients,
				cfg.transport,
				err,
			))
		}
	}
	<-recvDone
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func drainMultiPubSubAvailable(
	subs []*zlink.SubSocket,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
	recvStopAt time.Time,
) bool {
	processed := false
	for index, socket := range subs {
		for {
			message, err := socket.Subscribe(zlink.RecvFlagsDontWait)
			if err != nil {
				if perfcommon.IsTransient(err) {
					break
				}
				perfcommon.Must(fmt.Errorf("multi pubsub subscribe[%d]: %w", index, err))
			}
			if message == nil {
				break
			}
			processed = true
			part, err := message.SinglePartOrError()
			if err == nil {
				now := time.Now()
				if sentAt, ok := perfcommon.SentAtFromMessage(part, msgSize); ok && now.After(activeAt) && now.Before(recvStopAt) {
					stats.Add(sentAt)
				}
			}
			_ = message.Close()
		}
	}
	return processed
}
