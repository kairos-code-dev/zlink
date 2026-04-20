package main

import (
	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runPair(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	server, err := ctx.PairSocket()
	perfcommon.Must(err)
	defer server.Close()
	client, err := ctx.PairSocket()
	perfcommon.Must(err)
	defer client.Close()
	serverMon := perfcommon.OpenMonitor(server)
	defer serverMon.Close()
	clientMon := perfcommon.OpenMonitor(client)
	defer clientMon.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(server, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(client, cfg.transport))
	perfcommon.ApplySingleHWM(server)
	perfcommon.ApplySingleHWM(client)
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-pair")
	perfcommon.Must(client.Connect(endpoint))
	perfcommon.ApplySingleBenchmarkSocketOptions(server, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(client, cfg.transport)
	perfcommon.WaitConnectedWithTimeout(perfcommon.SingleReadyTimeout(), serverMon, clientMon)

	return runSingleOneWay(cfg, server, func(payload []byte) error {
		return client.Send(zlink.SendFlagsNone, perfcommon.NewMessage(payload))
	})
}
