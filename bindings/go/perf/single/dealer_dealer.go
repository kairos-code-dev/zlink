package main

import (
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runDealerDealer(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
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
	perfcommon.ApplySingleHWM(server)
	perfcommon.ApplySingleHWM(client)
	endpoint := perfcommon.BindAndResolveEndpoint(server, cfg.transport, "perf-dealer-dealer")
	perfcommon.Must(client.Connect(endpoint))
	perfcommon.ApplySingleBenchmarkSocketOptions(server, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(client, cfg.transport)
	perfcommon.WaitConnected(serverMon, clientMon)
	perfcommon.Must(client.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	perfcommon.Must(client.SetSendTimeout(perfcommon.BenchmarkSocketTimeout))
	startDealerEchoServer(server)

	stats := perfcommon.NewStats()
	payload := perfcommon.PreparePayload(cfg.msgSize)
	window := perfcommon.NewBenchmarkWindow(0, cfg.duration)

	for time.Now().Before(window.StopAt) {
		perfcommon.StampPayload(payload)
		err := client.Send(perfcommon.NewMessage(payload))
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

func startDealerEchoServer(server *zlink.DealerSocket) {
	perfcommon.Must(server.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	go func() {
		for {
			received, err := server.Recv()
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				return
			}
			err = server.Send(perfcommon.CloneMessages(received.Parts())...)
			if err != nil && !perfcommon.IsTransient(err) {
				perfcommon.Must(err)
			}
		}
	}()
}
