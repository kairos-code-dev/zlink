package main

import (
	"fmt"
	"os"
	"time"

	"zlink.systems/zlink"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

const singleSpotChannelName = "perf-spot-svc"
const singleSpotTopic = "bench.topic"

func runSpot(cfg benchmarkConfig) perfcommon.Result {
	channelName := fmt.Sprintf("%s-%d-%d", singleSpotChannelName, os.Getpid(), time.Now().UnixNano())
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
	subscriberNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer subscriberNode.Close()
	publisherDiscovery, err := ctx.Discovery(zlink.AutoConnectSpotMesh, channelName)
	perfcommon.Must(err)
	defer publisherDiscovery.Close()
	subscriberDiscovery, err := ctx.Discovery(zlink.AutoConnectSpotMesh, channelName)
	perfcommon.Must(err)
	defer subscriberDiscovery.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(publisherNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(subscriberNode, cfg.transport))
	perfcommon.ApplySingleSpotNodeAdmission(publisherNode)
	perfcommon.ApplySingleSpotNodeAdmission(subscriberNode)
	publisher, err := publisherNode.Spot()
	perfcommon.Must(err)
	defer publisher.Close()
	subscriber, err := subscriberNode.Spot()
	perfcommon.Must(err)
	defer subscriber.Close()
	registryPubEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-spot-registry-pub")
	registryRouterEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-spot-registry-router")
	publisherEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-spot-pub")
	subscriberEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-spot-sub")
	perfcommon.Must(registry.Bind(registryPubEndpoint, registryRouterEndpoint))
	perfcommon.Must(query.Connect(registryRouterEndpoint))
	perfcommon.Must(publisherDiscovery.ConnectRegistry(registryRouterEndpoint))
	perfcommon.Must(subscriberDiscovery.ConnectRegistry(registryRouterEndpoint))
	perfcommon.Must(publisherNode.AttachDiscovery(publisherDiscovery))
	perfcommon.Must(subscriberNode.AttachDiscovery(subscriberDiscovery))
	perfcommon.Must(publisherNode.Bind(publisherEndpoint))
	perfcommon.Must(subscriberNode.Bind(subscriberEndpoint))
	perfcommon.Must(publisher.SetNoDrop(true))
	perfcommon.Must(subscriber.SetSubscription("bench."))
	perfcommon.ApplySingleBenchmarkSocketOptions(publisher, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(subscriber, cfg.transport)
	perfcommon.Must(subscriber.SetRecvTimeout(perfcommon.SingleRecvTimeout()))
	poller := perfcommon.NewSocketPoller(subscriber, perfcommon.ZLinkPollIn)
	defer poller.Close()

	stats := perfcommon.NewStats()
	waitForSpotRegistryEntries(query, channelName)
	waitForSpotReady(publisher, subscriber, poller, cfg.msgSize, channelName)
	perfcommon.PostReadySettle(cfg.pattern)
	window := perfcommon.NewBenchmarkWindow(cfg.duration)
	go func() {
		payload := perfcommon.PreparePayload(cfg.msgSize)
		for time.Now().Before(window.StopAt) {
			perfcommon.StampWindowPayload(payload, window.ActiveAt)
			_, err := publisher.Publish(singleSpotTopic).Message(perfcommon.NewMessage(payload)).Submit(nil)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(err)
			}
		}
		// PERF_SINGLE_TEST_POLICY § 1.4: signal phase end via wire-level
		// stop token. Bounded retry through transient backpressure so
		// the subscriber always observes the terminator.
		sendSpotStopToken(publisher)
	}()

	// PERF_SINGLE_TEST_POLICY § 1.4: signal-driven wait (-1 ms). Loop
	// exits when the wire-level stop token arrives.
	for {
		event, err := poller.Wait(-1 * time.Millisecond)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if event == nil {
			continue
		}
		if event.Events&perfcommon.ZLinkPollIn == 0 {
			continue
		}
		stop, drainErr := drainSingleSpotUntilStop(subscriber, stats, window.ActiveAt, cfg.msgSize)
		if drainErr != nil {
			perfcommon.Must(drainErr)
		}
		if stop {
			break
		}
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

// drainSingleSpotUntilStop drains the spot subscriber until a transient
// EAGAIN or until the wire-level stop token arrives.
func drainSingleSpotUntilStop(
	subscriber *zlink.Spot,
	stats *perfcommon.Stats,
	activeAt time.Time,
	msgSize int,
) (bool, error) {
	for {
		message, err := subscriber.Subscribe(zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				return false, nil
			}
			return false, err
		}
		if message == nil {
			return false, nil
		}
		part, err := message.SinglePartOrError()
		if err == nil && perfcommon.IsStopTokenMessage(part) {
			_ = message.Close()
			return true, nil
		}
		if err == nil && stats != nil {
			perfcommon.RecordMessageLatency(stats, activeAt, msgSize, part)
		}
		_ = message.Close()
	}
}

// drainSingleSpotProbe drains pending spot messages once for ready-probe
// handshakes and reports whether any data was processed.
func drainSingleSpotProbe(subscriber *zlink.Spot) bool {
	processed := false
	for {
		message, err := subscriber.Subscribe(zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				return processed
			}
			perfcommon.Must(err)
		}
		if message == nil {
			return processed
		}
		processed = true
		_ = message.Close()
	}
}

// sendSpotStopToken pushes the wire-level stop token onto the bench
// topic. Bounded retry through transient backpressure.
func sendSpotStopToken(publisher *zlink.Spot) {
	for retry := 0; retry < perfcommon.StopTokenSendRetries; retry++ {
		_, err := publisher.Publish(singleSpotTopic).Message(perfcommon.NewMessage(perfcommon.StopToken)).Submit(nil)
		if err == nil {
			return
		}
		if !perfcommon.IsTransient(err) {
			return
		}
		time.Sleep(perfcommon.StopTokenSendBackoff)
	}
}

func waitForSpotRegistryEntries(query *zlink.RegistryQueryClient, channelName string) {
	deadline := time.Now().Add(perfcommon.SingleReadyTimeout())
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
	perfcommon.Must(fmt.Errorf("spot perf registry entries timed out"))
}

func waitForSpotReady(
	publisher *zlink.Spot,
	subscriber *zlink.Spot,
	poller *zlink.Poller,
	msgSize int,
	channelName string,
) {
	payload := perfcommon.PreparePayload(perfcommon.MetricHeaderSize)
	deadline := time.Now().Add(perfcommon.SingleReadyTimeout())
	for time.Now().Before(deadline) {
		perfcommon.StampProbePayload(payload)
		_, probeErr := publisher.Publish(singleSpotTopic).Message(perfcommon.NewMessage(payload)).Submit(nil)
		if probeErr != nil && !perfcommon.IsTransient(probeErr) {
			perfcommon.Must(probeErr)
		}
		timeout := time.Until(deadline)
		if timeout <= 0 {
			break
		}
		event, waitErr := poller.Wait(timeout)
		if waitErr != nil {
			if perfcommon.IsTransient(waitErr) {
				continue
			}
			perfcommon.Must(waitErr)
		}
		if event == nil || event.Events&perfcommon.ZLinkPollIn == 0 {
			continue
		}
		if drainSingleSpotProbe(subscriber) {
			return
		}
	}
	perfcommon.Must(fmt.Errorf("spot perf endpoint ready probe timed out"))
}
