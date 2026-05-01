package main

import (
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

type multiSpotSubscriber struct {
	node      *zlink.SpotNode
	spot      *zlink.Spot
	discovery *zlink.Discovery
}

const multiSpotServiceName = "perf-spot-svc"
const multiSpotTopic = "bench.topic"

func runMultiSpot(cfg multiConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	registry, err := ctx.Registry()
	perfcommon.Must(err)
	defer registry.Close()
	publisherNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer publisherNode.Close()
	publisherDiscovery, err := ctx.Discovery(zlink.ServiceTypeSpot, multiSpotServiceName)
	perfcommon.Must(err)
	defer publisherDiscovery.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(publisherNode, cfg.pattern)
	publisher, err := publisherNode.Spot()
	perfcommon.Must(err)
	defer publisher.Close()
	perfcommon.Must(publisher.SetNoDrop(true))
	perfcommon.ApplyMultiBenchmarkSocketOptions(publisher, cfg.transport)

	registryPubEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-registry-pub")
	registryRouterEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-registry-router")
	publisherEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-pub")
	perfcommon.Must(registry.Bind(registryPubEndpoint, registryRouterEndpoint))
	perfcommon.Must(publisherDiscovery.ConnectRegistry(registryRouterEndpoint))
	perfcommon.Must(publisherNode.AttachDiscovery(publisherDiscovery))
	perfcommon.Must(perfcommon.ConfigureTLSServer(publisherNode, cfg.transport))
	perfcommon.Must(publisherNode.Bind(publisherEndpoint))

	stats := perfcommon.NewStats()
	var window perfcommon.BenchmarkWindow

	subs := make([]multiSpotSubscriber, 0, cfg.clients)
	pollers := make([]*zlink.Poller, 0, cfg.clients)

	for i := 0; i < cfg.clients; i++ {
		node, err := ctx.SpotNode()
		perfcommon.Must(err)
		perfcommon.ApplyMultiSpotNodeAdmission(node, cfg.pattern)
		spot, err := node.Spot()
		perfcommon.Must(err)
		discovery, err := ctx.Discovery(zlink.ServiceTypeSpot, multiSpotServiceName)
		perfcommon.Must(err)
		perfcommon.Must(perfcommon.ConfigureTLSClient(node, cfg.transport))
		perfcommon.Must(discovery.ConnectRegistry(registryRouterEndpoint))
		perfcommon.Must(node.AttachDiscovery(discovery))
		subscriberEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-sub")
		perfcommon.Must(node.Bind(subscriberEndpoint))
		perfcommon.ApplyMultiBenchmarkSocketOptions(spot, cfg.transport)
		perfcommon.Must(spot.SetSubscription("bench."))
		poller := perfcommon.NewSocketPoller(spot, perfcommon.ZLinkPollIn)
		subs = append(subs, multiSpotSubscriber{node: node, spot: spot, discovery: discovery})
		pollers = append(pollers, poller)
	}
	defer func() {
		for _, poller := range pollers {
			_ = poller.Close()
		}
		for _, sub := range subs {
			_ = sub.spot.Close()
			_ = sub.node.Close()
			_ = sub.discovery.Close()
		}
	}()

	waitForMultiSpotReady(publisher, subs, pollers, cfg.msgSize)
	window = perfcommon.NewBenchmarkWindow(cfg.duration)
	recvDone := make(chan struct{})
	go func() {
		defer close(recvDone)
		idleDeadline := window.StopAt.Add(2 * time.Second)
		cooldownSeen := make([]bool, len(subs))
		for time.Now().Before(idleDeadline) {
			progressed := drainMultiSpotAvailable(
				subs,
				pollers,
				stats,
				cfg.msgSize,
				window.ActiveAt,
				window.StopAt,
				cooldownSeen,
			)
			if allMultiSpotCooldownSeen(cooldownSeen) && time.Now().After(window.StopAt) {
				return
			}
			if !progressed {
				time.Sleep(time.Millisecond)
			}
		}
	}()

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(window.StopAt) {
		perfcommon.StampWindowPayload(payload, window.ActiveAt)
		err := publisher.Publish(multiSpotServiceName, multiSpotTopic, zlink.SendFlagsDontWait, perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
	}
	perfcommon.StampCooldownPayload(payload)
	for {
		err := publisher.Publish(multiSpotServiceName, multiSpotTopic, zlink.SendFlagsDontWait, perfcommon.NewMessage(payload))
		if err == nil {
			break
		}
		if perfcommon.IsTransient(err) {
			continue
		}
		perfcommon.Must(err)
	}
	<-recvDone

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func allMultiSpotCooldownSeen(cooldownSeen []bool) bool {
	for _, seen := range cooldownSeen {
		if !seen {
			return false
		}
	}
	return len(cooldownSeen) > 0
}

func drainMultiSpotAvailable(
	subs []multiSpotSubscriber,
	pollers []*zlink.Poller,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
	stopAt time.Time,
	cooldownSeen []bool,
) bool {
	processed := false
	for index, sub := range subs {
		event, err := pollers[index].Wait(0)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if event == nil || event.Events&perfcommon.ZLinkPollIn == 0 {
			continue
		}
		for {
			message, err := sub.spot.Subscribe(zlink.RecvFlagsDontWait)
			if err != nil {
				if perfcommon.IsTransient(err) {
					break
				}
				perfcommon.Must(err)
			}
			if message == nil {
				break
			}
			processed = true
			part, err := message.SinglePartOrError()
			if err == nil && part != nil {
				data := part.Data()
				header, ok := perfcommon.DecodeMetricHeader(data)
				if ok && header.RunID == perfcommon.MetricRunID && int(header.MsgSize) == msgSize {
					if header.Phase == perfcommon.PhaseCooldown {
						cooldownSeen[index] = true
					} else if header.Phase == perfcommon.PhaseActive {
						now := time.Now()
						if now.After(activeAt) && now.Before(stopAt) {
							stats.AddLatencyNs(float64(now.UnixNano() - header.SentTsNs))
						}
					}
				}
			}
			_ = message.Close()
		}
	}
	return processed
}
