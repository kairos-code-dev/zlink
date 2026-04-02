// SPDX-License-Identifier: MPL-2.0

package zlink

/*
#include "zlink.h"
*/
import "C"

import (
	"math"
	"time"
	"unsafe"
)

type Version struct {
	Major int
	Minor int
	Patch int
}

func RuntimeVersion() Version {
	var major C.int
	var minor C.int
	var patch C.int
	C.zlink_version(&major, &minor, &patch)
	return Version{Major: int(major), Minor: int(minor), Patch: int(patch)}
}

type Context struct {
	handle unsafe.Pointer
	closed bool
}

func NewContext() (*Context, error) {
	handle := C.zlink_ctx_new()
	if handle == nil {
		return nil, lastError()
	}
	return &Context{handle: handle}, nil
}

func (c *Context) raw() unsafe.Pointer {
	return c.handle
}

func (c *Context) Close() error {
	if c == nil || c.closed {
		return nil
	}
	if err := checkRC(C.zlink_ctx_term(c.handle)); err != nil {
		return err
	}
	c.closed = true
	c.handle = nil
	return nil
}

func (c *Context) Shutdown() error {
	if c == nil || c.closed {
		return nil
	}
	return checkRC(C.zlink_ctx_shutdown(c.handle))
}

func (c *Context) SetIOThreads(value int) error {
	return c.setIntOption(C.ZLINK_IO_THREADS, value)
}

func (c *Context) IOThreads() (int, error) {
	return c.getIntOption(C.ZLINK_IO_THREADS)
}

func (c *Context) SetMaxSockets(value int) error {
	return c.setIntOption(C.ZLINK_MAX_SOCKETS, value)
}

func (c *Context) MaxSockets() (int, error) {
	return c.getIntOption(C.ZLINK_MAX_SOCKETS)
}

func (c *Context) SocketLimit() (int, error) {
	return c.getIntOption(C.ZLINK_SOCKET_LIMIT)
}

func (c *Context) SetThreadPriority(value int) error {
	return c.setIntOption(C.ZLINK_THREAD_PRIORITY, value)
}

func (c *Context) ThreadPriority() (int, error) {
	return c.getIntOption(C.ZLINK_THREAD_PRIORITY)
}

func (c *Context) SetThreadSchedPolicy(value int) error {
	return c.setIntOption(C.ZLINK_THREAD_SCHED_POLICY, value)
}

func (c *Context) ThreadSchedPolicy() (int, error) {
	return c.getIntOption(C.ZLINK_THREAD_SCHED_POLICY)
}

func (c *Context) SetMaxMessageSize(value int) error {
	return c.setIntOption(C.ZLINK_MAX_MSGSZ, value)
}

func (c *Context) MaxMessageSize() (int, error) {
	return c.getIntOption(C.ZLINK_MAX_MSGSZ)
}

func (c *Context) MessageStructSize() (int, error) {
	return c.getIntOption(C.ZLINK_MSG_T_SIZE)
}

func (c *Context) AddThreadAffinityCPU(cpu int) error {
	return c.setIntOption(C.ZLINK_THREAD_AFFINITY_CPU_ADD, cpu)
}

func (c *Context) RemoveThreadAffinityCPU(cpu int) error {
	return c.setIntOption(C.ZLINK_THREAD_AFFINITY_CPU_REMOVE, cpu)
}

func (c *Context) SetThreadNamePrefix(value int) error {
	return c.setIntOption(C.ZLINK_THREAD_NAME_PREFIX, value)
}

func (c *Context) ThreadNamePrefix() (int, error) {
	return c.getIntOption(C.ZLINK_THREAD_NAME_PREFIX)
}

func (c *Context) SetBlocky(value bool) error {
	raw := 0
	if value {
		raw = 1
	}
	return c.setIntOption(C.ZLINK_CTX_OPT_BLOCKY, raw)
}

func (c *Context) Blocky() (bool, error) {
	value, err := c.getIntOption(C.ZLINK_CTX_OPT_BLOCKY)
	return value != 0, err
}

func (c *Context) setIntOption(option C.zlink_ctx_option_t, value int) error {
	if c == nil || c.closed {
		return stateError("context is closed")
	}
	if value < math.MinInt32 || value > math.MaxInt32 {
		return validationError("context option overflows C int: %d", value)
	}
	return checkRC(C.zlink_ctx_set(c.handle, option, C.int(value)))
}

func (c *Context) getIntOption(option C.zlink_ctx_option_t) (int, error) {
	if c == nil || c.closed {
		return 0, stateError("context is closed")
	}
	value := C.zlink_ctx_get(c.handle, option)
	if value == -1 {
		return 0, lastError()
	}
	return int(value), nil
}

func durationToMillis(value time.Duration) (int32, error) {
	if value < 0 {
		return -1, nil
	}
	ms := value / time.Millisecond
	if ms > math.MaxInt32 {
		return 0, validationError("duration overflows int milliseconds: %s", value)
	}
	return int32(ms), nil
}

func (c *Context) PairSocket() (*PairSocket, error) {
	return newPairSocket(c)
}

func (c *Context) PubSocket() (*PubSocket, error) {
	return newPubSocket(c, C.ZLINK_SOCKET_PUB)
}

func (c *Context) SubSocket() (*SubSocket, error) {
	return newSubSocket(c, C.ZLINK_SOCKET_SUB)
}

func (c *Context) DealerSocket() (*DealerSocket, error) {
	return newDealerSocket(c)
}

func (c *Context) RouterSocket() (*RouterSocket, error) {
	return newRouterSocket(c)
}

func (c *Context) XPubSocket() (*XPubSocket, error) {
	return newXPubSocket(c)
}

func (c *Context) XSubSocket() (*XSubSocket, error) {
	return newXSubSocket(c)
}

func (c *Context) StreamSocket() (*StreamSocket, error) {
	return newStreamSocket(c)
}

func (c *Context) SpotNode() (*SpotNode, error) {
	return newSpotNode(c)
}

func (c *Context) Registry() (*Registry, error) {
	return newRegistry(c)
}

func (c *Context) Discovery(serviceType ServiceType, serviceName string) (*Discovery, error) {
	return newDiscovery(c, serviceType, serviceName)
}

func (c *Context) RegistryQueryClient() (*RegistryQueryClient, error) {
	return newRegistryQueryClient(c)
}
