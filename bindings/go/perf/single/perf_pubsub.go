package main

import (
	"fmt"
	"runtime"
	"time"

	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

const singlePubSubTopic = "bench"

func runPubSub(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := perfcommon.NewSingleContext()
	perfcommon.Must(err)
	defer ctx.Close()
	perfcommon.ApplySingleAutoHWMMsgUnit(ctx, cfg.msgSize)

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
	recvPart, err := zlink.NewMessageWithSize(0)
	perfcommon.Must(err)
	defer recvPart.Close()
	topicBuffer := make([]byte, 256)

	receiverDone := make(chan error, 1)
	go func() {
		// PERF_SINGLE_TEST_POLICY § 1.4: signal-driven wait (-1 ms). Loop
		// exits when the wire-level stop token arrives.
		for {
			event, err := poller.Wait(-1 * time.Millisecond)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				receiverDone <- err
				return
			}
			if event == nil {
				continue
			}
			if event.Events&perfcommon.ZLinkPollIn == 0 {
				continue
			}
			stop, drainErr := drainSinglePubSubUntilStop(subscriber, recvPart, topicBuffer, stats, cfg.msgSize, window.ActiveAt, window.StopAt)
			if drainErr != nil {
				receiverDone <- drainErr
				return
			}
			if stop {
				receiverDone <- nil
				return
			}
		}
	}()

	for time.Now().Before(window.StopAt) {
		perfcommon.StampWindowPayload(payload, window.ActiveAt)
		sent, err := publisher.Publish(singlePubSubTopic).Message(perfcommon.NewMessage(payload)).Flags(zlink.SendFlagsDontWait).Submit(nil)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if !sent {
			runtime.Gosched()
			continue
		}
		runtime.Gosched()
	}
	// PERF_SINGLE_TEST_POLICY § 1.4: signal phase end via wire-level
	// stop token published on the same topic so the subscriber sees
	// it as the last message in the active stream.
	sendPubSubStopToken(publisher)
	if err := <-receiverDone; err != nil {
		perfcommon.Must(err)
	}

	result := stats.Snapshot(cfg.duration, cfg.msgSize)
	perfcommon.PrintSingleAutoHWMDetail(pubMon, cfg.pattern, cfg.transport, "publisher", zlink.SocketTypePub, cfg.msgSize)
	perfcommon.PrintSingleAutoHWMDetail(subMon, cfg.pattern, cfg.transport, "subscriber", zlink.SocketTypeSub, cfg.msgSize)
	return result
}

// drainSinglePubSubUntilStop drains the subscriber until either a
// transient EAGAIN-style condition or the wire-level stop token arrives.
// It returns stop=true when the stop token has been observed.
func drainSinglePubSubUntilStop(
	subscriber *zlink.SubSocket,
	part *zlink.Message,
	topicBuffer []byte,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
	stopAt time.Time,
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
		if result.More || !topicMatches(topicBuffer, result.TopicLen, singlePubSubTopic) {
			return false, fmt.Errorf("unexpected PUBSUB part metadata topic_len=%d more=%v", result.TopicLen, result.More)
		}
		if perfcommon.IsStopTokenMessage(part) {
			return true, nil
		}
		perfcommon.RecordMessageLatency(stats, activeAt, stopAt, msgSize, part)
	}
}

func topicMatches(buffer []byte, topicLen int, expected string) bool {
	if topicLen != len(expected) || topicLen > len(buffer) {
		return false
	}
	for i := 0; i < topicLen; i++ {
		if buffer[i] != expected[i] {
			return false
		}
	}
	return true
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
