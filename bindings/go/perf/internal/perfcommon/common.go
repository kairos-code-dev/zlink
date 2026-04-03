package perfcommon

import (
	"encoding/binary"
	"fmt"
	"net"
	"os"
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
	latUs []float64
}

type Result struct {
	Throughput float64
	Bandwidth  float64
	Latency    float64
	LatencyP95 float64
	LatencyP99 float64
}

func NewStats() *Stats {
	return &Stats{}
}

func (s *Stats) Add(sentAt time.Time) {
	atomic.AddUint64(&s.count, 1)
	latencyUs := float64(time.Since(sentAt).Nanoseconds()) / 1000.0
	s.mu.Lock()
	s.latUs = append(s.latUs, latencyUs)
	s.mu.Unlock()
}

func (s *Stats) Snapshot(duration time.Duration, msgSize int) Result {
	s.mu.Lock()
	defer s.mu.Unlock()
	sort.Float64s(s.latUs)
	count := atomic.LoadUint64(&s.count)
	return Result{
		Throughput: float64(count) / duration.Seconds() / 1000.0,
		Bandwidth:  float64(count*uint64(msgSize)) / duration.Seconds() / (1024.0 * 1024.0),
		Latency:    percentile(s.latUs, 50),
		LatencyP95: percentile(s.latUs, 95),
		LatencyP99: percentile(s.latUs, 99),
	}
}

func PrintResult(pattern, transport string, msgSize int, result Result) {
	fmt.Printf("RESULT,current,%s,%s,%d,throughput,%.2f\n", pattern, transport, msgSize, result.Throughput)
	fmt.Printf("RESULT,current,%s,%s,%d,bandwidth,%.2f\n", pattern, transport, msgSize, result.Bandwidth)
	fmt.Printf("RESULT,current,%s,%s,%d,latency,%.2f\n", pattern, transport, msgSize, result.Latency)
	fmt.Printf("RESULT,current,%s,%s,%d,latency_p95,%.2f\n", pattern, transport, msgSize, result.LatencyP95)
	fmt.Printf("RESULT,current,%s,%s,%d,latency_p99,%.2f\n", pattern, transport, msgSize, result.LatencyP99)
	fmt.Println("| lib | pattern | transport | size | throughput | bandwidth | latency | latency_p95 | latency_p99 |")
	fmt.Println("| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |")
	fmt.Printf("| current | %s | %s | %d | %.2f | %.2f | %.2f | %.2f | %.2f |\n",
		pattern, transport, msgSize, result.Throughput, result.Bandwidth,
		result.Latency, result.LatencyP95, result.LatencyP99)
}

func Unsupported(pattern, transport, reason string) {
	fmt.Printf("UNSUPPORTED,current,%s,%s,%s\n", pattern, transport, reason)
}

func Must(err error) {
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func ValidateCommon(transport string, msgSize int, recvMode string) {
	if msgSize < 8 {
		Must(fmt.Errorf("msg-size must be >= 8, got %d", msgSize))
	}
	if recvMode != "recv" && recvMode != "callback" {
		Must(fmt.Errorf("unsupported recv mode %q", recvMode))
	}
	if transport != "tcp" {
		Must(fmt.Errorf("unsupported transport %q: go perf currently supports tcp only", transport))
	}
}

func UniqueTCPEndpoint(prefix string) string {
	id := atomic.AddUint64(&endpointCounter, 1)
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
	if idx := strings.IndexByte(addr, '?'); idx >= 0 {
		addr = addr[:idx]
	}
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	Must(err)
	return conn
}

func OpenMonitor(socket any) *zlink.SocketMonitor {
	mon, err := zlink.OpenSocketMonitor(socket, zlink.MonitorEventConnectionReady)
	Must(err)
	return mon
}

func WaitConnected(serverMon, clientMon *zlink.SocketMonitor) {
	WaitMonitorEvent(serverMon)
	WaitMonitorEvent(clientMon)
}

func WaitMonitorEvent(mon *zlink.SocketMonitor) *zlink.MonitorEvent {
	deadline := time.Now().Add(5 * time.Second)
	for time.Now().Before(deadline) {
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
			if out.err == nil {
				return out.event
			}
			if zerr, ok := out.err.(*zlink.ZlinkError); ok && zerr.Code == 4 {
				continue
			}
			Must(out.err)
		case <-time.After(500 * time.Millisecond):
		}
	}
	Must(fmt.Errorf("timed out waiting for monitor event"))
	return nil
}

func PreparePayload(size int) []byte {
	return make([]byte, size)
}

func StampPayload(payload []byte) {
	binary.LittleEndian.PutUint64(payload[:8], uint64(time.Now().UnixNano()))
}

func SentAtFromBytes(data []byte) (time.Time, bool) {
	if len(data) < 8 {
		return time.Time{}, false
	}
	return time.Unix(0, int64(binary.LittleEndian.Uint64(data[:8]))), true
}

func SentAtFromMessage(part *zlink.Message) (time.Time, bool) {
	if part == nil {
		return time.Time{}, false
	}
	data := part.Data()
	if len(data) < 8 {
		return time.Time{}, false
	}
	header := [8]byte{}
	copy(header[:], data[:8])
	return time.Unix(0, int64(binary.LittleEndian.Uint64(header[:]))), true
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

func percentile(values []float64, pct float64) float64 {
	if len(values) == 0 {
		return 0
	}
	idx := int((pct / 100.0) * float64(len(values)-1))
	return values[idx]
}
