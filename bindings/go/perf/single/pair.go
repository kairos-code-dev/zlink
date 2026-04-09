package main

import (
	"fmt"
	"os"
	"time"

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
	perfcommon.WaitConnected(serverMon, clientMon)
	startPairEchoServer(server, cfg.transport)

	stats := perfcommon.NewStats()
	payload := perfcommon.PreparePayload(cfg.msgSize)
	window := perfcommon.NewBenchmarkWindow(0, cfg.duration)
	usePollingRecv := true

	for time.Now().Before(window.StopAt) {
		perfcommon.StampPayload(payload)
		err := client.Send(perfcommon.NewMessage(payload))
		if err != nil {
			if perfcommon.DebugEnabled() {
				fmt.Fprintf(os.Stderr, "pair client send error: %v\n", err)
			}
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		var reply *zlink.Received
		if usePollingRecv {
			reply, err = client.Recv()
			if err != nil {
				if perfcommon.DebugEnabled() {
					fmt.Fprintf(os.Stderr, "pair client recv error: %v\n", err)
				}
				if perfcommon.IsTransient(err) {
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
		perfcommon.RecordMessageLatency(stats, window.ActiveAt, part)
		if err := reply.Close(); err != nil {
			if perfcommon.DebugEnabled() {
				fmt.Fprintf(os.Stderr, "pair client reply close error: %v\n", err)
			}
			perfcommon.Must(err)
		}
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func startPairEchoServer(server *zlink.PairSocket, transport string) {
	usePollingRecv := true
	go func() {
		iter := 0
		for {
			var received *zlink.Received
			var err error
			if usePollingRecv {
				received, err = server.Recv()
				if err != nil {
					if perfcommon.DebugEnabled() {
						fmt.Fprintf(os.Stderr, "pair server recv error: %v\n", err)
					}
					if perfcommon.IsTransient(err) {
						continue
					}
					return
				}
				if received == nil {
					continue
				}
			} else {
				received, err = server.Recv()
				if err != nil {
					if perfcommon.DebugEnabled() {
						fmt.Fprintf(os.Stderr, "pair server recv error: %v\n", err)
					}
					if perfcommon.IsTransient(err) {
						time.Sleep(100 * time.Microsecond)
						continue
					}
					return
				}
			}
			iter++
			if perfcommon.DebugEnabled() && iter%10000 == 0 {
				fmt.Fprintf(os.Stderr, "pair server received %d messages\n", iter)
			}
			err = server.Send(perfcommon.CloneMessages(received.Parts())...)
			if err != nil && !perfcommon.IsTransient(err) {
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
		}
	}()
}
