// SPDX-License-Identifier: MPL-2.0

import { NativeHandle } from '../../handles/native_handle';
import { requireNative } from '../../native/native';
import { closeCall, configCall, handlerCall, recvNativeError, submitNativeError } from '../../errors/native_errors';
import { validateCString } from '../../options/validation';
import { executeNativeRequest } from '../../messaging/request_executor';
import { normalizeRoutingId } from '../../core/routing_id';
import { startRequestProgress } from '../../messaging/request_progress';
import { adoptTopicMessage, materializeReceived, materializeTopicMessage, type NativeTopicMessageRaw } from '../../messaging/message_materializer';
import { int32Buffer, readInt32Option } from '../../sockets/socket_options';
import { RuntimeReplyOperation, RuntimeRequestOperation, RuntimeSendOperation, normalizeReplyFlags, submitErrorFromResult } from '../../sockets/socket_operations';
import { toMessageParts, toOwnedMessage } from '../../buffers/message_conversion';
import { Message, Received, RoutingId, TopicMessage, type MessageLike, type MessageSnapshot } from '../../../contracts';
import { RecvFlags, SendFlags } from '../../../contracts/sockets/socket_constants';
import { SubmitResult } from '../../../contracts/errors/errors';
import { SpotDispatchEvent, SpotDispatchSubjectKind, type ActorJoinRequest, type ActorJoinReplyOperation, type ActorPart, type ActorRef, type ReplyOperation, type RequestCallback, type RequestOperation, type SendOperation, type SpotActorLifecycleEvent, type SpotDispatchEventHandler, type SpotSendReadyHandler, type SubscriptionEntry } from '../../../contracts/service';
import { SpotOption } from './spot_options';
import { RuntimeActorJoinReplyOperation, actorJoinInfoFromRaw, actorJoinInfoToRaw, actorPartFromRaw, actorRefFromRaw, spotActorLifecycleInfoFromRaw, type SpotActorLifecycleInfoRaw } from './spot_operations';

type OwnerSpotNode = { nativeHandle(): unknown; readonly routingId: RoutingId; unregisterSpot(spot: Spot): void };

export class Spot extends NativeHandle {
  private static readonly CREATE_TOKEN = Symbol('Spot.create');
  private readonly _node: OwnerSpotNode;
  private constructor(node: OwnerSpotNode, token: symbol, native?: unknown) {
    if (token !== Spot.CREATE_TOKEN) {
      throw new TypeError('Spot instances must be created with SpotNode.createSpot()');
    }
    super(native ?? requireNative().spotNew(node.nativeHandle()));
    this._node = node;
  }

