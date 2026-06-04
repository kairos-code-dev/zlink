// SPDX-License-Identifier: MPL-2.0

import { NativeHandle } from '../../handles/native_handle';
import { requireNative } from '../../native/native';
import { closeCall, configCall, handlerCall, recvNativeError, submitNativeError } from '../../errors/native_errors';
import { validateCString } from '../../options/validation';
import { executeNativeRequest } from '../../messaging/request_executor';
import { normalizeRoutingId } from '../../core/routing_id';
import { startRequestProgress } from '../../messaging/request_progress';
import { adoptTopicMessage, materializeReceivedInto, materializeTopicMessage, type NativeTopicMessageRaw } from '../../messaging/message_materializer';
import { int32Buffer, readInt32Option } from '../../sockets/socket_options';
import { RuntimeReplyOperation, RuntimeRequestOperation, RuntimeSendOperation } from '../../sockets/socket_operation_builders';
import {
  normalizeReplyFlags,
  submitErrorFromResult,
} from '../../sockets/socket_submit_errors';
import { normalizeOperationPayload } from '../../buffers/message_conversion';
import { Message, Received, RoutingId, TopicMessage, type MessageLike } from '../../../contracts';
import { RecvFlags, SendFlags } from '../../../contracts/sockets/socket_constants';
import { SubmitResult } from '../../../contracts/errors/errors';
import { SpotDispatchEvent, SpotDispatchSubjectKind, type ActorJoinRequest, type ActorJoinReplyOperation, type ActorPart, type ActorRef, type ReplyOperation, type RequestCallback, type RequestOperation, type SendOperation, type SpotActorLifecycleEvent, type SpotDispatchEventHandler, type SpotSendReadyHandler, type SubscriptionEntry } from '../../../contracts/service';
import { SpotOption } from './spot_options';
import { actorJoinInfoFromRaw, actorJoinInfoToRaw, actorPartFromRaw, actorRefFromRaw, spotActorLifecycleInfoFromRaw } from './actor_models';
import { RuntimeActorJoinReplyOperation } from './actor_operations';
import type { ActorRefRaw, SpotActorJoinRecvRaw, SpotActorLifecycleRaw, SpotDispatchRaw, SpotRoutedRaw } from './spot_raw_models';

type OwnerSpotNode = { nativeHandle(): unknown; readonly routingId: RoutingId; unregisterSpot(spot: Spot): void };

const nativeBinding = requireNative();

