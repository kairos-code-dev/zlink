package main

import (
	"runtime"
	"sync"
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
	stopDealerEchoServer := startDealerEchoServer(server)
	defer stopDealerEchoServer()

	stats := perfcommon.NewStats()
	payload := perfcommon.PreparePayload(cfg.msgSize)
	window := perfcommon.NewBenchmarkWindow(0, cfg.duration)

	for time.Now().Before(window.StopAt) {
		perfcommon.StampPayload(payload)
		err := client.Send(zlink.SendFlagsNone, perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		reply, err := client.Recv(zlink.RecvFlagsNone)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		part, err := reply.SinglePartOrError()
		perfcommon.Must(err)
		runtime.KeepAlive(reply)
		perfcommon.RecordMessageLatency(stats, window.ActiveAt, part)
		perfcommon.Must(reply.Close())
		runtime.KeepAlive(part)
		runtime.KeepAlive(reply)
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func startDealerEchoServer(server *zlink.DealerSocket) func() {
	perfcommon.Must(server.SetRecvTimeout(500 * time.Millisecond))
	stop := make(chan struct{})
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		for {
			select {
			case <-stop:
				return
			default:
			}
			received, err := server.Recv(zlink.RecvFlagsNone)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				return
			}
			err = server.Send(zlink.SendFlagsNone, perfcommon.CloneMessages(received.Parts())...)
			if err != nil && !perfcommon.IsTransient(err) {
				perfcommon.Must(err)
			}
			perfcommon.Must(received.Close())
			runtime.KeepAlive(received)
		}
	}()
	return func() {
		close(stop)
		wg.Wait()
	}
}
