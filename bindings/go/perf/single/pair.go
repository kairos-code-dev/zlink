package main

import (
	"fmt"
	"os"
	"runtime"
	"sync"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func isPairLoopTransient(err error) bool {
	return perfcommon.IsTransient(err)
}

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
	perfcommon.Must(server.SetSendTimeout(perfcommon.BenchmarkSocketTimeout))
	perfcommon.Must(server.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	perfcommon.Must(client.SetSendTimeout(perfcommon.BenchmarkSocketTimeout))
	perfcommon.Must(client.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	perfcommon.WaitConnected(serverMon, clientMon)
	stopPairEchoServer := startPairEchoServer(server)
	defer stopPairEchoServer()

	stats := perfcommon.NewStats()
	payload := perfcommon.PreparePayload(cfg.msgSize)
	window := perfcommon.NewBenchmarkWindow(0, cfg.duration)
	usePollingRecv := true

	for time.Now().Before(window.StopAt) {
		perfcommon.StampPayload(payload)
		err := client.Send(zlink.SendFlagsNone, perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.DebugEnabled() {
				fmt.Fprintf(os.Stderr, "pair client send error: %v\n", err)
			}
			if isPairLoopTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		var reply *zlink.Received
		if usePollingRecv {
			reply, err = client.Recv(zlink.RecvFlagsNone)
			if err != nil {
				if perfcommon.DebugEnabled() {
					fmt.Fprintf(os.Stderr, "pair client recv error: %v\n", err)
				}
				if isPairLoopTransient(err) {
					continue
				}
				perfcommon.Must(err)
			}
			if reply == nil {
				continue
			}
		}
		part, err := reply.SinglePartOrError()
		perfcommon.Must(err)
		runtime.KeepAlive(reply)
		perfcommon.RecordMessageLatency(stats, window.ActiveAt, part)
		if err := reply.Close(); err != nil {
			if perfcommon.DebugEnabled() {
				fmt.Fprintf(os.Stderr, "pair client reply close error: %v\n", err)
			}
			perfcommon.Must(err)
		}
		runtime.KeepAlive(part)
		runtime.KeepAlive(reply)
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func startPairEchoServer(server *zlink.PairSocket) func() {
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
				if perfcommon.DebugEnabled() {
					fmt.Fprintf(os.Stderr, "pair server recv error: %v\n", err)
				}
				if isPairLoopTransient(err) {
					continue
				}
				return
			}
			if received == nil {
				continue
			}
		err = server.Send(zlink.SendFlagsNone, perfcommon.CloneMessages(received.Parts())...)
			if err != nil && !isPairLoopTransient(err) {
				if perfcommon.DebugEnabled() {
					fmt.Fprintf(os.Stderr, "pair server send error: %v\n", err)
				}
				perfcommon.Must(err)
			}
			if err := received.Close(); err != nil {
				if perfcommon.DebugEnabled() {
					fmt.Fprintf(os.Stderr, "pair server received close error: %v\n", err)
				}
				perfcommon.Must(err)
			}
			runtime.KeepAlive(received)
		}
	}()
	return func() {
		close(stop)
		wg.Wait()
	}
}
