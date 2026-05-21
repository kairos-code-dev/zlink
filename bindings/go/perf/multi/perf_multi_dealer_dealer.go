package main

import (
	"fmt"
	"time"

	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

type dealerDealerClient struct {
	ctx    *zlink.Context
	socket *zlink.DealerSocket
	mon    *zlink.SocketMonitor
}

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
		if useMultiDealerDealerRecvPart(cfg.msgSize) {
			if drainMultiDealerDealerServerRecvPart(server, cfg, window, stats, &stopRequested) {
				break
			}
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
	drainMultiDealerDealerActiveTail(server, poller, events, cfg)
	printMultiResult(cfg, stats.Snapshot(cfg.duration, cfg.msgSize))
}

func useMultiDealerDealerRecvPart(msgSize int) bool {
	return msgSize == 64 || msgSize == 65536
}

func drainMultiDealerDealerServerRecvPart(
	server *zlink.DealerSocket,
	cfg multiConfig,
	window perfcommon.BenchmarkWindow,
	stats *perfcommon.Stats,
	stopRequested *bool,
) bool {
	message := perfcommon.NewMessageWithSize(0)
	defer message.Close()
	for {
		result, ok, recvErr := server.RecvPart(message, zlink.RecvFlagsDontWait)
		if recvErr != nil {
			if perfcommon.IsTransient(recvErr) {
				return false
			}
			perfcommon.Must(fmt.Errorf("multi dealer/dealer server recv part: %w", recvErr))
		}
		if !ok {
			return false
		}
		if result.RoutingID.Size() != 0 || result.More {
			continue
		}
		if perfcommon.IsStopTokenMessage(message) {
			*stopRequested = true
			return true
		}
		now := time.Now()
		if sentAt, ok := perfcommon.SentAtFromMessage(message, cfg.msgSize); ok && now.After(window.ActiveAt) && now.Before(window.StopAt) {
			stats.Add(sentAt)
		}
	}
}

func drainMultiDealerDealerActiveTail(
	server *zlink.DealerSocket,
	poller *zlink.Poller,
	events []zlink.PollEvent,
	cfg multiConfig,
) {
	maxWait := cfg.duration
	if cfg.msgSize >= 65536 {
		maxWait = 2 * cfg.duration
		if maxWait < 10*time.Second {
			maxWait = 10 * time.Second
		}
	} else if maxWait < 2*time.Second {
		maxWait = 2 * time.Second
	}
	deadline := time.Now().Add(maxWait)
	idleWait := 50 * time.Millisecond
	idleDeadline := time.Now().Add(idleWait)
	var received zlink.Received
	for time.Now().Before(deadline) {
		ok, recvErr := server.Recv(&received, zlink.RecvFlagsDontWait)
		if recvErr != nil {
			if !perfcommon.IsTransient(recvErr) {
				perfcommon.Must(fmt.Errorf("multi dealer/dealer server tail drain recv: %w", recvErr))
			}
			if !time.Now().Before(idleDeadline) {
				return
			}
			wait := time.Until(idleDeadline)
			if wait <= 0 || wait > idleWait {
				wait = idleWait
			}
			if _, waitErr := perfcommon.WaitPollerOne(poller, events, wait); waitErr != nil && !perfcommon.IsTransient(waitErr) {
				perfcommon.Must(fmt.Errorf("multi dealer/dealer server tail drain poll: %w", waitErr))
			}
			continue
		}
		if !ok {
			if !time.Now().Before(idleDeadline) {
				return
			}
			continue
		}
		idleDeadline = time.Now().Add(idleWait)
		_ = received.Close()
	}
}

func runMultiDealerDealerClient(cfg multiConfig, endpoint string) {
	clients := make([]dealerDealerClient, 0, cfg.clients)
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
		clients = append(clients, dealerDealerClient{ctx: clientCtx, socket: client, mon: clientMon})
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
	runMultiDealerDealerSendWindow(clients, cfg, window)
	if len(clients) > 0 {
		sendMultiDealerStopToken(clients[0].socket)
	}
	flushControlLine("CLIENT_DONE,%d", cfg.msgSize)
}

func runMultiDealerDealerSendWindow(clients []dealerDealerClient, cfg multiConfig, window perfcommon.BenchmarkWindow) {
	if len(clients) == 0 {
		return
	}
	poller, err := zlink.NewPoller()
	perfcommon.Must(err)
	defer poller.Close()
	events := make([]zlink.PollEvent, len(clients))
	pending := make([]bool, len(clients))
	for i, client := range clients {
		perfcommon.Must(poller.AddSocket(client.socket, 0, uintptr(i)))
	}
	for time.Now().Before(window.StopAt) {
		pendingCount := 0
		for i, client := range clients {
			if pending[i] {
				pendingCount++
				continue
			}
			for time.Now().Before(window.StopAt) {
				sent, sendErr := perfcommon.SubmitWindowPayload(cfg.msgSize, window.ActiveAt, func(message *zlink.Message) (bool, error) {
					return client.socket.Send().Message(message).Flags(zlink.SendFlagsDontWait).Submit(nil)
				})
				if sendErr == nil && sent {
					continue
				}
				if sendErr != nil && !perfcommon.IsTransient(sendErr) {
					perfcommon.Must(fmt.Errorf("multi dealer/dealer client send: %w", sendErr))
				}
				pending[i] = true
				perfcommon.Must(poller.ModifySocket(client.socket, perfcommon.ZLinkPollOut))
				pendingCount++
				break
			}
		}
		if !time.Now().Before(window.StopAt) || pendingCount == 0 {
			continue
		}
		n, waitErr := poller.Wait(events, -1*time.Millisecond)
		if waitErr != nil {
			if perfcommon.IsTransient(waitErr) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi dealer/dealer client poll: %w", waitErr))
		}
		for i := 0; i < n; i++ {
			if events[i].Revents&perfcommon.ZLinkPollOut == 0 {
				continue
			}
			idx := int(events[i].Slot)
			if idx >= 0 && idx < len(pending) {
				pending[idx] = false
				perfcommon.Must(poller.ModifySocket(clients[idx].socket, 0))
			}
		}
	}
}

// sendMultiDealerStopToken pushes the wire-level stop token through the
// dealer socket. Bounded attempts through transient backpressure mirror
// the cpp / java / dotnet implementations.
func sendMultiDealerStopToken(socket *zlink.DealerSocket) {
	for attempt := 0; attempt < perfcommon.StopTokenSendAttempts; attempt++ {
		sent, err := perfcommon.SubmitPayload(perfcommon.StopToken, func(message *zlink.Message) (bool, error) {
			return socket.Send().Message(message).Submit(nil)
		})
		if err == nil && sent {
			return
		}
		if err != nil && !perfcommon.IsTransient(err) {
			return
		}
		time.Sleep(perfcommon.StopTokenSendBackoff)
	}
}
