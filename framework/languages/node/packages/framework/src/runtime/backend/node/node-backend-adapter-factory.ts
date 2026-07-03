import type { Context, MonitorEvent, TopicMessage } from '@zlink-systems/zlink';
import { loadBinding } from '../node-backend-adapter';
import type {
  ZLinkBackendAdapterFactory,
  ZLinkChannelBackendAdapter,
  ZLinkBackendContext,
  ZLinkBackendDealerSocket,
  ZLinkBackendObject,
  ZLinkBackendPublisherSocket,
  ZLinkBackendReadablePoller,
  ZLinkBackendRouterSocket,
  ZLinkBackendSocket,
  ZLinkBackendSocketMonitor,
  ZLinkBackendSpotNode,
  ZLinkBackendSpotRouteBridge,
  ZLinkBackendStreamSocket,
  ZLinkBackendSubscriberSocket,
  ZLinkMonitoringBackendAdapter,
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
    get(target, property) {
      const resolved =
        resolveBackendObjectProperty(target, property) ??
        resolveBackendSpotNodeProperty(target, property) ??
        resolveBackendRouteBridgeProperty(target, property) ??
        resolveBackendMessagingProperty(target, property);
      if (resolved !== undefined) {
        return resolved;
      }
      const value = Reflect.get(target, property, target);
      return typeof value === 'function' ? value.bind(target) : value;
    }
  }) as T & ZLinkBackendObject;
}

function resolveBackendObjectProperty(target: unknown, property: string | symbol): unknown {
  if (property === 'nativeInstance') {
    return target;
  }
  if (property === 'dispose') {
    return async () => {
      disableSocketLinger(target);
      await closeWithBusyRetry(target as { close(): void });
    };
  }
  return undefined;
}

function resolveBackendSpotNodeProperty(target: unknown, property: string | symbol): unknown {
  if (property === 'createRouteBridge') {
    return () => wrapBackendObject(
      (target as unknown as { createRouteBridge(): { close(): void } }).createRouteBridge()
    ) as unknown as ZLinkBackendSpotRouteBridge;
  }
  if (property === 'attachRouterChannel') {
    return (channelName: string, router: ZLinkBackendRouterSocket, options?: { readonly capabilities?: number }) =>
      (target as unknown as {
        attachRouterChannel(channelName: string, router: unknown, options?: { readonly capabilities?: number }): void;
      }).attachRouterChannel(channelName, unwrapBackendObject(router), options);
  }
  if (property === 'setRoutingId') {
    return (routingId: unknown) =>
      (target as unknown as { setRoutingId(routingId: unknown): void }).setRoutingId(toNativeRoutingId(routingId));
  }
  if (property === 'setPublisherRoutingId') {
    return (routingId: unknown) =>
      (target as unknown as { setPublisherRoutingId(routingId: unknown): void })
        .setPublisherRoutingId(toNativeRoutingId(routingId));
  }
  if (property === 'setSubscriberRoutingId') {
    return (routingId: unknown) =>
      (target as unknown as { setSubscriberRoutingId(routingId: unknown): void })
        .setSubscriberRoutingId(toNativeRoutingId(routingId));
  }
  if (property === 'connectPeerRid') {
    return (targetNodeRid: unknown, endpoint: string) =>
      (target as unknown as { connectPeerRid(targetNodeRid: unknown, endpoint: string): void })
        .connectPeerRid(toNativeRoutingId(targetNodeRid), endpoint);
  }
  if (property === 'createSpot' || property === 'entrySpot') {
    return () => wrapBackendObject((target as unknown as { [key: string]: () => { close(): void } })[property]());
  }
  if (property === 'getOrCreateSpot') {
    return (spotRid: unknown) => {
      const result = (target as unknown as {
        getOrCreateSpot(spotRid: unknown): { readonly spot: { close(): void }; readonly created: boolean };
      }).getOrCreateSpot(toNativeRoutingId(spotRid));
      return {
        ...result,
        spot: wrapBackendObject(result.spot)
      };
    };
  }
  if (property === 'createActor') {
    return (actorId: string, request?: unknown) =>
      (target as unknown as { createActor(actorId: string, request?: unknown): { actorRef: unknown } })
        .createActor(actorId, request)
        .actorRef;
  }
  if (property === 'actorLookup') {
    return (actorId: string) => {
      try {
        return (target as unknown as { actorLookup(actorId: string): unknown }).actorLookup(actorId);
      } catch (error) {
        if (isBindingNotFound(error)) {
          return undefined;
        }
        throw error;
      }
    };
  }
  return undefined;
}

