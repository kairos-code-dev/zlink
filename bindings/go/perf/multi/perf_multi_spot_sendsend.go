package main

import (
	"errors"
	"fmt"
	"strings"
	"time"

	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

type multiSpotSendSendClient struct {
	ctx          *zlink.Context
	node         *zlink.SpotNode
	spot         *zlink.Spot
	payload      []byte
	waitingReply bool
}

var (
	multiSpotSendSendNodeRID = zlink.NewRoutingID([]byte("SPOT-SENDSEND-SERVER-NODE"))
	multiSpotSendSendSpotRID = zlink.NewRoutingID([]byte("SPOT-SENDSEND-SERVER-SPOT"))
)

func runMultiSpotSendSendServer(cfg multiConfig) {
	serverCtx, err := perfcommon.NewMultiServerContext()
	perfcommon.Must(err)
	defer serverCtx.Close()
	perfcommon.ApplyMultiAutoHWMMsgUnit(serverCtx, cfg.msgSize)

	serverNode, err := serverCtx.SpotNode()
	perfcommon.Must(err)
	defer serverNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(serverNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSServer(serverNode, cfg.transport))
	perfcommon.Must(serverNode.SetRoutingID(multiSpotSendSendNodeRID))

	replier, err := serverNode.Spot()
	perfcommon.Must(err)
	defer replier.Close()
	perfcommon.ApplyMultiBenchmarkSocketOptions(replier, cfg.transport)
	perfcommon.Must(replier.SetRoutingID(multiSpotSendSendSpotRID))

	dataEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-sendsend")
	dataRouterEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-sendsend-router")
	perfcommon.Must(serverNode.SetRouterBind(dataRouterEndpoint))
	perfcommon.Must(serverNode.SetPubBind(dataEndpoint))
	controlNode, err := serverCtx.SpotNode()
	perfcommon.Must(err)
	defer controlNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(controlNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSServer(controlNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(controlNode, cfg.transport))
	perfcommon.Must(controlNode.SetRoutingID(zlink.NewRoutingID([]byte("SPOT-SENDSEND-CONTROL-SERVER-NODE"))))
	controlPub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlPub.Close()
	controlSub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlSub.Close()
	perfcommon.Must(controlSub.SetSubscription(multiSpotTopic))
	controlEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-sendsend-control-server")
	perfcommon.Must(controlNode.SetPubBind(controlEndpoint))
	controlEndpoint = spotNodeLastEndpoint(controlNode, controlEndpoint)

	var readyCount int
	dataConnected := false
	startRunner := false
	events := make(chan string, 16)
	go scanSpotRoleStdin(cfg, events)

	flushControlLine("READY,%s", dataEndpoint)
	flushControlLine("CONTROL_READY,%s", controlEndpoint)
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(deadline) {
		drainMultiSpotSendSendServer(replier, cfg.transport, cfg.msgSize)
		select {
		case text := <-events:
			switch {
			case strings.HasPrefix(text, "CONNECT_CONTROL,"):
				endpoint := strings.TrimPrefix(text, "CONNECT_CONTROL,")
				perfcommon.Must(controlNode.ConnectPeer(endpoint))
				flushControlLine("CONTROL_CONNECTED,%s", endpoint)
			case text == fmt.Sprintf("START,%d", cfg.msgSize):
				startRunner = true
			case text == "STOP" || text == "QUIT":
				return
			}
		default:
		}
		payload := receiveSpotControlPayload(controlSub)
		switch {
		case strings.HasPrefix(payload, "DATA_ENDPOINT,"):
			endpoint := strings.TrimPrefix(payload, "DATA_ENDPOINT,")
			perfcommon.Must(serverNode.ConnectPeer(endpoint))
			dataConnected = true
		case payload == "CONNECTED":
		case strings.HasPrefix(payload, "READY_COUNT,"):
			fields := strings.Split(payload, ",")
			if len(fields) == 3 && fields[1] == fmt.Sprintf("%d", cfg.msgSize) {
				var count int
				_, _ = fmt.Sscanf(fields[2], "%d", &count)
				readyCount += count
			}
		}
		if dataConnected && readyCount >= cfg.clients {
			break
		}
		time.Sleep(time.Millisecond)
	}
	if !dataConnected || readyCount < cfg.clients {
		perfcommon.Must(fmt.Errorf("spot sendsend server readiness timeout"))
	}
	for !startRunner {
		event := <-events
		if event == fmt.Sprintf("START,%d", cfg.msgSize) {
			startRunner = true
			break
		}
		if event == "STOP" || event == "QUIT" {
			return
		}
		drainMultiSpotSendSendServer(replier, cfg.transport, cfg.msgSize)
	}
	if !publishSpotControlPayload(controlPub, fmt.Sprintf("START,%d", cfg.msgSize), perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot sendsend server direct start publish timeout"))
	}
	idleDeadline := time.Now().Add(cfg.duration + 2*time.Second)
	for time.Now().Before(idleDeadline) {
		drainMultiSpotSendSendServer(replier, cfg.transport, cfg.msgSize)
		time.Sleep(time.Millisecond)
	}
}

func runMultiSpotSendSendClientRole(cfg multiConfig, endpoint string) perfcommon.Result {
	parts := strings.SplitN(endpoint, ",", 2)
	if len(parts) != 2 {
		perfcommon.Must(fmt.Errorf("spot sendsend client expects data_endpoint,control_endpoint"))
	}
	dataEndpoint, controlEndpoint := parts[0], parts[1]

	clientCtx, err := perfcommon.NewMultiClientContext()
	perfcommon.Must(err)
	defer clientCtx.Close()
	perfcommon.ApplyMultiAutoHWMMsgUnit(clientCtx, cfg.msgSize)
	clientNode, err := clientCtx.SpotNode()
	perfcommon.Must(err)
	defer clientNode.Close()
	controlNode, err := clientCtx.SpotNode()
	perfcommon.Must(err)
	defer controlNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(clientNode, cfg.pattern)
	perfcommon.ApplyMultiSpotNodeAdmission(controlNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSServer(clientNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(clientNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSServer(controlNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(controlNode, cfg.transport))
	perfcommon.Must(clientNode.SetRoutingID(zlink.NewRoutingID([]byte("SPOT-SENDSEND-CLIENT-NODE"))))
	perfcommon.Must(controlNode.SetRoutingID(zlink.NewRoutingID([]byte("SPOT-SENDSEND-CONTROL-CLIENT-NODE"))))
	controlPub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlPub.Close()
	controlSub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlSub.Close()
	perfcommon.Must(controlSub.SetSubscription(multiSpotTopic))
	controlBind := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-sendsend-control-client")
	perfcommon.Must(controlNode.SetPubBind(controlBind))
	perfcommon.Must(controlNode.ConnectPeer(controlEndpoint))
	clientControlEndpoint := spotNodeLastEndpoint(controlNode, controlBind)
	flushControlLine("CLIENT_CONTROL_ENDPOINT,%s", clientControlEndpoint)

	localEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-sendsend-client")
	localRouterEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-sendsend-client-router")
	perfcommon.Must(clientNode.SetRouterBind(localRouterEndpoint))
	perfcommon.Must(clientNode.SetPubBind(localEndpoint))
	perfcommon.Must(clientNode.ConnectPeer(dataEndpoint))

	clients := make([]multiSpotSendSendClient, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		spot, spotErr := clientNode.Spot()
		perfcommon.Must(spotErr)
		perfcommon.ApplyMultiBenchmarkSocketOptions(spot, cfg.transport)
		perfcommon.Must(spot.SetRoutingID(zlink.NewRoutingID([]byte(fmt.Sprintf("SPOT-SENDSEND-%06d", i)))))
		clients = append(clients, multiSpotSendSendClient{
			node:    clientNode,
			spot:    spot,
			payload: perfcommon.PreparePayload(cfg.msgSize),
		})
	}
	defer func() {
		for _, client := range clients {
			_ = client.spot.Close()
		}
	}()

	events := make(chan string, 16)
	go scanSpotRoleStdin(cfg, events)
	waitForSpotRoleEvent(events, "CONTROL_CONNECTED,")
	time.Sleep(perfcommon.MultiSpotReadySettleDuration())
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	if !publishSpotControlPayload(controlPub, fmt.Sprintf("DATA_ENDPOINT,%s", localEndpoint), perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot sendsend client data endpoint publish timeout"))
	}
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	waitMultiSpotSendSendReady(clients[0].spot, cfg.msgSize)
	if !publishSpotControlPayload(controlPub, "CONNECTED", perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot sendsend client connected publish timeout"))
	}
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	if !publishSpotControlPayload(controlPub, fmt.Sprintf("READY_COUNT,%d,%d", cfg.msgSize, cfg.clients), perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot sendsend client ready publish timeout"))
	}
	flushControlLine("CLIENT_READY,%d", cfg.msgSize)
	waitForSpotRoleEvent(events, fmt.Sprintf("START,%d", cfg.msgSize))
	waitForSpotControlStart(controlSub, cfg.msgSize)

	stats := perfcommon.NewStats()
	window := activeDeadline(cfg.duration)
	activeLimit := activeMultiSpotSendSendClientLimit(len(clients), cfg.msgSize)
	poller, err := zlink.NewPoller()
	perfcommon.Must(err)
	defer poller.Close()
	pollEvents := make([]zlink.PollEvent, activeLimit)
	for i := 0; i < activeLimit; i++ {
		perfcommon.Must(poller.AddSocket(clients[i].spot, perfcommon.ZLinkPollIn, uintptr(i)))
	}
	for time.Now().Before(window.StopAt) {
		sendProgress := false
		for i := 0; i < activeLimit; i++ {
			client := &clients[i]
			if client.waitingReply {
				if drainMultiSpotSendSend(client.spot, cfg.msgSize, window.StopAt, stats, true) {
					client.waitingReply = false
				}
			}
			if client.waitingReply {
				continue
			}
			perfcommon.StampPayload(client.payload)
			if submitMultiSpotSendSend(client.spot, client.payload) {
				client.waitingReply = true
				sendProgress = true
			}
		}
		if sendProgress {
			continue
		}
		if drainMultiSpotSendSendWaiting(clients, activeLimit, cfg.msgSize, window.StopAt, stats, true) {
			continue
		}
		remaining := time.Until(window.StopAt)
		if remaining <= 0 {
			break
		}
		if remaining > time.Millisecond {
			remaining = time.Millisecond
		}
		n, waitErr := poller.Wait(pollEvents, remaining)
		if waitErr != nil {
			if perfcommon.IsTransient(waitErr) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi spot sendsend client poll: %w", waitErr))
		}
		handleMultiSpotSendSendPollEvents(pollEvents[:n], clients, cfg.msgSize, window.StopAt, stats, true)
	}
	drainMultiSpotSendSendPending(poller, pollEvents, clients, activeLimit, cfg.msgSize, stats, window.StopAt)
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func handleMultiSpotSendSendPollEvents(
	events []zlink.PollEvent,
	clients []multiSpotSendSendClient,
	msgSize int,
	activeStopAt time.Time,
	stats *perfcommon.Stats,
	record bool,
) {
	for i := range events {
		idx := int(events[i].Slot)
		if idx < 0 || idx >= len(clients) || events[i].Revents&perfcommon.ZLinkPollIn == 0 {
			continue
		}
		client := &clients[idx]
		if client.waitingReply && drainMultiSpotSendSend(client.spot, msgSize, activeStopAt, stats, record) {
			client.waitingReply = false
		}
	}
}

func drainMultiSpotSendSendWaiting(
	clients []multiSpotSendSendClient,
	activeLimit int,
	msgSize int,
	activeStopAt time.Time,
	stats *perfcommon.Stats,
	record bool,
) bool {
	if activeLimit > len(clients) {
		activeLimit = len(clients)
	}
	progressed := false
	for i := 0; i < activeLimit; i++ {
		client := &clients[i]
		if client.waitingReply && drainMultiSpotSendSend(client.spot, msgSize, activeStopAt, stats, record) {
			client.waitingReply = false
			progressed = true
		}
	}
	return progressed
}

func drainMultiSpotSendSendPending(
	poller *zlink.Poller,
	events []zlink.PollEvent,
	clients []multiSpotSendSendClient,
	activeLimit int,
	msgSize int,
	stats *perfcommon.Stats,
	activeStopAt time.Time,
) {
	deadline := time.Now().Add(time.Second)
	for anyMultiSpotSendSendWaiting(clients, activeLimit) && time.Now().Before(deadline) {
		remaining := time.Until(deadline)
		if remaining <= 0 {
			return
		}
		if remaining > time.Millisecond {
			remaining = time.Millisecond
		}
		n, waitErr := poller.Wait(events, remaining)
		if waitErr != nil {
			if perfcommon.IsTransient(waitErr) {
				continue
			}
			perfcommon.Must(fmt.Errorf("multi spot sendsend pending poll: %w", waitErr))
		}
		handleMultiSpotSendSendPollEvents(events[:n], clients, msgSize, activeStopAt, stats, true)
	}
}

func anyMultiSpotSendSendWaiting(clients []multiSpotSendSendClient, activeLimit int) bool {
	if activeLimit > len(clients) {
		activeLimit = len(clients)
	}
	for i := 0; i < activeLimit; i++ {
		if clients[i].waitingReply {
			return true
		}
	}
	return false
}

func activeMultiSpotSendSendClientLimit(total int, msgSize int) int {
	if msgSize >= 131072 && total > 8 {
		return 8
	}
	if msgSize >= 65536 && total > 32 {
		return 32
	}
	return total
}

func submitMultiSpotSendSend(spot *zlink.Spot, payload []byte) bool {
	sent, err := perfcommon.SubmitPayload(payload, func(message *zlink.Message) (bool, error) {
		return spot.SendToSpot(multiSpotSendSendNodeRID, multiSpotSendSendSpotRID).
			Message(message).
			Flags(zlink.SendFlagsDontWait).
			Submit(nil)
	})
	if err != nil {
		if perfcommon.IsTransient(err) || perfcommon.IsSubmitNotConnected(err) {
			return false
		}
		perfcommon.Must(err)
	}
	return sent
}

func drainMultiSpotSendSendServer(replier *zlink.Spot, transport string, msgSize int) {
	if useMultiSpotSendSendForwardRouted(transport, msgSize) {
		drainMultiSpotSendSendServerForwardRouted(replier)
		return
	}
	for {
		var received zlink.Received
		ok, err := replier.RecvRouted(&received, zlink.RecvFlagsDontWait)
		if err != nil {
			var recvErr *zlink.RecvError
			if errors.As(err, &recvErr) && recvErr.Result == zlink.RecvNoData {
				return
			}
			perfcommon.Must(err)
		}
		if !ok {
			return
		}
		if received.HasRequestSeq() {
			perfcommon.Must(received.Close())
			continue
		}
		parts := received.Parts()
		if len(parts) > 0 {
			if transport == "wss" && msgSize == 131072 {
				sent, sendErr := received.Send().Bytes(parts[0].Data()).Flags(zlink.SendFlagsDontWait).Submit(nil)
				if sendErr != nil && !perfcommon.IsTransient(sendErr) {
					perfcommon.Must(sendErr)
				}
				_ = sent
				perfcommon.Must(received.Close())
				continue
			}
			reply := perfcommon.NewMessage(parts[0].Data())
			send := received.Send()
			var op zlink.SendSubmitOp
			if msgSize == 131072 {
				op = send.MoveMessage(reply)
			} else {
				op = send.Message(reply)
			}
			sent, sendErr := op.Flags(zlink.SendFlagsDontWait).Submit(nil)
			if sendErr != nil && !perfcommon.IsTransient(sendErr) {
				perfcommon.Must(sendErr)
			}
			_ = sent
			perfcommon.Must(reply.Close())
		}
		perfcommon.Must(received.Close())
	}
}

func useMultiSpotSendSendForwardRouted(transport string, msgSize int) bool {
	switch transport {
	case "tls":
		return msgSize >= 65536
	case "wss":
		return msgSize >= 65536
	case "tcp":
		// Under LockOSThread the consume-forward echo (C zlink_spot_forward_routed
		// parity) is the stable choice for tcp large: it lifts 65536 from ~0.1%
		// to ~14% and keeps 131072/262144 from collapsing (the builder echo is
		// unstable for tcp large, dropping to <100 msg/s on 262144).
		return msgSize >= 65536
	default:
		return false
	}
}

func drainMultiSpotSendSendServerForwardRouted(replier *zlink.Spot) {
	for {
		_, ok, err := replier.ForwardRouted(zlink.RecvFlagsDontWait, zlink.SendFlagsDontWait)
		if err != nil {
			if perfcommon.IsTransient(err) || perfcommon.IsSubmitNotConnected(err) {
				return
			}
			perfcommon.Must(err)
		}
		if !ok {
			return
		}
	}
}

func drainMultiSpotSendSend(
	spot *zlink.Spot,
	msgSize int,
	activeStopAt time.Time,
	stats *perfcommon.Stats,
	record bool,
) bool {
	progressed := false
	for {
		var received zlink.Received
		ok, err := spot.RecvRouted(&received, zlink.RecvFlagsDontWait)
		if err != nil {
			var recvErr *zlink.RecvError
			if errors.As(err, &recvErr) && recvErr.Result == zlink.RecvNoData {
				return progressed
			}
			perfcommon.Must(err)
		}
		if !ok {
			return progressed
		}
		progressed = true
		parts := received.Parts()
		now := time.Now()
		if record && now.Before(activeStopAt) && !received.HasRequestSeq() && len(parts) > 0 {
			if latencyNs, ok := perfcommon.LatencyNsFromMessageAt(parts[0], msgSize, perfcommon.PhaseActive, now); ok {
				stats.AddLatencyNs(latencyNs / 2.0)
			}
		}
		perfcommon.Must(received.Close())
	}
}

func waitMultiSpotSendSendReady(spot *zlink.Spot, msgSize int) {
	payload := perfcommon.PreparePayload(msgSize)
	perfcommon.StampProbePayload(payload)
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	waitingReply := false
	for time.Now().Before(deadline) {
		if !waitingReply && submitMultiSpotSendSend(spot, payload) {
			waitingReply = true
		}
		if drainMultiSpotSendSend(spot, msgSize, deadline, perfcommon.NewStats(), false) {
			return
		}
		time.Sleep(time.Millisecond)
	}
	perfcommon.Must(fmt.Errorf("multi spot sendsend ready probe timed out"))
}
