package main

import (
	"fmt"
	"sync"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
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

	stopServer := make(chan struct{})
	serverDone := make(chan struct{})
	go startMultiRouterRouterEchoServer(server, stopServer, serverDone)
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
	close(stopServer)
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

		event, err := poller.Wait(25 * time.Millisecond)
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
		perfcommon.Must(client.socket.SendTo(
			serverID,
			zlink.SendFlagsNone,
			perfcommon.NewMessage(payload),
		))

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

func startMultiRouterRouterEchoServer(server *zlink.RouterSocket, stop <-chan struct{}, done chan<- struct{}) {
	defer close(done)

	poller := perfcommon.NewSocketPoller(server, perfcommon.ZLinkPollIn)
	defer poller.Close()

	pending := make([]pendingRouterReply, 0, 8)
	stopping := false

	for {
		select {
		case <-stop:
			stopping = true
		default:
		}
		if stopping {
			return
		}

		events := perfcommon.ZLinkPollIn
		if len(pending) > 0 {
			events |= perfcommon.ZLinkPollOut
		}
		perfcommon.Must(poller.ModifySocket(server, events))

		event, err := poller.Wait(25 * time.Millisecond)
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
		for {
			received, recvErr := server.Recv(zlink.RecvFlagsDontWait)
			if recvErr != nil {
				if perfcommon.IsTransient(recvErr) {
					break
				}
				perfcommon.Must(fmt.Errorf("multi router/router server recv: %w", recvErr))
			}
			if received == nil {
				break
			}

			part, partErr := received.SinglePartOrError()
			if partErr == nil {
				if len(pending) == 0 {
					sent, sendErr := tryRouterSend(server, received.RoutingID(), part.Data())
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

func startMultiRouterEchoServer(server *zlink.RouterSocket) (chan struct{}, chan struct{}) {
	stop := make(chan struct{})
	done := make(chan struct{})
	go startMultiRouterRouterEchoServer(server, stop, done)
	return stop, done
}

func drainRouterReplies(
	socket *zlink.RouterSocket,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
) (bool, error) {
	drained := false
	for {
		reply, err := socket.Recv(zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				return drained, nil
			}
			return false, err
		}
		if reply == nil {
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
	err := socket.SendTo(target, zlink.SendFlagsDontWait, perfcommon.NewMessage(payload))
	if err == nil {
		return true, nil
	}
	if perfcommon.IsTransient(err) {
		return false, nil
	}
	return false, err
}
