// SPDX-License-Identifier: MPL-2.0

package native

/*
#include <stdint.h>
#include <stdlib.h>
#include "zlink.h"
*/
import "C"

import "unsafe"

func (n *SpotNode) SetOption(option SpotNodeOption, value int) error {
	handle, err := n.handleOrError()
	if err != nil {
		return err
	}
	raw := C.int(value)
	return configErrorFromResult(C.zlink_set_spot_node_option(
		handle,
		C.zlink_spot_node_option_t(option),
		unsafe.Pointer(&raw),
		C.size_t(C.sizeof_int),
	))
}

func (n *SpotNode) Option(option SpotNodeOption) (int, error) {
	handle, err := n.handleOrError()
	if err != nil {
		return 0, err
	}
	var raw C.int
	size := C.size_t(C.sizeof_int)
	if err := configErrorFromResult(C.zlink_get_spot_node_option(
		handle,
		C.zlink_spot_node_option_t(option),
		unsafe.Pointer(&raw),
		&size,
	)); err != nil {
		return 0, err
	}
	return int(raw), nil
}

func (n *SpotNode) SetRouterHighWaterMark(value int) error {
	return n.SetOption(SpotNodeOptionRouterHighWaterMark, value)
}

func (n *SpotNode) RouterHighWaterMark() (int, error) {
	return n.Option(SpotNodeOptionRouterHighWaterMark)
}

func (n *SpotNode) SetPubSubHighWaterMark(value int) error {
	return n.SetOption(SpotNodeOptionPubSubHighWaterMark, value)
}

func (n *SpotNode) PubSubHighWaterMark() (int, error) {
	return n.Option(SpotNodeOptionPubSubHighWaterMark)
}

func (n *SpotNode) SetRouterHwmProfile(value AutoHwmProfile) error {
	return n.SetOption(SpotNodeOptionRouterHwmProfile, int(value))
}

func (n *SpotNode) RouterHwmProfile() (AutoHwmProfile, error) {
	value, err := n.Option(SpotNodeOptionRouterHwmProfile)
	return AutoHwmProfile(value), err
}

func (n *SpotNode) SetPubSubHwmProfile(value AutoHwmProfile) error {
	return n.SetOption(SpotNodeOptionPubSubHwmProfile, int(value))
}

func (n *SpotNode) PubSubHwmProfile() (AutoHwmProfile, error) {
	value, err := n.Option(SpotNodeOptionPubSubHwmProfile)
	return AutoHwmProfile(value), err
}

func (n *SpotNode) SetDispatchWorkersMin(value int) error {
	return n.SetOption(SpotNodeOptionDispatchWorkersMin, value)
}

func (n *SpotNode) DispatchWorkersMin() (int, error) {
	return n.Option(SpotNodeOptionDispatchWorkersMin)
}

func (n *SpotNode) SetDispatchWorkersMax(value int) error {
	return n.SetOption(SpotNodeOptionDispatchWorkersMax, value)
}

func (n *SpotNode) DispatchWorkersMax() (int, error) {
	return n.Option(SpotNodeOptionDispatchWorkersMax)
}
