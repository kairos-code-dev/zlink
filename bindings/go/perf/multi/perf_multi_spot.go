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

const multiSpotChannelName = "perf-spot-svc"
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
	publisherDiscovery, err := ctx.Discovery(zlink.AutoConnectSpotMesh, multiSpotChannelName)
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
	subscriberDiscovery, err := ctx.Discovery(zlink.AutoConnectSpotMesh, multiSpotChannelName)
	perfcommon.Must(err)
	defer subscriberDiscovery.Close()
	perfcommon.Must(subscriberDiscovery.ConnectRegistry(registryRouterEndpoint))

	subs := make([]multiSpotSubscriber, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		spot, err := subscriberNode.Spot()
		perfcommon.Must(wrapMultiSpotError("subscriber spot create", i, err))
		subs = append(subs, multiSpotSubscriber{spot: spot})
	}
	// PERF_MULTI_TEST_POLICY § 1.3.1: a single poller covers every
	// subscriber so the drain goroutine waits with -1 (signal-driven)
	// for any inbound readiness.
	combinedPoller, err := zlink.NewPoller()
	perfcommon.Must(err)
	for i, sub := range subs {
		perfcommon.Must(wrapMultiSpotError("subscriber spot options", i,
			applyMultiSpotOptions(sub.spot, cfg.transport)))
		perfcommon.Must(wrapMultiSpotError("subscriber spot subscribe", i,
			sub.spot.SetSubscription("bench.")))
		perfcommon.Must(wrapMultiSpotError("subscriber spot poller add", i,
			combinedPoller.AddSocket(sub.spot, perfcommon.ZLinkPollIn, i)))
	}
	subscriberEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-sub")
	perfcommon.Must(subscriberNode.AttachDiscovery(subscriberDiscovery))
	perfcommon.Must(subscriberNode.Bind(subscriberEndpoint))
	waitForMultiSpotRegistryEntries(query, multiSpotChannelName)
	defer func() {
		_ = combinedPoller.Close()
		for _, sub := range subs {
			_ = sub.spot.Close()
		}
	}()

	waitForMultiSpotReady(subs)
	window = perfcommon.NewBenchmarkWindow(cfg.duration)
	recvDone := make(chan struct{})
	go func() {
		defer close(recvDone)
		stopSeen := make([]bool, len(subs))
		stopsRemaining := len(subs)
		for stopsRemaining > 0 {
			event, err := combinedPoller.Wait(-1 * time.Millisecond)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(fmt.Errorf("multi spot poll: %w", err))
			}
			if event == nil {
				continue
			}
			if event.Events&perfcommon.ZLinkPollIn == 0 {
				continue
			}
			drainMultiSpotAvailable(
				subs,
				stats,
				cfg.msgSize,
				window.ActiveAt,
				window.StopAt,
				stopSeen,
				&stopsRemaining,
			)
		}
	}()

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(window.StopAt) {
		perfcommon.StampWindowPayload(payload, window.ActiveAt)
		_, err := publisher.Publish(multiSpotTopic).Message(perfcommon.NewMessage(payload)).Flags(zlink.SendFlagsDontWait).Submit(nil)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
	}
	// PERF_MULTI_TEST_POLICY § 1.3.1: wire-level stop token replaces
	// the cooldown-payload terminator. Bounded retry through transient
	// backpressure guarantees every subscriber observes the marker.
	sendMultiSpotStopToken(publisher)
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

func waitForMultiSpotRegistryEntries(query *zlink.RegistryQueryClient, channelName string) {
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(deadline) {
		entries, err := query.Snapshot(nil)
		if err == nil {
			count := 0
			for _, entry := range entries {
				if entry.ChannelName == channelName {
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

func drainMultiSpotAvailable(
	subs []multiSpotSubscriber,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
	stopAt time.Time,
	stopSeen []bool,
	stopsRemaining *int,
) {
	for index, sub := range subs {
		if stopSeen[index] {
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
			part, err := message.SinglePartOrError()
			if err == nil && part != nil {
				if perfcommon.IsStopTokenMessage(part) {
					if !stopSeen[index] {
						stopSeen[index] = true
						*stopsRemaining--
					}
					_ = message.Close()
					break
				}
				data := part.Data()
				header, ok := perfcommon.DecodeMetricHeader(data)
				if ok && header.RunID == perfcommon.MetricRunID && int(header.MsgSize) == msgSize && header.Phase == perfcommon.PhaseActive {
					now := time.Now()
					if now.After(activeAt) && now.Before(stopAt) {
						stats.AddLatencyNs(float64(now.UnixNano() - header.SentTsNs))
					}
				}
			}
			_ = message.Close()
		}
	}
}

func sendMultiSpotStopToken(publisher *zlink.Spot) {
	for retry := 0; retry < perfcommon.StopTokenSendRetries; retry++ {
		sent, err := publisher.Publish(multiSpotTopic).Message(perfcommon.NewMessage(perfcommon.StopToken)).Flags(zlink.SendFlagsNone).Submit(nil)
		if err == nil && sent {
			return
		}
		if err != nil && !perfcommon.IsTransient(err) {
			return
		}
		time.Sleep(perfcommon.StopTokenSendBackoff)
	}
}
