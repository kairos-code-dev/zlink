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
	handle  unsafe.Pointer
	closed  bool
	options *ContextOptions
}

type ContextOptions struct {
	ctx *Context
}

func NewContext() (*Context, error) {
	handle := C.zlink_ctx_new()
	if handle == nil {
		return nil, configErrorFromErrno(currentErrno())
	}
	ctx := &Context{handle: handle}
	ctx.options = &ContextOptions{ctx: ctx}
	return ctx, nil
}

func (c *Context) raw() unsafe.Pointer {
	return c.handle
}

func (c *Context) Close() error {
	if c == nil || c.closed {
		return nil
	}
	if err := closeErrorFromResult(C.zlink_ctx_term(c.handle)); err != nil {
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
	return closeErrorFromResult(C.zlink_ctx_shutdown(c.handle))
}

func (c *Context) Options() *ContextOptions {
	if c == nil {
		return nil
	}
	return c.options
}

func (o *ContextOptions) SetIOThreads(value int) error {
	return o.ctx.setIntOption(C.ZLINK_IO_THREADS, value)
}

func (o *ContextOptions) IOThreads() (int, error) {
	return o.ctx.getIntOption(C.ZLINK_IO_THREADS)
}

func (o *ContextOptions) SetMaxSockets(value int) error {
	return o.ctx.setIntOption(C.ZLINK_MAX_SOCKETS, value)
}

func (o *ContextOptions) MaxSockets() (int, error) {
	return o.ctx.getIntOption(C.ZLINK_MAX_SOCKETS)
}

func (o *ContextOptions) SocketLimit() (int, error) {
	return o.ctx.getIntOption(C.ZLINK_SOCKET_LIMIT)
}

func (o *ContextOptions) SetThreadPriority(value int) error {
	return o.ctx.setIntOption(C.ZLINK_THREAD_PRIORITY, value)
}

func (o *ContextOptions) ThreadPriority() (int, error) {
	return o.ctx.getIntOption(C.ZLINK_THREAD_PRIORITY)
}

func (o *ContextOptions) SetThreadSchedulingPolicy(value int) error {
	return o.ctx.setIntOption(C.ZLINK_THREAD_SCHED_POLICY, value)
}

func (o *ContextOptions) ThreadSchedulingPolicy() (int, error) {
	return o.ctx.getIntOption(C.ZLINK_THREAD_SCHED_POLICY)
}

func (o *ContextOptions) SetMaxMessageSize(value int) error {
	return o.ctx.setIntOption(C.ZLINK_MAX_MSGSZ, value)
}

func (o *ContextOptions) MaxMessageSize() (int, error) {
	return o.ctx.getIntOption(C.ZLINK_MAX_MSGSZ)
}

func (o *ContextOptions) MessageStructSize() (int, error) {
	return o.ctx.getIntOption(C.ZLINK_MSG_T_SIZE)
}

func (o *ContextOptions) AddThreadAffinity(cpu int) error {
	return o.ctx.setIntOption(C.ZLINK_THREAD_AFFINITY_CPU_ADD, cpu)
}

func (o *ContextOptions) RemoveThreadAffinity(cpu int) error {
	return o.ctx.setIntOption(C.ZLINK_THREAD_AFFINITY_CPU_REMOVE, cpu)
}

func (o *ContextOptions) SetBlocky(value bool) error {
	raw := 0
	if value {
		raw = 1
	}
	return o.ctx.setIntOption(C.ZLINK_CTX_OPT_BLOCKY, raw)
}

func (o *ContextOptions) Blocky() (bool, error) {
	value, err := o.ctx.getIntOption(C.ZLINK_CTX_OPT_BLOCKY)
	return value != 0, err
}

func (c *Context) setIntOption(option C.zlink_ctx_option_t, value int) error {
	if c == nil || c.closed {
		return &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	if value < math.MinInt32 || value > math.MaxInt32 {
		return &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
	}
	return configErrorFromResult(ConfigResult(C.zlink_ctx_set(c.handle, option, C.int(value))))
}

func (c *Context) getIntOption(option C.zlink_ctx_option_t) (int, error) {
	if c == nil || c.closed {
		return 0, &ConfigError{Result: ConfigInvalidHandle, internalErrno: int(C.EFAULT)}
	}
	var result C.zlink_config_result_t
	value := C.zlink_ctx_get(c.handle, option, &result)
	if result != 0 {
		return 0, configErrorFromResult(result)
	}
	return int(value), nil
}

func durationToMillis(value time.Duration) (int32, error) {
	if value < 0 {
		return -1, nil
	}
	ms := value / time.Millisecond
	if ms > math.MaxInt32 {
		return 0, &ConfigError{Result: ConfigInvalidArgument, internalErrno: int(C.EINVAL)}
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

func (c *Context) SpotNodeWithOptions(options SpotNodeOptions) (*SpotNode, error) {
	return newSpotNodeWithOptions(c, &options)
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
