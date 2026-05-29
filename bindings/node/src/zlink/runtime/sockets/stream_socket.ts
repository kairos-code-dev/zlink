// SPDX-License-Identifier: MPL-2.0

import { StreamSocketOptions } from './socket_options';
import { SocketBase } from './socket_base';
import { messageFromNativeBuffer, normalizeMessageLikePayload } from '../buffers/message_conversion';
import { normalizeRoutingId } from '../core/routing_id';
import {
  RuntimeSendOperation,
} from './socket_operations';
import { submitErrorFromResult } from './socket_submit_errors';
import type { RuntimeContext as Context } from '../core/context';
import {
  configCall,
  handlerCall,
  recvNativeError,
  submitNativeError,
} from '../errors/native_errors';
import {
  materializeReceived,
  materializeReceivedInto,
} from '../messaging/message_materializer';
import { requireNative } from '../native/native';
import { validateCString } from '../options/validation';
import {
  Received,
  RoutingId,
  type Message,
  type MessageLike,
} from '../../contracts';
import { SubmitResult } from '../../contracts/errors/errors';
import { wrapRoutingId } from '../../contracts/service/spot/spot_models';
import { RecvFlags, SendFlags, SocketType as NativeSocketType } from '../../contracts/sockets/socket_constants';
import type {
  ActorBindOperation,
  ActorRef,
  ActorUnbindOperation,
  SendOperation,
  SocketSendReadyHandler,
  StreamPacketHandler,
} from '../../contracts/service';
import {
  RuntimeActorBindOperation,
  RuntimeActorUnbindOperation,
} from '../service/spot/actor_operations';
import {
  actorRefFromRaw,
  actorRefToRaw,
} from '../service/spot/actor_models';
import {
  invokeStreamBindActor,
  invokeStreamSendBoundActor,
  invokeStreamUnbindActor,
} from '../service/spot/actor_invokers';

type SpotNodeHandle = { nativeHandle(): unknown };

export class StreamSocket extends SocketBase {
  readonly options: StreamSocketOptions;
  constructor(ctx: Context) {
    super(ctx, NativeSocketType.STREAM);
    this.options = StreamSocketOptions.create(this);
  }
  send(routingId: RoutingId): SendOperation {
    return new RuntimeSendOperation((parts, flags) => this.sendDirect(routingId, parts, flags));
  }
  private sendDirect(routingId: RoutingId, payload: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
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
    try {
      if (Array.isArray(normalized)) {
        requireNative().socketSendRoutingParts(
          this.nativeHandle(),
          normalizedRoutingId,
          normalized,
          flags | 0
        );
      } else {
        requireNative().socketSendRouting(
          this.nativeHandle(),
          normalizedRoutingId,
          normalized,
          flags | 0
        );
      }
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
        ? requireNative().socketRecvMessageNoWait(this.nativeHandle())
        : requireNative().socketRecvMessage(this.nativeHandle(), recvFlags | 0);
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
      requireNative().handleSetRoutingId(this.nativeHandle(), normalizedRoutingId);
    });
  }
  getRoutingId(): RoutingId {
    return RoutingId.from(
      configCall('routing id get failed', () =>
        requireNative().handleGetRoutingId(this.nativeHandle()) as Buffer
      )
    );
  }
  disconnectRid(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('stream disconnect by routing id failed', () => {
      requireNative().socketDisconnectRid(this.nativeHandle(), normalizedRoutingId);
    });
  }
  attachActorGateway(node: SpotNodeHandle): void {
    configCall('stream actor gateway attachment failed', () => {
      requireNative().streamAttachActorGateway(this.nativeHandle(), node.nativeHandle());
    });
  }
  bindActor(sessionRid: RoutingId, actor: ActorRef): ActorBindOperation {
    const handle = this.nativeHandle();
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    const actorRaw = actorRefToRaw(actor);
    return new RuntimeActorBindOperation((callback, timeoutMs) =>
      invokeStreamBindActor(handle, normalizedSessionRid, actorRaw, callback, timeoutMs),
    );
  }
  unbindActor(sessionRid: RoutingId, actorId: string): ActorUnbindOperation {
    const handle = this.nativeHandle();
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    return new RuntimeActorUnbindOperation((callback, timeoutMs) =>
      invokeStreamUnbindActor(handle, normalizedSessionRid, normalizedActorId, callback, timeoutMs),
    );
  }
  sendBoundActor(sessionRid: RoutingId, actorId: string): SendOperation {
    const handle = this.nativeHandle();
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    return new RuntimeSendOperation((parts, flags) =>
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
