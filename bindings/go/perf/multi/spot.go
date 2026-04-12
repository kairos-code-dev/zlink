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

	endpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot")
	perfcommon.Must(perfcommon.ConfigureTLSServer(publisherNode, cfg.transport))
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
		perfcommon.Must(perfcommon.ConfigureTLSClient(node, cfg.transport))
		perfcommon.Must(node.ConnectPeer(endpoint))
		perfcommon.Must(spot.SetSubscription("bench."))
		subs = append(subs, multiSpotSubscriber{node: node, spot: spot})
	}
	defer func() {
		for _, sub := range subs {
			_ = sub.spot.Close()
			_ = sub.node.Close()
		}
	}()

	waitForMultiSpotReady(publisher, subs, tracker)

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(window.StopAt) {
		perfcommon.StampPayload(payload)
		err := publisher.Publish("bench.topic", zlink.SendFlagsDontWait, perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		_ = drainMultiSpotOnce(subs, stats, window.ActiveAt, tracker)
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}
