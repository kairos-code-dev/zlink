package main

import (
	"fmt"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

var (
	singleSpotReqRepRequesterRID = zlink.NewRoutingID([]byte("perf-spot-reqrep-requester"))
	singleSpotReqRepNodeRID      = zlink.NewRoutingID([]byte("perf-spot-reqrep-node"))
	singleSpotReqRepSpotRID      = zlink.NewRoutingID([]byte("perf-spot-reqrep-spot"))
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
	perfcommon.ApplySingleHWM(replier)
	perfcommon.ApplySingleBenchmarkSocketOptions(requester, cfg.transport)
	perfcommon.ApplySingleBenchmarkSocketOptions(replier, cfg.transport)
	perfcommon.Must(requester.SetRecvTimeout(perfcommon.SingleRecvTimeout()))
	perfcommon.Must(requester.SetRoutingID(singleSpotReqRepRequesterRID))
	perfcommon.Must(replierNode.SetRoutingID(singleSpotReqRepNodeRID))
	perfcommon.Must(replier.SetRoutingID(singleSpotReqRepSpotRID))

	endpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-spot-reqrep")
	perfcommon.Must(replierNode.Bind(endpoint))
	perfcommon.Must(requester.Connect(endpoint))

	perfcommon.Must(replier.OnDispatchEvent(func(currentSpot *zlink.Spot, info zlink.SpotDispatchInfo) {
		if info.Event != zlink.SpotDispatchEventRoutedReadable {
			return
		}
		drainSingleSpotReqRepRequests(currentSpot)
	}))

	waitSingleSpotReqRepReady(requester, cfg.msgSize)
	perfcommon.PostReadySettle(cfg.pattern)

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(cfg.duration)
	payload := perfcommon.PreparePayload(cfg.msgSize)

	for time.Now().Before(window.StopAt) {
		perfcommon.StampWindowPayload(payload, window.ActiveAt)
		replyDone := make(chan bool, 1)
		ok := requestSingleSpotReqRepReply(
			requester,
			payload,
			perfcommon.SingleRecvTimeout(),
			func(result zlink.RequestResult, parts []*zlink.Message) {
				defer closeMessageParts(parts)
				if result != zlink.RequestOK || len(parts) != 1 {
					replyDone <- false
					return
				}
				perfcommon.RecordMessageRTTLatency(stats, window.ActiveAt, cfg.msgSize, parts[0])
				replyDone <- true
			},
		)
		if !ok {
			continue
		}
		<-replyDone
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func drainSingleSpotReqRepRequests(replier *zlink.Spot) {
	for {
		received, err := replier.RecvRouted(zlink.RecvFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) {
				return
			}
			perfcommon.Must(err)
		}
		if received == nil {
			return
		}

		part, err := received.SinglePartOrError()
		if err == nil {
			reply, replyErr := zlink.NewMessage(part.Data())
			perfcommon.Must(replyErr)
			perfcommon.Must(received.Reply([]*zlink.Message{reply}))
			_ = reply.Close()
		}
		_ = received.Close()
	}
}

func requestSingleSpotReqRepReply(
	requester *zlink.RouterSocket,
	payload []byte,
	timeout time.Duration,
	callback zlink.RequestReplyCallback,
) bool {
	ok, err := requester.TryRequestToSpot(
		singleSpotReqRepNodeRID,
		singleSpotReqRepSpotRID,
		callback,
		timeout,
		perfcommon.NewMessage(payload),
	)
	if err != nil {
		if perfcommon.IsTransient(err) {
			return false
		}
		perfcommon.Must(err)
	}
	return ok
}

func waitSingleSpotReqRepReady(
	requester *zlink.RouterSocket,
	msgSize int,
) {
	payload := perfcommon.PreparePayload(msgSize)
	deadline := time.Now().Add(perfcommon.SingleReadyTimeout())
	for time.Now().Before(deadline) {
		perfcommon.StampProbePayload(payload)
		ready := make(chan bool, 1)
		if requestSingleSpotReqRepReply(
			requester,
			payload,
			200*time.Millisecond,
			func(result zlink.RequestResult, parts []*zlink.Message) {
				defer closeMessageParts(parts)
				if result != zlink.RequestOK || len(parts) != 1 {
					ready <- false
					return
				}
				_, matched := perfcommon.SentAtFromMessagePhase(parts[0], msgSize, perfcommon.PhaseWarmup)
				ready <- matched
			},
		) {
			select {
			case matched := <-ready:
				if matched {
					return
				}
			case <-time.After(250 * time.Millisecond):
			}
		}
		time.Sleep(10 * time.Millisecond)
	}
	perfcommon.Must(fmt.Errorf("spot reqrep ready probe timed out"))
}

func closeMessageParts(parts []*zlink.Message) {
	for _, part := range parts {
		_ = part.Close()
	}
}
