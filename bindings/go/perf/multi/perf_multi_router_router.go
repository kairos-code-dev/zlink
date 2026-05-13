package main

import (
	"fmt"
	"sync"
	"time"

	"zlink.systems/zlink"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

type multiRouterClient struct {
	ctx     *zlink.Context
	socket  *zlink.RouterSocket
	monitor *zlink.SocketMonitor
}

type pendingRouterReply struct {
	routingID zlink.RoutingID
	payload   []byte
}

func runMultiRouterRouter(cfg multiConfig) perfcommon.Result {
	serverCtx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer serverCtx.Close()

	server, err := serverCtx.RouterSocket()
	perfcommon.Must(err)
	defer server.Close()
	serverMon := perfcommon.OpenMonitor(server)
	defer serverMon.Close()

	serverID := zlink.NewRoutingID([]byte("SERVER"))
	perfcommon.Must(perfcommon.ConfigureTLSServer(server, cfg.transport))
	perfcommon.ApplyMultiHWM(server, cfg.pattern)
	perfcommon.ApplyMultiBenchmarkSocketOptions(server, cfg.transport)
	perfcommon.Must(server.SetRoutingID(serverID))
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-multi-router-router")

	stats := perfcommon.NewStats()
	clients := make([]multiRouterClient, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		clientCtx, createErr := zlink.NewContext()
		perfcommon.Must(createErr)
		client, socketErr := clientCtx.RouterSocket()
		perfcommon.Must(socketErr)
		clientMon := perfcommon.OpenMonitor(client)
		perfcommon.Must(perfcommon.ConfigureTLSClient(client, cfg.transport))
		perfcommon.ApplyMultiHWM(client, cfg.pattern)
		perfcommon.ApplyMultiBenchmarkSocketOptions(client, cfg.transport)

		clientID := zlink.NewRoutingID([]byte(fmt.Sprintf("router-%06d", i)))
		perfcommon.Must(client.SetRoutingID(clientID))
		perfcommon.Must(client.SetConnectRoutingID(serverID))
		if err := client.Connect(endpoint); err != nil {
			perfcommon.Must(fmt.Errorf("multi router/router connect client[%d]: %w", i, err))
		}
		perfcommon.WaitConnectedWithTimeout(perfcommon.MultiReadyTimeout(), serverMon, clientMon)

		clients = append(clients, multiRouterClient{
			ctx:     clientCtx,
			socket:  client,
			monitor: clientMon,
		})
	}
	defer func() {
		for _, client := range clients {
			_ = client.monitor.Close()
			_ = client.socket.Close()
			_ = client.ctx.Close()
		}
	}()

	serverDone := make(chan struct{})
	go startMultiRouterRouterEchoServer(server, serverDone)
	validateMultiRouterRoutes(serverID, clients, cfg.msgSize)
	window := perfcommon.NewBenchmarkWindow(cfg.duration)

	var wg sync.WaitGroup
	for _, client := range clients {
		wg.Add(1)
		go func(socket *zlink.RouterSocket) {
			defer wg.Done()
			runMultiRouterClient(socket, serverID, cfg, window, stats)
		}(client.socket)
	}

	wg.Wait()
	// PERF_MULTI_TEST_POLICY § 1.3.1: signal phase end via the
	// wire-level stop token. The first stop token received by the
	// server triggers shutdown; subsequent ones are ignored on the
	// reply path.
	if len(clients) > 0 {
		sendMultiRouterStopToken(clients[0].socket, serverID)
	}
	<-serverDone
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func runMultiRouterClient(
	socket *zlink.RouterSocket,
	serverID zlink.RoutingID,
	cfg multiConfig,
	window perfcommon.BenchmarkWindow,
	stats *perfcommon.Stats,
) {
	payload := perfcommon.PreparePayload(cfg.msgSize)
	poller := perfcommon.NewSocketPoller(socket, perfcommon.ZLinkPollIn|perfcommon.ZLinkPollOut)
	defer poller.Close()

	waitingReply := false
	sendPending := false

	for time.Now().Before(window.StopAt) {
		progressed := false
		if !waitingReply && !sendPending {
			perfcommon.StampWindowPayload(payload, window.ActiveAt)
			sent, err := tryRouterSend(socket, serverID, payload)
			if err != nil {
				perfcommon.Must(fmt.Errorf("multi router/router send: %w", err))
			}
			if sent {
				waitingReply = true
				progressed = true
			} else {
				sendPending = true
			}
		}

		drained, err := drainRouterReplies(socket, stats, cfg.msgSize, window.ActiveAt)
		if err != nil {
			perfcommon.Must(fmt.Errorf("multi router/router recv: %w", err))
		}
		if drained {
			waitingReply = false
			progressed = true
		}
		if progressed {
			continue
		}

		// PERF_MULTI_TEST_POLICY § 1.3.1: signal-driven wait. The
		// remaining wall-time bounds the wait so a stalled echo loop
		// at end-of-phase still releases the goroutine; under steady
		// state the timeout is large and the wakeup comes from inbound
		// readiness.
		remaining := time.Until(window.StopAt)
		if remaining <= 0 {
			break
		}
		event, err := poller.Wait(remaining)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi router/router poll: %w", err))
		}
		if event == nil {
			continue
		}
		if event.Events&perfcommon.ZLinkPollOut != 0 {
			sendPending = false
		}
		if event.Events&perfcommon.ZLinkPollIn != 0 {
			drained, err := drainRouterReplies(socket, stats, cfg.msgSize, window.ActiveAt)
			if err != nil {
				perfcommon.Must(fmt.Errorf("multi router/router recv: %w", err))
			}
			if drained {
				waitingReply = false
			}
		}
	}
}

func validateMultiRouterRoutes(serverID zlink.RoutingID, clients []multiRouterClient, msgSize int) {
	for index, client := range clients {
		payload := perfcommon.PreparePayload(msgSize)
		perfcommon.StampProbePayload(payload)
		_, sendErr := client.socket.SendTo(serverID).Message(perfcommon.NewMessage(payload)).Submit(nil)
		perfcommon.Must(sendErr)

		poller := perfcommon.NewSocketPoller(client.socket, perfcommon.ZLinkPollIn)
		deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
		validated := false
		for time.Now().Before(deadline) {
			event, err := poller.Wait(time.Until(deadline))
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(fmt.Errorf("multi router/router route probe[%d] poll: %w", index, err))
			}
			if event == nil || event.Events&perfcommon.ZLinkPollIn == 0 {
				continue
			}
			drained, err := drainRouterReplies(client.socket, nil, msgSize, time.Time{})
			if err != nil {
				perfcommon.Must(fmt.Errorf("multi router/router route probe[%d] recv: %w", index, err))
			}
			if drained {
				validated = true
				break
			}
		}
		_ = poller.Close()
		if !validated {
			perfcommon.Must(fmt.Errorf("multi router/router route probe[%d] timed out", index))
		}
	}
}

