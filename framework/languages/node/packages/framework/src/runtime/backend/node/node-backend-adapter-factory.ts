import type { Context, MonitorEvent, TopicMessage } from '@zlink-systems/zlink';
import { loadBinding } from '../node-backend-adapter';
import type {
  ZLinkBackendAdapterFactory,
  ZLinkChannelBackendAdapter,
  ZLinkBackendContext,
  ZLinkBackendDealerSocket,
  ZLinkBackendDiscovery,
  ZLinkBackendObject,
  ZLinkBackendPublisherSocket,
  ZLinkBackendReadablePoller,
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

  createTopicMessage(): TopicMessage {
    return new zlink.TopicMessage();
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

  createReadablePoller(socket: ZLinkBackendSubscriberSocket): ZLinkBackendReadablePoller {
    const poller = zlink.createPoller();
    const events = zlink.createPollEvents(1);
    poller.add(socket.nativeInstance as never, [zlink.PollEventFlag.PollIn], 0);
    return {
      wait(timeoutMs: number): boolean {
        return poller.wait(events, timeoutMs) > 0 && events.hasEvent(0, zlink.PollEventFlag.PollIn);
      },
      dispose(): void {
        events.close();
        poller.close();
      }
    };
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
        return async () => {
          disableSocketLinger(target);
          await closeWithBusyRetry(target);
        };
      }
      if (property === 'attachDiscovery') {
        return (discovery: ZLinkBackendDiscovery) =>
          (target as unknown as { attachDiscovery(discovery: unknown): void })
            .attachDiscovery(unwrapBackendObject(discovery));
      }
      if (property === 'attachChannelDealer') {
        return (discovery: ZLinkBackendDiscovery, dealer: ZLinkBackendDealerSocket) =>
          (target as unknown as { attachChannelDealer(discovery: unknown, dealer: unknown): void })
            .attachChannelDealer(unwrapBackendObject(discovery), unwrapBackendObject(dealer));
      }
      if (property === 'attachSpotRouteChannelDiscovery') {
        return (channelName: string, discovery: ZLinkBackendDiscovery) =>
          (target as unknown as { attachSpotRouteChannelDiscovery(channelName: string, discovery: unknown): void })
            .attachSpotRouteChannelDiscovery(channelName, unwrapBackendObject(discovery));
      }
      const value = Reflect.get(target, property, target);
      return typeof value === 'function' ? value.bind(target) : value;
    }
  }) as T & ZLinkBackendObject;
}

function wrapSocket<T extends { close(): void }>(nativeInstance: T): T & ZLinkBackendObject {
  const boundEndpoints = new Set<string>();
  const connectedEndpoints = new Set<string>();
  const peerRoutingIds = new Set<unknown>();
  return new Proxy(nativeInstance, {
    get(target, property, receiver) {
      const lifecycle = resolveSocketLifecycleProperty(target, property, receiver, {
        boundEndpoints,
        connectedEndpoints,
        peerRoutingIds
      });
      if (lifecycle !== undefined) {
        return lifecycle;
      }
      const messaging = resolveSocketMessagingProperty(target, property, receiver, peerRoutingIds);
      if (messaging !== undefined) {
        return messaging;
      }
      const actorGateway = resolveSocketActorGatewayProperty(target, property);
      if (actorGateway !== undefined) {
        return actorGateway;
      }
      if (property === 'attachDiscovery') {
        return (discovery: ZLinkBackendDiscovery) =>
          (target as unknown as { attachDiscovery(discovery: unknown): void })
            .attachDiscovery(unwrapBackendObject(discovery));
      }
      const value = Reflect.get(target, property, target);
      return typeof value === 'function' ? value.bind(target) : value;
    }
  }) as T & ZLinkBackendObject;
}

function unwrapBackendObject(value: unknown): unknown {
  if (typeof value === 'object' && value !== null) {
    const nativeInstance = (value as Partial<ZLinkBackendObject>).nativeInstance;
    if (nativeInstance !== undefined) {
      return nativeInstance;
    }
  }
  return value;
}

