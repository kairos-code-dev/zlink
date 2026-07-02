// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <errno.h>
#include <stdint.h>
#include "zlink.h"
*/
import "C"

func querySpotNodePeers(fetch func(*C.zlink_spot_node_peer_entry_t, *C.size_t) error) ([]SpotNodePeerEntry, error) {
	return queryCountedSnapshot(
		func() (int, error) {
			var count C.size_t
			if err := fetch(nil, &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		func(native []C.zlink_spot_node_peer_entry_t) (int, error) {
			count := C.size_t(len(native))
			if err := fetch(&native[0], &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		spotNodePeerEntryFromC,
	)
}

func querySpotNodeSubjects(fetch func(*C.zlink_spot_node_subject_entry_t, *C.size_t) error) ([]SpotNodeSubjectEntry, error) {
	return queryCountedSnapshot(
		func() (int, error) {
			var count C.size_t
			if err := fetch(nil, &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		func(native []C.zlink_spot_node_subject_entry_t) (int, error) {
			count := C.size_t(len(native))
			if err := fetch(&native[0], &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		spotNodeSubjectEntryFromC,
	)
}

func querySpotNodeInternalSockets(fetch func(*C.zlink_spot_node_socket_entry_t, *C.size_t) error) ([]SpotNodeSocketEntry, error) {
	return queryCountedSnapshot(
		func() (int, error) {
			var count C.size_t
			if err := fetch(nil, &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		func(native []C.zlink_spot_node_socket_entry_t) (int, error) {
			count := C.size_t(len(native))
			if err := fetch(&native[0], &count); err != nil {
				return 0, err
			}
			return int(count), nil
		},
		spotNodeSocketEntryFromC,
	)
}

func queryCountedSnapshot[CEntry any, T any](probe func() (int, error), fill func([]CEntry) (int, error), convert func(CEntry) T) ([]T, error) {
	const maxSnapshotRetries = 4

	for attempt := 0; attempt < maxSnapshotRetries; attempt++ {
		count, err := probe()
		if err != nil {
			return nil, err
		}
		if count == 0 {
			return nil, nil
		}

		native := make([]CEntry, count)
		count, err = fill(native)
		if err != nil {
			if isNativeErrorCode(err, int(C.ENOBUFS)) {
				continue
			}
			return nil, err
		}

		out := make([]T, count)
		for i := 0; i < count; i++ {
			out[i] = convert(native[i])
		}
		return out, nil
	}

	return nil, lastError()
}
