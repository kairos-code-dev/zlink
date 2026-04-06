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

	serverID, err := zlink.NewRoutingID([]byte("SERVER"))
	perfcommon.Must(err)
	endpoint := perfcommon.UniqueTCPEndpoint("perf-multi-router-router")
	perfcommon.Must(server.SetRoutingID(serverID))
	perfcommon.Must(server.Bind(endpoint))
	startMultiRouterEchoServer(server)

	stats := perfcommon.NewStats()
	stopAt := time.Now().Add(cfg.warmup + cfg.duration)
	activeAt := time.Now().Add(cfg.warmup)

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

		clientID, err := zlink.NewRoutingID([]byte(fmt.Sprintf("router-%06d", i)))
		perfcommon.Must(err)
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
			for time.Now().Before(stopAt) {
				perfcommon.StampPayload(payload)
				err := socket.SendTo(serverID, perfcommon.NewMessage(payload))
				if err != nil {
					if perfcommon.IsTransient(err) {
						continue
					}
					perfcommon.Must(fmt.Errorf("multi router/router send: %w", err))
				}
				reply, ok, err := socket.TryRecv()
				if err != nil {
					perfcommon.Must(fmt.Errorf("multi router/router recv: %w", err))
				}
				if !ok || reply == nil {
					continue
				}
				part, err := reply.SinglePartOrError()
				if err == nil {
					if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
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
		perfcommon.StampPayload(payload)
		err := client.SendTo(serverID, perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi router/router ready send: %w", err))
		}
		reply, ok, err := client.TryRecv()
		if err != nil {
			perfcommon.Must(fmt.Errorf("multi router/router ready recv: %w", err))
		}
		if !ok || reply == nil {
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
			received, ok, err := server.TryRecv()
			if err != nil {
				perfcommon.Must(fmt.Errorf("multi router/router server recv: %w", err))
			}
			if !ok || received == nil {
				continue
			}
			err = server.SendTo(received.RoutingID(),
				perfcommon.CloneMessages(received.Parts())...)
			if err != nil && !perfcommon.IsTransient(err) {
				perfcommon.Must(fmt.Errorf("multi router/router server send: %w", err))
			}
			perfcommon.Must(received.Close())
		}
	}()
}
