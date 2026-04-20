package main

import (
	"flag"
	"time"

	"zlink/perf/internal/perfcommon"
)

type benchmarkConfig struct {
	pattern   string
	transport string
	msgSize   int
	warmup    time.Duration
	duration  time.Duration
}

var (
	pattern   = flag.String("pattern", "PAIR", "")
	transport = flag.String("transport", "tcp", "")
	msgSize   = flag.Int("msg-size", 64, "")
	warmup    = flag.Int("warmup", 2, "")
	duration  = flag.Int("duration", 5, "")
)

func main() {
	flag.Parse()

	loaded := perfcommon.LoadSingleConfig(
		*pattern,
		*transport,
		*msgSize,
		*warmup,
		*duration,
	)
	cfg := benchmarkConfig{
		pattern:   loaded.Pattern,
		transport: loaded.Transport,
		msgSize:   loaded.MsgSize,
		warmup:    loaded.Warmup,
		duration:  loaded.Duration,
	}

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
	case "ROUTER_ROUTER":
		result = runRouterRouter(cfg)
	case "SPOT":
		result = runSpot(cfg)
	default:
		perfcommon.Must(
			&unsupportedPatternError{pattern: cfg.pattern},
		)
	}

	result = perfcommon.FinalizeResult(cfg.pattern, cfg.msgSize, result)
	perfcommon.PrintResult(cfg.pattern, cfg.transport, cfg.msgSize, result)
}

type unsupportedPatternError struct {
	pattern string
}

func (e *unsupportedPatternError) Error() string {
	return "unsupported single perf pattern: " + e.pattern
}