// startMultiRouterRouterEchoServer runs the echo loop until it receives
// a wire-level stop token from any client. Closes done to notify the
// main goroutine. PERF_MULTI_TEST_POLICY § 1.3.1: poller waits with -1
// (signal-driven) and the loop exits on stop token, not on a stop
// channel.
func startMultiRouterRouterEchoServer(server *zlink.RouterSocket, done chan<- struct{}) {
	defer close(done)

	poller := perfcommon.NewSocketPoller(server, perfcommon.ZLinkPollIn)
	defer poller.Close()

	pending := make([]pendingRouterReply, 0, 8)
	stopRequested := false

	for !stopRequested {
		events := perfcommon.ZLinkPollIn
		if len(pending) > 0 {
			events |= perfcommon.ZLinkPollOut
		}
		perfcommon.Must(poller.ModifySocket(server, events))

		event, err := poller.Wait(-1 * time.Millisecond)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi router/router server poll: %w", err))
		}
		if event == nil {
			continue
		}

		if event.Events&perfcommon.ZLinkPollOut != 0 {
			for len(pending) > 0 {
				sent, sendErr := tryRouterSend(server, pending[0].routingID, pending[0].payload)
				if sendErr != nil {
					perfcommon.Must(fmt.Errorf("multi router/router server send: %w", sendErr))
				}
				if !sent {
					break
				}
				pending = pending[1:]
			}
		}

		if event.Events&perfcommon.ZLinkPollIn == 0 {
			continue
		}
		var received zlink.Received
		for {
			ok, recvErr := server.Recv(&received, zlink.RecvFlagsDontWait)
			if recvErr != nil {
				if perfcommon.IsTransient(recvErr) {
					break
				}
				perfcommon.Must(fmt.Errorf("multi router/router server recv: %w", recvErr))
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
				if len(pending) == 0 {
					sent, sendErr := received.Send().Message(perfcommon.NewMessage(part.Data())).Flags(zlink.SendFlagsDontWait).Submit(nil)
					if sendErr != nil {
						_ = received.Close()
						perfcommon.Must(fmt.Errorf("multi router/router server send: %w", sendErr))
					}
					if sent {
						_ = received.Close()
						continue
					}
				}
				pending = append(pending, pendingRouterReply{
					routingID: received.RoutingID(),
					payload:   append([]byte(nil), part.Data()...),
				})
			}
			_ = received.Close()
		}
	}
}

// startMultiRouterEchoServer returns a `done` channel closed when the
// echo loop exits after observing a wire-level stop token. The legacy
// callers expected a (stop, done) pair where `stop` was closed by the
// caller to request shutdown; PERF_MULTI_TEST_POLICY § 1.3.1 routes
// shutdown over the wire instead, so the returned `stop` channel is now
// a no-op handle that never causes the loop to exit on its own — the
// caller still receives ownership of the channel and may close it for
// API symmetry.
func startMultiRouterEchoServer(server *zlink.RouterSocket) (chan struct{}, chan struct{}) {
	stop := make(chan struct{})
	done := make(chan struct{})
	go startMultiRouterRouterEchoServer(server, done)
	return stop, done
}

// sendMultiRouterStopToken pushes the wire-level stop token through the
// supplied router socket addressed to the server. Bounded retry through
// transient backpressure.
func sendMultiRouterStopToken(socket *zlink.RouterSocket, serverID zlink.RoutingID) {
	for retry := 0; retry < perfcommon.StopTokenSendRetries; retry++ {
		sent, err := socket.SendTo(serverID).Message(perfcommon.NewMessage(perfcommon.StopToken)).Submit(nil)
		if err == nil && sent {
			return
		}
		if err != nil && !perfcommon.IsTransient(err) {
			return
		}
		time.Sleep(perfcommon.StopTokenSendBackoff)
	}
}

func drainRouterReplies(
	socket *zlink.RouterSocket,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
) (bool, error) {
	drained := false
	var reply zlink.Received
	for {
		ok, err := socket.Recv(&reply, zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				return drained, nil
			}
			return false, err
		}
		if !ok {
			return drained, nil
		}
		part, partErr := reply.SinglePartOrError()
		if partErr == nil && stats != nil {
			perfcommon.RecordMessageRTTLatency(stats, activeAt, msgSize, part)
		}
		drained = true
		_ = reply.Close()
	}
}

func tryRouterSend(socket *zlink.RouterSocket, target zlink.RoutingID, payload []byte) (bool, error) {
	return socket.SendTo(target).Message(perfcommon.NewMessage(payload)).Flags(zlink.SendFlagsDontWait).Submit(nil)
}
