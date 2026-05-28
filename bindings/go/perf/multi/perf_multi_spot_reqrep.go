package main

import (
	"fmt"
	"strings"
	"sync"
	"time"

	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

type multiSpotReqRepClient struct {
	ctx    *zlink.Context
	socket *zlink.RouterSocket
}

var (
	multiSpotReqRepNodeRID = zlink.NewRoutingID([]byte("SPOT-REQREP-SERVER-NODE"))
	multiSpotReqRepSpotRID = zlink.NewRoutingID([]byte("SPOT-REQREP-SERVER-SPOT"))
)

type multiSpotReqRepSlot struct {
	spot    *zlink.Spot
	payload []byte
	seq     uint64
	mu      sync.Mutex
	waiting bool
}

func activeSpotReqRepSlotLimit(totalSlots, msgSize int) int {
	if msgSize >= 131072 && totalSlots > 8 {
		return 8
	}
	if msgSize >= 65536 && totalSlots > 32 {
		return 32
	}
	return totalSlots
}

func runMultiSpotReqRepServer(cfg multiConfig) {
	replierCtx, err := perfcommon.NewMultiServerContext()
	perfcommon.Must(err)
	defer replierCtx.Close()
	perfcommon.ApplyMultiAutoHWMMsgUnit(replierCtx, cfg.msgSize)

	replierNode, err := replierCtx.SpotNode()
	perfcommon.Must(err)
	defer replierNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(replierNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSServer(replierNode, cfg.transport))
	perfcommon.Must(replierNode.SetRoutingID(multiSpotReqRepNodeRID))
	dataEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-reqrep")
	dataRouterEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-reqrep-router")
	perfcommon.Must(replierNode.SetRouterBind(dataRouterEndpoint))
	perfcommon.Must(replierNode.SetPubBind(dataEndpoint))

	replier, err := replierNode.Spot()
	perfcommon.Must(err)
	defer replier.Close()
	perfcommon.ApplyMultiBenchmarkSocketOptions(replier, cfg.transport)
	perfcommon.Must(replier.SetRoutingID(multiSpotReqRepSpotRID))
	replierPoller, err := zlink.NewPoller()
	perfcommon.Must(err)
	defer replierPoller.Close()
	perfcommon.Must(replierPoller.AddSocket(replier, perfcommon.ZLinkPollIn, 0))
	replierPollEvents := make([]zlink.PollEvent, 1)

	controlNode, err := replierCtx.SpotNode()
	perfcommon.Must(err)
	defer controlNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(controlNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSServer(controlNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(controlNode, cfg.transport))
	perfcommon.Must(controlNode.SetRoutingID(zlink.NewRoutingID([]byte("SPOT-REQREP-CONTROL-SERVER-NODE"))))
	controlPub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlPub.Close()
	controlSub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlSub.Close()
	perfcommon.Must(controlSub.SetSubscription(multiSpotTopic))
	controlPoller := perfcommon.NewSocketPoller(controlSub, perfcommon.ZLinkPollIn)
	defer controlPoller.Close()
	controlEvents := make([]zlink.PollEvent, 1)
	controlEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-reqrep-control-server")
	perfcommon.Must(controlNode.SetPubBind(controlEndpoint))
	controlEndpoint = spotNodeLastEndpoint(controlNode, controlEndpoint)

	var readyCount int
	dataConnected := false
	startRunner := false
	events := make(chan string, 16)
	go scanSpotRoleStdin(cfg, events)

	flushControlLine("READY,%s", dataEndpoint)
	flushControlLine("CONTROL_READY,%s", controlEndpoint)
	readyTimeout := multiSpotEchoServerReadyTimeout()
	deadline := time.Now().Add(readyTimeout)
	for time.Now().Before(deadline) {
		drainMultiSpotReqRepServer(replier)
		select {
		case text := <-events:
			switch {
			case strings.HasPrefix(text, "CONNECT_CONTROL,"):
				endpoint := strings.TrimPrefix(text, "CONNECT_CONTROL,")
				perfcommon.Must(controlNode.ConnectPeer(endpoint))
				waitMultiSpotReqRepConnectedPeer(controlNode, readyTimeout)
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
			perfcommon.Must(replierNode.ConnectPeer(endpoint))
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
		waitMultiSpotReqRepServerIdle(controlPoller, controlEvents, replierPoller, replierPollEvents, deadline)
	}
	if !dataConnected || readyCount < cfg.clients {
		perfcommon.Must(fmt.Errorf("spot reqrep server readiness timeout"))
	}
	for !startRunner {
		drainMultiSpotReqRepServer(replier)
		select {
		case event := <-events:
			if event == fmt.Sprintf("START,%d", cfg.msgSize) {
				startRunner = true
				break
			}
			if event == "STOP" || event == "QUIT" {
				return
			}
		default:
		}
		if !startRunner {
			waitMultiSpotReqRepServerIdle(controlPoller, controlEvents, replierPoller, replierPollEvents, time.Now().Add(time.Millisecond))
		}
	}
	if !publishSpotControlPayload(controlPub, fmt.Sprintf("START,%d", cfg.msgSize), perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot reqrep server direct start publish timeout"))
	}
	idleDeadline := time.Now().Add(cfg.duration + 2*time.Second)
	for time.Now().Before(idleDeadline) {
		drainMultiSpotReqRepServer(replier)
		waitMultiSpotReqRepServerIdle(controlPoller, controlEvents, replierPoller, replierPollEvents, time.Now().Add(time.Millisecond))
	}
}

func drainMultiSpotReqRepServer(replier *zlink.Spot) {
	for {
		received := &zlink.Received{}
		ok, err := replier.RecvRouted(received, zlink.RecvFlagsDontWait)
		if err != nil {
			_ = received.Close()
			if perfcommon.IsTransient(err) {
				return
			}
			perfcommon.Must(err)
		}
		if !ok {
			_ = received.Close()
			return
		}
		replyMultiSpotReqRepServer(replier, received)
		_ = received.Close()
	}
}

func replyMultiSpotReqRepServer(replier *zlink.Spot, received *zlink.Received) {
	parts := received.Parts()
	if len(parts) == 0 {
		return
	}
	reply := perfcommon.NewMessage(parts[0].Data())
	defer reply.Close()
	sourceRID := received.RoutingID()
	requestSeq := received.RequestSeq()
	spotRID := received.SpotRID()
	if spotRID.Size() == 0 {
		perfcommon.Must(replier.ReplyToRouter(sourceRID, requestSeq).Message(reply).Submit(nil))
		return
	}
	perfcommon.Must(replier.ReplyToSpot(sourceRID, spotRID, requestSeq).Message(reply).Submit(nil))
}

func waitMultiSpotReqRepServerIdle(
	controlPoller *zlink.Poller,
	controlEvents []zlink.PollEvent,
	replierPoller *zlink.Poller,
	replierEvents []zlink.PollEvent,
	deadline time.Time,
) {
	if time.Now().After(deadline) {
		return
	}
	controlDeadline := time.Now().Add(time.Millisecond)
	if controlDeadline.After(deadline) {
		controlDeadline = deadline
	}
	waitSpotControlReadable(controlPoller, controlEvents, controlDeadline)
	remaining := time.Until(deadline)
	if remaining <= 0 {
		return
	}
	if remaining > time.Millisecond {
		remaining = time.Millisecond
	}
	_, err := replierPoller.Wait(replierEvents, remaining)
	if err != nil && !perfcommon.IsTransient(err) {
		perfcommon.Must(err)
	}
}

func waitMultiSpotReqRepConnectedPeer(node *zlink.SpotNode, timeout time.Duration) {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		status, err := node.Status()
		if err == nil && status != nil && status.ConnectedPeerCount > 0 {
			return
		}
		perfcommon.PollIdle(time.Millisecond)
	}
}

func multiSpotEchoServerReadyTimeout() time.Duration {
	timeout := perfcommon.MultiReadyTimeout() * 6
	if timeout < time.Second {
		return time.Second
	}
	return timeout
}

func runMultiSpotReqRepClientRole(cfg multiConfig, endpoint string) perfcommon.Result {
	parts := strings.SplitN(endpoint, ",", 2)
	if len(parts) != 2 {
		perfcommon.Must(fmt.Errorf("spot reqrep client expects data_endpoint,control_endpoint"))
	}
	dataEndpoint, controlEndpoint := parts[0], parts[1]

	clientCtx, err := perfcommon.NewMultiClientContext()
	perfcommon.Must(err)
	defer clientCtx.Close()
	perfcommon.ApplyMultiAutoHWMMsgUnit(clientCtx, cfg.msgSize)
	node, err := clientCtx.SpotNode()
	perfcommon.Must(err)
	defer node.Close()
	controlNode, err := clientCtx.SpotNode()
	perfcommon.Must(err)
	defer controlNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(node, cfg.pattern)
	perfcommon.ApplyMultiSpotNodeAdmission(controlNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSServer(node, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(node, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSServer(controlNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(controlNode, cfg.transport))
	perfcommon.Must(node.SetRoutingID(zlink.NewRoutingID([]byte("SPOT-REQREP-CLIENT-NODE"))))
	perfcommon.Must(controlNode.SetRoutingID(zlink.NewRoutingID([]byte("SPOT-REQREP-CONTROL-CLIENT-NODE"))))
	controlPub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlPub.Close()
	controlSub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlSub.Close()
	perfcommon.Must(controlSub.SetSubscription(multiSpotTopic))
	controlBind := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-reqrep-control-client")
	perfcommon.Must(controlNode.SetPubBind(controlBind))
	perfcommon.Must(controlNode.ConnectPeer(controlEndpoint))
	clientControlEndpoint := spotNodeLastEndpoint(controlNode, controlBind)
	flushControlLine("CLIENT_CONTROL_ENDPOINT,%s", clientControlEndpoint)

	localEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-reqrep-client")
	localRouterEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-reqrep-client-router")
	perfcommon.Must(node.SetRouterBind(localRouterEndpoint))
	perfcommon.Must(node.SetPubBind(localEndpoint))
	perfcommon.Must(node.ConnectPeer(dataEndpoint))

	spots := make([]*zlink.Spot, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		spot, err := node.Spot()
		perfcommon.Must(err)
		perfcommon.Must(spot.SetRoutingID(zlink.NewRoutingID([]byte(fmt.Sprintf("spot-req-client-spot-%06d", i)))))
		spots = append(spots, spot)
	}
	defer func() {
		for _, spot := range spots {
			_ = spot.Close()
		}
	}()
	poller, err := zlink.NewPoller()
	perfcommon.Must(err)
	defer poller.Close()
	pollEvents := make([]zlink.PollEvent, len(spots))
	slots := make([]multiSpotReqRepSlot, len(spots))
	for i, spot := range spots {
		perfcommon.Must(poller.AddSocket(spot, zlink.PollCompletion, uintptr(i)))
		slots[i] = multiSpotReqRepSlot{
			spot:    spot,
			payload: perfcommon.PreparePayload(cfg.msgSize),
			seq:     1,
		}
	}

	events := make(chan string, 16)
	go scanSpotRoleStdin(cfg, events)
	waitForSpotRoleEvent(events, "CONTROL_CONNECTED,")
	time.Sleep(perfcommon.MultiSpotReadySettleDuration())
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	if !publishSpotControlPayload(controlPub, fmt.Sprintf("DATA_ENDPOINT,%s", localEndpoint), perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot reqrep client data endpoint publish timeout"))
	}
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	waitMultiSpotReqRepConnectedPeer(node, perfcommon.MultiReadyTimeout())
	if !publishSpotControlPayload(controlPub, "CONNECTED", perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot reqrep client connected publish timeout"))
	}
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	if !publishSpotControlPayload(controlPub, fmt.Sprintf("READY_COUNT,%d,%d", cfg.msgSize, cfg.clients), perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot reqrep client ready publish timeout"))
	}
	flushControlLine("CLIENT_READY,%d", cfg.msgSize)
	waitForSpotRoleEvent(events, fmt.Sprintf("START,%d", cfg.msgSize))
	waitForSpotControlStart(controlSub, cfg.msgSize)

	stats := perfcommon.NewStats()
	window := activeDeadline(cfg.duration)
	latencies := make(chan float64, len(spots)*4)
	activeSlots := activeSpotReqRepSlotLimit(len(slots), cfg.msgSize)
	for time.Now().Before(window.StopAt) {
		submittedAny := false
		for i := 0; i < activeSlots; i++ {
			if submitMultiSpotReqRepRequest(&slots[i], cfg.transport, cfg.msgSize, window.StopAt, latencies) {
				submittedAny = true
			}
		}
		for {
			select {
			case latency := <-latencies:
				stats.AddLatencyNs(latency)
			default:
				goto drained
			}
		}
	drained:
		if submittedAny {
			continue
		}
		remaining := time.Until(window.StopAt)
		if remaining <= 0 {
			break
		}
		if remaining > 50*time.Millisecond {
			remaining = 50 * time.Millisecond
		}
		_, err := poller.Wait(pollEvents, remaining)
		perfcommon.Must(err)
	}
	for {
		select {
		case latency := <-latencies:
			stats.AddLatencyNs(latency)
		default:
			return stats.Snapshot(cfg.duration, cfg.msgSize)
		}
	}
}

func submitMultiSpotReqRepRequest(slot *multiSpotReqRepSlot, transport string, msgSize int, deadline time.Time, latencies chan<- float64) bool {
	slot.mu.Lock()
	if slot.waiting {
		slot.mu.Unlock()
		return false
	}
	perfcommon.StampPayload(slot.payload)
	slot.waiting = true
	slot.mu.Unlock()

	request := slot.spot.RequestToSpot(multiSpotReqRepNodeRID, multiSpotReqRepSpotRID)
	var submit zlink.RequestSubmitOp
	if useMultiSpotReqRepBytes(transport, msgSize) {
		submit = request.Bytes(slot.payload)
	} else {
		submit = request.Message(perfcommon.NewMessage(slot.payload))
	}
	ok, err := submit.Flags(zlink.SendFlagsDontWait).Timeout(200*time.Millisecond).Submit(nil, func(result zlink.RequestResult, parts []*zlink.Message) {
		defer func() {
			for _, part := range parts {
				_ = part.Close()
			}
			slot.mu.Lock()
			slot.waiting = false
			slot.mu.Unlock()
		}()
		if result != zlink.RequestOK || len(parts) == 0 || parts[0] == nil {
			return
		}
		now := time.Now()
		if latencyNs, ok := perfcommon.LatencyNsFromBytesAt(parts[0].Data(), msgSize, perfcommon.PhaseActive, now); ok && now.Before(deadline) {
			latencies <- latencyNs / 2.0
		}
	})
	if err != nil {
		slot.mu.Lock()
		slot.waiting = false
		slot.mu.Unlock()
		if perfcommon.IsTransient(err) || perfcommon.IsSubmitNotConnected(err) {
			return false
		}
		perfcommon.Must(err)
	}
	if !ok {
		slot.mu.Lock()
		slot.waiting = false
		slot.mu.Unlock()
		return false
	}
	return true
}

func useMultiSpotReqRepBytes(transport string, msgSize int) bool {
	switch transport {
	case "tcp":
		return msgSize == 65536 || msgSize == 131072
	case "ws":
		return msgSize >= 65536
	default:
		return false
	}
}
