package perfcommon

import (
	"fmt"
	"net"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"zlink"
)

var endpointCounter uint64

type Stats struct {
	count uint64
	mu    sync.Mutex
	latNs []float64
}

type Result struct {
	Throughput   float64
	Bandwidth    float64
	LatencyNs    float64
	LatencyP95Ns float64
	LatencyP99Ns float64
}

func NewStats() *Stats {
	return &Stats{}
}

func (s *Stats) Add(sentAt time.Time) {
	atomic.AddUint64(&s.count, 1)
	latencyNs := float64(time.Since(sentAt).Nanoseconds())
	s.mu.Lock()
	s.latNs = append(s.latNs, latencyNs)
	s.mu.Unlock()
}

func (s *Stats) Snapshot(duration time.Duration, msgSize int) Result {
	s.mu.Lock()
	defer s.mu.Unlock()
	sort.Float64s(s.latNs)
	count := atomic.LoadUint64(&s.count)
	return Result{
		Throughput:   float64(count) / duration.Seconds(),
		Bandwidth:    float64(count*uint64(msgSize)) / duration.Seconds() / 1_000_000.0,
		LatencyNs:    percentile(s.latNs, 50),
		LatencyP95Ns: percentile(s.latNs, 95),
		LatencyP99Ns: percentile(s.latNs, 99),
	}
}

func PrintResult(pattern, transport string, msgSize int, result Result) {
	fmt.Printf("RESULT,current,%s,%s,%d,throughput,%.2f\n", pattern, transport, msgSize, result.Throughput)
	fmt.Printf("RESULT,current,%s,%s,%d,bandwidth,%.2f\n", pattern, transport, msgSize, result.Bandwidth)
	fmt.Printf("RESULT,current,%s,%s,%d,latency,%.3f\n", pattern, transport, msgSize, result.LatencyNs/1_000_000.0)
	fmt.Printf("RESULT,current,%s,%s,%d,latency_p95,%.3f\n", pattern, transport, msgSize, result.LatencyP95Ns/1_000_000.0)
	fmt.Printf("RESULT,current,%s,%s,%d,latency_p99,%.3f\n", pattern, transport, msgSize, result.LatencyP99Ns/1_000_000.0)
}

func Must(err error) {
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func ValidateCommon(transport string, msgSize int) {
	if msgSize < 8 {
		Must(fmt.Errorf("msg-size must be >= 8, got %d", msgSize))
	}
	if strings.TrimSpace(transport) == "" {
		Must(fmt.Errorf("transport must not be empty"))
	}
}

func DebugEnabled() bool {
	return os.Getenv("PERF_DEBUG") != ""
}

const BenchmarkSocketTimeout = 10 * time.Second

func resolveSingleSocketHWM(send bool) int {
	base := 1000
	if send {
		return base
	}
	return base
}

type hwmSocket interface {
	SetSendHWM(int) error
	SetRecvHWM(int) error
}

type benchmarkSocket interface {
	SetLinger(time.Duration) error
	SetSendTimeout(time.Duration) error
	SetRecvTimeout(time.Duration) error
}

func ApplySingleHWM(socket hwmSocket) {
	if socket == nil {
		return
	}
	sndhwm := resolveSingleSocketHWM(true)
	rcvhwm := resolveSingleSocketHWM(false)
	Must(socket.SetSendHWM(sndhwm))
	Must(socket.SetRecvHWM(rcvhwm))
}

func ApplySingleBenchmarkSocketOptions(socket benchmarkSocket, transport string) {
	if socket == nil {
		return
	}
	if transport == "pgm" || transport == "epgm" {
		return
	}
	Must(socket.SetLinger(0))
	if transport == "inproc" || transport == "wss" {
		return
	}
	Must(socket.SetSendTimeout(BenchmarkSocketTimeout))
	Must(socket.SetRecvTimeout(BenchmarkSocketTimeout))
}

func UniqueTCPEndpoint(prefix string) string {
	return UniqueEndpoint("tcp", prefix)
}

type boundEndpointSocket interface {
	Bind(string) error
	LastEndpoint() (string, error)
}

func BindEndpoint(transport, prefix string) string {
	id := atomic.AddUint64(&endpointCounter, 1)
	switch transport {
	case "tcp", "tls", "ws", "wss":
		return fmt.Sprintf("%s://127.0.0.1:*", transport)
	case "inproc":
		return fmt.Sprintf("inproc://%s-%d-%d", prefix, os.Getpid(), id)
	case "ipc":
		return "ipc://*"
	default:
		return fmt.Sprintf("%s://%s-%d-%d", transport, prefix, os.Getpid(), id)
	}
}

func BindAndResolveEndpoint(sock boundEndpointSocket, transport, prefix string) string {
	if sock == nil {
		Must(fmt.Errorf("bind socket must not be nil"))
	}
	endpoint := BindEndpoint(transport, prefix)
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "bind start transport=%s endpoint=%q\n", transport, endpoint)
	}
	Must(sock.Bind(endpoint))
	if transport == "inproc" {
		return endpoint
	}
	resolved, err := sock.LastEndpoint()
	Must(err)
	resolved = strings.TrimRight(resolved, "\x00")
	if transport == "ws" || transport == "wss" {
		resolved = strings.TrimRight(resolved, "/")
	}
	if transport == "tls" || transport == "wss" {
		resolved = strings.Replace(resolved, "://127.0.0.1:", "://localhost:", 1)
	}
	if os.Getenv("PERF_DEBUG") != "" {
		fmt.Fprintf(os.Stderr, "bind resolved transport=%s endpoint=%q resolved=%q\n", transport, endpoint, resolved)
	}
	return resolved
}