function resolveBackendRouteBridgeProperty(target: unknown, property: string | symbol): unknown {
  if (property === 'handleRouterReceived') {
    return (
      channelName: string,
      sourceNodeRid: unknown,
      requestSeq: bigint | number,
      parts: readonly unknown[]
    ) =>
      (target as unknown as {
        handleRouterReceived(
          channelName: string,
          sourceNodeRid: unknown,
          requestSeq: bigint | number,
          parts: readonly unknown[]
        ): boolean;
      }).handleRouterReceived(channelName, toNativeRoutingId(sourceNodeRid), BigInt(requestSeq), parts);
  }
  if (isSpotRouteBridgeTarget(target) && property === 'send') {
    return (channelName: string, targetNodeRid: unknown, targetSpotRid: unknown) =>
      (target as unknown as {
        send(channelName: string, targetNodeRid: unknown, targetSpotRid: unknown): unknown;
      }).send(channelName, toNativeRoutingId(targetNodeRid), toNativeRoutingId(targetSpotRid));
  }
  if (isSpotRouteBridgeTarget(target) && property === 'request') {
    return (channelName: string, targetNodeRid: unknown, targetSpotRid: unknown) =>
      (target as unknown as {
        request(channelName: string, targetNodeRid: unknown, targetSpotRid: unknown): unknown;
      }).request(channelName, toNativeRoutingId(targetNodeRid), toNativeRoutingId(targetSpotRid));
  }
  return undefined;
}

