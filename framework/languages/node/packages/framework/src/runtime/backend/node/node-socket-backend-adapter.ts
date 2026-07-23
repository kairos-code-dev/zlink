import type { ZLinkBackendObject } from '../contracts';
import {
  closeBindingMessages,
  closeWithBusyRetry,
  disableSocketLinger,
  isBindingNotFound,
  isContextTerminatedError,
  isRouteRecvRetryable,
  submitBindingRequest,
  submitBindingRequestCallback,
  submitBindingSend,
  toNativeActorRef,
  toNativeRoutingId,
  waitForBindingOperation,
  withNativeFallback,
  zlink,
  type ZLinkBindingPromiseRequestSubmitOperation,
  type ZLinkBindingRequestOperation,
  type ZLinkBindingSendOperation
} from './node-backend-adapter-support';

export function wrapSocket<T extends { close(): void }>(nativeInstance: T): T & ZLinkBackendObject {
  const boundEndpoints = new Set<string>();
  const connectedEndpoints = new Set<string>();
  const peerRoutingIds = new Set<unknown>();
  const socket = nativeInstance as T & {
    options?: {
      peerWeight?: number;
      sendHwm?: number;
      recvHwm?: number;
      sendTimeout?: number;
      maxMsgSize?: bigint;
      lastEndpoint?: string;
    };
  };
  const adapter = {
    nativeInstance,
    async dispose(): Promise<void> {
      disableSocketLinger(nativeInstance);
      closeSocketRoutes(nativeInstance, peerRoutingIds);
      closeSocketEndpoints(nativeInstance, boundEndpoints, connectedEndpoints);
      await closeWithBusyRetry(nativeInstance);
    },
    close(): void {
      nativeInstance.close();
    },
    bind(endpoint: string): void {
      (nativeInstance as T & { bind(endpoint: string): void }).bind(endpoint);
      boundEndpoints.add(endpoint);
    },
    unbind(endpoint: string): void {
      (nativeInstance as T & { unbind(endpoint: string): void }).unbind(endpoint);
      boundEndpoints.delete(endpoint);
    },
    connect(endpoint: string): void {
      (nativeInstance as T & { connect(endpoint: string): void }).connect(endpoint);
      connectedEndpoints.add(endpoint);
    },
    disconnect(endpoint: string): void {
      (nativeInstance as T & { disconnect(endpoint: string): void }).disconnect(endpoint);
      connectedEndpoints.delete(endpoint);
    },
    setChannelName(channelName: string): void {
      const setChannelName = (nativeInstance as T & { setChannelName?: (value: string) => void }).setChannelName;
      setChannelName?.call(nativeInstance, channelName);
    },
    get lastEndpoint(): string | undefined {
      return socket.options?.lastEndpoint;
    },
    setRoutingId(routingId: unknown): void {
      (nativeInstance as T & { setRoutingId(value: unknown): void }).setRoutingId(toNativeRoutingId(routingId));
    },
    onSendReady(handler: () => void): void {
      (nativeInstance as T & { setSendReadyHandler(value: () => void): void }).setSendReadyHandler(handler);
    },
    get peerWeight(): number {
      return socket.options?.peerWeight ?? 100;
    },
    set peerWeight(value: number) {
      requireSocketOptions(socket).peerWeight = value;
    },
    get sendHighWaterMark(): number {
      return socket.options?.sendHwm ?? 0;
    },
    set sendHighWaterMark(value: number) {
      requireSocketOptions(socket).sendHwm = value;
    },
    get receiveHighWaterMark(): number {
      return socket.options?.recvHwm ?? 0;
    },
    set receiveHighWaterMark(value: number) {
      requireSocketOptions(socket).recvHwm = value;
    },
    get sendTimeoutMs(): number {
      return socket.options?.sendTimeout ?? 0;
    },
    set sendTimeoutMs(value: number) {
      requireSocketOptions(socket).sendTimeout = value;
    },
    get maxMessageSize(): number {
      return Number(socket.options?.maxMsgSize ?? 0n);
    },
    set maxMessageSize(value: number) {
      requireSocketOptions(socket).maxMsgSize = BigInt(value);
    },
    send(...args: unknown[]): unknown {
      if (args.length >= 3) {
        const [routingId, payload, flags] = args as [unknown, unknown, number];
        peerRoutingIds.add(routingId);
        return submitBindingSend(
          (nativeInstance as T & { send(routingId: unknown): ZLinkBindingSendOperation })
            .send(toNativeRoutingId(routingId)),
          payload,
          flags
        );
      }
      const [payload, flags] = args as [unknown, number | undefined];
      return submitBindingSend(
        (nativeInstance as T & { send(): ZLinkBindingSendOperation }).send(),
        payload,
        flags ?? 0
      );
    },
    request(...args: unknown[]): unknown {
      if (args.length >= 5) {
        const [routingId, payload, callback, flags, timeoutMs] = args as [
          unknown, unknown, unknown, number, number | undefined
        ];
        peerRoutingIds.add(routingId);
        return submitBindingRequestCallback(
          (nativeInstance as T & { request(routingId: unknown): ZLinkBindingRequestOperation })
            .request(toNativeRoutingId(routingId)),
          payload,
          callback,
          flags,
          timeoutMs
        );
      }
      if (args.length >= 4) {
        const [payload, callback, flags, timeoutMs] = args as [unknown, unknown, number, number | undefined];
        return submitBindingRequestCallback(
          (nativeInstance as T & { request(): ZLinkBindingRequestOperation }).request(),
          payload,
          callback,
          flags,
          timeoutMs
        );
      }
      return (nativeInstance as T & { request(...values: unknown[]): unknown }).request(...args);
    },
    reply(...args: unknown[]): unknown {
      const [routingId, requestSeq, payload] = args as [unknown, bigint, unknown];
      peerRoutingIds.add(routingId);
      const operation = (nativeInstance as T & {
        reply(routingId: unknown, requestSeq: bigint): ZLinkBindingSendOperation;
      }).reply(toNativeRoutingId(routingId), requestSeq);
      return args.length < 3 ? operation : submitBindingSend(operation, payload, 0);
    },
    recv(flags?: number): unknown {
      const received = new zlink.Received();
      let ok = false;
      try {
        ok = (nativeInstance as T & { recv(result: unknown, flags?: number): boolean }).recv(received, flags);
      } catch (error) {
        received.close();
        if (isRouteRecvRetryable(error)) {
          return undefined;
        }
        throw error;
      }
      return ok ? received : undefined;
    },
    publish(...args: unknown[]): unknown {
      if (args.length >= 3) {
        const [topic, payload, flags] = args as [string, unknown, number];
        return submitBindingSend(
          (nativeInstance as T & { publish(topic: string): ZLinkBindingSendOperation }).publish(topic),
          payload,
          flags
        );
      }
      return (nativeInstance as T & { publish(...values: unknown[]): unknown }).publish(...args);
    },
    sendToSpot(targetRid: unknown, targetSpot: unknown, payload: unknown, flags: number): boolean {
      return submitBindingSend(
        (nativeInstance as T & {
          sendToSpot(targetRid: unknown, targetSpot: unknown): ZLinkBindingSendOperation;
        }).sendToSpot(toNativeRoutingId(targetRid), toNativeRoutingId(targetSpot)),
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
      return submitBindingRequestCallback(
        (nativeInstance as T & {
          requestToSpot(targetRid: unknown, targetSpot: unknown): ZLinkBindingRequestOperation;
        }).requestToSpot(toNativeRoutingId(targetRid), toNativeRoutingId(targetSpot)),
        payload,
        callback,
        flags,
        timeoutMs
      );
    },
    disconnectPeer(routingId: unknown): void {
      (nativeInstance as T & { disconnectRid(value: unknown): void }).disconnectRid(toNativeRoutingId(routingId));
    },
    onFramedPacket(handler: unknown): void {
      (nativeInstance as T & { setPacketHandler(value: unknown): void }).setPacketHandler(handler);
    },
    async bindActor(sessionRid: unknown, actor: unknown, timeoutMs: number, signal?: AbortSignal): Promise<void> {
      const operation = (nativeInstance as T & {
        bindActor(sessionRid: unknown, actor: unknown): {
          timeout(value: number): ZLinkBindingPromiseRequestSubmitOperation<Array<{ close(): void }>>;
        };
      }).bindActor(toNativeRoutingId(sessionRid), toNativeActorRef(actor)).timeout(timeoutMs);
      const replies = await waitForBindingOperation(submitBindingRequest(operation), signal, closeBindingMessages);
      closeBindingMessages(replies);
    },
    async unbindActor(sessionRid: unknown, actorId: string, timeoutMs: number, signal?: AbortSignal): Promise<void> {
      const operation = (nativeInstance as T & {
        unbindActor(sessionRid: unknown, actorId: string): {
          timeout(value: number): ZLinkBindingPromiseRequestSubmitOperation<Array<{ close(): void }>>;
        };
      }).unbindActor(toNativeRoutingId(sessionRid), actorId).timeout(timeoutMs);
      const replies = await waitForBindingOperation(submitBindingRequest(operation), signal, closeBindingMessages);
      closeBindingMessages(replies);
    },
    sendBoundActor(sessionRid: unknown, actorId: string, parts: readonly unknown[], flags: number): boolean {
      const operation = (nativeInstance as T & {
        sendBoundActor(sessionRid: unknown, actorId: string): ZLinkBindingSendOperation;
      }).sendBoundActor(toNativeRoutingId(sessionRid), actorId);
      let submitter = operation;
      for (const part of parts) {
        submitter = submitter.message(part);
      }
      return submitter.flags(flags).submit();
    }
  };
  return withNativeFallback(adapter, nativeInstance) as unknown as T & ZLinkBackendObject;
}

function requireSocketOptions<TOptions>(socket: { readonly options?: TOptions }): TOptions {
  if (socket.options === undefined) {
    throw new TypeError('Binding socket does not expose options.');
  }
  return socket.options;
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
  return isBindingNotFound(error) || isContextTerminatedError(error) || (
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
  return isContextTerminatedError(error) || (
    error instanceof Error && 'code' in error &&
    ((error as { code: unknown }).code === 604 || (error as { code: unknown }).code === zlink.ConnectResult.NotFound)
  );
}
