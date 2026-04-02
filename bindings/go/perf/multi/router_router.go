package main

import (
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

func runMultiRouterRouter(cfg multiConfig) perfcommon.Result {
	stats := perfcommon.NewStats()
	segmentWarmup := splitDuration(cfg.warmup, cfg.clients)
	segmentDuration := splitDuration(cfg.duration, cfg.clients)

	for i := 0; i < cfg.clients; i++ {
		ctx, err := zlink.NewContext()
		perfcommon.Must(err)

		server, err := ctx.RouterSocket()
		perfcommon.Must(err)
		client, err := ctx.RouterSocket()
		perfcommon.Must(err)
		serverMon := perfcommon.OpenMonitor(server)
		clientMon := perfcommon.OpenMonitor(client)

		serverID, err := zlink.NewRoutingID([]byte("SERVER"))
		perfcommon.Must(err)
		clientID, err := zlink.NewRoutingID([]byte(time.Now().Add(time.Duration(i)).Format("150405.000000000")))
		perfcommon.Must(err)
		endpoint := perfcommon.UniqueTCPEndpoint("perf-multi-router-router")
		perfcommon.Must(server.SetRoutingID(serverID))
		perfcommon.Must(client.SetRoutingID(clientID))
		perfcommon.Must(client.SetConnectRoutingID(serverID))
		perfcommon.Must(server.Bind(endpoint))
		perfcommon.Must(client.Connect(endpoint))
		perfcommon.WaitConnected(serverMon, clientMon)
		perfcommon.Must(client.SetRecvTimeout(500 * time.Millisecond))
		perfcommon.Must(client.SetSendTimeout(500 * time.Millisecond))
		startMultiRouterCallbackEchoServer(server)
		waitForRouterClientReady(client, serverID)

		payload := perfcommon.PreparePayload(cfg.msgSize)
		stopAt := time.Now().Add(segmentWarmup + segmentDuration)
		activeAt := time.Now().Add(segmentWarmup)
		for time.Now().Before(stopAt) {
			perfcommon.StampPayload(payload)
			err := client.SendTo(serverID, perfcommon.NewMessage(payload))
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
			if err == nil {
				if sentAt, ok := perfcommon.SentAtFromMessage(part); ok && time.Now().After(activeAt) {
					stats.Add(sentAt)
				}
			}
			_ = reply.Close()
		}

		_ = clientMon.Close()
		_ = serverMon.Close()
		_ = client.Close()
		_ = server.Close()
		_ = ctx.Close()
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func waitForRouterClientReady(client *zlink.RouterSocket, serverID zlink.RoutingID) {
	payload := perfcommon.PreparePayload(64)
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		perfcommon.StampPayload(payload)
		err := client.SendTo(serverID, perfcommon.NewMessage(payload))
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
		perfcommon.Must(reply.Close())
		return
	}
	perfcommon.Must(&multiRouterRouterReadyError{})
}

type multiRouterRouterReadyError struct{}

func (e *multiRouterRouterReadyError) Error() string {
	return "multi router/router perf endpoint did not become ready"
}

func startMultiRouterCallbackEchoServer(server *zlink.RouterSocket) {
	perfcommon.Must(server.OnReceive(func(received *zlink.Received) {
		defer received.Close()
		perfcommon.Must(server.SendTo(received.RoutingID(),
			perfcommon.CloneMessages(received.Parts())...))
	}))
}

func splitDuration(total time.Duration, parts int) time.Duration {
	if parts <= 1 {
		if total <= 0 {
			return time.Second
		}
		return total
	}
	if total <= 0 {
		return time.Second
	}
	segment := total / time.Duration(parts)
	if segment <= 0 {
		return time.Millisecond
	}
	return segment
}
