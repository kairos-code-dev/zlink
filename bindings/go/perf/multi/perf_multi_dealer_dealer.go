package main

import (
	"fmt"
	"sync"
	"time"

	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

func runMultiDealerDealerServer(cfg multiConfig) {
	serverCtx, err := perfcommon.NewMultiServerContext()
	perfcommon.Must(err)
	defer serverCtx.Close()

	server, err := serverCtx.DealerSocket()
	perfcommon.Must(err)
	defer server.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(server, cfg.transport))
	perfcommon.ApplyMultiAutoHWMMsgUnit(serverCtx, cfg.msgSize)
	perfcommon.ApplyMultiHWM(server, cfg.pattern)
	perfcommon.ApplyMultiBenchmarkSocketOptions(server, cfg.transport)
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-multi-dealer-dealer")
	flushControlLine("READY,%s", endpoint)
	if !waitForStartToken(cfg.msgSize) {
		return
	}

	stats := perfcommon.NewStats()
	window := activeDeadline(cfg.duration)
	poller := perfcommon.NewSocketPoller(server, perfcommon.ZLinkPollIn)
	defer poller.Close()
	events := make([]zlink.PollEvent, 1)

	stopRequested := false
	for !stopRequested {
		event, pollErr := perfcommon.WaitPollerOne(poller, events, time.Until(window.StopAt))
		if pollErr != nil {
			if perfcommon.IsTransient(pollErr) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi dealer/dealer server poll: %w", pollErr))
		}
		if event == nil {
			break
		}
		if event.Revents&perfcommon.ZLinkPollIn == 0 {
			continue
		}
		var received zlink.Received
		for {
			ok, recvErr := server.Recv(&received, zlink.RecvFlagsDontWait)
			if recvErr != nil {
				if perfcommon.IsTransient(recvErr) {
					break
				}
				perfcommon.Must(fmt.Errorf("multi dealer/dealer server recv: %w", recvErr))
			}
			if !ok {
				break
			}
			part, partErr := received.SinglePartOrError()
			if partErr == nil {
				if perfcommon.IsStopTokenMessage(part) {
					stopRequested = true
					_ = received.Close()
					break
				}
				now := time.Now()
				if sentAt, ok := perfcommon.SentAtFromMessage(part, cfg.msgSize); ok && now.After(window.ActiveAt) && now.Before(window.StopAt) {
					stats.Add(sentAt)
				}
			}
			_ = received.Close()
		}
	}
	printMultiResult(cfg, stats.Snapshot(cfg.duration, cfg.msgSize))
}

func runMultiDealerDealerClient(cfg multiConfig, endpoint string) {
	type dealerClient struct {
		ctx    *zlink.Context
		socket *zlink.DealerSocket
		mon    *zlink.SocketMonitor
	}
	clients := make([]dealerClient, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		clientCtx, err := perfcommon.NewMultiClientContext()
		perfcommon.Must(err)
		client, err := clientCtx.DealerSocket()
		perfcommon.Must(err)
		clientMon := perfcommon.OpenMonitor(client)
		perfcommon.Must(perfcommon.ConfigureTLSClient(client, cfg.transport))
		perfcommon.ApplyMultiAutoHWMMsgUnit(clientCtx, cfg.msgSize)
		perfcommon.ApplyMultiHWM(client, cfg.pattern)
		perfcommon.ApplyMultiBenchmarkSocketOptions(client, cfg.transport)
		perfcommon.Must(client.Connect(endpoint))
		perfcommon.WaitConnectedWithTimeout(perfcommon.MultiReadyTimeout(), clientMon)
		clients = append(clients, dealerClient{ctx: clientCtx, socket: client, mon: clientMon})
	}
	defer func() {
		for _, client := range clients {
			_ = client.mon.Close()
			_ = client.socket.Close()
			_ = client.ctx.Close()
		}
	}()

	flushControlLine("CLIENT_READY,%d", cfg.msgSize)
	if !waitForStartToken(cfg.msgSize) {
		return
	}
	window := activeDeadline(cfg.duration)
	var wg sync.WaitGroup
	for _, client := range clients {
		wg.Add(1)
		go func(socket *zlink.DealerSocket) {
			defer wg.Done()
			// perf_multi_dealer_dealer_client.cpp run_send_window:
			// nonblocking send; on send_blocked set send_pending and
			// wait POLLOUT (-1), then retry. No immediate busy-retry.
			poller := perfcommon.NewSocketPoller(socket, perfcommon.ZLinkPollOut)
			defer poller.Close()
			events := make([]zlink.PollEvent, 1)
			payload := perfcommon.PreparePayload(cfg.msgSize)
			for time.Now().Before(window.StopAt) {
				perfcommon.StampWindowPayload(payload, window.ActiveAt)
				_, sendErr := socket.Send().Message(perfcommon.NewMessage(payload)).Flags(zlink.SendFlagsDontWait).Submit(nil)
				if sendErr == nil {
					continue
				}
				if !perfcommon.IsTransient(sendErr) {
					perfcommon.Must(fmt.Errorf("multi dealer/dealer client send: %w", sendErr))
				}
				// send_pending: block on POLLOUT until the socket is
				// writable again (signal-driven, -1 wait).
				if _, waitErr := perfcommon.WaitPollerOne(poller, events, -1*time.Millisecond); waitErr != nil {
					if perfcommon.IsTransient(waitErr) {
						continue
					}
					perfcommon.Must(fmt.Errorf("multi dealer/dealer client poll: %w", waitErr))
				}
			}
		}(client.socket)
	}
	wg.Wait()
	if len(clients) > 0 {
		sendMultiDealerStopToken(clients[0].socket)
	}
	flushControlLine("CLIENT_DONE,%d", cfg.msgSize)
}

// sendMultiDealerStopToken pushes the wire-level stop token through the
// dealer socket. Bounded attempts through transient backpressure mirror
// the cpp / java / dotnet implementations.
func sendMultiDealerStopToken(socket *zlink.DealerSocket) {
	for attempt := 0; attempt < perfcommon.StopTokenSendAttempts; attempt++ {
		sent, err := socket.Send().Message(perfcommon.NewMessage(perfcommon.StopToken)).Submit(nil)
		if err == nil && sent {
			return
		}
		if err != nil && !perfcommon.IsTransient(err) {
			return
		}
		time.Sleep(perfcommon.StopTokenSendBackoff)
	}
}
