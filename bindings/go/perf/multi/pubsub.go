package main

import (
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
	pubMon := perfcommon.OpenMonitor(publisher)
	defer pubMon.Close()

	endpoint := perfcommon.UniqueTCPEndpoint("perf-multi-pubsub")
	perfcommon.Must(publisher.Bind(endpoint))

	stats := perfcommon.NewStats()
	stopAt := time.Now().Add(cfg.warmup + cfg.duration)
	activeAt := time.Now().Add(cfg.warmup)

	subs := make([]*zlink.SubSocket, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		sub, err := ctx.SubSocket()
		perfcommon.Must(err)
		subs = append(subs, sub)
		subMon := perfcommon.OpenMonitor(sub)
		perfcommon.Must(sub.Connect(endpoint))
		perfcommon.WaitConnected(pubMon, subMon)
		_ = subMon.Close()
		perfcommon.Must(sub.SetSubscription("bench."))

		if cfg.recvMode == "callback" {
			perfcommon.Must(sub.OnSubscribe(func(message *zlink.TopicMessage) {
				defer message.Close()
				part, err := message.SinglePartOrError()
				if err != nil {
					return
				}
				if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
					stats.Add(sentAt)
				}
			}))
			continue
		}
	}
	defer func() {
		for _, sub := range subs {
			_ = sub.Close()
		}
	}()

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
			drainMultiPubSubOnce(subs, stats, activeAt)
		}
	}
	if cfg.recvMode == "recv" {
		drainUntil := time.Now().Add(500 * time.Millisecond)
		for time.Now().Before(drainUntil) {
			if !drainMultiPubSubOnce(subs, stats, activeAt) {
				time.Sleep(250 * time.Microsecond)
			}
		}
	} else {
		time.Sleep(500 * time.Millisecond)
	}
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func drainMultiPubSubOnce(subs []*zlink.SubSocket, stats *perfcommon.Stats, activeAt time.Time) bool {
	processed := false
	for _, socket := range subs {
		message, ok, err := socket.TrySubscribe()
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if !ok || message == nil {
			continue
		}
		processed = true
		part, err := message.SinglePartOrError()
		if err == nil {
			if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
				stats.Add(sentAt)
			}
		}
		_ = message.Close()
	}
	return processed
}
