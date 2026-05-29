// SPDX-License-Identifier: MPL-2.0

export * from './zlink/contracts';

import {
  createDefaultAtomicCounter,
  createDefaultContext,
  createDefaultDealerSocket,
  createDefaultDiscovery,
  createDefaultPairSocket,
  createDefaultPoller,
  createDefaultPollEvents,
  createDefaultPubSocket,
  createDefaultRegistry,
  createDefaultRegistryQueryClient,
  createDefaultRouterSocket,
  createDefaultSpotNode,
  createDefaultStopwatch,
  createDefaultStreamSocket,
  createDefaultSubSocket,
  createDefaultThread,
  createDefaultTimer,
  createDefaultXPubSocket,
  createDefaultXSubSocket,
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

export function createContext(): Context {
  return createDefaultContext() as unknown as Context;
}

export function createPairSocket(ctx: Context): PairSocket {
  return createDefaultPairSocket(ctx as never) as unknown as PairSocket;
}

export function createPubSocket(ctx: Context): PubSocket {
  return createDefaultPubSocket(ctx as never) as unknown as PubSocket;
}

export function createSubSocket(ctx: Context): SubSocket {
  return createDefaultSubSocket(ctx as never) as unknown as SubSocket;
}

export function createXPubSocket(ctx: Context): XPubSocket {
  return createDefaultXPubSocket(ctx as never) as unknown as XPubSocket;
}

export function createXSubSocket(ctx: Context): XSubSocket {
  return createDefaultXSubSocket(ctx as never) as unknown as XSubSocket;
}

export function createDealerSocket(ctx: Context): DealerSocket {
  return createDefaultDealerSocket(ctx as never) as unknown as DealerSocket;
}

export function createRouterSocket(ctx: Context): RouterSocket {
  return createDefaultRouterSocket(ctx as never) as unknown as RouterSocket;
}

export function createStreamSocket(ctx: Context): StreamSocket {
  return createDefaultStreamSocket(ctx as never) as unknown as StreamSocket;
}

export function createRegistry(ctx: Context): Registry {
  return createDefaultRegistry(ctx as never) as unknown as Registry;
}

export function createRegistryQueryClient(ctx: Context): RegistryQueryClient {
  return createDefaultRegistryQueryClient(ctx as never) as unknown as RegistryQueryClient;
}

export function createDiscovery(
  ctx: Context,
  autoConnectType: AutoConnectTypeValue,
  channelName: string
): Discovery {
  return createDefaultDiscovery(ctx as never, autoConnectType as never, channelName) as unknown as Discovery;
}

export function createSpotNode(ctx: Context, mode?: SpotNodeModeValue): SpotNode {
  return createDefaultSpotNode(ctx as never, mode as never) as unknown as SpotNode;
}

export function createPoller(): Poller {
  return createDefaultPoller() as unknown as Poller;
}

export function createPollEvents(capacity: number): PollEvents {
  return createDefaultPollEvents(capacity) as unknown as PollEvents;
}

export function createTimer(): Timer {
  return createDefaultTimer() as unknown as Timer;
}

export function createThread(handler: () => void): Thread {
  return createDefaultThread(handler) as unknown as Thread;
}

export function createStopwatch(): Stopwatch {
  return createDefaultStopwatch() as unknown as Stopwatch;
}

export function createAtomicCounter(initialValue = 0): AtomicCounter {
  return createDefaultAtomicCounter(initialValue) as unknown as AtomicCounter;
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
  runtimeProxy(frontend as never, backend as never, capture as never);
}

export function proxySteerable(
  frontend: BaseSocket,
  backend: BaseSocket,
  capture: BaseSocket | null,
  control: BaseSocket
): void {
  runtimeProxySteerable(frontend as never, backend as never, capture as never, control as never);
}

export function sleep(seconds: number): void {
  runtimeSleep(seconds);
}

export function multipartClose(parts: Message[]): void {
  runtimeMultipartClose(parts);
}
