// SPDX-License-Identifier: MPL-2.0

import {
  RuntimeContext,
} from './core/context';
import {
  has,
  multipartClose,
  proxy,
  proxySteerable,
  strerror,
  version,
} from './core/runtime_info';
import {
  RuntimeAtomicCounter,
  RuntimePoller,
  RuntimePollEvents,
  RuntimeStopwatch,
  RuntimeThread,
  RuntimeTimer,
  sleep,
} from './eventing';
import {
  RuntimeDealerSocket,
  RuntimePairSocket,
  RuntimePubSocket,
  RuntimeRouterSocket,
  RuntimeStreamSocket,
  RuntimeSubSocket,
  RuntimeXPubSocket,
  RuntimeXSubSocket,
} from './sockets';
import {
  RuntimeDiscovery,
  RuntimeRegistry,
  RuntimeRegistryQueryClient,
  RuntimeSpotNode,
} from './service';

export {
  has,
  multipartClose,
  proxy,
  proxySteerable,
  sleep,
  strerror,
  version,
};

export function createRuntimeContext(): RuntimeContext {
  return new RuntimeContext();
}

export function createRuntimePairSocket(ctx: RuntimeContext): RuntimePairSocket {
  return new RuntimePairSocket(ctx);
}

export function createRuntimePubSocket(ctx: RuntimeContext): RuntimePubSocket {
  return new RuntimePubSocket(ctx);
}

export function createRuntimeSubSocket(ctx: RuntimeContext): RuntimeSubSocket {
  return new RuntimeSubSocket(ctx);
}

export function createRuntimeXPubSocket(ctx: RuntimeContext): RuntimeXPubSocket {
  return new RuntimeXPubSocket(ctx);
}

export function createRuntimeXSubSocket(ctx: RuntimeContext): RuntimeXSubSocket {
  return new RuntimeXSubSocket(ctx);
}

export function createRuntimeDealerSocket(ctx: RuntimeContext): RuntimeDealerSocket {
  return new RuntimeDealerSocket(ctx);
}

export function createRuntimeRouterSocket(ctx: RuntimeContext): RuntimeRouterSocket {
  return new RuntimeRouterSocket(ctx);
}

export function createRuntimeStreamSocket(ctx: RuntimeContext): RuntimeStreamSocket {
  return new RuntimeStreamSocket(ctx);
}

export function createRuntimeRegistry(ctx: RuntimeContext): RuntimeRegistry {
  return new RuntimeRegistry(ctx);
}

export function createRuntimeRegistryQueryClient(ctx: RuntimeContext): RuntimeRegistryQueryClient {
  return new RuntimeRegistryQueryClient(ctx);
}

export function createRuntimeDiscovery(
  ctx: RuntimeContext,
  autoConnectType: ConstructorParameters<typeof RuntimeDiscovery>[1],
  channelName: string
): RuntimeDiscovery {
  return new RuntimeDiscovery(ctx, autoConnectType, channelName);
}

export function createRuntimeSpotNode(
  ctx: RuntimeContext,
  mode?: ConstructorParameters<typeof RuntimeSpotNode>[1]
): RuntimeSpotNode {
  return new RuntimeSpotNode(ctx, mode);
}

export function createRuntimePoller(): RuntimePoller {
  return new RuntimePoller();
}

export function createRuntimePollEvents(capacity: number): RuntimePollEvents {
  return new RuntimePollEvents(capacity);
}

export function createRuntimeTimer(): RuntimeTimer {
  return new RuntimeTimer();
}

export function createRuntimeThread(handler: () => void): RuntimeThread {
  return new RuntimeThread(handler);
}

export function createRuntimeStopwatch(): RuntimeStopwatch {
  return new RuntimeStopwatch();
}

export function createRuntimeAtomicCounter(initialValue = 0): RuntimeAtomicCounter {
  return new RuntimeAtomicCounter(initialValue);
}
