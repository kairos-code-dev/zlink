import type {
  ZLinkBackendRouterSocket,
  ZLinkBackendSpot,
  ZLinkBackendSpotNode,
  ZLinkBackendSpotRouteBridge
} from '../contracts';
import {
  closeBindingMessages,
  closeWithBusyRetry,
  disableSocketLinger,
  hasOperationMethod,
  isBindingNotFound,
  isRouteRecvRetryable,
  submitActorSendOperation,
  submitPreparedRequestOperation,
  submitRequestOperation,
  submitSendOperation,
  toNativeActorRef,
  toNativeRoutingId,
  unwrapBackendObject,
  waitForBindingOperation,
  withNativeFallback,
  zlink,
  type ZLinkBindingModule
} from './node-backend-adapter-support';

type ZLinkBindingSpotNode = ReturnType<ZLinkBindingModule['createSpotNode']>;
type ZLinkBindingSpot = ReturnType<ZLinkBindingSpotNode['createSpot']>;
type ZLinkBindingSpotRouteBridge = ReturnType<ZLinkBindingSpotNode['createRouteBridge']>;
type ZLinkBindingActor = ReturnType<ZLinkBindingSpotNode['createActor']>;

export function wrapSpotNode(nativeInstance: ZLinkBindingSpotNode): ZLinkBackendSpotNode {
  const actors = new Map<string, ZLinkBindingActor>();
  const adapter = {
    nativeInstance,
    async dispose(): Promise<void> {
      disableSocketLinger(nativeInstance);
      await closeWithBusyRetry(nativeInstance);
    },
    createRouteBridge(): ZLinkBackendSpotRouteBridge {
      return wrapSpotRouteBridge(nativeInstance.createRouteBridge());
    },
    setRoutingId(routingId: unknown): void {
      nativeInstance.setRoutingId(toNativeRoutingId(routingId) as never);
    },
    setPublisherRoutingId(routingId: unknown): void {
      nativeInstance.setPublisherRoutingId(toNativeRoutingId(routingId) as never);
    },
    setSubscriberRoutingId(routingId: unknown): void {
      nativeInstance.setSubscriberRoutingId(toNativeRoutingId(routingId) as never);
    },
    connectPeerRid(targetNodeRid: unknown, endpoint: string): void {
      nativeInstance.connectPeerRid(toNativeRoutingId(targetNodeRid) as never, endpoint);
    },
    disconnectPeerRid(targetNodeRid: unknown): void {
      nativeInstance.disconnectPeerRid(toNativeRoutingId(targetNodeRid) as never);
    },
    createSpot(): ZLinkBackendSpot {
      return wrapSpot(nativeInstance.createSpot());
    },
    entrySpot(): ZLinkBackendSpot {
      return wrapSpot(nativeInstance.entrySpot());
    },
    getOrCreateSpot(spotRid: unknown): { readonly spot: ZLinkBackendSpot; readonly created: boolean } {
      const result = nativeInstance.getOrCreateSpot(toNativeRoutingId(spotRid) as never);
      return { ...result, spot: wrapSpot(result.spot) };
    },
    createActor(actorId: string, request?: unknown): unknown {
      const actor = nativeInstance.createActor(actorId, request as never);
      actors.set(actor.actorRef.actorId, actor);
      return actor.actorRef;
    },
    actorLookup(actorId: string): unknown {
      try {
        return nativeInstance.actorLookup(actorId);
      } catch (error) {
        if (isBindingNotFound(error)) {
          return undefined;
        }
        throw error;
      }
    },
    async destroyActor(actor: unknown, timeoutMs: number, signal?: AbortSignal): Promise<void> {
      signal?.throwIfAborted();
      const actorRef = toNativeActorRef(actor) as { readonly actorId?: string };
      const operation = nativeInstance.destroyActor(actorRef as never).timeout(timeoutMs);
      const pending = operation.submit();
      const actorId = actorRef.actorId;
      if (actorId !== undefined) {
        void pending.then(
          () => actors.delete(actorId),
          () => undefined
        );
      }
      const replies = await waitForBindingOperation(pending, signal, closeBindingMessages);
      closeBindingMessages(replies);
    },
    async closeActorBoundSession(actor: unknown, timeoutMs: number, signal?: AbortSignal): Promise<void> {
      signal?.throwIfAborted();
      const actorId = (actor as { readonly actorId?: unknown } | null)?.actorId;
      if (typeof actorId !== 'string') {
        throw new TypeError('Actor bound-session close requires an actor id.');
      }
      const localActor = actors.get(actorId);
      if (localActor === undefined) {
        throw new zlink.ConfigError(zlink.ConfigResult.NotFound, 0);
      }
      localActor.closeBoundSession(timeoutMs);
    },
    ...createPacketMessagingAdapter(nativeInstance),
    sendActorBoundSession(actor: unknown, payload: unknown, flags: number): boolean {
      return submitSendOperation(nativeInstance.sendActorBoundSession(toNativeActorRef(actor) as never), payload, flags);
    },
    sendToActor(actor: unknown, payload: unknown, flags: number): boolean | Promise<boolean> {
      const actorRef = toNativeActorRef(actor);
      if (hasOperationMethod(nativeInstance, 'sendToActorCallback')) {
        return submitActorSendOperation(nativeInstance, actorRef, payload, flags);
      }
      return submitSendOperation(nativeInstance.sendToActor(actorRef as never), payload, flags);
    },
    requestToActor(
      actor: unknown,
      payload: unknown,
      callback: unknown,
      flags: number,
      timeoutMs?: number
    ): boolean {
      return submitRequestOperation(
        nativeInstance.requestToActor(toNativeActorRef(actor) as never),
        payload,
        callback,
        flags,
        timeoutMs
      );
    },
    bindRemoteActorSession(actor: unknown, sourceNodeRid: unknown, sourceSessionRid: unknown): void {
      nativeInstance.bindRemoteActorSession(
        toNativeActorRef(actor) as never,
        toNativeRoutingId(sourceNodeRid) as never,
        toNativeRoutingId(sourceSessionRid) as never
      );
    },
    replyActorNoBind(info: unknown, parts: unknown, result: unknown): void {
      nativeInstance.replyActorNoBind(info as never, parts as never, result as never);
    },
    joinActor(
      actor: unknown,
      destNodeRid: unknown,
      destSpotRid: unknown,
      payload: unknown,
      callback: unknown,
      timeoutMs?: number
    ): boolean {
      const operation = nativeInstance.joinActor(
        toNativeActorRef(actor) as never,
        toNativeRoutingId(destNodeRid) as never,
        toNativeRoutingId(destSpotRid) as never
      );
      return submitRequestOperation(operation, payload, callback, 0, timeoutMs);
    },
    joinActorEntrySpot(
      actor: unknown,
      destNodeRid: unknown,
      request: unknown,
      callback: unknown,
      timeoutMs?: number
    ): boolean {
      const operation = nativeInstance.joinActorEntrySpot(
        toNativeActorRef(actor) as never,
        toNativeRoutingId(destNodeRid) as never,
        request as never
      );
      return submitPreparedRequestOperation(operation, callback, 0, timeoutMs);
    }
  };
  return withNativeFallback(adapter, nativeInstance) as unknown as ZLinkBackendSpotNode;
}

