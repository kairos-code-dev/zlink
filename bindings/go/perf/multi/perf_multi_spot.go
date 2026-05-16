package main

import (
	"bufio"
	"fmt"
	"strings"
	"time"

	zlink "zlink.systems/zlink/contracts"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

const multiSpotTopic = "bench"

func runMultiSpotServer(cfg multiConfig) {
	ctx, err := perfcommon.NewMultiServerContext()
	perfcommon.Must(err)
	defer ctx.Close()

	dataNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer dataNode.Close()
	controlNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer controlNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(dataNode, cfg.pattern)
	perfcommon.ApplyMultiSpotNodeAdmission(controlNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSServer(dataNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSServer(controlNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(controlNode, cfg.transport))
	perfcommon.Must(dataNode.SetRoutingID(zlink.NewRoutingID([]byte("z-go-multi-spot-server"))))
	perfcommon.Must(controlNode.SetRoutingID(zlink.NewRoutingID([]byte("z-go-multi-spot-control-server"))))
	dataSpot, err := dataNode.Spot()
	perfcommon.Must(err)
	defer dataSpot.Close()
	controlPub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlPub.Close()
	controlSub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlSub.Close()
	perfcommon.Must(controlSub.SetSubscription(multiSpotTopic))

	dataEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-data")
	controlEndpoint := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-control-server")
	perfcommon.Must(dataNode.Bind(dataEndpoint))
	perfcommon.Must(controlNode.Bind(controlEndpoint))
	dataEndpoint = spotNodeLastEndpoint(dataNode, dataEndpoint)
	controlEndpoint = spotNodeLastEndpoint(controlNode, controlEndpoint)
	flushControlLine("READY,%s", dataEndpoint)
	flushControlLine("CONTROL_READY,%s", controlEndpoint)

	events := make(chan string, 16)
	go scanSpotRoleStdin(cfg, events)
	readyCount := 0
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(deadline) {
		select {
		case event := <-events:
			switch {
			case strings.HasPrefix(event, "CONNECT_CONTROL,"):
				endpoint := strings.TrimPrefix(event, "CONNECT_CONTROL,")
				perfcommon.Must(controlNode.ConnectPeer(endpoint))
				flushControlLine("CONTROL_CONNECTED,%s", endpoint)
			case event == fmt.Sprintf("START,%d", cfg.msgSize):
			case event == "STOP" || event == "QUIT":
				return
			}
		default:
		}
		payload := receiveSpotControlPayload(controlSub)
		if strings.HasPrefix(payload, "READY_COUNT,") {
			fields := strings.Split(payload, ",")
			if len(fields) == 3 && fields[1] == fmt.Sprintf("%d", cfg.msgSize) {
				var count int
				_, _ = fmt.Sscanf(fields[2], "%d", &count)
				readyCount += count
			}
		}
		if readyCount >= cfg.clients {
			break
		}
		time.Sleep(time.Millisecond)
	}
	if readyCount < cfg.clients {
		perfcommon.Must(fmt.Errorf("spot server readiness timeout"))
	}
	for {
		event := <-events
		if event == fmt.Sprintf("START,%d", cfg.msgSize) {
			break
		}
		if event == "STOP" || event == "QUIT" {
			return
		}
	}
	if !publishSpotControlPayload(controlPub, fmt.Sprintf("START,%d", cfg.msgSize), perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot server direct start publish timeout"))
	}
	payload := perfcommon.PreparePayload(cfg.msgSize)
	stopAt := time.Now().Add(cfg.duration)
	for time.Now().Before(stopAt) {
		perfcommon.StampPayload(payload)
		_, err := dataSpot.Publish(multiSpotTopic).Message(perfcommon.NewMessage(payload)).Flags(zlink.SendFlagsDontWait).Submit(nil)
		if err != nil && !perfcommon.IsTransient(err) {
			perfcommon.Must(err)
		}
	}
	// perf_multi_spot_server.cpp end-of-active: signal phase end on the
	// data topic (the C server uses a cooldown-phase header; Go uses the
	// shared wire-level stop token) so the client recv-drain exits
	// deterministically instead of relying purely on its own clock.
	sendMultiSpotStopToken(dataSpot)
}

// sendMultiSpotStopToken publishes the wire-level stop token on the
// data topic with bounded retry through transient backpressure.
func sendMultiSpotStopToken(spot *zlink.Spot) {
	for attempt := 0; attempt < perfcommon.StopTokenSendAttempts; attempt++ {
		sent, err := spot.Publish(multiSpotTopic).Message(perfcommon.NewMessage(perfcommon.StopToken)).Flags(zlink.SendFlagsDontWait).Submit(nil)
		if err == nil && sent {
			return
		}
		if err != nil && !perfcommon.IsTransient(err) {
			return
		}
		time.Sleep(perfcommon.StopTokenSendBackoff)
	}
}

func runMultiSpotClient(cfg multiConfig, endpoint string) perfcommon.Result {
	parts := strings.SplitN(endpoint, ",", 2)
	if len(parts) != 2 {
		perfcommon.Must(fmt.Errorf("spot client expects data_endpoint,control_endpoint"))
	}
	dataEndpoint, controlEndpoint := parts[0], parts[1]
	ctx, err := perfcommon.NewMultiClientContext()
	perfcommon.Must(err)
	defer ctx.Close()
	dataNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer dataNode.Close()
	controlNode, err := ctx.SpotNode()
	perfcommon.Must(err)
	defer controlNode.Close()
	perfcommon.ApplyMultiSpotNodeAdmission(dataNode, cfg.pattern)
	perfcommon.ApplyMultiSpotNodeAdmission(controlNode, cfg.pattern)
	perfcommon.Must(perfcommon.ConfigureTLSClient(dataNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSServer(controlNode, cfg.transport))
	perfcommon.Must(perfcommon.ConfigureTLSClient(controlNode, cfg.transport))
	controlPub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlPub.Close()
	controlSub, err := controlNode.Spot()
	perfcommon.Must(err)
	defer controlSub.Close()
	perfcommon.Must(controlSub.SetSubscription(multiSpotTopic))
	controlBind := perfcommon.UniqueEndpoint(cfg.transport, "perf-multi-spot-control-client")
	perfcommon.Must(controlNode.Bind(controlBind))
	perfcommon.Must(controlNode.ConnectPeer(controlEndpoint))
	clientControlEndpoint := spotNodeLastEndpoint(controlNode, controlBind)
	flushControlLine("CLIENT_CONTROL_ENDPOINT,%s", clientControlEndpoint)

	perfcommon.Must(dataNode.ConnectPeer(dataEndpoint))
	spots := make([]*zlink.Spot, 0, cfg.clients)
	for i := 0; i < cfg.clients; i++ {
		spot, err := dataNode.Spot()
		perfcommon.Must(err)
		perfcommon.Must(spot.SetRoutingID(zlink.NewRoutingID([]byte(fmt.Sprintf("a-go-multi-spot-client-spot-%06d", i)))))
		perfcommon.Must(spot.SetSubscription(multiSpotTopic))
		spots = append(spots, spot)
	}
	defer func() {
		for _, spot := range spots {
			_ = spot.Close()
		}
	}()
	events := make(chan string, 16)
	go scanSpotRoleStdin(cfg, events)
	waitForSpotRoleEvent(events, "CONTROL_CONNECTED,")
	time.Sleep(perfcommon.MultiSpotReadySettleDuration())
	time.Sleep(perfcommon.MultiSpotControlSettleDuration())
	if !publishSpotControlPayload(controlPub, fmt.Sprintf("READY_COUNT,%d,%d", cfg.msgSize, cfg.clients), perfcommon.MultiReadyTimeout()) {
		perfcommon.Must(fmt.Errorf("spot client ready publish timeout"))
	}
	flushControlLine("CLIENT_READY,%d", cfg.msgSize)
	waitForSpotRoleEvent(events, fmt.Sprintf("START,%d", cfg.msgSize))
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(deadline) {
		if receiveSpotControlPayload(controlSub) == fmt.Sprintf("START,%d", cfg.msgSize) {
			break
		}
		time.Sleep(time.Millisecond)
	}
	stats := perfcommon.NewStats()
	stopAt := time.Now().Add(cfg.duration)
	// perf_multi_spot_client.cpp spot_client_recv_worker_loop /
	// drain_spot_client_slot: the SPOT handle has no poll fd (same as
	// single SPOT), so the C reference uses a per-slot DONTWAIT drain to
	// empty, a 1ms idle yield when no slot progressed (not a busy spin),
	// and ends the phase on the wire-level stop token. Counting is
	// bounded by the active deadline.
	stopSeen := make([]bool, len(spots))
	stopsRemaining := len(spots)
	for stopsRemaining > 0 && time.Now().Before(stopAt) {
		progressed := false
		for idx, spot := range spots {
			if stopSeen[idx] {
				continue
			}
			for {
				var message zlink.TopicMessage
				ok, err := spot.Subscribe(&message, zlink.RecvFlagsDontWait)
				if err != nil {
					if perfcommon.IsTransient(err) {
						break
					}
					perfcommon.Must(err)
				}
				if !ok {
					break
				}
				progressed = true
				part, partErr := message.SinglePartOrError()
				if partErr == nil && part != nil {
					if perfcommon.IsStopTokenMessage(part) {
						stopSeen[idx] = true
						stopsRemaining--
						_ = message.Close()
						break
					}
					if sentAt, ok := perfcommon.SentAtFromBytes(part.Data(), cfg.msgSize); ok && time.Now().Before(stopAt) {
						stats.AddLatencyNs(float64(time.Since(sentAt).Nanoseconds()))
					}
				}
				_ = message.Close()
			}
		}
		if !progressed {
			// drain_spot_client_slot idle path: 1ms yield, no busy spin.
			time.Sleep(time.Millisecond)
		}
	}
	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func spotNodeLastEndpoint(node *zlink.SpotNode, fallback string) string {
	status, err := node.StatusSnapshot()
	if err == nil && status != nil && status.LocalEndpoint != "" {
		return status.LocalEndpoint
	}
	return fallback
}

func scanSpotRoleStdin(cfg multiConfig, events chan<- string) {
	scanner := bufio.NewScanner(newStdinReader())
	for scanner.Scan() {
		text := strings.TrimSpace(scanner.Text())
		if text != "" {
			events <- text
		}
	}
}

func waitForSpotRoleEvent(events <-chan string, prefix string) {
	deadline := time.After(perfcommon.MultiReadyTimeout())
	for {
		select {
		case event := <-events:
			if strings.HasPrefix(event, prefix) || event == prefix {
				return
			}
		case <-deadline:
			perfcommon.Must(fmt.Errorf("spot role event timeout: %s", prefix))
		}
	}
}

func waitForSpotControlStart(controlSub *zlink.Spot, msgSize int) {
	expected := fmt.Sprintf("START,%d", msgSize)
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(deadline) {
		if receiveSpotControlPayload(controlSub) == expected {
			return
		}
		time.Sleep(time.Millisecond)
	}
	perfcommon.Must(fmt.Errorf("spot control start timeout: %s", expected))
}

func publishSpotControlPayload(spot *zlink.Spot, payload string, timeout time.Duration) bool {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		sent, err := spot.Publish(multiSpotTopic).Message(perfcommon.NewMessage([]byte(payload))).Flags(zlink.SendFlagsDontWait).Submit(nil)
		if err == nil && sent {
			return true
		}
		if err != nil && !perfcommon.IsTransient(err) {
			perfcommon.Must(err)
		}
		time.Sleep(time.Millisecond)
	}
	return false
}

func receiveSpotControlPayload(spot *zlink.Spot) string {
	var message zlink.TopicMessage
	ok, err := spot.Subscribe(&message, zlink.RecvFlagsDontWait)
	if err != nil {
		if perfcommon.IsTransient(err) {
			return ""
		}
		perfcommon.Must(err)
	}
	if !ok {
		return ""
	}
	defer message.Close()
	part, err := message.SinglePartOrError()
	if err != nil || part == nil {
		return ""
	}
	return string(part.Data())
}
