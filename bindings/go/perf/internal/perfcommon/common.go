package perfcommon

import (
	"errors"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"

	"zlink.systems/zlink"
)

var endpointCounter uint64

type Stats struct {
	count uint64
	mu    sync.Mutex
	latNs []float64
	sumNs float64
}

type Result struct {
	Throughput   float64
	Bandwidth    float64
	LatencyNs    float64
	LatencyP95Ns float64
	LatencyP99Ns float64
}

func NewStats() *Stats {
	return &Stats{
		latNs: make([]float64, 0, latencySampleCap()),
	}
}

func (s *Stats) Add(sentAt time.Time) {
	s.AddLatencyNs(float64(time.Since(sentAt).Nanoseconds()))
}

func (s *Stats) AddLatencyNs(latencyNs float64) {
	atomic.AddUint64(&s.count, 1)
	s.mu.Lock()
	s.sumNs += latencyNs
	if len(s.latNs) < latencySampleCap() {
		s.latNs = append(s.latNs, latencyNs)
	}
	s.mu.Unlock()
}

func (s *Stats) Snapshot(duration time.Duration, msgSize int) Result {
	s.mu.Lock()
	defer s.mu.Unlock()
	sort.Float64s(s.latNs)
	count := atomic.LoadUint64(&s.count)
	latencyMean := 0.0
	if count > 0 {
		latencyMean = s.sumNs / float64(count)
	}
	return Result{
		Throughput:   float64(count) / duration.Seconds(),
		Bandwidth:    float64(count*uint64(msgSize)) / duration.Seconds() / 1_000_000.0,
		LatencyNs:    latencyMean,
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

func latencySampleCap() int {
	raw := os.Getenv("PERF_SINGLE_LATENCY_SAMPLE_CAP")
	if raw == "" {
		raw = os.Getenv("PERF_MULTI_LATENCY_SAMPLE_CAP")
	}
	if raw == "" {
		return 200000
	}
	value, err := strconv.Atoi(raw)
	if err != nil || value <= 0 {
		return 200000
	}
	return value
}

func Must(err error) {
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func ValidateCommon(transport string, msgSize int) {
	if msgSize < MetricHeaderSize {
		Must(fmt.Errorf("msg-size must be >= %d, got %d", MetricHeaderSize, msgSize))
	}
	if strings.TrimSpace(transport) == "" {
		Must(fmt.Errorf("transport must not be empty"))
	}
}

const BenchmarkSocketTimeout = 200 * time.Millisecond

func singleSocketTimeout(send bool) time.Duration {
	if send {
		return durationFromEnv("PERF_SINGLE_SNDTIMEO_MS", BenchmarkSocketTimeout)
	}
	return durationFromEnv("PERF_SINGLE_RCVTIMEO_MS", BenchmarkSocketTimeout)
}

func multiSocketTimeout(send bool) time.Duration {
	if send {
		return durationFromEnv("PERF_MULTI_SNDTIMEO_MS", BenchmarkSocketTimeout)
	}
	return durationFromEnv("PERF_MULTI_RCVTIMEO_MS", BenchmarkSocketTimeout)
}

func SingleSendTimeout() time.Duration {
	return singleSocketTimeout(true)
}

func SingleRecvTimeout() time.Duration {
	return singleSocketTimeout(false)
}

func MultiSendTimeout() time.Duration {
	return multiSocketTimeout(true)
}

func MultiRecvTimeout() time.Duration {
	return multiSocketTimeout(false)
}

func resolveSingleSocketHWM(send bool) int {
	return resolveSocketHWM("PERF_SINGLE_HWM", "PERF_SINGLE_SNDHWM", "PERF_SINGLE_RCVHWM", send)
}

func resolveMultiSocketHWM(pattern string, send bool) int {
	fallback := 100
	if strings.EqualFold(pattern, "MULTI_STREAM") {
		fallback = 10
	}
	return resolveSocketHWMWithFallback(
		"PERF_MULTI_HWM",
		"PERF_MULTI_SNDHWM",
		"PERF_MULTI_RCVHWM",
		send,
		fallback,
	)
}

func resolveSocketHWM(baseEnv, sendEnv, recvEnv string, send bool) int {
	return resolveSocketHWMWithFallback(baseEnv, sendEnv, recvEnv, send, 1000)
}

func resolveSocketHWMWithFallback(baseEnv, sendEnv, recvEnv string, send bool, fallback int) int {
	base := resolveIntEnv(baseEnv, fallback)
	if send {
		return resolveIntEnv(sendEnv, base)
	}
	return resolveIntEnv(recvEnv, base)
}

func resolveIntEnv(name string, fallback int) int {
	raw := os.Getenv(name)
	if raw == "" {
		return fallback
	}
	value, err := strconv.Atoi(raw)
	if err != nil || value <= 0 {
		return fallback
	}
	return value
}

type hwmSocket interface {
	SetSendHWM(int) error
	SetRecvHWM(int) error
}

type spotNodeAdmission interface {
	SetPubSubHWM(int) error
	SetRouterHWM(int) error
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

func ApplySingleSpotNodeAdmission(node spotNodeAdmission) {
	if node == nil {
		return
	}
	Must(node.SetPubSubHWM(resolveSingleSocketHWM(true)))
	Must(node.SetRouterHWM(resolveSingleSocketHWM(false)))
}

func ApplyMultiHWM(socket hwmSocket, pattern string) {
	if socket == nil {
		return
	}
	sndhwm := resolveMultiSocketHWM(pattern, true)
	rcvhwm := resolveMultiSocketHWM(pattern, false)
	Must(socket.SetSendHWM(sndhwm))
	Must(socket.SetRecvHWM(rcvhwm))
}

func ApplyMultiSpotNodeAdmission(node spotNodeAdmission, pattern string) {
	if node == nil {
		return
	}
	Must(node.SetPubSubHWM(resolveMultiSocketHWM(pattern, true)))
	Must(node.SetRouterHWM(resolveMultiSocketHWM(pattern, false)))
}

func ApplySingleBenchmarkSocketOptions(socket benchmarkSocket, transport string) {
	if socket == nil {
		return
	}
	if transport == "pgm" || transport == "epgm" {
		return
	}
	Must(socket.SetLinger(0))
	Must(socket.SetSendTimeout(singleSocketTimeout(true)))
	Must(socket.SetRecvTimeout(singleSocketTimeout(false)))
}

func ApplyMultiBenchmarkSocketOptions(socket benchmarkSocket, transport string) {
	if socket == nil {
		return
	}
	if transport == "pgm" || transport == "epgm" {
		return
	}
	Must(socket.SetLinger(0))
	Must(socket.SetSendTimeout(multiSocketTimeout(true)))
	Must(socket.SetRecvTimeout(multiSocketTimeout(false)))
}

type boundEndpointSocket interface {
	Bind(string) error
	LastEndpoint() (string, error)
}

func BindEndpoint(transport, prefix string) string {
	switch transport {
	case "tcp", "tls", "ws", "wss":
		return UniqueEndpoint(transport, prefix)
	case "inproc":
		id := atomic.AddUint64(&endpointCounter, 1)
		return fmt.Sprintf("inproc://%s-%d-%d", prefix, os.Getpid(), id)
	case "ipc":
		return UniqueEndpoint(transport, prefix)
	default:
		id := atomic.AddUint64(&endpointCounter, 1)
		return fmt.Sprintf("%s://%s-%d-%d", transport, prefix, os.Getpid(), id)
	}
}

func FinalizeResult(pattern string, msgSize int, result Result) Result {
	factor := 1.0
	if isEchoPattern(pattern) {
		factor = 2.0
	}
	result.Bandwidth = result.Throughput * float64(msgSize) * factor / 1_000_000.0
	return result
}

func isEchoPattern(pattern string) bool {
	switch pattern {
	case "MULTI_DEALER_ROUTER", "MULTI_ROUTER_ROUTER", "MULTI_STREAM", "MULTI_SPOT_REQREP":
		return true
	default:
		return false
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

func PreparePayload(size int) []byte {
	return make([]byte, size)
}

func NewMessage(payload []byte) *zlink.Message {
	msg, err := zlink.NewMessage(payload)
	Must(err)
	return msg
}

func IsTransient(err error) bool {
	var zerr zlink.ZlinkError
	if !errors.As(err, &zerr) {
		return false
	}
	switch zerr.InternalErrno() {
	case int(syscall.EAGAIN), int(syscall.EINTR), int(syscall.ENOENT),
		int(syscall.ENOTCONN), int(syscall.EHOSTUNREACH),
		int(syscall.ECONNREFUSED), int(syscall.ENETUNREACH):
		return true
	default:
		return false
	}
}
