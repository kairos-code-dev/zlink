// SPDX-License-Identifier: MPL-2.0

import {
  CommonSocketOptions,
  DealerSocketOptions,
  PubSocketOptions,
  RouterSocketOptions,
  StreamSocketOptions,
  SubSocketOptions,
} from './socket_options';
import { SocketBase } from './socket_base';
import { messageFromNativeBuffer, normalizeMessageLikePayload, toMessageParts } from '../buffers/message_conversion';
import { normalizeRoutingId } from '../core/routing_id';
import {
  MessageSocket,
  NativeSocketType,
  PublisherSocket,
  RecvFlags,
  Received,
  RequestOperation,
  RoutedMessageSocket,
  SendFlags,
  SendOperation,
  SocketOption,
  SubscriberSocket,
  SubscriptionEvent,
  SubmitResult,
  TopicMessage,
  RoutingId,
  wrapRoutingId,
  validateCString,
  requireNative,
  configCall,
  handlerCall,
  recvNativeError,
  submitNativeError,
  submitErrorFromResult,
  normalizeBufferLike,
  normalizeReplyFlags,
  materializeReceived,
  materializeReceivedInto,
  materializeTopicMessage,
  adoptTopicMessage,
  executeNativeRequest,
  startRequestProgress,
  ReplyOperation,
  RecvResult,
  requestErrorFromResult,
  type ActorBindOp,
  type ActorRef,
  type ActorUnbindOp,
  type BufferLike,
  type Context,
  type Message,
  type MessageLike,
  type MessageSnapshot,
  type RequestCallback,
  type RequestOp,
  type ReplyOp,
  type SendOp,
  type SocketSendReadyHandler,
  type StreamPacketHandler,
} from './socket_operations';
import {
  ActorBindOperation,
  ActorUnbindOperation,
  actorRefFromRaw,
  actorRefToRaw,
  invokeStreamBindActor,
  invokeStreamSendBoundActor,
  invokeStreamUnbindActor,
} from '../service/spot/spot_operations';

type SpotNodeHandle = { nativeHandle(): unknown };

export class StreamSocket extends SocketBase {
  readonly options: StreamSocketOptions;
  constructor(ctx: Context) {
    super(ctx, NativeSocketType.STREAM);
    this.options = StreamSocketOptions.create(this);
  }
  send(routingId: RoutingId): SendOp {
    return new SendOperation((parts, flags) => this.sendDirect(routingId, parts, flags));
  }
  private sendDirect(routingId: RoutingId, payload: readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const normalized = normalizeMessageLikePayload(payload);
    const normalizedRoutingId = normalizeRoutingId(routingId);
    if ((flags | 0) & (SendFlags.DontWait | 0)) {
      let result;
      try {
        result = Array.isArray(normalized)
          ? requireNative().socketSendRoutingNoWaitResultParts(this.nativeHandle(), normalizedRoutingId, normalized) as number
          : requireNative().socketSendRoutingNoWaitResult(this.nativeHandle(), normalizedRoutingId, normalized) as number;
      } catch (error) {
        throw submitNativeError(error, flags, 'send failed');
      }
      if (result === SubmitResult.Ok) return true;
      if (result === SubmitResult.Backpressured) return false;
      throw submitErrorFromResult(result as SubmitResult, 'send failed');
    }
    const parts = Array.isArray(normalized)
      ? [normalizedRoutingId, ...normalized]
      : [normalizedRoutingId, normalized];
    try {
      requireNative().socketSendParts(this.nativeHandle(), parts, flags | 0);
    } catch (error) {
      throw submitNativeError(error, flags, 'send failed');
    }
    return true;
  }
  recv(result: Received, flags?: RecvFlags): boolean;
  recv(resultOrFlags: Received | RecvFlags = RecvFlags.None,
      flags: RecvFlags = RecvFlags.None): Received | null | boolean {
    const result = resultOrFlags instanceof Received ? resultOrFlags : null;
    const recvFlags: RecvFlags = result ? flags : resultOrFlags as RecvFlags;
    let raw;
    try {
      raw = ((recvFlags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketRecvMessageNoWait(this.nativeHandle()) as { parts: MessageSnapshot[]; routingId?: Buffer | null } | null
        : requireNative().socketRecvMessage(this.nativeHandle(), recvFlags | 0) as { parts: MessageSnapshot[]; routingId?: Buffer | null } | null;
    } catch (error) {
      throw recvNativeError(error, recvFlags, 'recv failed');
    }
    if (raw == null) return result ? false : null;
    const send = (parts: readonly Message[], sendFlags: SendFlags) => {
        if (!raw.routingId) {
          throw submitErrorFromResult(SubmitResult.InvalidState, 'missing routed send target');
        }
        return this.sendDirect(RoutingId.from(raw.routingId), parts, sendFlags);
      };
    if (!result) return materializeReceived(raw, undefined, send);
    materializeReceivedInto(result, raw, undefined, send);
    return true;
  }
  setPacketHandler(handler: StreamPacketHandler): void {
    handlerCall('stream packet handler registration failed', () => {
      requireNative().socketStreamAttach(
        this.nativeHandle(),
        (routingId: Buffer | null, packets: Buffer[]) => {
          const sourceRid = wrapRoutingId(routingId);
          if (!sourceRid) {
            return 0;
          }
          const header = messageFromNativeBuffer(packets[0]);
          const body = messageFromNativeBuffer(packets[1]);
          handler(sourceRid, header, body);
          return 0;
        },
        1
      );
    });
  }
  setSendReadyHandler(handler: SocketSendReadyHandler): void {
    handlerCall('send-ready handler registration failed', () => {
      requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
    });
  }
  setRoutingId(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('routing id set failed', () => {
      requireNative().socketSetOpt(
        this.nativeHandle(),
        SocketOption.ROUTING_ID | 0,
        normalizedRoutingId
      );
    });
  }
  getRoutingId(): RoutingId {
    return RoutingId.from(
      configCall('routing id get failed', () =>
        requireNative().socketGetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0) as Buffer
      )
    );
  }
  attachActorGateway(node: SpotNodeHandle): void {
    configCall('stream actor gateway attachment failed', () => {
      requireNative().streamAttachActorGateway(this.nativeHandle(), node.nativeHandle());
    });
  }
  bindActor(sessionRid: RoutingId, actor: ActorRef): ActorBindOp {
    const handle = this.nativeHandle();
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    const actorRaw = actorRefToRaw(actor);
    return new ActorBindOperation((callback, timeoutMs) =>
      invokeStreamBindActor(handle, normalizedSessionRid, actorRaw, callback, timeoutMs),
    );
  }
  unbindActor(sessionRid: RoutingId, actorId: string): ActorUnbindOp {
    const handle = this.nativeHandle();
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    return new ActorUnbindOperation((callback, timeoutMs) =>
      invokeStreamUnbindActor(handle, normalizedSessionRid, normalizedActorId, callback, timeoutMs),
    );
  }
  sendBoundActor(sessionRid: RoutingId, actorId: string): SendOp {
    const handle = this.nativeHandle();
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    return new SendOperation((parts, flags) =>
      invokeStreamSendBoundActor(handle, normalizedSessionRid, normalizedActorId, parts, flags),
    );
  }
  boundActors(sessionRid: RoutingId): ActorRef[] {
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    return (configCall('stream bound actors snapshot failed', () =>
      requireNative().streamBoundActors(this.nativeHandle(), normalizedSessionRid) as Array<{ nodeRid: Buffer; actorId: string; generation: bigint | number }>
    )).map((entry) => actorRefFromRaw(entry));
  }
}