function resolveBackendMessagingProperty(target: unknown, property: string | symbol): unknown {
  if (property === 'sendToChannel') {
    return (channelName: string, payload: unknown, flags: number) => {
      const operation = (target as unknown as {
        sendToChannel(channelName: string): unknown;
      }).sendToChannel(channelName);
      return submitSendOperation(operation, payload, flags);
    };
  }
  if (property === 'requestToChannel') {
    return (channelName: string, payload: unknown, callback: unknown, flags: number, timeoutMs?: number) => {
      const operation = (target as unknown as {
        requestToChannel(channelName: string): unknown;
      }).requestToChannel(channelName);
      return submitRequestOperation(operation, payload, callback, flags, timeoutMs);
    };
  }
  if (property === 'publish') {
    return (topic: string, payload: unknown, flags: number) => {
      const operation = (target as unknown as {
        publish(topic: string): unknown;
      }).publish(topic);
      return submitSendOperation(operation, payload, flags);
    };
  }
  if (property === 'sendToSpot') {
    return (targetRid: unknown, spotRid: unknown, payload: unknown, flags: number) => {
      const operation = (target as unknown as {
        sendToSpot(targetRid: unknown, spotRid: unknown): unknown;
      }).sendToSpot(toNativeRoutingId(targetRid), toNativeRoutingId(spotRid));
      return submitSendOperation(operation, payload, flags);
    };
  }
  if (property === 'requestToSpot') {
    return (targetRid: unknown, spotRid: unknown, payload: unknown, callback: unknown, flags: number, timeoutMs?: number) => {
      const operation = (target as unknown as {
        requestToSpot(targetRid: unknown, spotRid: unknown): unknown;
      }).requestToSpot(toNativeRoutingId(targetRid), toNativeRoutingId(spotRid));
      return submitRequestOperation(operation, payload, callback, flags, timeoutMs);
    };
  }
  if (property === 'sendActorBoundSession') {
    return (actor: unknown, payload: unknown, flags: number) => {
      const operation = (target as unknown as {
        sendActorBoundSession(actor: unknown): unknown;
      }).sendActorBoundSession(toNativeActorRef(actor));
      return submitSendOperation(operation, payload, flags);
    };
  }
  if (property === 'bindRemoteActorSession') {
    return (actor: unknown, sourceNodeRid: unknown, sourceSessionRid: unknown) =>
      (target as unknown as {
        bindRemoteActorSession(actor: unknown, sourceNodeRid: unknown, sourceSessionRid: unknown): void;
      }).bindRemoteActorSession(
        toNativeActorRef(actor),
        toNativeRoutingId(sourceNodeRid),
        toNativeRoutingId(sourceSessionRid)
      );
  }
  if (property === 'recvRoute') {
    return (received: unknown, flags: number) => {
      try {
        return (target as unknown as {
          recvRouted(received: unknown, flags: number): boolean;
        }).recvRouted(received, flags);
      } catch (error) {
        if (isRouteRecvRetryable(error)) {
          return false;
        }
        throw error;
      }
    };
  }
  if (property === 'joinActor') {
    return (actor: unknown, destNodeRid: unknown, destSpotRid: unknown, payload: unknown, callback: unknown, timeoutMs?: number) => {
      const operation = (target as unknown as {
        joinActor(actor: unknown, destNodeRid: unknown, destSpotRid: unknown): unknown;
      }).joinActor(actor, toNativeRoutingId(destNodeRid), toNativeRoutingId(destSpotRid));
      return submitRequestOperation(operation, payload, callback, 0, timeoutMs);
    };
  }
  if (property === 'joinActorEntrySpot') {
    return (actor: unknown, destNodeRid: unknown, request: unknown, callback: unknown, timeoutMs?: number) => {
      const operation = (target as unknown as {
        joinActorEntrySpot(actor: unknown, destNodeRid: unknown, request: unknown): unknown;
      }).joinActorEntrySpot(actor, toNativeRoutingId(destNodeRid), request);
      return submitPreparedRequestOperation(operation, callback, 0, timeoutMs);
    };
  }
  return undefined;
}

type ZLinkBindingOperation = { [key: string]: (...args: unknown[]) => unknown };

function isSpotRouteBridgeTarget(target: unknown): boolean {
  return target !== null &&
    typeof target === 'object' &&
    'attachRouterChannel' in target &&
    'handleRouterReceived' in target;
}

function submitSendOperation(operation: unknown, payload: unknown, flags: number): boolean {
  let current = appendOperationParts(operation, payload);
  if (flags !== 0 && hasOperationMethod(current, 'flags')) {
    current = current.flags(flags) as ZLinkBindingOperation;
  }
  if (!hasOperationMethod(current, 'submit')) {
    throw new TypeError('Binding send operation does not expose submit().');
  }
  return Boolean(current.submit());
}

function submitRequestOperation(
  operation: unknown,
  payload: unknown,
  callback: unknown,
  flags: number,
  timeoutMs?: number
): boolean {
  let current = appendOperationParts(operation, payload);
  if (timeoutMs !== undefined && timeoutMs > 0 && hasOperationMethod(current, 'timeout')) {
    current = current.timeout(timeoutMs) as ZLinkBindingOperation;
  }
  if (flags !== 0 && hasOperationMethod(current, 'flags')) {
    current = current.flags(flags) as ZLinkBindingOperation;
  }
  if (!hasOperationMethod(current, 'submit')) {
    throw new TypeError('Binding request operation does not expose submit().');
  }
  return Boolean(current.submit(callback));
}

