package main

import (
	"fmt"
	"os"
	"strconv"
	"time"

	zlink "zlink.systems/zlink/v11"
	"zlink.systems/zlink/v11/perf/internal/perfcommon"
)

func runMultiPubSubServer(cfg multiConfig) {
	ctx, err := perfcommon.NewMultiServerContext()
	perfcommon.Must(err)
	defer ctx.Close()

	publisher, err := ctx.PubSocket()
	perfcommon.Must(err)
	defer publisher.Close()
	perfcommon.Must(perfcommon.ConfigureTLSServer(publisher, cfg.transport))
	perfcommon.ApplyMultiAutoHWMMsgUnit(ctx, cfg.msgSize)
	perfcommon.ApplyMultiHWM(publisher, cfg.pattern)
	perfcommon.ApplyMultiBenchmarkSocketOptions(publisher, cfg.transport)
	endpoint := perfcommon.BindAndResolveEndpoint(publisher, cfg.transport, "perf-multi-pubsub")
	flushControlLine("READY,%s", endpoint)
	if !waitForStartToken(cfg.msgSize) {
		return
	}

	window := activeDeadline(cfg.duration)
	payload := perfcommon.PreparePayload(cfg.msgSize)
	for time.Now().Before(window.StopAt) {
		if useMultiPubSubWindowMessage(cfg.transport, cfg.msgSize) {
			_, err := perfcommon.SubmitWindowPayload(cfg.msgSize, window.ActiveAt, func(message *zlink.Message) (bool, error) {
				return publisher.Publish("bench").MoveMessage(message).Flags(zlink.SendFlagsDontWait).Submit(nil)
			})
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(fmt.Errorf("multi pubsub publish size=%d clients=%d transport=%s: %w",
					cfg.msgSize, cfg.clients, cfg.transport, err))
			}
			continue
		}
		perfcommon.StampWindowPayload(payload, window.ActiveAt)
		msg := perfcommon.NewMessage(payload)
		_, err = publisher.Publish("bench").MoveMessage(msg).Flags(zlink.SendFlagsDontWait).Submit(nil)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi pubsub publish size=%d clients=%d transport=%s: %w",
				cfg.msgSize, cfg.clients, cfg.transport, err))
		}
	}
	sendMultiPubSubStopToken(publisher)
}

func useMultiPubSubWindowMessage(transport string, msgSize int) bool {
	switch transport {
	case "tls":
		return msgSize <= 256
	case "wss":
		return msgSize == 64
	default:
		return false
	}
}

func runMultiPubSubClient(cfg multiConfig, endpoint string) perfcommon.Result {
	ctx, err := perfcommon.NewMultiClientContext()
	perfcommon.Must(err)
	defer ctx.Close()
	perfcommon.ApplyMultiAutoHWMMsgUnit(ctx, cfg.msgSize)

	stats := perfcommon.NewStats()
	latencyStride := resolveMultiPubSubLatencySampleStride()
	subs := make([]*zlink.SubSocket, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		sub, err := ctx.SubSocket()
		if err != nil {
			perfcommon.Must(fmt.Errorf("multi pubsub create sub socket[%d]: %w", i, err))
		}
		subs = append(subs, sub)
		perfcommon.Must(perfcommon.ConfigureTLSClient(sub, cfg.transport))
		perfcommon.ApplyMultiHWM(sub, cfg.pattern)
		perfcommon.ApplyMultiBenchmarkSocketOptions(sub, cfg.transport)
		subMon := perfcommon.OpenMonitor(sub)
		if err := sub.SetSubscription("bench"); err != nil {
			perfcommon.Must(fmt.Errorf("multi pubsub subscribe[%d]: %w", i, err))
		}
		if err := sub.Connect(endpoint); err != nil {
			perfcommon.Must(fmt.Errorf("multi pubsub connect sub[%d]: %w", i, err))
		}
		perfcommon.WaitConnectedWithTimeout(perfcommon.MultiReadyTimeout(), subMon)
		_ = subMon.Close()
	}
	defer func() {
		for _, sub := range subs {
			_ = sub.Close()
		}
	}()
	recvParts := make([]*zlink.Message, len(subs))
	topicBuffers := make([][]byte, len(subs))
	for i := range subs {
		part, err := zlink.NewMessageWithSize(0)
		perfcommon.Must(err)
		recvParts[i] = part
		topicBuffers[i] = make([]byte, 256)
	}
	defer func() {
		for _, part := range recvParts {
			_ = part.Close()
		}
	}()

	flushControlLine("CLIENT_READY,%d", cfg.msgSize)
	if !waitForStartToken(cfg.msgSize) {
		return stats.Snapshot(cfg.duration, cfg.msgSize)
	}
	window := activeDeadline(cfg.duration)
	poller, err := zlink.NewPoller()
	perfcommon.Must(err)
	defer poller.Close()
	for i, sub := range subs {
		perfcommon.Must(poller.AddSocket(sub, perfcommon.ZLinkPollIn, uintptr(i)))
	}

	// perf_multi_pubsub_client.cpp run_recv_duration: poller wait -1
	// (signal-driven), phase ends on the wire-level stop token /
	// cooldown-phase header; counting is bounded by the active
	// deadline (enforced inside drainMultiPubSubAvailable).
	phaseDone := false
	events := make([]zlink.PollEvent, len(subs))
	for !phaseDone {
		n, err := poller.Wait(events, -1*time.Millisecond)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi pubsub client poll: %w", err))
		}
		drainMultiPubSubReady(subs, recvParts, topicBuffers, events[:n], stats, cfg.msgSize, window.StopAt, latencyStride, &phaseDone)
	}
	flushControlLine("CLIENT_DONE,%d", cfg.msgSize)
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func drainMultiPubSubReady(
	subs []*zlink.SubSocket,
	recvParts []*zlink.Message,
	topicBuffers [][]byte,
	events []zlink.PollEvent,
	stats *perfcommon.Stats,
	msgSize int,
	recvStopAt time.Time,
	latencyStride uint64,
	phaseDone *bool,
) {
	for _, event := range events {
		if event.Revents&perfcommon.ZLinkPollIn == 0 {
			continue
		}
		index := int(event.Slot)
		if index < 0 || index >= len(subs) {
			continue
		}
		drainMultiPubSubSocket(index, subs[index], recvParts[index], topicBuffers[index], stats, msgSize, recvStopAt, latencyStride, phaseDone)
		if *phaseDone {
			return
		}
	}
}

