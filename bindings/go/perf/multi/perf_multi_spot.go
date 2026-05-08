package main

import (
	"fmt"
	"time"

	"zlink.systems/zlink"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

type multiSpotSubscriber struct {
	spot *zlink.Spot
}

const multiSpotServiceName = "perf-spot-svc"
const multiSpotTopic = "bench.topic"
const multiSpotMaxDrainPerSpot = 1024

func runMultiSpot(cfg multiConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	registry, err := ctx.Registry()
	perfcommon.Must(err)
	defer registry.Close()
	query, err := ctx.RegistryQueryClient()
	perfcommon.Must(err)
	defer query.Close()
	publisherNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer publisherNode.Close()
	publisherDiscovery, err := ctx.Discovery(zlink.AutoConnectSpotMesh, multiSpotServiceName)
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
	perfcommon.Must(query.Connect(registryRouterEndpoint))
	perfcommon.Must(publisherDiscovery.ConnectRegistry(registryRouterEndpoint))
	perfcommon.Must(publisherNode.AttachDiscovery(publisherDiscovery))
	perfcommon.Must(perfcommon.ConfigureTLSServer(publisherNode, cfg.transport))
	perfcommon.Must(publisherNode.Bind(publisherEndpoint))

	stats := perfcommon.NewStats()
	var window perfcommon.BenchmarkWindow

	subscriberNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer subscriberNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(subscriberNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSClient(subscriberNode, cfg.transport))
	subscriberDiscovery, err := ctx.Discovery(zlink.AutoConnectSpotMesh, multiSpotServiceName)
	perfcommon.Must(err)
	defer subscriberDiscovery.Close()
	perfcommon.Must(subscriberDiscovery.ConnectRegistry(registryRouterEndpoint))

	subs := make([]multiSpotSubscriber, 0, cfg.clients)
	pollers := make([]*zlink.Poller, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		spot, err := subscriberNode.Spot()
		perfcommon.Must(wrapMultiSpotError("subscriber spot create", i, err))
		subs = append(subs, multiSpotSubscriber{spot: spot})
	}
	for i, sub := range subs {
		perfcommon.Must(wrapMultiSpotError("subscriber spot options", i,
			applyMultiSpotOptions(sub.spot, cfg.transport)))
		perfcommon.Must(wrapMultiSpotError("subscriber spot subscribe", i,
			sub.spot.SetSubscription("bench.")))
		pollers = append(pollers,
			perfcommon.NewSocketPoller(sub.spot, perfcommon.ZLinkPollIn))
	}
	subscriberEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-sub")
	perfcommon.Must(subscriberNode.AttachDiscovery(subscriberDiscovery))
	perfcommon.Must(subscriberNode.Bind(subscriberEndpoint))
	waitForMultiSpotRegistryEntries(query, multiSpotServiceName)
	defer func() {
		for _, poller := range pollers {
			_ = poller.Close()
		}
		for _, sub := range subs {
			_ = sub.spot.Close()
		}
	}()

	waitForMultiSpotReady(subs)
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
		_, err := publisher.Publish(multiSpotServiceName, multiSpotTopic).Message(perfcommon.NewMessage(payload)).Flags(zlink.SendFlagsDontWait).Submit(nil)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
	}
	perfcommon.StampCooldownPayload(payload)
	for {
		_, err := publisher.Publish(multiSpotServiceName, multiSpotTopic).Message(perfcommon.NewMessage(payload)).Flags(zlink.SendFlagsDontWait).Submit(nil)
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

func applyMultiSpotOptions(spot *zlink.Spot, transport string) error {
	if transport == "pgm" || transport == "epgm" {
		return nil
	}
	if err := spot.SetLinger(0); err != nil {
		return err
	}
	if err := spot.SetSendTimeout(perfcommon.MultiSendTimeout()); err != nil {
		return err
	}
	if err := spot.SetRecvTimeout(perfcommon.MultiRecvTimeout()); err != nil {
		return err
	}
	return nil
}

func wrapMultiSpotError(step string, index int, err error) error {
	if err == nil {
		return nil
	}
	return fmt.Errorf("multi spot setup %s[%d]: %w", step, index, err)
}

func waitForMultiSpotRegistryEntries(query *zlink.RegistryQueryClient, serviceName string) {
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(deadline) {
		entries, err := query.Snapshot(nil)
		if err == nil {
			count := 0
			for _, entry := range entries {
				if entry.ChannelName == serviceName {
					count++
				}
			}
			if count >= 2 {
				return
			}
		}
		time.Sleep(10 * time.Millisecond)
	}
	perfcommon.Must(fmt.Errorf("multi spot perf registry entries timed out"))
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
		for drained := 0; drained < multiSpotMaxDrainPerSpot; drained++ {
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
