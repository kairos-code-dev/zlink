package main

import (
	"fmt"
	"time"

	"zlink"
	"zlink/perf/internal/perfcommon"
)

type recvSocket interface {
	Recv(zlink.RecvFlags) (*zlink.Received, error)
}

func runSingleOneWay(
	cfg benchmarkConfig,
	receiver recvSocket,
	send func([]byte) error,
) perfcommon.Result {
	stats := perfcommon.NewStats()
	window := perfcommon.NewBenchmarkWindow(cfg.duration)
	payload := perfcommon.PreparePayload(cfg.msgSize)

	sendDone := make(chan struct{})
	go func() {
		defer close(sendDone)
		for time.Now().Before(window.StopAt) {
			perfcommon.StampWindowPayload(payload, window.ActiveAt)
			err := send(payload)
			if err != nil {
				if perfcommon.IsTransient(err) {
					continue
				}
				perfcommon.Must(err)
			}
		}
	}()

	for time.Now().Before(window.StopAt) {
		if !drainSingleOneWay(receiver, stats, cfg.msgSize, window.ActiveAt, true) {
			continue
		}
	}

	<-sendDone

	idleDrainDeadline := time.Now().Add(500 * time.Millisecond)
	for time.Now().Before(idleDrainDeadline) {
		if !drainSingleOneWay(receiver, nil, cfg.msgSize, window.ActiveAt, false) {
			continue
		}
	}

	return stats.Snapshot(cfg.duration, cfg.msgSize)
}

func drainSingleOneWay(
	receiver recvSocket,
	stats *perfcommon.Stats,
	msgSize int,
	activeAt time.Time,
	countActive bool,
) bool {
	received, err := receiver.Recv(zlink.RecvFlagsNone)
	if err != nil {
		if perfcommon.IsTransient(err) {
			return false
		}
		perfcommon.Must(err)
	}
	if received == nil {
		return false
	}
	part, err := perfPayloadPart(received)
	if err == nil && countActive && stats != nil {
		perfcommon.RecordMessageLatency(stats, activeAt, msgSize, part)
	}
	_ = received.Close()
	return true
}

func waitSingleRouteReady(
	name string,
	send func([]byte) error,
	receiver recvSocket,
) {
	payload := perfcommon.PreparePayload(64)
	perfcommon.StampProbePayload(payload)
	if err := send(payload); err != nil {
		perfcommon.Must(fmt.Errorf("%s ready probe send: %w", name, err))
	}
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		if drainSingleOneWay(receiver, nil, len(payload), time.Time{}, false) {
			return
		}
	}
	perfcommon.Must(fmt.Errorf("%s did not become ready", name))
}

func perfPayloadPart(received *zlink.Received) (*zlink.Message, error) {
	if received == nil {
		return nil, nil
	}
	if received.IsSinglePart() {
		return received.SinglePartOrError()
	}
	parts := received.Parts()
	if received.HasRoutingID() && len(parts) > 0 {
		return parts[len(parts)-1], nil
	}
	return received.SinglePartOrError()
}
