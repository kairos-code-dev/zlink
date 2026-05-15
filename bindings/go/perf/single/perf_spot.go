package main

import (
	"fmt"
	"time"

	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

const singleSpotTopic = "bench"

func runSpot(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := perfcommon.NewSingleContext()
	perfcommon.Must(err)
	defer ctx.Close()

	publisherNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer publisherNode.Close()
	subscriberNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer subscriberNode.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(publisherNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(subscriberNode, cfg.transport))
	perfcommon.ApplySingleSpotNodeAdmission(publisherNode)
	perfcommon.ApplySingleSpotNodeAdmission(subscriberNode)
	perfcommon.Must(publisherNode.SetRoutingID(zlink.NewRoutingID([]byte("z-go-perf-spot-publisher"))))
	perfcommon.Must(subscriberNode.SetRoutingID(zlink.NewRoutingID([]byte("a-go-perf-spot-subscriber"))))
	publisher, err := publisherNode.Spot()
	perfcommon.Must(err)
	defer publisher.Close()
	stopPublisher, err := subscriberNode.Spot()
	perfcommon.Must(err)
	defer stopPublisher.Close()
	subscriber, err := subscriberNode.Spot()
	perfcommon.Must(err)
	defer subscriber.Close()
	perfcommon.Must(publisher.SetRoutingID(zlink.NewRoutingID([]byte("z-go-perf-spot-publisher-spot"))))
	perfcommon.Must(stopPublisher.SetRoutingID(zlink.NewRoutingID([]byte("m-go-perf-spot-stop-spot"))))
	perfcommon.Must(subscriber.SetRoutingID(zlink.NewRoutingID([]byte("a-go-perf-spot-subscriber-spot"))))
	publisherEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-spot-pub")
	perfcommon.Must(publisherNode.Bind(publisherEndpoint))
	perfcommon.Must(subscriberNode.ConnectPeer(publisherEndpoint))
	perfcommon.Must(publisher.SetNoDrop(true))
	perfcommon.Must(subscriber.SetSubscription(singleSpotTopic))
	perfcommon.ApplySingleBenchmarkSocketOptions(publisher, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(stopPublisher, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(subscriber, cfg.transport)
	perfcommon.Must(subscriber.SetRecvTimeout(perfcommon.SingleRecvTimeout()))
	poller := perfcommon.NewSocketPoller(subscriber, perfcommon.ZLinkPollIn)
	defer poller.Close()

	stats := perfcommon.NewStats()
	waitForSpotPeerConnected(subscriberNode)
	waitForSpotReady(publisher, subscriber, poller, cfg.msgSize)
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
		// stop token. Bounded attempts through transient backpressure so
		// the subscriber always observes the terminator.
		sendSpotStopToken(stopPublisher)
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
		stop, drainErr := drainSingleSpotUntilStop(subscriber, stats, window.ActiveAt, window.StopAt, cfg.msgSize)
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
	stopAt time.Time,
	msgSize int,
) (bool, error) {
	for {
		var message zlink.TopicMessage
		ok, err := subscriber.Subscribe(&message, zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				return false, nil
			}
			return false, err
		}
		if !ok {
			return false, nil
		}
		part, err := message.SinglePartOrError()
		if err == nil && perfcommon.IsStopTokenMessage(part) {
			_ = message.Close()
			return true, nil
		}
		if err == nil && stats != nil {
			perfcommon.RecordMessageLatency(stats, activeAt, stopAt, msgSize, part)
		}
		_ = message.Close()
	}
}

// drainSingleSpotProbe drains pending spot messages once for ready-probe
// handshakes and reports whether any data was processed.
func drainSingleSpotProbe(subscriber *zlink.Spot) bool {
	processed := false
	for {
		var message zlink.TopicMessage
		ok, err := subscriber.Subscribe(&message, zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				return processed
			}
			perfcommon.Must(err)
		}
		if !ok {
			return processed
		}
		processed = true
		_ = message.Close()
	}
}

// sendSpotStopToken pushes the wire-level stop token onto the bench
// topic with bounded attempts through transient backpressure.
func sendSpotStopToken(publisher *zlink.Spot) {
	for attempt := 0; attempt < perfcommon.StopTokenSendAttempts; attempt++ {
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

func waitForSpotPeerConnected(node *zlink.SpotNode) {
	deadline := time.Now().Add(perfcommon.SingleReadyTimeout())
	for time.Now().Before(deadline) {
		status, err := node.StatusSnapshot()
		if err == nil && status.ConnectedPeerCount > 0 {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	perfcommon.Must(fmt.Errorf("spot perf peer connection timed out"))
}

func waitForSpotReady(
	publisher *zlink.Spot,
	subscriber *zlink.Spot,
	poller *zlink.Poller,
	msgSize int,
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
