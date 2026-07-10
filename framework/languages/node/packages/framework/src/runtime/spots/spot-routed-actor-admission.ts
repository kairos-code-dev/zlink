import type {
  ZLinkActor,
  ZLinkMessage,
  ZLinkMessageSerializer,
  ZLinkSpotActorJoinResponse
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { Message as BindingMessage, Received as BindingReceived } from '@zlink-systems/zlink';
import type { ZLinkRemoteBoundSessionTarget } from '../actors';
import type { ZLinkBackendActorRef } from '../backend/contracts';
import {
  decodeChannelEnvelope,
  decodeChannelPayload,
  type ZLinkChannelEnvelopeCodecRegistry
} from '../channels/channel-envelope';
import {
  encodeFrameworkPayloadMessage,
  wrapFrameworkPayloadMessage
} from '../messaging/payload-codec';
import { encodeRoutingIdHex } from './route-wire-codec';
import {
  REMOTE_ACTOR_JOIN_PACKET,
  decodeRemoteActorJoinPayload,
  hasRemoteActorJoinIdentity,
  isRemoteActorJoinPayload,
  type ZLinkDecodedRemoteActorJoinRequest,
  type ZLinkRemoteActorJoinActor,
  type ZLinkRemoteActorJoinWirePayload
} from './spot-remote-codec';
import {
  submitRoutedActorJoinError,
  submitRoutedActorJoinReply
} from './spot-route-replies';
import type { ZLinkSpotSerialExecutor } from './spot-serial-executor';

interface ZLinkRoutedActorAdmissionTarget {
  onActorJoin?(actor: ZLinkActor, request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotActorJoinResponse>;
  onJoinedActor?(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
}

interface ZLinkSpotRoutedActorAdmissionOptions {
  readonly serial: ZLinkSpotSerialExecutor;
  readonly resolveActor: (actorId: string) => ZLinkActor | undefined;
  readonly getTarget: () => ZLinkRoutedActorAdmissionTarget;
  readonly defaultAccept: boolean;
  readonly routedActorProvider?: (
    actorId: string,
    actorType: string,
    actorRef?: ZLinkBackendActorRef,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    actorCreateRequest?: Message,
    signal?: AbortSignal
  ) => Promise<ZLinkRemoteActorJoinActor>;
  readonly commitRoutedActor?: (actor: ZLinkActor) => Promise<void> | void;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
}

export class ZLinkSpotRoutedActorAdmission {
  constructor(private readonly options: ZLinkSpotRoutedActorAdmissionOptions) {}

  async admit(received: BindingReceived): Promise<boolean> {
    if (received.requestSeq === null) {
      return false;
    }
    const decoded = this.decodeRemoteActorJoinRequest(received.parts, received);
    if (decoded === undefined) {
      return false;
    }
    if (this.options.routedActorProvider === undefined) {
      await this.admitResolved(decoded, received);
      return true;
    }
    await this.admitProvided(decoded, received);
    return true;
  }

  private async admitProvided(
    decoded: ZLinkDecodedRemoteActorJoinRequest,
    received: BindingReceived
  ): Promise<void> {
    try {
      const { actor, actorRef } = await this.options.routedActorProvider!(
        decoded.actorId,
        decoded.actorType,
        decoded.actorRef,
        decoded.remoteBoundSessionTarget,
        decoded.actorCreateRequest
      );
      const response = await this.runJoinCallback(actor, decoded.request);
      const reply = response.reply === undefined
        ? undefined
        : encodeFrameworkPayloadMessage(response.reply, this.options.messageSerializers);
      if (response.accepted) {
        await this.options.commitRoutedActor?.(actor);
        await this.options.serial.execute(() => this.options.getTarget().onJoinedActor?.(actor));
      }
      const replyPayload = {
        accepted: response.accepted,
        actorNodeRid: String(actorRef.nodeRid),
        actorNodeRidHex: encodeRoutingIdHex(actorRef.nodeRid),
        actorId: actorRef.actorId,
        actorGeneration: actorRef.generation.toString(),
        reply: reply?.data().toString('base64')
      };
      try {
        submitRoutedActorJoinReply(received, decoded, replyPayload);
      } finally {
        decoded.request.close();
        decoded.actorCreateRequest?.close();
      }
    } catch (error) {
      try {
        submitRoutedActorJoinError(received, decoded, error);
      } finally {
        decoded.request.close();
        decoded.actorCreateRequest?.close();
      }
    }
  }

  private async admitResolved(
    decoded: ZLinkDecodedRemoteActorJoinRequest,
    received: BindingReceived
  ): Promise<void> {
    const actor = this.options.resolveActor(decoded.actorId);
    let response: ZLinkSpotActorJoinResponse = { accepted: false };
    if (actor !== undefined) {
      response = await this.runJoinCallback(actor, decoded.request);
    }
    const reply = response.reply === undefined
      ? undefined
      : encodeFrameworkPayloadMessage(response.reply, this.options.messageSerializers);
    const actorRef = decoded.actorRef;
    const replyPayload = {
      accepted: response.accepted,
      actorNodeRid: String(actorRef?.nodeRid ?? ''),
      actorNodeRidHex: actorRef?.nodeRid === undefined ? undefined : encodeRoutingIdHex(actorRef.nodeRid),
      actorId: decoded.actorId,
      actorGeneration: (actorRef?.generation ?? 0n).toString(),
      reply: reply?.data().toString('base64')
    };
    try {
      submitRoutedActorJoinReply(received, decoded, replyPayload);
    } finally {
      decoded.request.close();
    }
    if (response.accepted && actor !== undefined) {
      await this.options.serial.execute(() => this.options.getTarget().onJoinedActor?.(actor));
    }
  }

  private runJoinCallback(actor: ZLinkActor, request: Message): Promise<ZLinkSpotActorJoinResponse> {
    const target = this.options.getTarget();
    const joinPayload = wrapFrameworkPayloadMessage(request, this.options.messageSerializers);
    return this.options.serial.execute(async () =>
      target.onActorJoin === undefined
        ? { accepted: this.options.defaultAccept }
        : target.onActorJoin(actor, joinPayload)
    );
  }

  private decodeRemoteActorJoinRequest(
    parts: readonly BindingMessage[],
    received: BindingReceived
  ): ZLinkDecodedRemoteActorJoinRequest | undefined {
    if (parts.length < 2 || parts[0].data().length === 0) {
      if (parts.length !== 1 || parts[0].data().length === 0) {
        return undefined;
      }
      try {
        const payload = JSON.parse(parts[0].data().toString()) as ZLinkRemoteActorJoinWirePayload;
        if (
          payload.packetName !== REMOTE_ACTOR_JOIN_PACKET ||
          !isRemoteActorJoinPayload(payload)
        ) {
          return undefined;
        }
        return decodeRemoteActorJoinPayload(
          payload,
          BindingMessage.from(Buffer.from(payload.request, 'base64')),
          received,
          true
        );
      } catch {
        return undefined;
      }
    }
    try {
      const envelope = decodeChannelEnvelope(parts);
      if (envelope.packetName !== REMOTE_ACTOR_JOIN_PACKET) {
        return undefined;
      }
      const payload = decodeChannelPayload(envelope, this.channelCodecs());
      if (!isRemoteActorJoinPayload(payload)) {
        return undefined;
      }
      return decodeRemoteActorJoinPayload(
        payload,
        BindingMessage.from(Buffer.from(payload.request, 'base64')),
        received,
        false,
        envelope
      );
    } catch {
      try {
        const header = JSON.parse(parts[0].data().toString()) as ZLinkRemoteActorJoinWirePayload;
        if (
          header.packetName !== REMOTE_ACTOR_JOIN_PACKET ||
          !hasRemoteActorJoinIdentity(header) ||
          parts.length < 2
        ) {
          return undefined;
        }
        return decodeRemoteActorJoinPayload(
          header,
          BindingMessage.from(Buffer.from(parts[1].data())),
          received,
          true
        );
      } catch {
        return undefined;
      }
    }
  }

  private channelCodecs(): ZLinkChannelEnvelopeCodecRegistry | undefined {
    return this.options.messageSerializers === undefined
      ? undefined
      : { serializers: this.options.messageSerializers };
  }
}
