package main

import (
	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runDealerRouter(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	router, err := ctx.RouterSocket()
	perfcommon.Must(err)
	defer router.Close()
	dealer, err := ctx.DealerSocket()
	perfcommon.Must(err)
	defer dealer.Close()

	rid := zlink.NewRoutingID([]byte("perf-dealer"))

	perfcommon.Must(perfcommon.ConfigureTLSServer(router, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(dealer, cfg.transport))
	perfcommon.ApplySingleHWM(router)
	perfcommon.ApplySingleHWM(dealer)
	endpoint := perfcommon.BindAndResolveEndpoint(router, cfg.transport, "perf-dealer-router")
	perfcommon.Must(dealer.SetRoutingID(rid))
	perfcommon.Must(dealer.Connect(endpoint))
	perfcommon.ApplySingleBenchmarkSocketOptions(router, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(dealer, cfg.transport)
	perfcommon.Must(dealer.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	perfcommon.Must(dealer.SetSendTimeout(perfcommon.BenchmarkSocketTimeout))
	waitSingleRouteReady("dealer/router perf endpoint", func(payload []byte) error {
		return dealer.Send(zlink.SendFlagsNone, perfcommon.NewMessage(payload))
	}, router)

	return runSingleOneWay(cfg, router, func(payload []byte) error {
		return dealer.Send(zlink.SendFlagsNone, perfcommon.NewMessage(payload))
	})
}
