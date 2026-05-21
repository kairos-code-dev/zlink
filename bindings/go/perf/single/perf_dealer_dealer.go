package main

import (
	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

func runDealerDealer(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := perfcommon.NewSingleContext()
	perfcommon.Must(err)
	defer ctx.Close()
	perfcommon.ApplySingleAutoHWMMsgUnit(ctx, cfg.msgSize)

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
	perfcommon.ApplySingleHWM(server)
	perfcommon.ApplySingleHWM(client)
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-dealer-dealer")
	perfcommon.Must(client.Connect(endpoint))
	perfcommon.ApplySingleBenchmarkSocketOptions(server, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(client, cfg.transport)
	perfcommon.WaitConnectedWithTimeout(perfcommon.SingleReadyTimeout(), serverMon, clientMon)

	result := runSingleOneWay(cfg, server, func(message *zlink.Message) (bool, error) {
		return client.Send().Message(message).Flags(zlink.SendFlagsDontWait).Submit(nil)
	}, func(message *zlink.Message) error {
		_, err := client.Send().Message(message).Submit(nil)
		return err
	})
	perfcommon.PrintSingleAutoHWMDetail(serverMon, cfg.pattern, cfg.transport, "receiver", zlink.SocketTypeDealer, cfg.msgSize)
	perfcommon.PrintSingleAutoHWMDetail(clientMon, cfg.pattern, cfg.transport, "sender", zlink.SocketTypeDealer, cfg.msgSize)
	return result
}
