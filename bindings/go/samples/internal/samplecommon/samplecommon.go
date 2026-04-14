package samplecommon

import (
	"errors"
	"fmt"
	"net"
	"os"
	"runtime"
	"strings"
	"sync/atomic"
	"syscall"
	"time"

	"zlink"
)

var counter uint64

func Must(err error) {
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func UniqueTCP(prefix string) string {
	id := atomic.AddUint64(&counter, 1)
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	Must(err)
	addr := listener.Addr().(*net.TCPAddr)
	_ = listener.Close()
	_ = prefix
	_ = id
	return fmt.Sprintf("tcp://127.0.0.1:%d", addr.Port)
}

func DialEndpoint(endpoint string) net.Conn {
	addr := strings.TrimPrefix(endpoint, "tcp://")
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	Must(err)
	return conn
}

func OpenMonitor(socket zlink.SocketTarget) *zlink.SocketMonitor {
	mon, err := zlink.OpenSocketMonitor(socket, zlink.MonitorEventConnectionReady)
	Must(err)
	return mon
}

func WaitConnected(serverMon, clientMon *zlink.SocketMonitor) {
	WaitMonitorEvent(serverMon)
	WaitMonitorEvent(clientMon)
}

func WaitMonitorEvent(mon *zlink.SocketMonitor) *zlink.MonitorEvent {
	type result struct {
		event *zlink.MonitorEvent
		err   error
	}
	ch := make(chan result, 1)
	go func() {
		event, err := mon.Recv()
		ch <- result{event: event, err: err}
	}()
	select {
	case out := <-ch:
		Must(out.err)
		return out.event
	case <-time.After(5 * time.Second):
		fmt.Fprintln(os.Stderr, "timed out waiting for monitor event")
		os.Exit(1)
		return nil
	}
}

func Message(text string) *zlink.Message {
	msg, err := zlink.NewMessage([]byte(text))
	Must(err)
	return msg
}

func MustStep(step string, err error) {
	if err != nil {
		Must(fmt.Errorf("%s: %w", step, err))
	}
}

func WaitUntil(timeout time.Duration, description string, predicate func() bool) {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if predicate() {
			return
		}
		runtime.Gosched()
	}
	Must(fmt.Errorf("timed out waiting for %s", description))
}

func WaitSpotPeerConnected(node *zlink.SpotNode, timeout time.Duration) {
	WaitUntil(timeout, "spot peer connection", func() bool {
		status, err := node.StatusSnapshot()
		return err == nil && status != nil && status.ConnectedPeerCount > 0
	})
}

func WaitServiceEvent(monitor *zlink.ServiceMonitor, eventMask uint32) *zlink.ServiceMonitorEvent {
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		event, err := monitor.Recv()
		if isTemporaryEmpty(err) {
			runtime.Gosched()
			continue
		}
		Must(err)
		if event != nil && (event.EventType&zlink.ServiceMonitorEventType(eventMask)) != 0 {
			return event
		}
	}
	Must(fmt.Errorf("timed out waiting for service monitor event"))
	return nil
}

func WaitTopologyEntry(fetch func() ([]zlink.RegistryTopologyEntry, error), serviceName string) *zlink.RegistryTopologyEntry {
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		entries, err := fetch()
		Must(err)
		for i := range entries {
			if entries[i].ServiceName == serviceName {
				return &entries[i]
			}
		}
		runtime.Gosched()
	}
	Must(fmt.Errorf("timed out waiting for topology entry"))
	return nil
}

func isTemporaryEmpty(err error) bool {
	if err == nil {
		return false
	}
	var zerr zlink.ZlinkError
	if !errors.As(err, &zerr) {
		return false
	}
	return zerr.InternalErrno() == int(syscall.EAGAIN)
}