func UniqueEndpoint(transport, prefix string) string {
	id := atomic.AddUint64(&endpointCounter, 1)
	switch transport {
	case "tcp", "ws":
		listener, err := net.Listen("tcp", "127.0.0.1:0")
		Must(err)
		addr := listener.Addr().(*net.TCPAddr)
		_ = listener.Close()
		return fmt.Sprintf("%s://127.0.0.1:%d", transport, addr.Port)
	case "tls", "wss":
		listener, err := net.Listen("tcp", "127.0.0.1:0")
		Must(err)
		addr := listener.Addr().(*net.TCPAddr)
		_ = listener.Close()
		return fmt.Sprintf("%s://127.0.0.1:%d", transport, addr.Port)
	case "inproc":
		return fmt.Sprintf("inproc://%s-%d-%d", prefix, os.Getpid(), id)
	case "ipc":
		return fmt.Sprintf("ipc://%s", filepath.Join(os.TempDir(), fmt.Sprintf("%s-%d-%d.sock", prefix, os.Getpid(), id)))
	default:
		return fmt.Sprintf("%s://%s-%d-%d", transport, prefix, os.Getpid(), id)
	}
}

func OpenMonitor(socket zlink.SocketTarget) *zlink.SocketMonitor {
	mon, err := zlink.OpenSocketMonitor(socket, zlink.MonitorEventConnectionReady)
	Must(err)
	return mon
}

func WaitMonitorEvent(mon *zlink.SocketMonitor) *zlink.MonitorEvent {
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
		event, ok, err := mon.TryRecv()
		if err != nil {
			if zerr, ok := err.(*zlink.ZlinkError); ok && zerr.Code == 4 {
				time.Sleep(50 * time.Millisecond)
				continue
			}
			Must(err)
		}
		if !ok || event == nil {
			time.Sleep(50 * time.Millisecond)
			continue
		}
		return event
	}
	Must(fmt.Errorf("timed out waiting for monitor event"))
	return nil
}

func PreparePayload(size int) []byte {
	return make([]byte, size)
}

func NewMessage(payload []byte) *zlink.Message {
	msg, err := zlink.NewMessage(payload)
	Must(err)
	return msg
}

func CloneMessages(parts []*zlink.Message) []*zlink.Message {
	cloned := make([]*zlink.Message, 0, len(parts))
	for _, part := range parts {
		cloned = append(cloned, NewMessage(append([]byte(nil), part.Data()...)))
	}
	return cloned
}

func IsTransient(err error) bool {
	zerr, ok := err.(*zlink.ZlinkError)
	return ok && (zerr.Code == 4 || zerr.Code == 11)
}
