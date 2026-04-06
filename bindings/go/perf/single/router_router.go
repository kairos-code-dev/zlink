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
	window := perfcommon.NewBenchmarkWindow(cfg.warmup, cfg.duration)

	for time.Now().Before(window.StopAt) {
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
		perfcommon.RecordMessageLatency(stats, window.ActiveAt, part)
		perfcommon.Must(reply.Close())
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitForRouterRouterReady(client *zlink.RouterSocket, serverID zlink.RoutingID) {
	payload := perfcommon.PreparePayload(64)
	perfcommon.Must(perfcommon.WaitReady(perfcommon.ReadyConfig{
		Name: "router/router perf endpoint",
		Probe: func() (bool, error) {
			perfcommon.StampPayload(payload)
			err := client.SendTo(serverID, perfcommon.NewMessage(payload))
			if err != nil {
				if perfcommon.IsTransient(err) {
					return false, nil
				}
				return false, err
			}
			reply, err := client.Recv()
			if err != nil {
				if perfcommon.IsTransient(err) {
					return false, nil
				}
				return false, err
			}
			return true, reply.Close()
		},
	}))
}

func startRouterRouterEchoServer(server *zlink.RouterSocket) {
	perfcommon.Must(server.OnReceive(func(received *zlink.Received) {
		defer received.Close()
		perfcommon.Must(server.SendTo(received.RoutingID(),
			perfcommon.CloneMessages(received.Parts())...))
	}))
}
