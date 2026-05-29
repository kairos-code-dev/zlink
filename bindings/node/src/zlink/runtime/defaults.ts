// SPDX-License-Identifier: MPL-2.0

import {
  DefaultContext,
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
  DefaultAtomicCounter,
  DefaultPoller,
  DefaultPollEvents,
  DefaultStopwatch,
  DefaultThread,
  DefaultTimer,
  sleep,
} from './eventing';
import {
  DefaultDealerSocket,
  DefaultPairSocket,
  DefaultPubSocket,
  DefaultRouterSocket,
  DefaultStreamSocket,
  DefaultSubSocket,
  DefaultXPubSocket,
  DefaultXSubSocket,
} from './sockets';
import {
  DefaultDiscovery,
  DefaultRegistry,
  DefaultRegistryQueryClient,
  DefaultSpotNode,
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

export function createDefaultContext(): DefaultContext {
  return new DefaultContext();
}

export function createDefaultPairSocket(ctx: DefaultContext): DefaultPairSocket {
  return new DefaultPairSocket(ctx);
}

export function createDefaultPubSocket(ctx: DefaultContext): DefaultPubSocket {
  return new DefaultPubSocket(ctx);
}

export function createDefaultSubSocket(ctx: DefaultContext): DefaultSubSocket {
  return new DefaultSubSocket(ctx);
}

export function createDefaultXPubSocket(ctx: DefaultContext): DefaultXPubSocket {
  return new DefaultXPubSocket(ctx);
}

export function createDefaultXSubSocket(ctx: DefaultContext): DefaultXSubSocket {
  return new DefaultXSubSocket(ctx);
}

export function createDefaultDealerSocket(ctx: DefaultContext): DefaultDealerSocket {
  return new DefaultDealerSocket(ctx);
}

export function createDefaultRouterSocket(ctx: DefaultContext): DefaultRouterSocket {
  return new DefaultRouterSocket(ctx);
}

export function createDefaultStreamSocket(ctx: DefaultContext): DefaultStreamSocket {
  return new DefaultStreamSocket(ctx);
}

export function createDefaultRegistry(ctx: DefaultContext): DefaultRegistry {
  return new DefaultRegistry(ctx);
}

export function createDefaultRegistryQueryClient(ctx: DefaultContext): DefaultRegistryQueryClient {
  return new DefaultRegistryQueryClient(ctx);
}

export function createDefaultDiscovery(
  ctx: DefaultContext,
  autoConnectType: ConstructorParameters<typeof DefaultDiscovery>[1],
  channelName: string
): DefaultDiscovery {
  return new DefaultDiscovery(ctx, autoConnectType, channelName);
}

export function createDefaultSpotNode(
  ctx: DefaultContext,
  mode?: ConstructorParameters<typeof DefaultSpotNode>[1]
): DefaultSpotNode {
  return new DefaultSpotNode(ctx, mode);
}

export function createDefaultPoller(): DefaultPoller {
  return new DefaultPoller();
}

export function createDefaultPollEvents(capacity: number): DefaultPollEvents {
  return new DefaultPollEvents(capacity);
}

export function createDefaultTimer(): DefaultTimer {
  return new DefaultTimer();
}

export function createDefaultThread(handler: () => void): DefaultThread {
  return new DefaultThread(handler);
}

export function createDefaultStopwatch(): DefaultStopwatch {
  return new DefaultStopwatch();
}

export function createDefaultAtomicCounter(initialValue = 0): DefaultAtomicCounter {
  return new DefaultAtomicCounter(initialValue);
}
