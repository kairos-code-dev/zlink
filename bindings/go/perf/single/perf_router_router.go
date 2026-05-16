package main

import (
	"fmt"
	"os"
	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

func runRouterRouter(cfg benchmarkConfig) perfcommon.Result {
	debug := os.Getenv("PERF_DEBUG") != ""
	debugf := func(format string, args ...any) {
		if debug {
			fmt.Fprintf(os.Stderr, format+"\n", args...)
		}
	}

	ctx, err := perfcommon.NewSingleContext()
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

	debugf("router/router tls setup server")
	perfcommon.Must(perfcommon.ConfigureTLSServer(server, cfg.transport))
	debugf("router/router tls setup client")
	perfcommon.Must(perfcommon.ConfigureTLSClient(client, cfg.transport))
	// perf_router_router.cpp: apply_single_auto_hwm_msg_unit on the raw
	// sockets, then apply_single_hwm (override-gated).
	perfcommon.ApplySingleAutoHWMMsgUnit(server, cfg.msgSize)
	perfcommon.ApplySingleAutoHWMMsgUnit(client, cfg.msgSize)
	perfcommon.ApplySingleHWM(server)
	perfcommon.ApplySingleHWM(client)
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-router-router")
	debugf("router/router set server routing id")
	perfcommon.Must(server.SetRoutingID(serverID))
	debugf("router/router set client routing id")
	perfcommon.Must(client.SetRoutingID(clientID))
	debugf("router/router set connect routing id")
	perfcommon.Must(client.SetConnectRoutingID(serverID))
	debugf("router/router connect %s", endpoint)
	perfcommon.Must(client.Connect(endpoint))
	debugf("router/router apply benchmark socket options")
	perfcommon.ApplySingleBenchmarkSocketOptions(server, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(client, cfg.transport)
	debugf("router/router wait connected")
	perfcommon.WaitConnectedWithTimeout(perfcommon.SingleReadyTimeout(), serverMon, clientMon)
	debugf("router/router wait route ready")
	waitSingleRouteReady("router/router perf endpoint", func(payload []byte) error {
		_, err := client.SendTo(serverID).Message(perfcommon.NewMessage(payload)).Submit(nil)
		return err
	}, server)

	return runSingleOneWay(cfg, server, func(payload []byte) error {
		_, err := client.SendTo(serverID).Message(perfcommon.NewMessage(payload)).Submit(nil)
		return err
	})
}
