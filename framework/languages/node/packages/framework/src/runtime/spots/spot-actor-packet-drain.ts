import type {
  ActorRef,
  RoutingId,
  ZLinkMessageSerializer
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { Message as BindingMessage, RequestResult } from '@zlink-systems/zlink';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendActorRecvInfo
} from '../backend/contracts';
import type { ZLinkRemoteBoundSessionTarget } from '../actors';
import { REMOTE_BOUND_SESSION_BIND_PACKET } from './spot-remote-codec';
import { ZLINK_RECV_DONT_WAIT } from './spot-native-flags';
import {
  decodeStreamHeader,
  encodeStreamFrame,
  messageToBytes,
  ZLinkStreamCodec,
  ZLinkStreamHeaderFlags,
  ZLinkStreamMessageKind
} from '../streams/protocol';
import { encodeFrameworkPayloadMessage } from '../messaging/payload-codec';

export interface ZLinkActorDispatchPart {
  readonly info: {
    readonly actor: ZLinkBackendActorRef;
    readonly sourceNodeRid?: RoutingId;
    readonly sourceSessionRid?: RoutingId;
    readonly requestId?: bigint;
    readonly flags?: number;
  };
  readonly message: Message;
  readonly more: boolean;
}

interface ZLinkSpotActorPacketDrainOptions {
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly actorPacketHandler?: (
    actorId: string,
    parts: readonly Message[],
    returnResponse?: boolean,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ) => Promise<unknown>;
  readonly bindRemoteActorSession?: (
    actor: ZLinkBackendActorRef,
    sourceNodeRid: RoutingId,
    sourceSessionRid: RoutingId
  ) => void;
  readonly replyActorNoBind?: (
    info: ZLinkBackendActorRecvInfo,
    parts: readonly Message[],
    result: RequestResult
  ) => void;
  readonly waitIdle: () => Promise<void>;
}

const ZLINK_SPOT_ACTOR_RECV_INFO_NO_BIND = 1;

export class ZLinkSpotActorPacketDrain {
  constructor(private readonly options: ZLinkSpotActorPacketDrainOptions) {}

  async drain(info: {
    recvActorPart(flags?: number): ZLinkActorDispatchPart | null;
  }): Promise<void> {
    const parts: Message[] = [];
    let actorId: string | undefined;
    let actorRef: ZLinkBackendActorRef | undefined;
    let sourceNodeRid: RoutingId | undefined;
    let sourceSessionRid: RoutingId | undefined;
    let requestId: bigint | undefined;
    let flags: number | undefined;
    try {
      for (;;) {
        const part = info.recvActorPart(ZLINK_RECV_DONT_WAIT);
        if (part === null) {
          await this.options.waitIdle();
          return;
        }
        actorId ??= part.info.actor.actorId;
        actorRef ??= part.info.actor;
        sourceNodeRid ??= part.info.sourceNodeRid;
        sourceSessionRid ??= part.info.sourceSessionRid;
        requestId ??= part.info.requestId;
        flags ??= part.info.flags;
        parts.push(part.message);
        if (!part.more) {
          break;
        }
      }
      const noBindInfo = this.createNoBindReplyInfo(actorRef, sourceNodeRid, sourceSessionRid, requestId, flags, parts);
      if (noBindInfo === undefined && sourceNodeRid !== undefined && sourceSessionRid !== undefined) {
        this.options.bindRemoteActorSession?.(actorRef, sourceNodeRid, sourceSessionRid);
      }
      if (this.consumeRemoteBoundSessionBind(actorRef, sourceNodeRid, sourceSessionRid, parts)) {
        return;
      }
      if (this.options.actorPacketHandler === undefined) {
        return;
      }
      if (noBindInfo !== undefined) {
        await this.dispatchNoBindActorRequest(noBindInfo, actorId, parts, actorRef);
      } else {
        await this.options.actorPacketHandler(
          actorId,
          parts,
          false,
          undefined,
          actorRef as unknown as ActorRef | undefined
        );
      }
    } finally {
      for (const part of parts) {
        part.close();
      }
    }
  }