function submitPreparedRequestOperation(
  operation: unknown,
  callback: unknown,
  flags: number,
  timeoutMs?: number
): boolean {
  let current = operation;
  if (timeoutMs !== undefined && timeoutMs > 0 && hasOperationMethod(current, 'timeout')) {
    current = current.timeout(timeoutMs) as ZLinkBindingOperation;
  }
  if (flags !== 0 && hasOperationMethod(current, 'flags')) {
    current = current.flags(flags) as ZLinkBindingOperation;
  }
  if (!hasOperationMethod(current, 'submit')) {
    throw new TypeError('Binding request operation does not expose submit().');
  }
  return Boolean(current.submit(callback));
}

function appendOperationParts(operation: unknown, payload: unknown): ZLinkBindingOperation {
  const parts = Array.isArray(payload) ? payload : [payload];
  if (parts.length === 0) {
    throw new TypeError('Binding operation payload must contain at least one part.');
  }
  let current: unknown = operation;
  for (const part of parts) {
    if (!hasOperationMethod(current, 'message')) {
      throw new TypeError('Binding operation does not expose message().');
    }
    current = current.message(part);
  }
  return current as ZLinkBindingOperation;
}

function hasOperationMethod(value: unknown, method: string): value is ZLinkBindingOperation {
  return typeof value === 'object' && value !== null && typeof (value as { [key: string]: unknown })[method] === 'function';
}

function isBindingNotFound(error: unknown): boolean {
  return error instanceof zlink.ConfigError && error.result === zlink.ConfigResult.NotFound;
}

function isNonBlockingRecvEmpty(error: unknown): boolean {
  return error instanceof zlink.RecvError &&
    error.result === zlink.RecvResult.NoData;
}

function isRouteRecvRetryable(error: unknown): boolean {
  return isNonBlockingRecvEmpty(error) ||
    (error instanceof zlink.RecvError &&
      (error.result === zlink.RecvResult.InternalError || error.result === zlink.RecvResult.InvalidHandle) &&
      isNativeBadAddress(error));
}

function isNativeBadAddress(error: { nativeErrno?: unknown; message?: unknown }): boolean {
  return error.nativeErrno === 14 ||
    /Bad address/i.test(String(error.message ?? ''));
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
      const sessionRelay = resolveSocketSessionRelayProperty(target, property);
      if (sessionRelay !== undefined) {
        return sessionRelay;
      }
      const value = Reflect.get(target, property, target);
      return typeof value === 'function' ? value.bind(target) : value;
    },
    set(target, property, value, receiver) {
      if (property === 'peerWeight') {
        const options = (target as { options?: { peerWeight?: number } }).options;
        if (options === undefined || !('peerWeight' in options)) {
          return false;
        }
        options.peerWeight = value as number;
        return true;
      }
      const socketOption = socketConfigProperty(property);
      if (socketOption !== undefined) {
        const options = (target as { options?: Record<string, unknown> }).options;
        if (options === undefined || !(socketOption in options)) {
          return false;
        }
        options[socketOption] = value;
        return true;
      }
      return Reflect.set(target, property, value, receiver);
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
      disableSocketLinger(target);
      closeSocketRoutes(target, state.peerRoutingIds);
      closeSocketEndpoints(target, state.boundEndpoints, state.connectedEndpoints);
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
  if (property === 'peerWeight') {
    return (target as { options?: { peerWeight?: number } }).options?.peerWeight ?? 100;
  }
  const socketOption = socketConfigProperty(property);
  if (socketOption !== undefined) {
    return (target as { options?: Record<string, unknown> }).options?.[socketOption];
  }
  return undefined;
}

