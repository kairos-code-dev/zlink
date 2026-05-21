package main

import (
	"fmt"
	"runtime"
	"time"

	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

const singleSpotTopic = "bench"

func runSpot(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := perfcommon.NewSingleContext()
	perfcommon.Must(err)
	defer ctx.Close()
	perfcommon.ApplySingleAutoHWMMsgUnit(ctx, cfg.msgSize)

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
	perfcommon.Must(subscriber.SetSubscription(singleSpotTopic))
	perfcommon.ApplySingleBenchmarkSocketOptions(publisher, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(stopPublisher, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(subscriber, cfg.transport)
	perfcommon.Must(subscriber.SetRecvTimeout(perfcommon.SingleRecvTimeout()))
	poller := perfcommon.NewSocketPoller(subscriber, perfcommon.ZLinkPollIn)
	defer poller.Close()

	stats := perfcommon.NewSingleStats(cfg.duration, cfg.msgSize)
	recvPart, err := zlink.NewMessageWithSize(0)
	perfcommon.Must(err)
	defer recvPart.Close()
	topicBuffer := make([]byte, 256)
	// perf_spot.cpp run_case: SPOT readiness is the local probe-publish +
	// first-valid-recv barrier (wait_for_spot_ready_barrier). There is no
	// connected-peer-count snapshot precondition.
	waitForSpotReady(publisher, subscriber, poller, cfg.msgSize)
	perfcommon.PostReadySettle(cfg.pattern)
	window := perfcommon.NewBenchmarkWindow(cfg.duration)

	// perf_spot.cpp run_active_window: SPOT active receive uses DONTWAIT
	// drain + yield because the SPOT handle does not use the raw socket
	// poller path during active measurement. It also starts the receiver
	// before the sender so tiny messages do not build an initial queue
	// before measurement begins.
	receiverDone := make(chan error, 1)
	go func() {
		for {
			stop, drainErr := drainSingleSpotUntilStop(subscriber, recvPart, topicBuffer, stats, window.ActiveAt, window.StopAt, cfg.msgSize)
			if drainErr != nil {
				receiverDone <- drainErr
				return
			}
			if stop {
				receiverDone <- nil
				return
			}
			runtime.Gosched()
		}
	}()

	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(window.StopAt) {
		perfcommon.StampWindowPayload(payload, window.ActiveAt)
		sent, err := publisher.PublishPart(singleSpotTopic, perfcommon.NewMessage(payload), zlink.SendFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				time.Sleep(time.Millisecond)
				continue
			}
			perfcommon.Must(err)
		}
		if !sent {
			time.Sleep(time.Millisecond)
			continue
		}
		runtime.Gosched()
	}
	// PERF_SINGLE_TEST_POLICY § 1.4: signal phase end via wire-level
	// stop token. Bounded attempts through transient backpressure so
	// the subscriber always observes the terminator.
	sendSpotStopToken(stopPublisher)
	if err := <-receiverDone; err != nil {
		perfcommon.Must(err)
	}

	result := stats.Snapshot(cfg.duration, cfg.msgSize)
	perfcommon.PrintSingleSpotNodeAutoHWMDetail(publisherNode, cfg.pattern, cfg.transport, "publisher", cfg.msgSize)
	perfcommon.PrintSingleSpotNodeAutoHWMDetail(subscriberNode, cfg.pattern, cfg.transport, "subscriber", cfg.msgSize)
	return result
}

// drainSingleSpotUntilStop drains the spot subscriber until a transient
// EAGAIN or until the wire-level stop token arrives.
func drainSingleSpotUntilStop(
	subscriber *zlink.Spot,
	part *zlink.Message,
	topicBuffer []byte,
	stats *perfcommon.Stats,
	activeAt time.Time,
	stopAt time.Time,
	msgSize int,
) (bool, error) {
	for {
		result, ok, err := subscriber.SubscribePart(part, topicBuffer, zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				return false, nil
			}
			return false, err
		}
		if !ok {
			return false, nil
		}
		if result.More || !topicMatches(topicBuffer, result.TopicLen, singleSpotTopic) {
			return false, fmt.Errorf("unexpected SPOT part metadata topic_len=%d more=%v", result.TopicLen, result.More)
		}
		if perfcommon.IsStopTokenMessage(part) {
			return true, nil
		}
		if stats != nil {
			perfcommon.RecordMessageLatency(stats, activeAt, stopAt, msgSize, part)
		}
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

func waitForSpotReady(
	publisher *zlink.Spot,
	subscriber *zlink.Spot,
	poller *zlink.Poller,
	msgSize int,
) {
	payload := perfcommon.PreparePayload(msgSize)
	deadline := time.Now().Add(perfcommon.SingleReadyTimeout())
	for time.Now().Before(deadline) {
		perfcommon.StampProbePayload(payload)
		// perf_spot.cpp wait_for_spot_ready_barrier: DONTWAIT probe
		// publish; the ready-probe transient set tolerates
		// ENOTCONN/EHOSTUNREACH/ENETUNREACH while the peer connects.
		_, probeErr := publisher.Publish(singleSpotTopic).Message(perfcommon.NewMessage(payload)).Flags(zlink.SendFlagsDontWait).Submit(nil)
		if probeErr != nil && !perfcommon.IsReadyProbeTransient(probeErr) {
			perfcommon.Must(probeErr)
		}
		timeout := minDuration(time.Until(deadline), 50*time.Millisecond)
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

func minDuration(a, b time.Duration) time.Duration {
	if a < b {
		return a
	}
	return b
}
