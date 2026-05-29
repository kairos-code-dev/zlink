// SPDX-License-Identifier: MPL-2.0

export * from './zlink/contracts';

import {
  createRuntimeAtomicCounter,
  createRuntimeContext,
  createRuntimeDealerSocket,
  createRuntimeDiscovery,
  createRuntimePairSocket,
  createRuntimePoller,
  createRuntimePollEvents,
  createRuntimePubSocket,
  createRuntimeRegistry,
  createRuntimeRegistryQueryClient,
  createRuntimeRouterSocket,
  createRuntimeSpotNode,
  createRuntimeStopwatch,
  createRuntimeStreamSocket,
  createRuntimeSubSocket,
  createRuntimeThread,
  createRuntimeTimer,
  createRuntimeXPubSocket,
  createRuntimeXSubSocket,
  has as runtimeHas,
  multipartClose as runtimeMultipartClose,
  proxy as runtimeProxy,
  proxySteerable as runtimeProxySteerable,
  sleep as runtimeSleep,
  strerror as runtimeStrerror,
  version as runtimeVersion,
} from './zlink/runtime/defaults';
import type {
  AtomicCounter,
  AutoConnectTypeValue,
  Context,
  DealerSocket,
  Discovery,
  PairSocket,
  Poller,
  PollEvents,
  PubSocket,
  Registry,
  RegistryQueryClient,
  RouterSocket,
  SpotNode,
  Stopwatch,
  StreamSocket,
  SubSocket,
  Thread,
  Timer,
  XPubSocket,
  XSubSocket,
} from './zlink/contracts';
import type { BaseSocket, Message, SpotNodeModeValue } from './zlink/contracts';
import type { RuntimeContext } from './zlink/runtime/core/context';
import type { RuntimeBaseSocket } from './zlink/runtime/sockets';

function runtimeContext(ctx: Context): RuntimeContext {
  return ctx as unknown as RuntimeContext;
}

function runtimeSocket(socket: BaseSocket): RuntimeBaseSocket {
  return socket as unknown as RuntimeBaseSocket;
}

function publicContract<T>(value: unknown): T {
  return value as T;
}

export function createContext(): Context {
  return publicContract<Context>(createRuntimeContext());
}

export function createPairSocket(ctx: Context): PairSocket {
  return publicContract<PairSocket>(createRuntimePairSocket(runtimeContext(ctx)));
}

export function createPubSocket(ctx: Context): PubSocket {
  return publicContract<PubSocket>(createRuntimePubSocket(runtimeContext(ctx)));
}

export function createSubSocket(ctx: Context): SubSocket {
  return publicContract<SubSocket>(createRuntimeSubSocket(runtimeContext(ctx)));
}

export function createXPubSocket(ctx: Context): XPubSocket {
  return publicContract<XPubSocket>(createRuntimeXPubSocket(runtimeContext(ctx)));
}

export function createXSubSocket(ctx: Context): XSubSocket {
  return publicContract<XSubSocket>(createRuntimeXSubSocket(runtimeContext(ctx)));
}

export function createDealerSocket(ctx: Context): DealerSocket {
  return publicContract<DealerSocket>(createRuntimeDealerSocket(runtimeContext(ctx)));
}

export function createRouterSocket(ctx: Context): RouterSocket {
  return publicContract<RouterSocket>(createRuntimeRouterSocket(runtimeContext(ctx)));
}

export function createStreamSocket(ctx: Context): StreamSocket {
  return publicContract<StreamSocket>(createRuntimeStreamSocket(runtimeContext(ctx)));
}

export function createRegistry(ctx: Context): Registry {
  return publicContract<Registry>(createRuntimeRegistry(runtimeContext(ctx)));
}

export function createRegistryQueryClient(ctx: Context): RegistryQueryClient {
  return publicContract<RegistryQueryClient>(createRuntimeRegistryQueryClient(runtimeContext(ctx)));
}

export function createDiscovery(
  ctx: Context,
  autoConnectType: AutoConnectTypeValue,
  channelName: string
): Discovery {
  return publicContract<Discovery>(createRuntimeDiscovery(runtimeContext(ctx), autoConnectType, channelName));
}

export function createSpotNode(ctx: Context, mode?: SpotNodeModeValue): SpotNode {
  return publicContract<SpotNode>(createRuntimeSpotNode(runtimeContext(ctx), mode));
}

export function createPoller(): Poller {
  return publicContract<Poller>(createRuntimePoller());
}

export function createPollEvents(capacity: number): PollEvents {
  return publicContract<PollEvents>(createRuntimePollEvents(capacity));
}

export function createTimer(): Timer {
  return publicContract<Timer>(createRuntimeTimer());
}

export function createThread(handler: () => void): Thread {
  return publicContract<Thread>(createRuntimeThread(handler));
}

export function createStopwatch(): Stopwatch {
  return publicContract<Stopwatch>(createRuntimeStopwatch());
}

export function createAtomicCounter(initialValue = 0): AtomicCounter {
  return publicContract<AtomicCounter>(createRuntimeAtomicCounter(initialValue));
}

export function version(): [number, number, number] {
  return runtimeVersion();
}

export function strerror(code: number): string {
  return runtimeStrerror(code);
}

export function has(capability: string): boolean {
  return runtimeHas(capability);
}

export function proxy(frontend: BaseSocket, backend: BaseSocket, capture?: BaseSocket): void {
  runtimeProxy(runtimeSocket(frontend), runtimeSocket(backend), capture ? runtimeSocket(capture) : undefined);
}

export function proxySteerable(
  frontend: BaseSocket,
  backend: BaseSocket,
  capture: BaseSocket | null,
  control: BaseSocket
): void {
  runtimeProxySteerable(
    runtimeSocket(frontend),
    runtimeSocket(backend),
    capture ? runtimeSocket(capture) : undefined,
    runtimeSocket(control)
  );
}

export function sleep(seconds: number): void {
  runtimeSleep(seconds);
}

export function multipartClose(parts: Message[]): void {
  runtimeMultipartClose(parts);
}
