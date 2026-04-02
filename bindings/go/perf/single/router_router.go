package main

import (
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runRouterRouter(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	server, err := ctx.RouterSocket()
	perfcommon.Must(err)
	defer server.Close()
	client, err := ctx.RouterSocket()
	perfcommon.Must(err)
	defer client.Close()
	serverMon := perfcommon.OpenMonitor(server)
	defer serverMon.Close()
	clientMon := perfcommon.OpenMonitor(client)
	defer clientMon.Close()

	serverID, err := zlink.NewRoutingID([]byte("ROUTER1"))
	perfcommon.Must(err)
	clientID, err := zlink.NewRoutingID([]byte("ROUTER2"))
	perfcommon.Must(err)

	endpoint := perfcommon.UniqueTCPEndpoint("perf-router-router")
	perfcommon.Must(server.SetRoutingID(serverID))
	perfcommon.Must(client.SetRoutingID(clientID))
	perfcommon.Must(client.SetConnectRoutingID(serverID))
	perfcommon.Must(server.Bind(endpoint))
	perfcommon.Must(client.Connect(endpoint))
	perfcommon.WaitConnected(serverMon, clientMon)
	perfcommon.Must(client.SetRecvTimeout(500 * time.Millisecond))
	perfcommon.Must(client.SetSendTimeout(500 * time.Millisecond))
	startRouterRouterEchoServer(server)
	waitForRouterRouterReady(client, serverID)

	stats := perfcommon.NewStats()
	payload := perfcommon.PreparePayload(cfg.msgSize)
	stopAt := time.Now().Add(cfg.warmup + cfg.duration)
	activeAt := time.Now().Add(cfg.warmup)

	for time.Now().Before(stopAt) {
		perfcommon.StampPayload(payload)
		err := client.SendTo(serverID, perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		reply, err := client.Recv()
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		part, err := reply.SinglePartOrError()
		perfcommon.Must(err)
		if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
			stats.Add(sentAt)
		}
		perfcommon.Must(reply.Close())
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitForRouterRouterReady(client *zlink.RouterSocket, serverID zlink.RoutingID) {
	payload := perfcommon.PreparePayload(64)
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		perfcommon.StampPayload(payload)
		err := client.SendTo(serverID, perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		reply, err := client.Recv()
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		perfcommon.Must(reply.Close())
		return
	}
	perfcommon.Must(&routerRouterReadyError{})
}

type routerRouterReadyError struct{}

func (e *routerRouterReadyError) Error() string {
	return "router/router perf endpoint did not become ready"
}

func startRouterRouterEchoServer(server *zlink.RouterSocket) {
	perfcommon.Must(server.OnReceive(func(received *zlink.Received) {
		defer received.Close()
		perfcommon.Must(server.SendTo(received.RoutingID(),
			perfcommon.CloneMessages(received.Parts())...))
	}))
}