  private createNoBindReplyInfo(
    actor: ZLinkBackendActorRef | undefined,
    sourceNodeRid: RoutingId | undefined,
    sourceSessionRid: RoutingId | undefined,
    requestId: bigint | undefined,
    flags: number | undefined,
    parts: readonly Message[]
  ): ZLinkBackendActorRecvInfo | undefined {
    if (
      actor === undefined
      || sourceNodeRid === undefined
      || sourceSessionRid === undefined
      || requestId === undefined
      || requestId === 0n
      || flags === undefined
      || (flags & ZLINK_SPOT_ACTOR_RECV_INFO_NO_BIND) === 0
      || this.options.replyActorNoBind === undefined
      || !this.isActorRequest(parts)
    ) {
      return undefined;
    }
    return { actor, sourceNodeRid, sourceSessionRid, requestId, flags };
  }

  private isActorRequest(parts: readonly Message[]): boolean {
    if (parts.length < 1) {
      return false;
    }
    try {
      const header = decodeStreamHeader(messageToBytes(parts[0]));
      return header.kind === ZLinkStreamMessageKind.Request && header.requestSeq !== undefined;
    } catch {
      return false;
    }
  }

  private async dispatchNoBindActorRequest(
    info: ZLinkBackendActorRecvInfo,
    actorId: string,
    parts: readonly Message[],
    actorRef: ZLinkBackendActorRef | undefined
  ): Promise<void> {
    try {
      const response = await this.options.actorPacketHandler?.(
        actorId,
        parts,
        true,
        undefined,
        actorRef as unknown as ActorRef | undefined
      );
      this.options.replyActorNoBind?.(
        info,
        [this.encodeActorReplyFrame(parts[0], ZLinkStreamMessageKind.Response, response)],
        RequestResult.Ok
      );
    } catch (error) {
      this.options.replyActorNoBind?.(
        info,
        [this.encodeActorReplyFrame(parts[0], ZLinkStreamMessageKind.Error, frameworkErrorPayload(error))],
        RequestResult.Ok
      );
    }
  }

  private encodeActorReplyFrame(
    requestHeaderPart: Message,
    kind: ZLinkStreamMessageKind.Response | ZLinkStreamMessageKind.Error,
    payload: unknown
  ): Message {
    const requestHeader = decodeStreamHeader(messageToBytes(requestHeaderPart));
    const payloadMessage = encodeFrameworkPayloadMessage(payload, this.options.messageSerializers);
    try {
      return BindingMessage.from(Buffer.from(encodeStreamFrame({
        kind,
        codec: ZLinkStreamCodec.Json,
        flags: ZLinkStreamHeaderFlags.None,
        requestSeq: requestHeader.requestSeq,
        name: requestHeader.name,
        metadata: new Map(),
        correlationId: requestHeader.correlationId
      }, payloadMessage.data()))) as Message;
    } finally {
      payloadMessage.close();
    }
  }

  private consumeRemoteBoundSessionBind(
    actor: ZLinkBackendActorRef | undefined,
    sourceNodeRid: RoutingId | undefined,
    sourceSessionRid: RoutingId | undefined,
    parts: readonly Message[]
  ): boolean {
    if (parts.length < 1) {
      return false;
    }
    let header: ReturnType<typeof decodeStreamHeader>;
    try {
      header = decodeStreamHeader(messageToBytes(parts[0]));
    } catch {
      return false;
    }
    if (header.name !== REMOTE_BOUND_SESSION_BIND_PACKET) {
      return false;
    }
    if (actor !== undefined && sourceNodeRid !== undefined && sourceSessionRid !== undefined) {
      this.options.bindRemoteActorSession?.(actor, sourceNodeRid, sourceSessionRid);
    }
    return true;
  }
}

function frameworkErrorPayload(error: unknown): {
  readonly code: string;
  readonly message: string;
  readonly kind?: string;
  readonly isRetriable?: boolean;
} {
  return error instanceof Error
    ? {
        code: error.constructor.name,
        message: error.message,
        kind: 'kind' in error ? String(error.kind) : undefined,
        isRetriable: 'isRetriable' in error && error.isRetriable === true
      }
    : { code: typeof error, message: String(error) };
}
