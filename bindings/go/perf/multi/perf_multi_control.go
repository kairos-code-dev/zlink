package main

import (
	"bufio"
	"fmt"
	"net"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"

	"zlink.systems/zlink/perf/internal/perfcommon"
)

func waitForStartToken(msgSize int) bool {
	scanner := bufio.NewScanner(os.Stdin)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		switch {
		case line == "STOP" || line == "QUIT":
			return false
		case line == fmt.Sprintf("START,%d", msgSize):
			return true
		case strings.HasPrefix(line, "START,"):
			parts := strings.Split(line, ",")
			if len(parts) == 2 {
				size, err := strconv.Atoi(parts[1])
				if err == nil && size == msgSize {
					return true
				}
			}
		}
	}
	return false
}

func waitForStopToken() {
	scanner := bufio.NewScanner(os.Stdin)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "STOP" || line == "QUIT" {
			return
		}
	}
}

func waitForStopAsync() <-chan struct{} {
	done := make(chan struct{})
	go func() {
		defer close(done)
		waitForStopToken()
	}()
	return done
}

func listenControlEndpoint() (net.Listener, string) {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	perfcommon.Must(err)
	return listener, "tcp://" + listener.Addr().String()
}

func dialControlEndpoint(endpoint string, timeout time.Duration) net.Conn {
	target := strings.TrimPrefix(endpoint, "tcp://")
	conn, err := net.DialTimeout("tcp", target, timeout)
	perfcommon.Must(err)
	return conn
}

func writeControlLine(conn net.Conn, format string, args ...interface{}) {
	_, err := fmt.Fprintf(conn, format+"\n", args...)
	perfcommon.Must(err)
}

func newStdinReader() *os.File {
	return os.Stdin
}

func readSpotClientStart(conn net.Conn, mu *sync.Mutex, directStart *bool, directStartSize *int) {
	scanner := bufio.NewScanner(conn)
	for scanner.Scan() {
		text := strings.TrimSpace(scanner.Text())
		if strings.HasPrefix(text, "START,") {
			size, err := strconv.Atoi(strings.TrimPrefix(text, "START,"))
			if err == nil {
				mu.Lock()
				*directStart = true
				*directStartSize = size
				mu.Unlock()
			}
		}
	}
}

func readSpotClientStdin(cfg multiConfig, mu *sync.Mutex, runnerStart *bool, runnerControlConnected *bool) {
	stdin := bufio.NewScanner(newStdinReader())
	for stdin.Scan() {
		text := strings.TrimSpace(stdin.Text())
		switch {
		case text == fmt.Sprintf("START,%d", cfg.msgSize):
			mu.Lock()
			*runnerStart = true
			mu.Unlock()
		case strings.HasPrefix(text, "CONTROL_CONNECTED,"):
			mu.Lock()
			*runnerControlConnected = true
			mu.Unlock()
		}
	}
}

func waitSpotClientControlReady(
	readyConnCh <-chan net.Conn,
	mu *sync.Mutex,
	runnerControlConnected *bool,
) net.Conn {
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	var readyConn net.Conn
	for time.Now().Before(deadline) {
		select {
		case readyConn = <-readyConnCh:
		default:
		}
		mu.Lock()
		ok := readyConn != nil && *runnerControlConnected
		mu.Unlock()
		if ok {
			return readyConn
		}
		time.Sleep(time.Millisecond)
	}
	perfcommon.Must(fmt.Errorf("spot client control handshake timeout"))
	return nil
}

func waitSpotClientStartBarrier(
	cfg multiConfig,
	mu *sync.Mutex,
	runnerStart *bool,
	directStart *bool,
	directStartSize *int,
) {
	deadline := time.Now().Add(perfcommon.MultiReadyTimeout())
	for time.Now().Before(deadline) {
		mu.Lock()
		ok := *runnerStart && *directStart && *directStartSize == cfg.msgSize
		mu.Unlock()
		if ok {
			return
		}
		time.Sleep(time.Millisecond)
	}
	perfcommon.Must(fmt.Errorf("spot start handshake timeout"))
}

func activeDeadline(duration time.Duration) perfcommon.BenchmarkWindow {
	return perfcommon.NewBenchmarkWindow(duration)
}
