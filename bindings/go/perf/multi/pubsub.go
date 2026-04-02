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
	perfcommon.Must(publisher.SetNoDrop(true))
	perfcommon.Must(publisher.SetSendHWM(100))
	pubMon := perfcommon.OpenMonitor(publisher)
	defer pubMon.Close()

	endpoint := perfcommon.UniqueTCPEndpoint("perf-multi-pubsub")
	perfcommon.Must(publisher.Bind(endpoint))

	stats := perfcommon.NewStats()
	stopAt := time.Now().Add(cfg.warmup + cfg.duration)
	activeAt := time.Now().Add(cfg.warmup)
	recvStopAt := stopAt.Add(500 * time.Millisecond)

	subs := make([]*zlink.SubSocket, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		sub, err := ctx.SubSocket()
		if err != nil {
			perfcommon.Must(fmt.Errorf("multi pubsub create sub socket[%d]: %w", i, err))
		}
		subs = append(subs, sub)
		if err := sub.SetRecvHWM(100); err != nil {
			perfcommon.Must(fmt.Errorf("multi pubsub set recv hwm[%d]: %w", i, err))
		}
		subMon := perfcommon.OpenMonitor(sub)
		if err := sub.Connect(endpoint); err != nil {
			perfcommon.Must(fmt.Errorf("multi pubsub connect sub[%d]: %w", i, err))
		}
		perfcommon.WaitConnected(pubMon, subMon)
		_ = subMon.Close()
		if err := sub.SetSubscription("bench."); err != nil {
			perfcommon.Must(fmt.Errorf("multi pubsub subscribe[%d]: %w", i, err))
		}

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
	recvDone := make(chan struct{})
	if cfg.recvMode == "recv" {
		go func() {
			defer close(recvDone)
			for time.Now().Before(recvStopAt) {
				if !drainMultiPubSubAvailable(subs, stats, activeAt, recvStopAt) {
					time.Sleep(50 * time.Microsecond)
				}
			}
		}()
	}
	for time.Now().Before(stopAt) {
		perfcommon.StampPayload(payload)
		msg, err := zlink.NewMessage(payload)
		if err != nil {
			perfcommon.Must(fmt.Errorf(
				"multi pubsub create payload message size=%d clients=%d recv=%s: %w",
				cfg.msgSize,
				cfg.clients,
				cfg.recvMode,
				err,
			))
		}
		result, err := publisher.TryPublish("bench.topic", msg)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(fmt.Errorf(
				"multi pubsub publish size=%d clients=%d recv=%s: %w",
				cfg.msgSize,
				cfg.clients,
				cfg.recvMode,
				err,
			))
		}
		if result != zlink.SendResultSent {
			time.Sleep(250 * time.Microsecond)
		}
	}
	if cfg.recvMode == "recv" {
		<-recvDone
	} else {
		time.Sleep(500 * time.Millisecond)
	}
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func drainMultiPubSubAvailable(
	subs []*zlink.SubSocket,
	stats *perfcommon.Stats,
	activeAt time.Time,
	recvStopAt time.Time,
) bool {
	processed := false
	for index, socket := range subs {
		for {
			message, ok, err := socket.TrySubscribe()
			if err != nil {
				if perfcommon.IsTransient(err) {
					break
				}
				perfcommon.Must(fmt.Errorf("multi pubsub try-subscribe[%d]: %w", index, err))
			}
			if !ok || message == nil {
				break
			}
			processed = true
			part, err := message.SinglePartOrError()
			if err == nil {
				now := time.Now()
				if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && now.After(activeAt) && now.Before(recvStopAt) {
					stats.Add(sentAt)
				}
			}
			_ = message.Close()
		}
	}
	return processed
}
