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
	requesterPoller, err := zlink.NewPoller()
	perfcommon.Must(err)
	defer requesterPoller.Close()

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
	perfcommon.Must(requesterPoller.AddSocket(requester, perfcommon.ZLinkPollIn))

	perfcommon.Must(replier.OnDispatchEvent(func(currentSpot *zlink.Spot, info zlink.SpotDispatchInfo) {
		if info.Event != zlink.SpotDispatchEventRoutedReadable {
			return
		}
		drainSingleSpotReqRepRequests(currentSpot)
	}))

	waitSingleSpotReqRepReady(requester, requesterPoller, cfg.msgSize)
	perfcommon.PostReadySettle(cfg.pattern)

	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(cfg.duration)
	payload := perfcommon.PreparePayload(cfg.msgSize)

	for time.Now().Before(window.StopAt) {
		perfcommon.StampWindowPayload(payload, window.ActiveAt)
		if !sendSingleSpotReqRepRequest(requester, payload, zlink.SendFlagsNone) {
			continue
		}
		reply, ok := awaitSingleSpotReqRepReply(requester, requesterPoller, window.StopAt.Add(2*time.Second))
		if !ok {
			continue
		}
		if part, err := reply.SinglePartOrError(); err == nil {
			perfcommon.RecordMessageRTTLatency(stats, window.ActiveAt, cfg.msgSize, part)
		}
		_ = reply.Close()
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

func sendSingleSpotReqRepRequest(
	requester *zlink.RouterSocket,
	payload []byte,
	flags zlink.SendFlags,
) bool {
	err := requester.SendToSpot(
		singleSpotReqRepNodeRID,
		singleSpotReqRepSpotRID,
		flags,
		perfcommon.NewMessage(payload),
	)
	if err != nil {
		if perfcommon.IsTransient(err) {
			return false
		}
		perfcommon.Must(err)
	}
	return true
}

func awaitSingleSpotReqRepReply(
	requester *zlink.RouterSocket,
	poller *zlink.Poller,
	deadline time.Time,
) (*zlink.Received, bool) {
	for time.Now().Before(deadline) {
		event, err := poller.Wait(time.Until(deadline))
		if err != nil {
			if perfcommon.IsTransient(err) {
				continue
			}
			perfcommon.Must(err)
		}
		if event == nil {
			continue
		}
		for {
			received, err := requester.Recv(zlink.RecvFlagsDontWait)
			if err != nil {
				if perfcommon.IsTransient(err) {
					break
				}
				perfcommon.Must(err)
			}
			if received == nil {
				break
			}
			return received, true
		}
	}
	return nil, false
}

func waitSingleSpotReqRepReady(
	requester *zlink.RouterSocket,
	poller *zlink.Poller,
	msgSize int,
) {
	payload := perfcommon.PreparePayload(msgSize)
	deadline := time.Now().Add(perfcommon.SingleReadyTimeout())
	for time.Now().Before(deadline) {
		perfcommon.StampProbePayload(payload)
		if sendSingleSpotReqRepRequest(requester, payload, zlink.SendFlagsNone) {
			reply, ok := awaitSingleSpotReqRepReply(requester, poller, time.Now().Add(200*time.Millisecond))
			if ok {
				part, err := reply.SinglePartOrError()
				if err == nil {
					if _, matched := perfcommon.SentAtFromMessagePhase(part, msgSize, perfcommon.PhaseWarmup); matched {
						_ = reply.Close()
						return
					}
				}
				_ = reply.Close()
			}
		}
		time.Sleep(10 * time.Millisecond)
	}
	perfcommon.Must(fmt.Errorf("spot reqrep ready probe timed out"))
}