function socketConfigProperty(property: string | symbol): string | undefined {
  switch (property) {
    case 'sendHighWaterMark':
      return 'sendHwm';
    case 'receiveHighWaterMark':
      return 'recvHwm';
    case 'sendTimeoutMs':
      return 'sendTimeout';
    default:
      return undefined;
  }
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
        peerRoutingIds.add(routingId);
        return submitBindingRequestCallback(
          (target as { request(routingId: unknown): ZLinkBindingRequestOperation }).request(toNativeRoutingId(routingId)),
          payload,
          callback,
          flags,
          timeoutMs
        );
      }
      if (args.length >= 4) {
        const [payload, callback, flags, timeoutMs] = args as [unknown, unknown, number, number | undefined];
        return submitBindingRequestCallback(
          (target as { request(): ZLinkBindingRequestOperation }).request(),
          payload,
          callback,
          flags,
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
      let ok = false;
      try {
        ok = (target as {
          recv(result: unknown, flags?: number): boolean;
        }).recv(received, flags);
      } catch (error) {
        received.close();
        if (isRouteRecvRetryable(error)) {
          return undefined;
        }
        throw error;
      }
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
  if (property === 'sendToSpot') {
    return (targetRid: unknown, spotRid: unknown, payload: unknown, flags: number) =>
      submitBindingSend(
        (target as { sendToSpot(targetRid: unknown, spotRid: unknown): ZLinkBindingSendOperation })
          .sendToSpot(toNativeRoutingId(targetRid), toNativeRoutingId(spotRid)),
        payload,
        flags
      );
  }
  if (property === 'requestToSpot') {
    return (targetRid: unknown, spotRid: unknown, payload: unknown, callback: unknown, flags: number, timeoutMs?: number) =>
      submitBindingRequestCallback(
        (target as { requestToSpot(targetRid: unknown, spotRid: unknown): ZLinkBindingRequestOperation })
          .requestToSpot(toNativeRoutingId(targetRid), toNativeRoutingId(spotRid)),
        payload,
        callback,
        flags,
        timeoutMs
      );
  }
  return undefined;
}

function resolveSocketSessionRelayProperty(target: unknown, property: string | symbol): unknown {
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
      }).bindActor(toNativeRoutingId(sessionRid), toNativeActorRef(actor));
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

export function isDisconnectRouteNotFoundError(error: unknown): boolean {
  return isBindingNotFound(error) || (
    error instanceof Error && 'code' in error &&
    ((error as { code: unknown }).code === zlink.ConnectResult.NotFound ||
      (error as { code: unknown }).code === zlink.ConnectResult.Busy ||
      ((error as { code: unknown }).code === zlink.ConnectResult.InternalError &&
        /current state/i.test(error.message)))
  );
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
      if (isSuccessfulOrAlreadyShutdownCloseError(error)) {
        return;
      }
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
    ([401, 404].includes((error as { code: number }).code));
}

function isSuccessfulOrAlreadyShutdownCloseError(error: unknown): boolean {
  return error instanceof Error && 'code' in error &&
    ([0, 402, 403].includes((error as { code: number }).code));
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
  flags: number,
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
  current ??= operation.message(Buffer.alloc(0));
  if (timeoutMs !== undefined) {
    current = current.timeout(timeoutMs);
  }
  if (flags !== 0) {
    return current.flags(flags).submit(callback);
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

function toNativeTopologyFilter(filter: unknown): unknown {
  if (filter === undefined || filter === null || typeof filter !== 'object' || !('routingId' in filter)) {
    return filter;
  }
  return {
    ...(filter as Record<string, unknown>),
    routingId: toNativeRoutingId((filter as { routingId?: unknown }).routingId)
  };
}

function toFrameworkRoutingIdEntries(entries: unknown): unknown {
  if (!Array.isArray(entries)) {
    return entries;
  }
  return entries.map((entry) => {
    if (entry === null || typeof entry !== 'object' || !('routingId' in entry)) {
      return entry;
    }
    const routingId = (entry as { routingId?: unknown }).routingId;
    return {
      ...(entry as Record<string, unknown>),
      routingId: routingId === undefined || routingId === null ? undefined : String(routingId)
    };
  });
}

function toNativeActorRef(actor: unknown): unknown {
  if (typeof actor !== 'object' || actor === null || !('nodeRid' in actor)) {
    return actor;
  }
  return {
    ...(actor as Record<string, unknown>),
    nodeRid: toNativeRoutingId((actor as { nodeRid: unknown }).nodeRid)
  };
}
