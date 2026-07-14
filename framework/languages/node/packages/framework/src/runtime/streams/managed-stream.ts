import type {
  ActorRef,
  RoutingId,
  ZLinkMessage,
  ZLinkMessageSerializer,
  ZLinkStream
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { throwIfAborted } from '../abort';
import { encodeFrameworkPayloadMessage } from '../messaging/payload-codec';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendSendFlags,
  ZLinkBackendStreamSocket
} from '../backend/contracts';
import { Message as NativeMessage } from '@zlink-systems/zlink';
import {
  encodeSessionClosingFrame,
  encodeStreamControlFrame,
  ZLinkStreamCloseReasonCode
} from './protocol';

export class ZLinkManagedStream implements ZLinkStream {
  private currentLocalAddr: string | undefined;
  private currentRemoteAddr: string | undefined;

  constructor(
    private readonly socket: ZLinkBackendStreamSocket,
    private readonly backendSessionRoutingId: unknown,
    private readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>,
    private readonly publicSessionId = streamSessionIdFromRoutingId(backendSessionRoutingId)
  ) {}

  get sessionId(): string {
    return this.publicSessionId;
  }

  get routingId(): RoutingId {
    return this.publicSessionId;
  }

  get actorBindingRoutingId(): RoutingId {
    return this.backendSessionRoutingId as RoutingId;
  }

  get localAddr(): string | undefined {
    return this.currentLocalAddr;
  }

  get remoteAddr(): string | undefined {
    return this.currentRemoteAddr;
  }

  write(payload: ZLinkMessage, flags?: ZLinkBackendSendFlags): boolean {
    const message = encodeFrameworkPayloadMessage(payload, this.messageSerializers);
    try {
      return this.socket.send(this.backendRoutingId(), message, flags ?? 0);
    } finally {
      message.close();
    }
  }

  writeRaw(payload: Message, flags?: ZLinkBackendSendFlags): boolean {
    return this.socket.send(this.backendRoutingId(), payload, flags ?? 0);
  }

  async close(signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    this.socket.disconnectPeer(this.backendRoutingId());
  }

  async closeForDrain(signal?: AbortSignal): Promise<void> {
    await this.closeForReason(ZLinkStreamCloseReasonCode.ServerDrain, 'server drain', signal);
  }

  writeControl(name: string): boolean {
    const control = NativeMessage.from(encodeStreamControlFrame(name));
    try {
      return this.socket.send(this.backendRoutingId(), control, 0);
    } finally {
      control.close();
    }
  }

  async closeForReason(
    reason: ZLinkStreamCloseReasonCode,
    diagnostic: string,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const closing = NativeMessage.from(encodeSessionClosingFrame(diagnostic, reason));
    try {
      this.socket.send(this.backendRoutingId(), closing, 0);
    } finally {
      closing.close();
    }
    this.socket.disconnectPeer(this.backendRoutingId());
  }

  async bindActor(actor: ActorRef, timeoutMs: number, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    await this.socket.bindActor(this.backendRoutingId(), toBackendActorRef(actor), timeoutMs, signal);
  }

  async unbindActor(actorId: string, timeoutMs: number, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    await this.socket.unbindActor(this.backendRoutingId(), actorId, timeoutMs, signal);
  }

  sendBoundActor(actorId: string, parts: readonly Message[], flags?: ZLinkBackendSendFlags): boolean {
    return this.socket.sendBoundActor(this.backendRoutingId(), actorId, parts, flags ?? 0);
  }

  updateAddresses(localAddr: string | undefined, remoteAddr: string | undefined): void {
    this.currentLocalAddr = localAddr;
    this.currentRemoteAddr = remoteAddr;
  }

  private backendRoutingId(): never {
    return this.backendSessionRoutingId as never;
  }
}

export function streamSessionIdFromRoutingId(routingId: unknown): string {
  if (typeof routingId === 'string') {
    return routingId;
  }
  if (
    typeof routingId === 'object'
    && routingId !== null
    && 'toHex' in routingId
    && typeof (routingId as { toHex?: unknown }).toHex === 'function'
  ) {
    return (routingId as { toHex(): string }).toHex();
  }
  if (
    typeof routingId === 'object'
    && routingId !== null
    && 'toString' in routingId
    && typeof (routingId as { toString?: unknown }).toString === 'function'
  ) {
    return (routingId as { toString(): string }).toString();
  }
  throw new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.RouteNotConnected,
    'Stream session routing id is invalid.'
  );
}

function toBackendActorRef(actor: ActorRef): ZLinkBackendActorRef {
  return {
    nodeRid: actor.nodeRid,
    actorId: actor.actorId,
    generation: actor.generation
  };
}
