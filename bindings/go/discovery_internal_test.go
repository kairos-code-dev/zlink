package zlink

import (
	"syscall"
	"testing"
)

func TestQueryCountedSnapshotRetriesENOBUFS(t *testing.T) {
	fetchCalls := 0
	got, err := queryCountedSnapshot(
		func() (int, error) {
			fetchCalls++
			switch fetchCalls {
			case 1:
				return 1, nil
			case 3:
				return 2, nil
			default:
				t.Fatalf("unexpected probe call %d", fetchCalls)
				return 0, nil
			}
		},
		func(values []int) (int, error) {
			fetchCalls++
			switch fetchCalls {
			case 2:
				return 0, &ZlinkError{Kind: ErrorKindNative, Code: int(syscall.ENOBUFS), Message: "No buffer space available"}
			case 4:
				values[0] = 11
				values[1] = 29
				return 2, nil
			default:
				t.Fatalf("unexpected fill call %d", fetchCalls)
				return 0, nil
			}
		},
		func(value int) int { return value },
	)
	if err != nil {
		t.Fatalf("queryCountedSnapshot() error = %v", err)
	}
	if len(got) != 2 || got[0] != 11 || got[1] != 29 {
		t.Fatalf("queryCountedSnapshot() = %v, want [11 29]", got)
	}
}