function wrapSpot(nativeInstance: ZLinkBindingSpot): ZLinkBackendSpot {
  const adapter = {
    nativeInstance,
    async dispose(): Promise<void> {
      disableSocketLinger(nativeInstance);
      await closeWithBusyRetry(nativeInstance);
    },
    setRoutingId(routingId: unknown): void {
      nativeInstance.setRoutingId(toNativeRoutingId(routingId) as never);
    },
    ...createPacketMessagingAdapter(nativeInstance)
  };
  return withNativeFallback(adapter, nativeInstance) as unknown as ZLinkBackendSpot;
}

function wrapSpotRouteBridge(nativeInstance: ZLinkBindingSpotRouteBridge): ZLinkBackendSpotRouteBridge {
  const adapter = {
    nativeInstance,
    async dispose(): Promise<void> {
      disableSocketLinger(nativeInstance);
      await closeWithBusyRetry(nativeInstance);
    },
    attachRouterChannel(
      channelName: string,
      router: ZLinkBackendRouterSocket,
      options?: { readonly capabilities?: number }
    ): void {
      nativeInstance.attachRouterChannel(channelName, unwrapBackendObject(router) as never, options);
    },
    handleRouterReceived(
      channelName: string,
      sourceNodeRid: unknown,
      requestSeq: bigint | number,
      parts: readonly unknown[]
    ): boolean {
      return nativeInstance.handleRouterReceived(
        channelName,
        toNativeRoutingId(sourceNodeRid) as never,
        BigInt(requestSeq),
        parts as never
      );
    },
    send(channelName: string, targetNodeRid: unknown, targetSpotRid: unknown): unknown {
      return nativeInstance.send(
        channelName,
        toNativeRoutingId(targetNodeRid) as never,
        toNativeRoutingId(targetSpotRid) as never
      );
    },
    request(channelName: string, targetNodeRid: unknown, targetSpotRid: unknown): unknown {
      return nativeInstance.request(
        channelName,
        toNativeRoutingId(targetNodeRid) as never,
        toNativeRoutingId(targetSpotRid) as never
      );
    }
  };
  return withNativeFallback(adapter, nativeInstance) as unknown as ZLinkBackendSpotRouteBridge;
}

function createPacketMessagingAdapter(target: unknown) {
  const messaging = target as {
    sendToChannel(channelName: string): unknown;
    requestToChannel(channelName: string): unknown;
    publish(topic: string): unknown;
    sendToSpot(targetRid: unknown, targetSpot: unknown): unknown;
    requestToSpot(targetRid: unknown, targetSpot: unknown): unknown;
    recvRouted(received: unknown, flags: number): boolean;
  };
  return {
    sendToChannel(channelName: string, payload: unknown, flags: number): boolean {
      return submitSendOperation(messaging.sendToChannel(channelName), payload, flags);
    },
    requestToChannel(
      channelName: string,
      payload: unknown,
      callback: unknown,
      flags: number,
      timeoutMs?: number
    ): boolean {
      return submitRequestOperation(messaging.requestToChannel(channelName), payload, callback, flags, timeoutMs);
    },
    publish(topic: string, payload: unknown, flags: number): boolean {
      return submitSendOperation(messaging.publish(topic), payload, flags);
    },
    sendToSpot(targetRid: unknown, targetSpot: unknown, payload: unknown, flags: number): boolean {
      return submitSendOperation(
        messaging.sendToSpot(toNativeRoutingId(targetRid), toNativeRoutingId(targetSpot)),
        payload,
        flags
      );
    },
    requestToSpot(
      targetRid: unknown,
      targetSpot: unknown,
      payload: unknown,
      callback: unknown,
      flags: number,
      timeoutMs?: number
    ): boolean {
      return submitRequestOperation(
        messaging.requestToSpot(toNativeRoutingId(targetRid), toNativeRoutingId(targetSpot)),
        payload,
        callback,
        flags,
        timeoutMs
      );
    },
    recvRoute(received: unknown, flags: number): boolean {
      try {
        return messaging.recvRouted(received, flags);
      } catch (error) {
        if (isRouteRecvRetryable(error)) {
          return false;
        }
        throw error;
      }
    }
  };
}
