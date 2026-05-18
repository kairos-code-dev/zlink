package main

import (
	"time"

	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

const singlePubSubTopic = "bench"

func runPubSub(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := perfcommon.NewSingleContext()
	perfcommon.Must(err)
	defer ctx.Close()

	publisher, err := ctx.PubSocket()
	perfcommon.Must(err)
	defer publisher.Close()
	subscriber, err := ctx.SubSocket()
	perfcommon.Must(err)
	defer subscriber.Close()

	pubMon := perfcommon.OpenMonitor(publisher)
	defer pubMon.Close()
	subMon := perfcommon.OpenMonitor(subscriber)
	defer subMon.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(publisher, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(subscriber, cfg.transport))
	perfcommon.Must(publisher.SetNoDrop(true))
	// perf_pubsub.cpp: apply_single_auto_hwm_msg_unit on pub/sub (raw
	// sockets), then apply_single_hwm (override-gated).
	perfcommon.ApplySingleAutoHWMMsgUnit(publisher, cfg.msgSize)
	perfcommon.ApplySingleAutoHWMMsgUnit(subscriber, cfg.msgSize)
	perfcommon.ApplySingleHWM(publisher)
	perfcommon.ApplySingleHWM(subscriber)
	endpoint := perfcommon.BindAndResolveEndpoint(publisher, cfg.transport, "perf-pubsub")
	perfcommon.Must(subscriber.Connect(endpoint))
	perfcommon.ApplySingleBenchmarkSocketOptions(publisher, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(subscriber, cfg.transport)
	perfcommon.WaitConnectedWithTimeout(perfcommon.SingleReadyTimeout(), pubMon, subMon)

	perfcommon.Must(subscriber.SetSubscription(""))
	perfcommon.Must(subscriber.SetRecvTimeout(perfcommon.SinglePubSubRecvTimeout()))
	perfcommon.PostReadySettle(cfg.pattern)
	poller := perfcommon.NewSocketPoller(subscriber, perfcommon.ZLinkPollIn)
	defer poller.Close()

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(cfg.duration)
	payload := perfcommon.PreparePayload(cfg.msgSize)

	go func() {
		for time.Now().Before(window.StopAt) {
			perfcommon.StampWindowPayload(payload, window.ActiveAt)
			_, err := publisher.Publish(singlePubSubTopic).Message(perfcommon.NewMessage(payload)).Flags(zlink.SendFlagsDontWait).Submit(nil)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(err)
			}
		}
		// PERF_SINGLE_TEST_POLICY § 1.4: signal phase end via wire-level
		// stop token published on the same topic so the subscriber sees
		// it as the last message in the active stream.
		sendPubSubStopToken(publisher)
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
		stop, drainErr := drainSinglePubSubUntilStop(subscriber, stats, cfg.msgSize, window.ActiveAt, window.StopAt)
		if drainErr != nil {
			perfcommon.Must(drainErr)
		}
		if stop {
			break
		}
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

// drainSinglePubSubUntilStop drains the subscriber until either a
// transient EAGAIN-style condition or the wire-level stop token arrives.
// It returns stop=true when the stop token has been observed.
func drainSinglePubSubUntilStop(
	subscriber *zlink.SubSocket,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
	stopAt time.Time,
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
		if err == nil {
			perfcommon.RecordMessageLatency(stats, activeAt, stopAt, msgSize, part)
		}
		_ = message.Close()
	}
}

// sendPubSubStopToken pushes the wire-level stop token on the bench
// topic. Bounded attempts through transient backpressure mirror the
// pattern used in single one-way / spot. The token is published on the
// same `bench.topic` channel so it is delivered to subscribers that
// matched the active stream.
func sendPubSubStopToken(publisher *zlink.PubSocket) {
	for attempt := 0; attempt < perfcommon.StopTokenSendAttempts; attempt++ {
		_, err := publisher.Publish(singlePubSubTopic).Message(perfcommon.NewMessage(perfcommon.StopToken)).Submit(nil)
		if err == nil {
			return
		}
		if !perfcommon.IsTransient(err) {
			return
		}
		time.Sleep(perfcommon.StopTokenSendBackoff)
	}
}
