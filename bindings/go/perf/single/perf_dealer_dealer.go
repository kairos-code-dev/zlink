package main

import "zlink.systems/zlink/perf/internal/perfcommon"

func runDealerDealer(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := perfcommon.NewSingleContext()
	perfcommon.Must(err)
	defer ctx.Close()

	server, err := ctx.DealerSocket()
	perfcommon.Must(err)
	defer server.Close()
	client, err := ctx.DealerSocket()
	perfcommon.Must(err)
	defer client.Close()
	serverMon := perfcommon.OpenMonitor(server)
	defer serverMon.Close()
	clientMon := perfcommon.OpenMonitor(client)
	defer clientMon.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(server, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(client, cfg.transport))
	// perf_dealer_dealer.cpp: apply_single_auto_hwm_msg_unit on the raw
	// sockets, then apply_single_hwm (override-gated).
	perfcommon.ApplySingleAutoHWMMsgUnit(server, cfg.msgSize)
	perfcommon.ApplySingleAutoHWMMsgUnit(client, cfg.msgSize)
	perfcommon.ApplySingleHWM(server)
	perfcommon.ApplySingleHWM(client)
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-dealer-dealer")
	perfcommon.Must(client.Connect(endpoint))
	perfcommon.ApplySingleBenchmarkSocketOptions(server, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(client, cfg.transport)
	perfcommon.WaitConnectedWithTimeout(perfcommon.SingleReadyTimeout(), serverMon, clientMon)

	return runSingleOneWay(cfg, server, func(payload []byte) error {
		_, err := client.Send().Message(perfcommon.NewMessage(payload)).Submit(nil)
		return err
	})
}
