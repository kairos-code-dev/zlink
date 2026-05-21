package main

import (
	"fmt"
	"time"

	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
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
		perfcommon.StampWindowPayload(payload, window.ActiveAt)
		msg, err := zlink.NewMessage(payload)
		if err != nil {
			perfcommon.Must(fmt.Errorf("multi pubsub create payload message size=%d clients=%d transport=%s: %w",
				cfg.msgSize, cfg.clients, cfg.transport, err))
		}
		_, err = publisher.Publish("bench").Message(msg).Flags(zlink.SendFlagsDontWait).Submit(nil)
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

func runMultiPubSubClient(cfg multiConfig, endpoint string) perfcommon.Result {
	ctx, err := perfcommon.NewMultiClientContext()
	perfcommon.Must(err)
	defer ctx.Close()
	perfcommon.ApplyMultiAutoHWMMsgUnit(ctx, cfg.msgSize)

	stats := perfcommon.NewStats()
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
		drainMultiPubSubReady(subs, events[:n], stats, cfg.msgSize, window.ActiveAt, window.StopAt, &phaseDone)
	}
	flushControlLine("CLIENT_DONE,%d", cfg.msgSize)
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func drainMultiPubSubReady(
	subs []*zlink.SubSocket,
	events []zlink.PollEvent,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
	recvStopAt time.Time,
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
		drainMultiPubSubSocket(index, subs[index], stats, msgSize, activeAt, recvStopAt, phaseDone)
		if *phaseDone {
			return
		}
	}
}

func drainMultiPubSubAvailable(
	subs []*zlink.SubSocket,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
	recvStopAt time.Time,
	phaseDone *bool,
) {
	for index, socket := range subs {
		drainMultiPubSubSocket(index, socket, stats, msgSize, activeAt, recvStopAt, phaseDone)
		if *phaseDone {
			return
		}
	}
}

func drainMultiPubSubSocket(
	index int,
	socket *zlink.SubSocket,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
	recvStopAt time.Time,
	phaseDone *bool,
) {
	for {
		var message zlink.TopicMessage
		ok, err := socket.Subscribe(&message, zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				break
			}
			perfcommon.Must(fmt.Errorf("multi pubsub subscribe[%d]: %w", index, err))
		}
		if !ok {
			break
		}
		part, err := message.SinglePartOrError()
		if err == nil {
			if perfcommon.IsStopTokenMessage(part) {
				*phaseDone = true
				_ = message.Close()
				break
			}
			now := time.Now()
			if sentAt, ok := perfcommon.SentAtFromMessage(part, msgSize); ok && now.After(activeAt) && now.Before(recvStopAt) {
				stats.Add(sentAt)
			}
		}
		_ = message.Close()
	}
}

func sendMultiPubSubStopToken(publisher *zlink.PubSocket) {
	for attempt := 0; attempt < perfcommon.StopTokenSendAttempts; attempt++ {
		sent, err := publisher.Publish("bench").Message(perfcommon.NewMessage(perfcommon.StopToken)).Submit(nil)
		if err == nil && sent {
			return
		}
		if err != nil && !perfcommon.IsTransient(err) {
			return
		}
		time.Sleep(perfcommon.StopTokenSendBackoff)
	}
}