interface ZLinkSocketLifecycleState {
  readonly boundEndpoints: Set<string>;
  readonly connectedEndpoints: Set<string>;
  readonly peerRoutingIds: Set<unknown>;
}

function resolveSocketLifecycleProperty(
  target: unknown,
  property: string | symbol,
  receiver: unknown,
  state: ZLinkSocketLifecycleState
): unknown {
  if (property === 'nativeInstance') {
    return target;
  }
  if (property === 'dispose') {
    return async () => {
      closeSocketRoutes(target, state.peerRoutingIds);
      closeSocketEndpoints(target, state.boundEndpoints, state.connectedEndpoints);
      disableSocketLinger(target);
      await closeWithBusyRetry(target as { close(): void });
    };
  }
  if (property === 'bind') {
    return (endpoint: string) => {
      (target as { bind(endpoint: string): void }).bind(endpoint);
      state.boundEndpoints.add(endpoint);
    };
  }
  if (property === 'unbind') {
    return (endpoint: string) => {
      (target as { unbind(endpoint: string): void }).unbind(endpoint);
      state.boundEndpoints.delete(endpoint);
    };
  }
  if (property === 'connect') {
    return (endpoint: string) => {
      (target as { connect(endpoint: string): void }).connect(endpoint);
      state.connectedEndpoints.add(endpoint);
    };
  }
  if (property === 'disconnect') {
    return (endpoint: string) => {
      (target as { disconnect(endpoint: string): void }).disconnect(endpoint);
      state.connectedEndpoints.delete(endpoint);
    };
  }
  if (property === 'setChannelName' && typeof Reflect.get(target as object, property, receiver) !== 'function') {
    return () => undefined;
  }
  if (property === 'setRoutingId') {
    return (routingId: unknown) =>
      (target as { setRoutingId(routingId: unknown): void }).setRoutingId(toNativeRoutingId(routingId));
  }
  if (property === 'onSendReady') {
    return (handler: () => void) =>
      (target as { setSendReadyHandler(handler: () => void): void }).setSendReadyHandler(handler);
  }
  return undefined;
}

function resolveSocketMessagingProperty(
  target: unknown,
  property: string | symbol,
  receiver: unknown,
  peerRoutingIds: Set<unknown>
): unknown {
  if (property === 'send') {
    return (...args: unknown[]) => {
      if (args.length >= 3) {
        const [routingId, payload, flags] = args as [unknown, unknown, number];
        peerRoutingIds.add(routingId);
        return submitBindingSend(
          (target as { send(routingId: unknown): ZLinkBindingSendOperation }).send(toNativeRoutingId(routingId)),
          payload,
          flags
        );
      }
      const [payload, flags] = args as [unknown, number | undefined];
      return submitBindingSend(
        (target as { send(): ZLinkBindingSendOperation }).send(),
        payload,
        flags ?? 0
      );
    };
  }
  if (property === 'request') {
    return (...args: unknown[]) => {
      if (args.length >= 5) {
        const [routingId, payload, callback, flags, timeoutMs] = args as [unknown, unknown, unknown, number, number | undefined];
        void flags;
        peerRoutingIds.add(routingId);
        return submitBindingRequestCallback(
          (target as { request(routingId: unknown): ZLinkBindingRequestOperation }).request(toNativeRoutingId(routingId)),
          payload,
          callback,
          timeoutMs
        );
      }
      if (args.length >= 4) {
        const [payload, callback, flags, timeoutMs] = args as [unknown, unknown, number, number | undefined];
        void flags;
        return submitBindingRequestCallback(
          (target as { request(): ZLinkBindingRequestOperation }).request(),
          payload,
          callback,
          timeoutMs
        );
      }
      return (Reflect.get(target as object, property, receiver) as (...values: unknown[]) => unknown)(...args);
    };
  }
  if (property === 'reply') {
    return (...args: unknown[]) => {
      const [routingId, requestSeq, payload] = args as [unknown, bigint, unknown];
      peerRoutingIds.add(routingId);
      const operation = (target as {
        reply(routingId: unknown, requestSeq: bigint): ZLinkBindingSendOperation;
      }).reply(toNativeRoutingId(routingId), requestSeq);
      if (args.length < 3) {
        return operation;
      }
      return submitBindingSend(operation, payload, 0);
    };
  }
  if (property === 'recv') {
    return (flags?: number) => {
      const received = new zlink.Received();
      const ok = (target as {
        recv(result: unknown, flags?: number): boolean;
      }).recv(received, flags);
      return ok ? received : undefined;
    };
  }
  if (property === 'publish') {
    return (...args: unknown[]) => {
      if (args.length >= 3) {
        const [topic, payload, flags] = args as [string, unknown, number];
        return submitBindingSend(
          (target as { publish(topic: string): ZLinkBindingSendOperation }).publish(topic),
          payload,
          flags
        );
      }
      return (Reflect.get(target as object, property, receiver) as (...values: unknown[]) => unknown)(...args);
    };
  }
  return undefined;
}

