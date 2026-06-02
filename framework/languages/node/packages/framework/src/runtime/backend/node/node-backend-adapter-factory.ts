import type { Context, MonitorEvent } from '@zlink-systems/zlink';
import { loadBinding } from '../node-backend-adapter';
import type {
  ZLinkBackendAdapterFactory,
  ZLinkChannelBackendAdapter,
  ZLinkBackendContext,
  ZLinkBackendDealerSocket,
  ZLinkBackendDiscovery,
  ZLinkBackendObject,
  ZLinkBackendPublisherSocket,
  ZLinkBackendRegistry,
  ZLinkBackendRegistryQueryClient,
  ZLinkBackendRouterSocket,
  ZLinkBackendSocket,
  ZLinkBackendSocketMonitor,
  ZLinkBackendSpotNode,
  ZLinkBackendStreamSocket,
  ZLinkBackendSubscriberSocket,
  ZLinkMonitoringBackendAdapter,
  ZLinkRegistryBackendAdapter,
  ZLinkSpotBackendAdapter,
  ZLinkStreamBackendAdapter
} from '../contracts';

type ZLinkBindingModule = typeof import('@zlink-systems/zlink');

const zlink = loadBinding() as ZLinkBindingModule;

export class ZLinkNodeBackendAdapterFactory implements ZLinkBackendAdapterFactory {
  createChannelAdapter(): ZLinkChannelBackendAdapter {
    return new ZLinkNodeChannelBackendAdapter();
  }

  createSpotAdapter(): ZLinkSpotBackendAdapter {
    return new ZLinkNodeSpotBackendAdapter();
  }

  createStreamAdapter(): ZLinkStreamBackendAdapter {
    return new ZLinkNodeStreamBackendAdapter();
  }

  createRegistryAdapter(): ZLinkRegistryBackendAdapter {
    return new ZLinkNodeRegistryBackendAdapter();
  }

  createMonitoringAdapter(): ZLinkMonitoringBackendAdapter {
    return new ZLinkNodeMonitoringBackendAdapter();
  }
}

class ZLinkNodeChannelBackendAdapter implements ZLinkChannelBackendAdapter {
  createContext(): ZLinkBackendContext {
    return new ZLinkNodeBackendContext(zlink.createContext());
  }

  createDiscovery(
    context: ZLinkBackendContext,
    autoConnectType: number,
    channelName: string
  ): ZLinkBackendDiscovery {
    return wrapBackendObject(
      zlink.createDiscovery(
        asNodeContext(context),
        autoConnectType as Parameters<ZLinkBindingModule['createDiscovery']>[1],
        channelName
      )
    ) as unknown as ZLinkBackendDiscovery;
  }

  createDealerSocket(context: ZLinkBackendContext): ZLinkBackendDealerSocket {
    return wrapSocket(zlink.createDealerSocket(asNodeContext(context))) as unknown as ZLinkBackendDealerSocket;
  }

  createRouterSocket(context: ZLinkBackendContext): ZLinkBackendRouterSocket {
    return wrapSocket(zlink.createRouterSocket(asNodeContext(context))) as unknown as ZLinkBackendRouterSocket;
  }

  createPublisherSocket(context: ZLinkBackendContext): ZLinkBackendPublisherSocket {
    return wrapSocket(zlink.createPubSocket(asNodeContext(context))) as unknown as ZLinkBackendPublisherSocket;
  }

  createSubscriberSocket(context: ZLinkBackendContext): ZLinkBackendSubscriberSocket {
    return wrapSocket(zlink.createSubSocket(asNodeContext(context))) as unknown as ZLinkBackendSubscriberSocket;
  }
}

class ZLinkNodeSpotBackendAdapter implements ZLinkSpotBackendAdapter {
  createSpotNode(
    context: ZLinkBackendContext,
    mode: Parameters<ZLinkBindingModule['createSpotNode']>[1]
  ): ZLinkBackendSpotNode {
    return wrapBackendObject(zlink.createSpotNode(asNodeContext(context), mode)) as unknown as ZLinkBackendSpotNode;
  }
}

