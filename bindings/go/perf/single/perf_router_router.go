package main

import (
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

	serverID := zlink.NewRoutingID([]byte("ROUTER1"))
	clientID := zlink.NewRoutingID([]byte("ROUTER2"))

	perfcommon.Must(perfcommon.ConfigureTLSServer(server, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(client, cfg.transport))
	perfcommon.ApplySingleHWM(server)
	perfcommon.ApplySingleHWM(client)
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-router-router")
	perfcommon.Must(server.SetRoutingID(serverID))
	perfcommon.Must(client.SetRoutingID(clientID))
	perfcommon.Must(client.SetConnectRoutingID(serverID))
	perfcommon.Must(client.Connect(endpoint))
	perfcommon.ApplySingleBenchmarkSocketOptions(server, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(client, cfg.transport)
	perfcommon.WaitConnected(serverMon, clientMon)
	perfcommon.Must(server.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	perfcommon.Must(client.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	perfcommon.Must(client.SetSendTimeout(perfcommon.BenchmarkSocketTimeout))
	waitSingleRouteReady("router/router perf endpoint", func(payload []byte) error {
		return client.SendTo(serverID, zlink.SendFlagsNone, perfcommon.NewMessage(payload))
	}, server)

	return runSingleOneWay(cfg, server, func(payload []byte) error {
		return client.SendTo(serverID, zlink.SendFlagsNone, perfcommon.NewMessage(payload))
	})
}
