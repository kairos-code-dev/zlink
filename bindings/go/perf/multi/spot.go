package main

import (
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

type multiSpotSubscriber struct {
	node *zlink.SpotNode
	spot *zlink.Spot
}

func runMultiSpot(cfg multiConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	publisherNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer publisherNode.Close()
	publisher, err := publisherNode.Spot()
	perfcommon.Must(err)
	defer publisher.Close()
	perfcommon.Must(publisher.SetNoDrop(true))

	endpoint := perfcommon.UniqueTCPEndpoint("perf-multi-spot")
	perfcommon.Must(publisherNode.Bind(endpoint))

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(0, cfg.duration)

	subs := make([]multiSpotSubscriber, 0, cfg.clients)
	tracker := newMultiSpotReadyTracker(cfg.clients)

	for i := 0; i < cfg.clients; i++ {
		node, err := ctx.SpotNode()
		perfcommon.Must(err)
		spot, err := node.Spot()
		perfcommon.Must(err)
		perfcommon.Must(node.ConnectPeer(endpoint))
		perfcommon.Must(spot.SetSubscription("bench."))
		if cfg.recvMode == "callback" {
			index := i
			perfcommon.Must(spot.OnSubscribe(func(message *zlink.TopicMessage) {
				defer message.Close()
				tracker.signal(index)
				part, err := message.SinglePartOrError()
				if err != nil {
					return
				}
				perfcommon.RecordMessageLatency(stats, window.ActiveAt, part)
			}))
		}
		subs = append(subs, multiSpotSubscriber{node: node, spot: spot})
	}
	defer func() {
		for _, sub := range subs {
			_ = sub.spot.Close()
			_ = sub.node.Close()
		}
	}()

	waitForMultiSpotReady(publisher, subs, cfg.recvMode, tracker)

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(window.StopAt) {
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
			_ = drainMultiSpotOnce(subs, stats, window.ActiveAt, tracker)
		}
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}