class ZLinkNodeStreamBackendAdapter implements ZLinkStreamBackendAdapter {
  createStreamSocket(context: ZLinkBackendContext): ZLinkBackendStreamSocket {
    return wrapSocket(zlink.createStreamSocket(asNodeContext(context))) as unknown as ZLinkBackendStreamSocket;
  }
}

class ZLinkNodeRegistryBackendAdapter implements ZLinkRegistryBackendAdapter {
  createRegistry(context: ZLinkBackendContext): ZLinkBackendRegistry {
    return wrapBackendObject(zlink.createRegistry(asNodeContext(context))) as unknown as ZLinkBackendRegistry;
  }

  createRegistryQueryClient(context: ZLinkBackendContext): ZLinkBackendRegistryQueryClient {
    return wrapBackendObject(zlink.createRegistryQueryClient(asNodeContext(context))) as unknown as ZLinkBackendRegistryQueryClient;
  }
}

class ZLinkNodeMonitoringBackendAdapter implements ZLinkMonitoringBackendAdapter {
  openSocketMonitor(socket: ZLinkBackendSocket): ZLinkBackendSocketMonitor {
    const nativeSocket = socket.nativeInstance as {
      monitorOpen(): { close(): void; recv(flags?: number): MonitorEvent | null };
    };
    return wrapMonitorSocket(nativeSocket.monitorOpen());
  }
}

class ZLinkNodeBackendContext implements ZLinkBackendContext {
  constructor(readonly nativeInstance: Context) {}

  shutdown(): void {
    this.nativeInstance.shutdown();
  }

  async dispose(): Promise<void> {
    this.nativeInstance.close();
  }

  close(): void {
    this.nativeInstance.close();
  }
}

function asNodeContext(context: ZLinkBackendContext): Context {
  return context.nativeInstance as Context;
}

function wrapBackendObject<T extends { close(): void }>(nativeInstance: T): T & ZLinkBackendObject {
  return new Proxy(nativeInstance, {
    get(target, property, receiver) {
      if (property === 'nativeInstance') {
        return target;
      }
      if (property === 'dispose') {
        return async () => target.close();
      }
      return Reflect.get(target, property, receiver);
    }
  }) as T & ZLinkBackendObject;
}

function wrapSocket<T extends { close(): void }>(nativeInstance: T): T & ZLinkBackendObject {
  return new Proxy(nativeInstance, {
    get(target, property, receiver) {
      if (property === 'nativeInstance') {
        return target;
      }
      if (property === 'dispose') {
        return async () => target.close();
      }
      if (property === 'onSendReady') {
        return (handler: () => void) =>
          (target as unknown as { setSendReadyHandler(handler: () => void): void }).setSendReadyHandler(handler);
      }
      if (property === 'disconnectPeer') {
        return (routingId: unknown) =>
          (target as unknown as { disconnectRid(routingId: unknown): void }).disconnectRid(routingId);
      }
      if (property === 'onFramedPacket') {
        return (handler: unknown) =>
          (target as unknown as { setPacketHandler(handler: unknown): void }).setPacketHandler(handler);
      }
      return Reflect.get(target, property, receiver);
    }
  }) as T & ZLinkBackendObject;
}

function wrapMonitorSocket(nativeInstance: { close(): void; recv(flags?: number): MonitorEvent | null }): ZLinkBackendSocketMonitor {
  return new Proxy(nativeInstance, {
    get(target, property, receiver) {
      if (property === 'nativeInstance') {
        return target;
      }
      if (property === 'dispose') {
        return async () => target.close();
      }
      if (property === 'recv') {
        return () => {
          const event = target.recv();
          if (event === null) {
            return undefined;
          }
          return {
            nativeEvent: event.event,
            routingId: event.routingId ?? undefined,
            localAddr: event.localAddr,
            remoteAddr: event.remoteAddr,
            value: event.value
          };
        };
      }
      if (property === 'onEvent') {
        return (handler: (event: unknown) => void) =>
          (target as unknown as { onEvent(handler: (event: MonitorEvent) => void): void }).onEvent((event) =>
            handler({
              nativeEvent: event.event,
              routingId: event.routingId ?? undefined,
              localAddr: event.localAddr,
              remoteAddr: event.remoteAddr,
              value: event.value
            })
          );
      }
      return Reflect.get(target, property, receiver);
    }
  }) as unknown as ZLinkBackendSocketMonitor;
}
