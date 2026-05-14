package main

import (
	"fmt"
	"time"

	"zlink.systems/zlink"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

func runMultiPubSub(cfg multiConfig) perfcommon.Result {
	ctx, err := perfcommon.NewMultiContext()
	perfcommon.Must(err)
	defer ctx.Close()

	publisher, err := ctx.PubSocket()
	perfcommon.Must(err)
	defer publisher.Close()
	perfcommon.ApplyMultiHWM(publisher, cfg.pattern)
	pubMon := perfcommon.OpenMonitor(publisher)
	defer pubMon.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(publisher, cfg.transport))
	perfcommon.ApplyMultiBenchmarkSocketOptions(publisher, cfg.transport)
	endpoint := perfcommon.BindAndResolveEndpoint(publisher, cfg.transport, "perf-multi-pubsub")

	stats := perfcommon.NewStats()
	var window perfcommon.BenchmarkWindow

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
		perfcommon.WaitConnectedWithTimeout(perfcommon.MultiReadyTimeout(), pubMon, subMon)
		_ = subMon.Close()

	}
	defer func() {
		for _, sub := range subs {
			_ = sub.Close()
		}
	}()
	window = perfcommon.NewBenchmarkWindow(cfg.duration)

	// PERF_MULTI_TEST_POLICY § 1.3.1: each subscriber gets its own
	// poller. The drain goroutine waits with -1 timeout on the first
	// poller as a wakeup anchor; under steady load every subscriber is
	// fed by the same publisher so the publisher's send-side pacing
	// guarantees that at least the anchor subscriber stays readable.
	pollers := make([]*zlink.Poller, 0, len(subs))
	for _, sub := range subs {
		pollers = append(pollers, perfcommon.NewSocketPoller(sub, perfcommon.ZLinkPollIn))
	}
	defer func() {
		for _, p := range pollers {
			_ = p.Close()
		}
	}()

	payload := perfcommon.PreparePayload(cfg.msgSize)
	recvDone := make(chan struct{})
	go func() {
		defer close(recvDone)
		stopSeen := make([]bool, len(subs))
		stopsRemaining := len(subs)
		for stopsRemaining > 0 {
			// Wait on subscriber 0 (anchor) with -1 timeout. The publisher
			// pushes to every subscriber so the anchor fires on every
			// active-phase send; the helper below drains every readable
			// subscriber non-blocking.
			event, err := pollers[0].Wait(-1 * time.Millisecond)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(fmt.Errorf("multi pubsub poll: %w", err))
			}
			if event == nil {
				continue
			}
			drainMultiPubSubAvailable(
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
	for time.Now().Before(window.StopAt) {
		perfcommon.StampWindowPayload(payload, window.ActiveAt)
		msg, err := zlink.NewMessage(payload)
		if err != nil {
			perfcommon.Must(fmt.Errorf(
				"multi pubsub create payload message size=%d clients=%d transport=%s: %w",
				cfg.msgSize,
				cfg.clients,
				cfg.transport,
				err,
			))
		}
		_, err = publisher.Publish("bench").Message(msg).Flags(zlink.SendFlagsDontWait).Submit(nil)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(fmt.Errorf(
				"multi pubsub publish size=%d clients=%d transport=%s: %w",
				cfg.msgSize,
				cfg.clients,
				cfg.transport,
				err,
			))
		}
	}
	// PERF_MULTI_TEST_POLICY § 1.3.1: emit the wire-level stop token on
	// the bench topic so every subscriber sees it as the last in-flight
	// payload of the active stream.
	sendMultiPubSubStopToken(publisher)
	<-recvDone
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func runMultiPubSubServer(cfg multiConfig) {
	ctx, err := perfcommon.NewMultiServerContext()
	perfcommon.Must(err)
	defer ctx.Close()

	publisher, err := ctx.PubSocket()
	perfcommon.Must(err)
	defer publisher.Close()
	perfcommon.Must(perfcommon.ConfigureTLSServer(publisher, cfg.transport))
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
	pollers := make([]*zlink.Poller, 0, len(subs))
	for _, sub := range subs {
		pollers = append(pollers, perfcommon.NewSocketPoller(sub, perfcommon.ZLinkPollIn))
	}
	defer func() {
		for _, p := range pollers {
			_ = p.Close()
		}
	}()

	stopSeen := make([]bool, len(subs))
	stopsRemaining := len(subs)
	for stopsRemaining > 0 {
		event, err := pollers[0].Wait(time.Until(window.StopAt.Add(perfcommon.MultiRecvTimeout())))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi pubsub client poll: %w", err))
		}
		if event == nil {
			break
		}
		drainMultiPubSubAvailable(subs, stats, cfg.msgSize, window.ActiveAt, window.StopAt, stopSeen, &stopsRemaining)
	}
	flushControlLine("CLIENT_DONE,%d", cfg.msgSize)
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func drainMultiPubSubAvailable(
	subs []*zlink.SubSocket,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
	recvStopAt time.Time,
	stopSeen []bool,
	stopsRemaining *int,
) {
	for index, socket := range subs {
		if stopSeen[index] {
			continue
		}
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
					if !stopSeen[index] {
						stopSeen[index] = true
						*stopsRemaining--
					}
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
}

func sendMultiPubSubStopToken(publisher *zlink.PubSocket) {
	for attempt := 0; attempt < perfcommon.StopTokenSendAttempts; attempt++ {
		sent, err := publisher.Publish("bench").Message(perfcommon.NewMessage(perfcommon.StopToken)).Flags(zlink.SendFlagsDontWait).Submit(nil)
		if err == nil && sent {
			return
		}
		if err != nil && !perfcommon.IsTransient(err) {
			return
		}
		time.Sleep(perfcommon.StopTokenSendBackoff)
	}
}
