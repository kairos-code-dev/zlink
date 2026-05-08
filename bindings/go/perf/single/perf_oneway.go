package main

import (
	"fmt"
	"os"
	"time"

	"zlink.systems/zlink"
	"zlink.systems/zlink/perf/internal/perfcommon"
)

type recvSocket interface {
	zlink.SocketTarget
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
	poller := perfcommon.NewSocketPoller(receiver, perfcommon.ZLinkPollIn)
	defer poller.Close()

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
				if os.Getenv("PERF_DEBUG") != "" {
					fmt.Fprintf(os.Stderr, "single active send error: %v\n", err)
				}
				perfcommon.Must(err)
			}
		}
		perfcommon.StampCooldownPayload(payload)
		err := send(payload)
		if err != nil && !perfcommon.IsTransient(err) {
			if os.Getenv("PERF_DEBUG") != "" {
				fmt.Fprintf(os.Stderr, "single cooldown send error: %v\n", err)
			}
			perfcommon.Must(err)
		}
	}()

	for time.Now().Before(window.StopAt) {
		timeout := time.Until(window.StopAt)
		if timeout <= 0 {
			break
		}
		event, err := poller.Wait(timeout)
		if err != nil {
			if os.Getenv("PERF_DEBUG") != "" {
				fmt.Fprintf(os.Stderr, "single active poller wait error: %v\n", err)
			}
			perfcommon.Must(err)
		}
		if event == nil || event.Events&perfcommon.ZLinkPollIn == 0 {
			continue
		}
		for drainSingleOneWay(receiver, stats, cfg.msgSize, window.ActiveAt, true) {
		}
	}

	<-sendDone

	idleDrainDeadline := time.Now().Add(perfcommon.SingleIdleDrainDuration())
	for time.Now().Before(idleDrainDeadline) {
		timeout := time.Until(idleDrainDeadline)
		if timeout <= 0 {
			break
		}
		event, err := poller.Wait(timeout)
		if err != nil {
			if os.Getenv("PERF_DEBUG") != "" {
				fmt.Fprintf(os.Stderr, "single idle drain poller wait error: %v\n", err)
			}
			perfcommon.Must(err)
		}
		if event == nil || event.Events&perfcommon.ZLinkPollIn == 0 {
			continue
		}
		for drainSingleOneWay(receiver, nil, cfg.msgSize, window.ActiveAt, false) {
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
	received, err := receiver.Recv(zlink.RecvFlagsDontWait)
	if err != nil {
		if perfcommon.IsTransient(err) {
			return false
		}
		if os.Getenv("PERF_DEBUG") != "" {
			fmt.Fprintf(os.Stderr, "single drain recv error: %v\n", err)
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
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "%s ready probe poller create\n", name)
	}
	poller := perfcommon.NewSocketPoller(receiver, perfcommon.ZLinkPollIn)
	defer poller.Close()
	perfcommon.StampProbePayload(payload)
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "%s ready probe send\n", name)
	}
	if err := send(payload); err != nil {
		perfcommon.Must(fmt.Errorf("%s ready probe send: %w", name, err))
	}
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "%s ready probe sent\n", name)
	}
	deadline := time.Now().Add(perfcommon.SingleReadyTimeout())
	for time.Now().Before(deadline) {
		timeout := time.Until(deadline)
		if timeout <= 0 {
			break
		}
		event, err := poller.Wait(timeout)
		if err != nil {
			if os.Getenv("PERF_DEBUG") != "" {
				fmt.Fprintf(os.Stderr, "%s ready probe poller wait error: %v\n", name, err)
			}
			perfcommon.Must(err)
		}
		if event == nil || event.Events&perfcommon.ZLinkPollIn == 0 {
			if os.Getenv("PERF_DEBUG") != "" {
				fmt.Fprintf(os.Stderr, "%s ready probe poller wait timeout\n", name)
			}
			continue
		}
		if os.Getenv("PERF_DEBUG") != "" {
			fmt.Fprintf(os.Stderr, "%s ready probe poller readable\n", name)
		}
		for drainSingleOneWay(receiver, nil, len(payload), time.Time{}, false) {
			if os.Getenv("PERF_DEBUG") != "" {
				fmt.Fprintf(os.Stderr, "%s ready probe drained\n", name)
			}
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
