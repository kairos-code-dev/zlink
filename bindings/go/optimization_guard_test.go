package zlink

import (
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"testing"
)

var aggregateHotPathSymbols = []string{
	"zlink_send",
	"zlink_recv",
	"zlink_publish",
	"zlink_subscribe",
	"zlink_router_recv",
	"zlink_dealer_request",
	"zlink_router_request",
	"zlink_router_reply",
	"zlink_spot_send_channel",
	"zlink_spot_request_channel",
	"zlink_spot_request_spot",
	"zlink_spot_request_router",
	"zlink_spot_publish",
	"zlink_spot_subscribe",
	"zlink_spot_send_spot",
	"zlink_spot_reply_spot",
	"zlink_spot_reply_router",
	"zlink_spot_recv",
}

var requiredPartSymbols = []string{
	"zlink_send_part",
	"zlink_recv_part",
	"zlink_publish_part",
	"zlink_subscribe_part",
	"zlink_router_recv_part",
	"zlink_dealer_request_part",
	"zlink_router_request_part",
	"zlink_router_reply_part",
	"zlink_spot_publish_part",
	"zlink_spot_subscribe_part",
	"zlink_spot_request_channel_part",
	"zlink_spot_request_spot_part",
	"zlink_spot_reply_router_part",
}

func bindingRoot(t *testing.T) string {
	t.Helper()
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("runtime.Caller failed")
	}
	return filepath.Dir(file)
}

func implementationGoFiles(t *testing.T) []string {
	t.Helper()
	root := bindingRoot(t)
	var files []string
	err := filepath.WalkDir(root, func(path string, entry os.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if entry.IsDir() {
			name := entry.Name()
			if name == "perf" || name == "samples" || name == "tests" || name == "codec" {
				return filepath.SkipDir
			}
			return nil
		}
		if strings.HasSuffix(path, ".go") && !strings.HasSuffix(path, "_test.go") {
			files = append(files, path)
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	return files
}

func TestOptimizationGuardUsesPartSubstrate(t *testing.T) {
	files := implementationGoFiles(t)
	var all strings.Builder
	for _, path := range files {
		body, err := os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		all.Write(body)
		all.WriteByte('\n')
	}
	source := all.String()
	for _, symbol := range requiredPartSymbols {
		if !strings.Contains(source, symbol) {
			t.Fatalf("missing required helper substrate symbol %s", symbol)
		}
	}

	var violations []string
	for _, path := range files {
		bodyBytes, err := os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		body := string(bodyBytes)
		for _, symbol := range aggregateHotPathSymbols {
			pattern := regexp.MustCompile(`\bC\.` + regexp.QuoteMeta(symbol) + `\s*\(`)
			for _, loc := range pattern.FindAllStringIndex(body, -1) {
				if strings.HasPrefix(body[loc[0]:], "C."+symbol+"_part") {
					continue
				}
				violations = append(violations, filepath.Base(path)+":"+symbol)
			}
		}
	}
	if len(violations) != 0 {
		t.Fatalf("aggregate hot-path calls found: %v", violations)
	}
}

func TestOptimizationGuardAvoidsRuntimeFinalizersAndSleeps(t *testing.T) {
	for _, path := range implementationGoFiles(t) {
		bodyBytes, err := os.ReadFile(path)
		if err != nil {
			t.Fatal(err)
		}
		body := string(bodyBytes)
		if strings.Contains(body, "runtime.SetFinalizer") {
			t.Fatalf("%s uses runtime.SetFinalizer; explicit close owns lifecycle", path)
		}
		if strings.Contains(body, "time.Sleep(") {
			t.Fatalf("%s uses time.Sleep in binding implementation hot path", path)
		}
	}
}