export class Spot extends NativeHandle {
  private static readonly CREATE_TOKEN = Symbol('Spot.create');
  private readonly _node: OwnerSpotNode;
  private constructor(node: OwnerSpotNode, token: symbol, native?: unknown) {
    if (token !== Spot.CREATE_TOKEN) {
      throw new TypeError('Spot instances must be created with SpotNode.createSpot()');
    }
    super(native ?? nativeBinding.spotNew(node.nativeHandle()));
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
      nativeBinding.handleSetRoutingId(this._native, normalizedRoutingId);
    });
  }
  get routingId(): RoutingId {
    return RoutingId.from(configCall('spot routing id get failed', () =>
      nativeBinding.handleGetRoutingId(this._native) as Buffer
    ));
  }
  get requestTimeout(): number {
    return readInt32Option(configCall('spot request timeout get failed', () =>
      nativeBinding.spotGetOption(this._native, SpotOption.REQUEST_TIMEOUT_MS) as Buffer
    ), 'requestTimeout');
  }
  set requestTimeout(value: number) {
    const buffer = int32Buffer(value, 'requestTimeout');
    configCall('spot request timeout set failed', () => {
      nativeBinding.spotSetOption(this._native, SpotOption.REQUEST_TIMEOUT_MS, buffer);
    });
  }
  publish(topic: string): SendOperation {
    const normalizedTopic = validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER);
    return new RuntimeSendOperation((parts, opFlags) => this.publishDirect(normalizedTopic, parts, opFlags));
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
  private publishDirect(topic: string, payloadParts: MessageLike | readonly MessageLike[], flags: SendFlags): boolean {
    return this.submitSpotSend(flags, 'spot publish failed', () => {
      nativeBinding.spotPublish(
        this._native,
        topic,
        normalizeOperationPayload(payloadParts),
        flags | 0
      );
    });
  }
  setSubscription(topicOrPattern: string): void {
    const normalized = validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER);
    configCall('spot subscription set failed', () => {
      nativeBinding.spotSubscribe(this._native, normalized);
    });
  }
  unsetSubscription(topicOrPattern: string): void {
    const normalized = validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER);
    configCall('spot subscription unset failed', () => {
      nativeBinding.spotUnsubscribe(this._native, normalized);
    });
  }
  subscriptionAt(index: number): SubscriptionEntry | null {
    return configCall('spot subscription lookup failed', () =>
      nativeBinding.subscriptionAt(this._native, index >>> 0) as SubscriptionEntry | null
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
        ? nativeBinding.spotRecvNoWait(this._native) as NativeTopicMessageRaw | null
        : nativeBinding.spotRecv(this._native, flags | 0) as NativeTopicMessageRaw | null;
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
      nativeBinding.spotSendReadyHandler(this._native, handler);
    });
  }
  sendToChannel(channelName: string): SendOperation {
    return new RuntimeSendOperation((parts, opFlags) => this.sendChannelDirect(channelName, parts, opFlags));
  }
  private sendChannelDirect(channelName: string, payloadParts: MessageLike | readonly MessageLike[], flags: SendFlags): boolean {
    return this.submitSpotSend(flags, 'spot sendToChannel failed', () => {
      nativeBinding.spotSendChannel(
        this._native,
        validateCString(channelName, 'channelName', Number.MAX_SAFE_INTEGER),
        normalizeOperationPayload(payloadParts),
        flags | 0
      );
    });
  }
  sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): SendOperation {
    return new RuntimeSendOperation((parts, opFlags) => this.sendToSpotDirect(destNodeRid, destSpotRid, parts, opFlags));
  }
  private sendToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, payloadParts: MessageLike | readonly MessageLike[], flags: SendFlags): boolean {
    const nodeRid = normalizeRoutingId(destNodeRid);
    const spotRid = normalizeRoutingId(destSpotRid);
    return this.sendToSpotRawDirect(nodeRid, spotRid, payloadParts, flags);
  }
  private sendToSpotRawDirect(nodeRid: Buffer, spotRid: Buffer, payloadParts: MessageLike | readonly MessageLike[], flags: SendFlags): boolean {
    const payload = normalizeOperationPayload(payloadParts);
    if ((flags | 0) & (SendFlags.DontWait | 0)) {
      let result;
      try {
        result = nativeBinding.spotSendToSpotNoWaitResult(
          this._native,
          nodeRid,
          spotRid,
          payload
        ) as number;
      } catch (error) {
        throw submitNativeError(error, flags, 'spot sendToSpot failed');
      }
      if (result === SubmitResult.Ok) return true;
      if (result === SubmitResult.Backpressured) return false;
      throw submitErrorFromResult(result as SubmitResult, 'spot sendToSpot failed');
    }
    return this.submitSpotSend(flags, 'spot sendToSpot failed', () => {
      nativeBinding.spotSendToSpot(
        this._native,
        nodeRid,
        spotRid,
        payload,
        flags | 0
      );
    });
  }
  requestToChannel(channelName: string): RequestOperation {
    return new RuntimeRequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestChannelDirect(channelName, parts, cbOrTimeout, opFlags, opTimeout)
    );
  }
  private requestChannelDirect(channelName: string, partsInput: MessageLike | readonly MessageLike[], callbackOrTimeout?: RequestCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
    const normalizedChannelName = validateCString(channelName, 'channelName', Number.MAX_SAFE_INTEGER);
    return this.executeSpotRequest(
      partsInput,
      callbackOrTimeout,
      flagsOrTimeout,
      maybeTimeout,
      'requestToChannel failed',
      (spotHandle, parts, callback, flags, timeoutMs) => {
        nativeBinding.spotRequestChannel(
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
  private requestToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, partsInput: MessageLike | readonly MessageLike[], callbackOrTimeout?: RequestCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
    const nodeRid = normalizeRoutingId(destNodeRid, 'destNodeRid');
    const spotRid = normalizeRoutingId(destSpotRid, 'destSpotRid');
    return this.executeSpotRequest(
      partsInput,
      callbackOrTimeout,
      flagsOrTimeout,
      maybeTimeout,
      'requestToSpot failed',
      (spotHandle, parts, callback, flags, timeoutMs) => {
        nativeBinding.spotRequestSpot(
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
  private requestToRouterDirect(peerRid: RoutingId, partsInput: MessageLike | readonly MessageLike[], callbackOrTimeout?: RequestCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
    const peer = normalizeRoutingId(peerRid, 'peerRid');
    return this.executeSpotRequest(
      partsInput,
      callbackOrTimeout,
      flagsOrTimeout,
      maybeTimeout,
      'requestToRouter failed',
      (spotHandle, parts, callback, flags, timeoutMs) => {
        nativeBinding.spotRequestRouter(
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
    partsInput: MessageLike | readonly MessageLike[],
    callbackOrTimeout: RequestCallback | number | undefined,
    flagsOrTimeout: SendFlags | number | undefined,
    maybeTimeout: number | undefined,
    errorMessage: string,
    invoke: (
      spotHandle: unknown,
      parts: ReturnType<typeof normalizeOperationPayload>,
      callback: (result: number, replyParts: Buffer[] | null) => void,
      flags: SendFlags,
      timeoutMs: number
    ) => void,
  ): Promise<Message[]> | boolean {
    const parts = normalizeOperationPayload(partsInput);
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
    return new RuntimeReplyOperation((parts, opFlags) => this.replyToSpotInternal(destNodeRid, destSpotRid, requestSeq, parts, opFlags));
  }
  replyToRouter(peerRid: RoutingId, requestSeq: bigint): ReplyOperation {
    return new RuntimeReplyOperation((parts, opFlags) => this.replyToRouterInternal(peerRid, requestSeq, parts, opFlags));
  }
  private replyToSpotInternal(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint, parts: MessageLike | readonly MessageLike[], flags: SendFlags): void {
    normalizeReplyFlags(flags);
    const normalizedDestNodeRid = normalizeRoutingId(destNodeRid);
    const normalizedDestSpotRid = normalizeRoutingId(destSpotRid);
    try {
      nativeBinding.spotReplySpot(
        this._native,
        normalizedDestNodeRid,
        normalizedDestSpotRid,
        requestSeq,
        normalizeOperationPayload(parts)
      );
    } catch (error) {
      throw submitNativeError(error, flags, 'spot replyToSpot failed');
    }
  }
  private replyToRouterInternal(peerRid: RoutingId, requestSeq: bigint, parts: MessageLike | readonly MessageLike[], flags: SendFlags): void {
    normalizeReplyFlags(flags);
    const normalizedPeerRid = normalizeRoutingId(peerRid);
    try {
      nativeBinding.spotReplyRouter(
        this._native,
        normalizedPeerRid,
        requestSeq,
        normalizeOperationPayload(parts)
      );
    } catch (error) {
      throw submitNativeError(error, flags, 'spot replyToRouter failed');
    }
  }
  recvRouted(result: Received, flags?: RecvFlags): boolean;
  recvRouted(result: Received, flags: RecvFlags = RecvFlags.None): boolean {
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? nativeBinding.spotRecvRoutedNoWait(this._native) as SpotRoutedRaw | null
        : nativeBinding.spotRecvRouted(this._native, flags | 0) as SpotRoutedRaw | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'recvRouted failed');
    }
    if (!raw) return false;
    materializeReceivedInto(
      result,
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
        return this.sendToSpotRawDirect(raw.sourceRid, raw.spotRid, parts, flags);
      }
    );
    return true;
  }
  setDispatchHandler(handler: SpotDispatchEventHandler): void {
    handlerCall('spot dispatch handler registration failed', () => {
      nativeBinding.spotDispatchEventHandler(this._native, this._node.nativeHandle(), (raw: SpotDispatchRaw) => {
        const rawActorParts = raw.actorParts ?? [];
        const actorParts = new Array<ActorPart | undefined>(rawActorParts.length);
        const actorRef = rawActorParts[0]?.info.actor
          ? actorRefFromRaw(rawActorParts[0].info.actor)
          : null;
        let index = 0;
        handler({
          event: raw.event as SpotDispatchEvent,
          subjectKind: raw.subjectKind as SpotDispatchSubjectKind,
          timer: null,
          actorRef,
          recvActorPart(flags: RecvFlags = RecvFlags.None): ActorPart | null {
            const current = index++;
            const rawPart = rawActorParts[current] ?? null;
            if (!rawPart && ((flags | 0) & (RecvFlags.DontWait | 0))) {
              return null;
            }
            if (!rawPart) {
              return null;
            }
            actorParts[current] ??= actorPartFromRaw(rawPart);
            return actorParts[current];
          }
        });
      });
    });
  }
  recvActorJoin(flags: RecvFlags = RecvFlags.None): ActorJoinRequest | null {
    let raw;
    try {
      raw = nativeBinding.spotActorJoinRecv(this._native, flags | 0) as SpotActorJoinRecvRaw | null;
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
      const parts = normalizeOperationPayload(partsInput);
      try {
        nativeBinding.spotActorJoinReply(spotHandle, rawInfo, code, parts);
      } catch (error) {
        throw submitNativeError(error, SendFlags.None, 'actor join reply failed');
      }
    });
  }
  recvActorLifecycle(flags: RecvFlags = RecvFlags.None): SpotActorLifecycleEvent | null {
    let raw;
    try {
      raw = nativeBinding.spotRecvActorLifecycle(this._native, flags | 0) as SpotActorLifecycleRaw | null;
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
      nativeBinding.spotActors(this._native) as ActorRefRaw[]
    ))
      .map((entry) => actorRefFromRaw(entry));
  }
  close(): void {
    if (this._native) {
      closeCall('spot close failed', () => {
        nativeBinding.spotDestroy(this._native);
      });
      this._native = null;
      this._node.unregisterSpot(this);
    }
  }
}
