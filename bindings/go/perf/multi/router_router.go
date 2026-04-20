package main

import (
	"fmt"
	"sync"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runMultiRouterRouter(cfg multiConfig) perfcommon.Result {
	serverCtx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer serverCtx.Close()

	server, err := serverCtx.RouterSocket()
	perfcommon.Must(err)
	defer server.Close()

	serverID := zlink.NewRoutingID([]byte("SERVER"))
	perfcommon.Must(perfcommon.ConfigureTLSServer(server, cfg.transport))
	perfcommon.Must(server.SetRoutingID(serverID))
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-multi-router-router")
	startMultiRouterEchoServer(server)

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(cfg.warmup, cfg.duration)

	type routerClient struct {
		ctx     *zlink.Context
		socket  *zlink.RouterSocket
		monitor *zlink.SocketMonitor
	}
	clients := make([]routerClient, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		clientCtx, err := zlink.NewContext()
		perfcommon.Must(err)
		client, err := clientCtx.RouterSocket()
		perfcommon.Must(err)
		clientMon := perfcommon.OpenMonitor(client)
		perfcommon.Must(perfcommon.ConfigureTLSClient(client, cfg.transport))

		clientID := zlink.NewRoutingID([]byte(fmt.Sprintf("router-%06d", i)))
		perfcommon.Must(client.SetRoutingID(clientID))
		perfcommon.Must(client.SetConnectRoutingID(serverID))
		if err := client.Connect(endpoint); err != nil {
			perfcommon.Must(fmt.Errorf("multi router/router connect client[%d]: %w", i, err))
		}
		perfcommon.WaitMonitorEvent(clientMon)
		if err := client.SetRecvTimeout(500 * time.Millisecond); err != nil {
			perfcommon.Must(fmt.Errorf("multi router/router set recv timeout client[%d]: %w", i, err))
		}
		if err := client.SetSendTimeout(500 * time.Millisecond); err != nil {
			perfcommon.Must(fmt.Errorf("multi router/router set send timeout client[%d]: %w", i, err))
		}
		waitForRouterClientReady(client, serverID)

		clients = append(clients, routerClient{
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

	var wg sync.WaitGroup
	for _, client := range clients {
		wg.Add(1)
		go func(socket *zlink.RouterSocket) {
			defer wg.Done()

			payload := perfcommon.PreparePayload(cfg.msgSize)
			for time.Now().Before(window.StopAt) {
				perfcommon.StampWindowPayload(payload, window.ActiveAt)
				err := socket.SendTo(serverID, zlink.SendFlagsNone, perfcommon.NewMessage(payload))
				if err != nil {
					if perfcommon.IsTransient(err) {
						continue
					}
					perfcommon.Must(fmt.Errorf("multi router/router send: %w", err))
				}
				reply, err := socket.Recv(zlink.RecvFlagsDontWait)
				if err != nil {
					if perfcommon.IsTransient(err) {
						continue
					}
					perfcommon.Must(fmt.Errorf("multi router/router recv: %w", err))
				}
				if reply == nil {
					continue
				}
				part, err := reply.SinglePartOrError()
				if err == nil {
					if sentAt, ok := perfcommon.SentAtFromMessage(part, cfg.msgSize); ok && time.Now().After(window.ActiveAt) {
						stats.Add(sentAt)
					}
				}
				_ = reply.Close()
			}
		}(client.socket)
	}

	wg.Wait()
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitForRouterClientReady(client *zlink.RouterSocket, serverID zlink.RoutingID) {
	payload := perfcommon.PreparePayload(64)
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		perfcommon.StampProbePayload(payload)
		err := client.SendTo(serverID, zlink.SendFlagsNone, perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi router/router ready send: %w", err))
		}
		reply, err := client.Recv(zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi router/router ready recv: %w", err))
		}
		if reply == nil {
			continue
		}
		perfcommon.Must(reply.Close())
		return
	}
	perfcommon.Must(&multiRouterRouterReadyError{})
}

type multiRouterRouterReadyError struct{}

func (e *multiRouterRouterReadyError) Error() string {
	return "multi router/router perf endpoint did not become ready"
}

func startMultiRouterEchoServer(server *zlink.RouterSocket) {
	perfcommon.Must(server.SetRecvTimeout(500 * time.Millisecond))
	go func() {
		for {
			received, err := server.Recv(zlink.RecvFlagsDontWait)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(fmt.Errorf("multi router/router server recv: %w", err))
			}
			if received == nil {
				continue
			}
			err = server.SendTo(received.RoutingID(), zlink.SendFlagsNone,
				perfcommon.CloneMessages(received.Parts())...)
			if err != nil && !perfcommon.IsTransient(err) {
				perfcommon.Must(fmt.Errorf("multi router/router server send: %w", err))
			}
			perfcommon.Must(received.Close())
		}
	}()
}