  /** @internal */
  static create(node: OwnerSpotNode): Spot {
    return new Spot(node, Spot.CREATE_TOKEN);
  }
  /** @internal */
  static fromNative(node: OwnerSpotNode, native: unknown): Spot {
    return new Spot(node, Spot.CREATE_TOKEN, native);
  }
  /** @internal */
  ownerNodeRoutingId(): RoutingId {
    return this._node.routingId;
  }
  setRoutingId(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('spot routing id set failed', () => {
      requireNative().handleSetRoutingId(this._native, normalizedRoutingId);
    });
  }
  get routingId(): RoutingId {
    return RoutingId.from(configCall('spot routing id get failed', () =>
      requireNative().handleGetRoutingId(this._native) as Buffer
    ));
  }
  get requestTimeout(): number {
    return readInt32Option(configCall('spot request timeout get failed', () =>
      requireNative().spotGetOption(this._native, SpotOption.REQUEST_TIMEOUT_MS) as Buffer
    ), 'requestTimeout');
  }
  set requestTimeout(value: number) {
    const buffer = int32Buffer(value, 'requestTimeout');
    configCall('spot request timeout set failed', () => {
      requireNative().spotSetOption(this._native, SpotOption.REQUEST_TIMEOUT_MS, buffer);
    });
  }
  publish(topic: string): SendOperation {
    return new RuntimeSendOperation((parts, opFlags) => this.publishDirect(topic, parts, opFlags));
  }
  private submitSpotSend(flags: SendFlags, errorMessage: string, invoke: () => void): boolean {
    try {
      invoke();
      return true;
    } catch (error) {
      const submitError = submitNativeError(error, flags, errorMessage);
      if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
        return false;
      }
      throw submitError;
    }
  }
  private publishDirect(topic: string, payloadParts: readonly MessageLike[], flags: SendFlags): boolean {
    return this.submitSpotSend(flags, 'spot publish failed', () => {
      requireNative().spotPublish(
        this._native,
        validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER),
        toMessageParts(payloadParts),
        flags | 0
      );
    });
  }
  setSubscription(topicOrPattern: string): void {
    const normalized = validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER);
    configCall('spot subscription set failed', () => {
      requireNative().spotSubscribe(this._native, normalized);
    });
  }
  unsetSubscription(topicOrPattern: string): void {
    const normalized = validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER);
    configCall('spot subscription unset failed', () => {
      requireNative().spotUnsubscribe(this._native, normalized);
    });
  }
  subscriptionAt(index: number): SubscriptionEntry | null {
    return configCall('spot subscription lookup failed', () =>
      requireNative().subscriptionAt(this._native, index >>> 0) as SubscriptionEntry | null
    );
  }
  subscribe(result: TopicMessage, flags?: RecvFlags): boolean;
  subscribe(resultOrFlags: TopicMessage | RecvFlags = RecvFlags.None,
            maybeFlags: RecvFlags = RecvFlags.None): TopicMessage | null | boolean {
    const hasResult = resultOrFlags instanceof TopicMessage;
    const flags = hasResult ? maybeFlags : resultOrFlags as RecvFlags;
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().spotRecvNoWait(this._native) as NativeTopicMessageRaw | null
        : requireNative().spotRecv(this._native, flags | 0) as NativeTopicMessageRaw | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'subscribe failed');
    }
    if (!raw) {
      return hasResult ? false : null;
    }
    if (hasResult) {
      adoptTopicMessage(resultOrFlags, raw);
      return true;
    }
    return materializeTopicMessage(raw);
  }
  setSendReadyHandler(handler: SpotSendReadyHandler): void {
    handlerCall('spot send-ready handler registration failed', () => {
      requireNative().spotSendReadyHandler(this._native, handler);
    });
  }
  sendToChannel(channelName: string): SendOperation {
    return new RuntimeSendOperation((parts, opFlags) => this.sendChannelDirect(channelName, parts, opFlags));
  }
  private sendChannelDirect(channelName: string, payloadParts: readonly MessageLike[], flags: SendFlags): boolean {
    return this.submitSpotSend(flags, 'spot sendToChannel failed', () => {
      requireNative().spotSendChannel(
        this._native,
        validateCString(channelName, 'channelName', Number.MAX_SAFE_INTEGER),
        toMessageParts(payloadParts),
        flags | 0
      );
    });
  }
  sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): SendOperation {
    return new RuntimeSendOperation((parts, opFlags) => this.sendToSpotDirect(destNodeRid, destSpotRid, parts, opFlags));
  }
  private sendToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, payloadParts: readonly MessageLike[], flags: SendFlags): boolean {
    return this.submitSpotSend(flags, 'spot sendToSpot failed', () => {
      requireNative().spotSendToSpot(
        this._native,
        normalizeRoutingId(destNodeRid),
        normalizeRoutingId(destSpotRid),
        toMessageParts(payloadParts),
        flags | 0
      );
    });
  }
  requestToChannel(channelName: string): RequestOperation {
    return new RuntimeRequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestChannelDirect(channelName, parts, cbOrTimeout, opFlags, opTimeout)
    );
  }
  private requestChannelDirect(channelName: string, partsInput: readonly MessageLike[], callbackOrTimeout?: RequestCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
    const normalizedChannelName = validateCString(channelName, 'channelName', Number.MAX_SAFE_INTEGER);
    return this.executeSpotRequest(
      partsInput,
      callbackOrTimeout,
      flagsOrTimeout,
      maybeTimeout,
      'requestToChannel failed',
      (spotHandle, parts, callback, flags, timeoutMs) => {
        requireNative().spotRequestChannel(
          spotHandle,
          normalizedChannelName,
          parts,
          callback,
          flags | 0,
          timeoutMs | 0
        );
      }
    );
  }
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): RequestOperation {
    return new RuntimeRequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestToSpotDirect(destNodeRid, destSpotRid, parts, cbOrTimeout, opFlags, opTimeout)
    );
  }
  requestToRouter(peerRid: RoutingId): RequestOperation {
    return new RuntimeRequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestToRouterDirect(peerRid, parts, cbOrTimeout, opFlags, opTimeout)
    );
  }
  private requestToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, partsInput: readonly MessageLike[], callbackOrTimeout?: RequestCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
    const nodeRid = normalizeRoutingId(destNodeRid, 'destNodeRid');
    const spotRid = normalizeRoutingId(destSpotRid, 'destSpotRid');
    return this.executeSpotRequest(
      partsInput,
      callbackOrTimeout,
      flagsOrTimeout,
      maybeTimeout,
      'requestToSpot failed',
      (spotHandle, parts, callback, flags, timeoutMs) => {
        requireNative().spotRequestSpot(
          spotHandle,
          nodeRid,
          spotRid,
          parts,
          callback,
          flags | 0,
          timeoutMs | 0
        );
      }
    );
  }
  private requestToRouterDirect(peerRid: RoutingId, partsInput: readonly MessageLike[], callbackOrTimeout?: RequestCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
    const peer = normalizeRoutingId(peerRid, 'peerRid');
    return this.executeSpotRequest(
      partsInput,
      callbackOrTimeout,
      flagsOrTimeout,
      maybeTimeout,
      'requestToRouter failed',
      (spotHandle, parts, callback, flags, timeoutMs) => {
        requireNative().spotRequestRouter(
          spotHandle,
          peer,
          parts,
          callback,
          flags | 0,
          timeoutMs | 0
        );
      }
    );
  }
  private executeSpotRequest(
    partsInput: readonly MessageLike[],
    callbackOrTimeout: RequestCallback | number | undefined,
    flagsOrTimeout: SendFlags | number | undefined,
    maybeTimeout: number | undefined,
    errorMessage: string,
    invoke: (
      spotHandle: unknown,
      parts: ReturnType<typeof toMessageParts>,
      callback: (result: number, replyParts: Buffer[] | null) => void,
      flags: SendFlags,
      timeoutMs: number
    ) => void,
  ): Promise<Message[]> | boolean {
    const parts = toMessageParts(partsInput);
    const spotHandle = this._native;
    return executeNativeRequest({
      callbackOrTimeout,
      flagsOrTimeout,
      maybeTimeout,
      promiseTimeoutMayUseFlagsOrTimeout: true,
      startProgress: () => startRequestProgress(spotHandle),
      invoke: (callback, flags, timeoutMs) => {
        invoke(spotHandle, parts, callback, flags, timeoutMs);
      },
      submitErrorMessage: errorMessage,
      requestErrorMessage: errorMessage
    });
  }
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint): ReplyOperation {
    return new RuntimeReplyOperation((parts, opFlags) => this.replyToSpotInternal(destNodeRid, destSpotRid, requestSeq, parts.map(toOwnedMessage), opFlags));
  }
  replyToRouter(peerRid: RoutingId, requestSeq: bigint): ReplyOperation {
    return new RuntimeReplyOperation((parts, opFlags) => this.replyToRouterInternal(peerRid, requestSeq, parts.map(toOwnedMessage), opFlags));
  }
  private replyToSpotInternal(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint, parts: readonly Message[], flags: SendFlags): void {
    normalizeReplyFlags(flags);
    const normalizedDestNodeRid = normalizeRoutingId(destNodeRid);
    const normalizedDestSpotRid = normalizeRoutingId(destSpotRid);
    try {
      requireNative().spotReplySpot(
        this._native,
        normalizedDestNodeRid,
        normalizedDestSpotRid,
        requestSeq,
        parts.map((part) => part.data())
      );
    } catch (error) {
      throw submitNativeError(error, flags, 'spot replyToSpot failed');
    }
  }
  private replyToRouterInternal(peerRid: RoutingId, requestSeq: bigint, parts: readonly Message[], flags: SendFlags): void {
    normalizeReplyFlags(flags);
    const normalizedPeerRid = normalizeRoutingId(peerRid);
    try {
      requireNative().spotReplyRouter(
        this._native,
        normalizedPeerRid,
        requestSeq,
        parts.map((part) => part.data())
      );
    } catch (error) {
      throw submitNativeError(error, flags, 'spot replyToRouter failed');
    }
  }
  recvRouted(result: Received, flags?: RecvFlags): boolean;
  recvRouted(result: Received, flags: RecvFlags = RecvFlags.None): boolean {
    let raw;
    try {
      raw = requireNative().spotRecvRouted(this._native, flags | 0) as { sourceRid?: Buffer | null; spotRid?: Buffer | null; requestSeq?: bigint | null; parts: MessageSnapshot[] } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'recvRouted failed');
    }
    if (!raw) return false;
    const received = materializeReceived(
      {
        parts: raw.parts,
        routingId: raw.sourceRid ?? null,
        requestSeq: raw.requestSeq ?? null,
        spotRid: raw.spotRid ?? null
      },
      (requestSeq, parts, flags) => {
        if (!raw.sourceRid) {
          throw submitErrorFromResult(SubmitResult.InvalidState, 'missing routed reply target');
        }
        const sourceRid = RoutingId.from(raw.sourceRid);
        if (raw.spotRid) {
          this.replyToSpotInternal(sourceRid, RoutingId.from(raw.spotRid), requestSeq, parts, flags);
          return;
        }
        this.replyToRouterInternal(sourceRid, requestSeq, parts, flags);
      },
      (parts, flags) => {
        if (!raw.sourceRid || !raw.spotRid) {
          throw submitErrorFromResult(SubmitResult.InvalidState, 'missing routed send target');
        }
        return this.sendToSpotDirect(
          RoutingId.from(raw.sourceRid),
          RoutingId.from(raw.spotRid),
          parts,
          flags
        );
      }
    );
    result._adoptFrom(received);
    return true;
  }
  setDispatchHandler(handler: SpotDispatchEventHandler): void {
    handlerCall('spot dispatch handler registration failed', () => {
      requireNative().spotDispatchEventHandler(this._native, this._node.nativeHandle(), (raw: {
        event: number;
        subjectKind: number;
        subjectHandle: bigint;
        actorParts?: Array<{
          info: {
            actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
            sourceNodeRid: Buffer;
            sourceSessionRid: Buffer;
            flags: number;
          };
          message: Buffer;
          more: boolean;
        }>;
      }) => {
        const actorParts = (raw.actorParts ?? []).map((part) => actorPartFromRaw(part));
        const actorRef = actorParts[0]?.info.actor ?? null;
        let index = 0;
        handler({
          event: raw.event as SpotDispatchEvent,
          subjectKind: raw.subjectKind as SpotDispatchSubjectKind,
          timer: null,
          actorRef,
          recvActorPart(flags: RecvFlags = RecvFlags.None): ActorPart | null {
            const part = actorParts[index++] ?? null;
            if (!part && ((flags | 0) & (RecvFlags.DontWait | 0))) {
              return null;
            }
            return part;
          }
        });
      });
    });
  }
  recvActorJoin(flags: RecvFlags = RecvFlags.None): ActorJoinRequest | null {
    let raw;
    try {
      raw = requireNative().spotActorJoinRecv(this._native, flags | 0) as {
        info: {
          actor?: { nodeRid: Buffer; actorId: string; generation: bigint | number };
          sourceActor?: { nodeRid: Buffer; actorId: string; generation: bigint | number };
          targetActor?: { nodeRid: Buffer; actorId: string; generation: bigint | number };
          sourceNodeRid: Buffer;
          sourceSpotRid?: Buffer | null;
          targetNodeRid?: Buffer | null;
          targetSpotRid?: Buffer | null;
          joinEpoch?: bigint | number;
          flags: number;
          requestHandle: bigint;
        };
        message: MessageSnapshot;
      } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'actor join recv failed');
    }
    if (!raw) {
      return null;
    }
    return {
      info: actorJoinInfoFromRaw(raw.info),
      message: Message.fromSnapshot(raw.message)
    };
  }
  replyActorJoin(request: ActorJoinRequest, joinResultCode: number): ActorJoinReplyOperation {
    const spotHandle = this._native;
    const rawInfo = actorJoinInfoToRaw(request.info);
    const code = joinResultCode | 0;
    return new RuntimeActorJoinReplyOperation((partsInput) => {
      const parts = toMessageParts(partsInput);
      try {
        requireNative().spotActorJoinReply(spotHandle, rawInfo, code, parts);
      } catch (error) {
        throw submitNativeError(error, SendFlags.None, 'actor join reply failed');
      }
    });
  }
  recvActorLifecycle(flags: RecvFlags = RecvFlags.None): SpotActorLifecycleEvent | null {
    let raw;
    try {
      raw = requireNative().spotRecvActorLifecycle(this._native, flags | 0) as {
        kind: number;
        info: SpotActorLifecycleInfoRaw;
      } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'actor lifecycle recv failed');
    }
    if (!raw) {
      return null;
    }
    return {
      kind: raw.kind,
      info: spotActorLifecycleInfoFromRaw(raw.info)
    };
  }
  actors(): ActorRef[] {
    return (configCall('spot actors snapshot failed', () =>
      requireNative().spotActors(this._native) as Array<{ nodeRid: Buffer; actorId: string; generation: bigint | number }>
    ))
      .map((entry) => actorRefFromRaw(entry));
  }
  close(): void {
    if (this._native) {
      closeCall('spot close failed', () => {
        requireNative().spotDestroy(this._native);
      });
      this._native = null;
      this._node.unregisterSpot(this);
    }
  }
}