function resolveSocketActorGatewayProperty(target: unknown, property: string | symbol): unknown {
  if (property === 'disconnectPeer') {
    return (routingId: unknown) =>
      (target as { disconnectRid(routingId: unknown): void }).disconnectRid(toNativeRoutingId(routingId));
  }
  if (property === 'onFramedPacket') {
    return (handler: unknown) =>
      (target as { setPacketHandler(handler: unknown): void }).setPacketHandler(handler);
  }
  if (property === 'bindActor') {
    return async (sessionRid: unknown, actor: unknown, timeoutMs: number) => {
      const operation = (target as {
        bindActor(sessionRid: unknown, actor: unknown): {
          timeout(timeoutMs: number): ZLinkBindingPromiseRequestSubmitOperation<Array<{ close(): void }>>;
        };
      }).bindActor(sessionRid, actor);
      const replies = await submitBindingRequest(operation.timeout(timeoutMs));
      for (const reply of replies) {
        reply.close();
      }
    };
  }
  if (property === 'unbindActor') {
    return async (sessionRid: unknown, actorId: string, timeoutMs: number) => {
      const operation = (target as {
        unbindActor(sessionRid: unknown, actorId: string): {
          timeout(timeoutMs: number): ZLinkBindingPromiseRequestSubmitOperation<Array<{ close(): void }>>;
        };
      }).unbindActor(sessionRid, actorId);
      const replies = await submitBindingRequest(operation.timeout(timeoutMs));
      for (const reply of replies) {
        reply.close();
      }
    };
  }
  if (property === 'sendBoundActor') {
    return (sessionRid: unknown, actorId: string, parts: readonly unknown[], flags: number) => {
      const operation = (target as {
        sendBoundActor(sessionRid: unknown, actorId: string): {
          message(part: unknown): { message(part: unknown): unknown; flags(flags: number): { submit(): boolean } };
          flags(flags: number): { submit(): boolean };
        };
      }).sendBoundActor(sessionRid, actorId);
      let submitter = operation;
      for (const part of parts) {
        submitter = submitter.message(part) as typeof operation;
      }
      return submitter.flags(flags).submit();
    };
  }
  return undefined;
}

function closeSocketRoutes(target: unknown, peerRoutingIds: Set<unknown>): void {
  if (peerRoutingIds.size === 0 || !hasDisconnectRid(target)) {
    return;
  }
  for (const routingId of peerRoutingIds) {
    try {
      target.disconnectRid(toNativeRoutingId(routingId));
    } catch (error) {
      if (!isDisconnectRouteNotFoundError(error)) {
        throw error;
      }
    }
    peerRoutingIds.delete(routingId);
  }
}

function isDisconnectRouteNotFoundError(error: unknown): boolean {
  return error instanceof Error && 'code' in error &&
    (error as { code: unknown }).code === zlink.ConnectResult.NotFound;
}

function hasDisconnectRid(target: unknown): target is { disconnectRid(routingId: unknown): void } {
  return target !== null && typeof target === 'object' && 'disconnectRid' in target &&
    typeof (target as { disconnectRid: unknown }).disconnectRid === 'function';
}