func drainMultiPubSubAvailable(
	subs []*zlink.SubSocket,
	recvParts []*zlink.Message,
	topicBuffers [][]byte,
	stats *perfcommon.Stats,
	msgSize int,
	recvStopAt time.Time,
	latencyStride uint64,
	phaseDone *bool,
) {
	for index, socket := range subs {
		drainMultiPubSubSocket(index, socket, recvParts[index], topicBuffers[index], stats, msgSize, recvStopAt, latencyStride, phaseDone)
		if *phaseDone {
			return
		}
	}
}

func drainMultiPubSubSocket(
	index int,
	socket *zlink.SubSocket,
	part *zlink.Message,
	topicBuffer []byte,
	stats *perfcommon.Stats,
	msgSize int,
	recvStopAt time.Time,
	latencyStride uint64,
	phaseDone *bool,
) {
	for {
		result, ok, err := socket.SubscribePart(part, topicBuffer, zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				break
			}
			perfcommon.Must(fmt.Errorf("multi pubsub subscribe[%d]: %w", index, err))
		}
		if !ok {
			break
		}
		if result.More || !multiTopicMatches(topicBuffer, result.TopicLen, "bench") {
			perfcommon.Must(fmt.Errorf("multi pubsub unexpected part metadata[%d]: topic_len=%d more=%v", index, result.TopicLen, result.More))
		}
		if perfcommon.IsStopTokenMessage(part) {
			*phaseDone = true
			break
		}
		if !perfcommon.HasMetricHeaderPhase(part.Data(), msgSize, perfcommon.PhaseActive) {
			continue
		}
		count := stats.AddCount()
		if shouldSampleMultiPubSubLatency(count, latencyStride) {
			now := time.Now()
			if latencyNs, ok := perfcommon.LatencyNsFromMessageAt(part, msgSize, perfcommon.PhaseActive, now); ok && now.Before(recvStopAt) {
				stats.AddLatencySampleNs(latencyNs)
			}
		}
	}
}

func resolveMultiPubSubLatencySampleStride() uint64 {
	return uint64(positiveMultiPubSubIntEnv("PERF_MULTI_PUBSUB_LATENCY_SAMPLE_STRIDE", 32))
}

func positiveMultiPubSubIntEnv(name string, fallback int) int {
	raw := os.Getenv(name)
	if raw == "" {
		return fallback
	}
	value, err := strconv.Atoi(raw)
	if err != nil || value <= 0 {
		return fallback
	}
	return value
}

func shouldSampleMultiPubSubLatency(index, stride uint64) bool {
	return stride <= 1 || index == 1 || index%stride == 0
}

func sendMultiPubSubStopToken(publisher *zlink.PubSocket) {
	for attempt := 0; attempt < perfcommon.StopTokenSendAttempts; attempt++ {
		sent, err := perfcommon.SubmitPayload(perfcommon.StopToken, func(message *zlink.Message) (bool, error) {
			return publisher.Publish("bench").MoveMessage(message).Submit(nil)
		})
		if err == nil && sent {
			return
		}
		if err != nil && !perfcommon.IsTransient(err) {
			return
		}
		perfcommon.PollIdle(perfcommon.StopTokenSendBackoff)
	}
}
