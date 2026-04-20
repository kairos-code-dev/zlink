package main

import (
	"fmt"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runSpotReqRep(cfg benchmarkConfig) perfcommon.Result {
	ctx, err := zlink.NewContext()
	perfcommon.Must(err)
	defer ctx.Close()

	requester, err := ctx.RouterSocket()
	perfcommon.Must(err)
	defer requester.Close()
	replierNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer replierNode.Close()
	replier, err := replierNode.Spot()
	perfcommon.Must(err)
	defer replier.Close()

	perfcommon.Must(perfcommon.ConfigureTLSServer(replierNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(requester, cfg.transport))
	perfcommon.ApplySingleHWM(requester)
	endpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-spot-reqrep")
	perfcommon.Must(replierNode.Bind(endpoint))
	perfcommon.Must(requester.Connect(endpoint))
	perfcommon.ApplySingleBenchmarkSocketOptions(requester, cfg.transport)
	perfcommon.Must(requester.SetRecvTimeout(perfcommon.BenchmarkSocketTimeout))
	perfcommon.Must(requester.SetSendTimeout(perfcommon.BenchmarkSocketTimeout))
	replierNodeRID, err := replierNode.RoutingID()
	perfcommon.Must(err)
	replierSpotRID, err := replier.RoutingID()
	perfcommon.Must(err)

	perfcommon.Must(replier.OnDispatchEvent(func(event zlink.SpotDispatchEvent) {
		if event != zlink.SpotDispatchEventRoutedReadable {
			return
		}
		for {
			received, err := replier.RecvRouted(zlink.RecvFlagsDontWait)
			if err != nil {
				if perfcommon.IsTransient(err) {
					return
				}
				perfcommon.Must(fmt.Errorf("spot reqrep replier recv: %w", err))
			}
			if received == nil {
				return
			}
			part, err := received.SinglePartOrError()
			if err == nil {
				reply := perfcommon.NewMessage(append([]byte(nil), part.Data()...))
				perfcommon.Must(received.Reply([]*zlink.Message{reply}))
			}
			_ = received.Close()
		}
	}))

	waitSingleSpotReqRepReady(requester, replierNodeRID, replierSpotRID, cfg.msgSize)
	perfcommon.PostReadySettle(cfg.pattern)

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(cfg.duration)
	payload := perfcommon.PreparePayload(cfg.msgSize)
	replyDone := make(chan error, 1)

	for time.Now().Before(window.StopAt) {
		perfcommon.StampPayload(payload)
		ok, err := requester.TryRequestToSpot(
			replierNodeRID,
			replierSpotRID,
			func(result zlink.RequestResult, parts []*zlink.Message) {
				defer func() {
					for _, part := range parts {
						_ = part.Close()
					}
				}()
				if result != zlink.RequestOK || len(parts) == 0 {
					replyDone <- fmt.Errorf("spot reqrep request failed: %v", result)
					return
				}
				if sentAt, ok := perfcommon.SentAtFromMessage(parts[0], cfg.msgSize); ok {
					stats.AddLatencyNs(float64(time.Since(sentAt).Nanoseconds()) / 2.0)
				}
				replyDone <- nil
			},
			perfcommon.BenchmarkSocketTimeout,
			perfcommon.NewMessage(payload),
		)
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if !ok {
			continue
		}
		perfcommon.Must(<-replyDone)
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitSingleSpotReqRepReady(requester *zlink.RouterSocket, nodeRID, spotRID zlink.RoutingID, msgSize int) {
	payload := perfcommon.PreparePayload(msgSize)
	perfcommon.StampProbePayload(payload)
	ready := make(chan error, 1)
	ok, err := requester.TryRequestToSpot(
		nodeRID,
		spotRID,
		func(result zlink.RequestResult, parts []*zlink.Message) {
			defer func() {
				for _, part := range parts {
					_ = part.Close()
				}
			}()
			if result != zlink.RequestOK || len(parts) == 0 {
				ready <- fmt.Errorf("spot reqrep ready probe failed: %v", result)
				return
			}
			if _, ok := perfcommon.SentAtFromMessage(parts[0], msgSize); !ok {
				ready <- fmt.Errorf("spot reqrep ready probe returned invalid payload")
				return
			}
			ready <- nil
		},
		perfcommon.BenchmarkSocketTimeout,
		perfcommon.NewMessage(payload),
	)
	if err != nil {
		perfcommon.Must(err)
	}
	if !ok {
		perfcommon.Must(fmt.Errorf("spot reqrep ready probe backpressured"))
	}
	perfcommon.Must(<-ready)
}
