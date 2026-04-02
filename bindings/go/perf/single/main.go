package main

import (
	"flag"
	"strings"
	"time"

	"zlink/perf/internal/perfcommon"
)

type benchmarkConfig struct {
	pattern   string
	transport string
	msgSize   int
	warmup    time.Duration
	duration  time.Duration
	recvMode  string
}

var (
	pattern   = flag.String("pattern", "PAIR", "")
	transport = flag.String("transport", "tcp", "")
	msgSize   = flag.Int("msg-size", 64, "")
	warmup    = flag.Int("warmup", 2, "")
	duration  = flag.Int("duration", 5, "")
	recvMode  = flag.String("recv", "callback", "")
)

func main() {
	flag.Parse()

	cfg := benchmarkConfig{
		pattern:   strings.ToUpper(*pattern),
		transport: strings.ToLower(*transport),
		msgSize:   *msgSize,
		warmup:    time.Duration(*warmup) * time.Second,
		duration:  time.Duration(*duration) * time.Second,
		recvMode:  strings.ToLower(*recvMode),
	}
	perfcommon.ValidateCommon(cfg.transport, cfg.msgSize, cfg.recvMode)

	var result perfcommon.Result
	switch cfg.pattern {
	case "PAIR":
		result = runPair(cfg)
	case "PUBSUB":
		result = runPubSub(cfg)
	case "DEALER_DEALER":
		result = runDealerDealer(cfg)
	case "DEALER_ROUTER":
		result = runDealerRouter(cfg)
	case "STREAM":
		result = runStream(cfg)
	case "ROUTER_ROUTER":
		result = runRouterRouter(cfg)
	case "SPOT":
		result = runSpot(cfg)
	default:
		perfcommon.Must(
			&unsupportedPatternError{pattern: cfg.pattern},
		)
	}

	perfcommon.PrintResult(cfg.pattern, cfg.transport, cfg.msgSize, result)
}

type unsupportedPatternError struct {
	pattern string
}

func (e *unsupportedPatternError) Error() string {
	return "unsupported single perf pattern: " + e.pattern
}