function closeSocketEndpoints(target: unknown, boundEndpoints: Set<string>, connectedEndpoints: Set<string>): void {
  for (const endpoint of connectedEndpoints) {
    try {
      (target as { disconnect(endpoint: string): void }).disconnect(endpoint);
    } catch (error) {
      if (!isEndpointCloseIgnorableError(error)) {
        throw error;
      }
    }
    connectedEndpoints.delete(endpoint);
  }
  for (const endpoint of boundEndpoints) {
    try {
      (target as { unbind(endpoint: string): void }).unbind(endpoint);
    } catch (error) {
      if (!isEndpointCloseIgnorableError(error)) {
        throw error;
      }
    }
    boundEndpoints.delete(endpoint);
  }
}

function isEndpointCloseIgnorableError(error: unknown): boolean {
  return error instanceof Error && 'code' in error &&
    ((error as { code: unknown }).code === 604 || (error as { code: unknown }).code === zlink.ConnectResult.NotFound);
}

async function closeWithBusyRetry(target: { close(): void }): Promise<void> {
  let lastError: unknown;
  for (let attempt = 0; attempt < 8; attempt++) {
    try {
      target.close();
      return;
    } catch (error) {
      if (!isBusyCloseError(error)) {
        throw error;
      }
      lastError = error;
      await new Promise<void>((resolve) => setImmediate(resolve));
    }
  }
  throw lastError;
}

function isBusyCloseError(error: unknown): boolean {
  return error instanceof Error && 'code' in error &&
    ([401, 403, 404].includes((error as { code: number }).code));
}

function disableSocketLinger(target: unknown): void {
  if (
    target !== null &&
    typeof target === 'object' &&
    'options' in target &&
    typeof target.options === 'object' &&
    target.options !== null &&
    'linger' in target.options
  ) {
    (target.options as { linger: number }).linger = 0;
  }
}

interface ZLinkBindingSendOperation {
  message(message: unknown): ZLinkBindingSendOperation;
  flags(flags: number): { submit(): boolean };
}

interface ZLinkBindingRequestOperation {
  message(message: unknown): ZLinkBindingRequestSubmitOperation;
}

interface ZLinkBindingRequestSubmitOperation {
  message(message: unknown): ZLinkBindingRequestSubmitOperation;
  timeout(timeoutMs: number): ZLinkBindingRequestSubmitOperation;
  flags(flags: number): { submit(callback: unknown): boolean };
  submit(callback: unknown): boolean;
}

interface ZLinkBindingPromiseRequestSubmitOperation<TParts extends unknown[]> {
  submit(callback: (result: number, parts: TParts) => void): boolean;
}

function submitBindingSend(operation: ZLinkBindingSendOperation, payload: unknown, flags: number): boolean {
  let current = operation;
  if (Array.isArray(payload)) {
    for (const part of payload) {
      current = current.message(part);
    }
  } else {
    current = current.message(payload);
  }
  return current.flags(flags).submit();
}

function submitBindingRequestCallback(
  operation: ZLinkBindingRequestOperation,
  payload: unknown,
  callback: unknown,
  timeoutMs: number | undefined
): boolean {
  let current: ZLinkBindingRequestSubmitOperation | undefined;
  if (Array.isArray(payload)) {
    for (const part of payload) {
      current = current === undefined ? operation.message(part) : current.message(part);
    }
  } else {
    current = operation.message(payload);
  }
  if (current === undefined) {
    current = operation.message(Buffer.alloc(0));
  }
  if (timeoutMs !== undefined) {
    current = current.timeout(timeoutMs);
  }
  return current.submit(callback);
}

function submitBindingRequest<TParts extends unknown[]>(
  operation: ZLinkBindingPromiseRequestSubmitOperation<TParts>
): Promise<TParts> {
  return new Promise((resolve, reject) => {
    const accepted = operation.submit((result, parts) => {
      if (result !== 0) {
        reject(new Error(`Binding request failed with result ${result}.`));
        return;
      }
      resolve(parts);
    });
    if (!accepted) {
      reject(new Error('Binding request submit was not accepted.'));
    }
  });
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

function toNativeRoutingId(routingId: unknown): unknown {
  if (typeof routingId === 'string') {
    return zlink.RoutingId.from(routingId);
  }
  return routingId;
}
