// SPDX-License-Identifier: MPL-2.0

package native

import "strings"

func validateFixedCString(name string, value string) error {
	if strings.IndexByte(value, 0) >= 0 {
		return validationError("string contains null byte")
	}
	if len(value) > maxFixedCStringFieldSize {
		return validationError("%s length %d exceeds %d", name, len(value), maxFixedCStringFieldSize)
	}
	return nil
}

func validateEndpointString(value string) error {
	return validateFixedCString("endpoint", value)
}

func validateChannelName(value string) error {
	return validateFixedCString("channel_name", value)
}

func validateSpotNodeSubjectFilter(filter SpotNodeSubjectFilter) error {
	if filter.Subject == nil {
		return nil
	}
	return validateFixedCString("subject", *filter.Subject)
}

func validateSpotNodePeerFilter(filter SpotNodePeerFilter) error {
	if filter.PeerEndpoint == nil {
		return nil
	}
	return validateFixedCString("peer_endpoint", *filter.PeerEndpoint)
}

func validateSpotNodeSocketFilter(filter SpotNodeSocketFilter) error {
	if filter.SocketName == nil || *filter.SocketName == "" {
		return nil
	}
	if strings.IndexByte(*filter.SocketName, 0) >= 0 {
		return validationError("socket_name contains null byte")
	}
	if len(*filter.SocketName) > 63 {
		return validationError("socket_name length %d exceeds 63", len(*filter.SocketName))
	}
	return nil
}

func validateRegistryServiceSummaryFilter(filter RegistryServiceSummaryFilter) error {
	if filter.ChannelName == nil {
		return nil
	}
	return validateFixedCString("channel_name", *filter.ChannelName)
}

func validateRegistryTopologyFilter(filter RegistryTopologyFilter) error {
	if filter.ChannelName != nil {
		if err := validateFixedCString("channel_name", *filter.ChannelName); err != nil {
			return err
		}
	}
	if filter.RoutingID != nil && filter.RoutingID.Size() == 0 {
		return validationError("routing_id is empty")
	}
	return nil
}
