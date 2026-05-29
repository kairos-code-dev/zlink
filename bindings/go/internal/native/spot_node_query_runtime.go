// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include "zlink.h"
*/
import "C"

func (n *SpotNode) Status() (*SpotNodeStatus, error) {
	if n == nil || n.closed {
		return nil, stateError("spot node is closed")
	}
	var raw C.zlink_spot_node_status_t
	if err := checkRC(C.zlink_spot_node_status(n.raw(), &raw)); err != nil {
		return nil, err
	}
	return spotNodeStatusFromC(raw), nil
}

func (n *SpotNode) Peers() ([]SpotNodePeerEntry, error) {
	if n == nil || n.closed {
		return nil, stateError("spot node is closed")
	}
	return querySpotNodePeers(func(entries *C.zlink_spot_node_peer_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_spot_node_peers(n.raw(), nil, entries, count))
	})
}

func (n *SpotNode) PeersQuery(filter *SpotNodePeerFilter) ([]SpotNodePeerEntry, error) {
	if n == nil || n.closed {
		return nil, stateError("spot node is closed")
	}
	var rawFilter *C.zlink_spot_node_peer_filter_t
	var cfilter C.zlink_spot_node_peer_filter_t
	if filter != nil {
		if err := validateSpotNodePeerFilter(*filter); err != nil {
			return nil, err
		}
		cfilter = filter.toC()
		rawFilter = &cfilter
	}
	return querySpotNodePeers(func(entries *C.zlink_spot_node_peer_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_spot_node_peers(n.raw(), rawFilter, entries, count))
	})
}

func (n *SpotNode) Subjects(filters ...*SpotNodeSubjectFilter) ([]SpotNodeSubjectEntry, error) {
	if n == nil || n.closed {
		return nil, stateError("spot node is closed")
	}
	var filter *SpotNodeSubjectFilter
	if len(filters) > 0 {
		filter = filters[0]
	}
	var rawFilter *C.zlink_spot_node_subject_filter_t
	var cfilter C.zlink_spot_node_subject_filter_t
	if filter != nil {
		if err := validateSpotNodeSubjectFilter(*filter); err != nil {
			return nil, err
		}
		cfilter = filter.toC()
		rawFilter = &cfilter
	}
	return querySpotNodeSubjects(func(entries *C.zlink_spot_node_subject_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_spot_node_subjects(n.raw(), rawFilter, entries, count))
	})
}

func (n *SpotNode) InternalSockets(filter *SpotNodeSocketFilter) ([]SpotNodeSocketEntry, error) {
	if n == nil || n.closed {
		return nil, stateError("spot node is closed")
	}
	var rawFilter *C.zlink_spot_node_socket_filter_t
	var cfilter C.zlink_spot_node_socket_filter_t
	if filter != nil {
		if err := validateSpotNodeSocketFilter(*filter); err != nil {
			return nil, err
		}
		cfilter = filter.toC()
		rawFilter = &cfilter
	}
	return querySpotNodeInternalSockets(func(entries *C.zlink_spot_node_socket_entry_t, count *C.size_t) error {
		return checkRC(C.zlink_spot_node_internal_sockets(n.raw(), rawFilter, entries, count))
	})
}
