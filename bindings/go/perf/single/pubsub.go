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

	endpoint := perfcommon.UniqueTCPEndpoint("perf-pubsub")
	perfcommon.Must(publisher.Bind(endpoint))
	perfcommon.Must(subscriber.Connect(endpoint))
	perfcommon.Must(subscriber.SetSubscription("bench."))
	_, err = publisher.ReceiveSubscriptionEvent()
	perfcommon.Must(err)

	stats := perfcommon.NewStats()
	stopAt := time.Now().Add(cfg.duration)
	activeAt := time.Now()

	if cfg.recvMode == "callback" {
		perfcommon.Must(subscriber.OnSubscribe(func(message *zlink.TopicMessage) {
			defer message.Close()
			part, err := message.SinglePartOrError()
			if err != nil {
				return
			}
			if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
				stats.Add(sentAt)
			}
		}))
	} else {
		perfcommon.Must(subscriber.SetRecvTimeout(500 * time.Millisecond))
		go func() {
			for time.Now().Before(stopAt) {
				message, err := subscriber.Subscribe()
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
	}

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(stopAt) {
		perfcommon.StampPayload(payload)
		perfcommon.Must(publisher.Publish("bench.topic", perfcommon.NewMessage(payload)))
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}
