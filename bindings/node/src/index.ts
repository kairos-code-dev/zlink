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
import {
  asPublicContract,
  asRuntimeContext,
  asRuntimeSocket,
} from './zlink/runtime/public_bridge';
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

export function createContext(): Context {
  return asPublicContract<Context>(createRuntimeContext());
}

export function createPairSocket(ctx: Context): PairSocket {
  return asPublicContract<PairSocket>(createRuntimePairSocket(asRuntimeContext(ctx)));
}

export function createPubSocket(ctx: Context): PubSocket {
  return asPublicContract<PubSocket>(createRuntimePubSocket(asRuntimeContext(ctx)));
}

export function createSubSocket(ctx: Context): SubSocket {
  return asPublicContract<SubSocket>(createRuntimeSubSocket(asRuntimeContext(ctx)));
}

export function createXPubSocket(ctx: Context): XPubSocket {
  return asPublicContract<XPubSocket>(createRuntimeXPubSocket(asRuntimeContext(ctx)));
}

export function createXSubSocket(ctx: Context): XSubSocket {
  return asPublicContract<XSubSocket>(createRuntimeXSubSocket(asRuntimeContext(ctx)));
}

export function createDealerSocket(ctx: Context): DealerSocket {
  return asPublicContract<DealerSocket>(createRuntimeDealerSocket(asRuntimeContext(ctx)));
}

export function createRouterSocket(ctx: Context): RouterSocket {
  return asPublicContract<RouterSocket>(createRuntimeRouterSocket(asRuntimeContext(ctx)));
}

export function createStreamSocket(ctx: Context): StreamSocket {
  return asPublicContract<StreamSocket>(createRuntimeStreamSocket(asRuntimeContext(ctx)));
}

export function createRegistry(ctx: Context): Registry {
  return asPublicContract<Registry>(createRuntimeRegistry(asRuntimeContext(ctx)));
}

export function createRegistryQueryClient(ctx: Context): RegistryQueryClient {
  return asPublicContract<RegistryQueryClient>(createRuntimeRegistryQueryClient(asRuntimeContext(ctx)));
}

export function createDiscovery(
  ctx: Context,
  autoConnectType: AutoConnectTypeValue,
  channelName: string
): Discovery {
  return asPublicContract<Discovery>(createRuntimeDiscovery(asRuntimeContext(ctx), autoConnectType, channelName));
}

export function createSpotNode(ctx: Context, mode?: SpotNodeModeValue): SpotNode {
  return asPublicContract<SpotNode>(createRuntimeSpotNode(asRuntimeContext(ctx), mode));
}

export function createPoller(): Poller {
  return asPublicContract<Poller>(createRuntimePoller());
}

export function createPollEvents(capacity: number): PollEvents {
  return asPublicContract<PollEvents>(createRuntimePollEvents(capacity));
}

export function createTimer(): Timer {
  return asPublicContract<Timer>(createRuntimeTimer());
}

export function createThread(handler: () => void): Thread {
  return asPublicContract<Thread>(createRuntimeThread(handler));
}

export function createStopwatch(): Stopwatch {
  return asPublicContract<Stopwatch>(createRuntimeStopwatch());
}

export function createAtomicCounter(initialValue = 0): AtomicCounter {
  return asPublicContract<AtomicCounter>(createRuntimeAtomicCounter(initialValue));
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
  runtimeProxy(
    asRuntimeSocket(frontend),
    asRuntimeSocket(backend),
    capture ? asRuntimeSocket(capture) : undefined
  );
}

export function proxySteerable(
  frontend: BaseSocket,
  backend: BaseSocket,
  capture: BaseSocket | null,
  control: BaseSocket
): void {
  runtimeProxySteerable(
    asRuntimeSocket(frontend),
    asRuntimeSocket(backend),
    capture ? asRuntimeSocket(capture) : undefined,
    asRuntimeSocket(control)
  );
}

export function sleep(seconds: number): void {
  runtimeSleep(seconds);
}

export function multipartClose(parts: Message[]): void {
  runtimeMultipartClose(parts);
}
