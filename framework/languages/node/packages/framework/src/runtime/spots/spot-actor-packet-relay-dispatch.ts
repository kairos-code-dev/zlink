import type { ActorRef } from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { Message as BindingMessage, Received as BindingReceived } from '@zlink-systems/zlink';
import type {
  ZLinkRemoteActorPacketTarget,
  ZLinkRemoteBoundSessionTarget
} from '../actors';
import {
  decodeStreamHeader,
  messageToBytes,
  ZLinkStreamMessageKind
} from '../streams/protocol';
import {
  encodeRemoteActorPacketTarget
} from '../actors/actor-packet-relay-wire';
import { decodeRoutingId as decodeWireRoutingId } from '../routing-id';
import type { ZLinkRemoteActorPacketRelay } from './spot-remote-route-codec';
import {
  isReplyableRequestSeq,
  submitSpotRouteBridgeReply
} from './spot-route-replies';

interface ZLinkSpotActorPacketRelayDispatchOptions {
  readonly resolveActor: (actorId: string) => unknown;
  readonly actorPacketHandler?: (
    actorId: string,
    parts: readonly Message[],
    returnResponse?: boolean,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ) => Promise<unknown>;
  readonly actorPacketTargetProvider?: (actorId: string) => ZLinkRemoteActorPacketTarget | undefined;
}

export class ZLinkSpotActorPacketRelayDispatch {
  constructor(private readonly options: ZLinkSpotActorPacketRelayDispatchOptions) {}

  async dispatch(
    received: BindingReceived,
    actorPacketRelay: ZLinkRemoteActorPacketRelay
  ): Promise<boolean> {
    if (this.options.actorPacketHandler === undefined) {
      return false;
    }
    const remoteBoundSessionTarget =
      actorPacketRelay.routerChannelId === undefined ||
      actorPacketRelay.boundSessionTargetNodeRid === undefined ||
      actorPacketRelay.boundSessionSpotRid === undefined
      ? undefined
      : {
          routerChannelId: actorPacketRelay.routerChannelId,
          targetNodeRid: decodeWireRoutingId(
            actorPacketRelay.boundSessionTargetNodeRid,
            undefined
          ),
          spotRid: decodeWireRoutingId(
            actorPacketRelay.boundSessionSpotRid,
            undefined
          )
        };
    const header = BindingMessage.from(Buffer.from(actorPacketRelay.header, 'base64'));
    const payload = BindingMessage.from(Buffer.from(actorPacketRelay.payload, 'base64'));
    let closeFrameMessages = true;
    try {
      const frameHeader = decodeStreamHeader(messageToBytes(header));
      const canDeferResponse =
        (actorPacketRelay.actorRef as (ActorRef & { handoffForwarded?: boolean }) | undefined)
          ?.handoffForwarded !== true &&
        this.options.resolveActor(actorPacketRelay.actorId) !== undefined;
      if (
        canDeferResponse &&
        frameHeader.kind === ZLinkStreamMessageKind.Request &&
        frameHeader.requestSeq !== undefined
      ) {
        const dispatch = this.options.actorPacketHandler(
          actorPacketRelay.actorId,
          [header, payload],
          false,
          remoteBoundSessionTarget,
          actorPacketRelay.actorRef
        );
        closeFrameMessages = false;
        void dispatch.catch(() => undefined).finally(() => {
          header.close();
          payload.close();
        });
        const actorPacketTarget = this.actorPacketTarget(actorPacketRelay.actorId);
        if (isReplyableRequestSeq(received.requestSeq)) {
          submitSpotRouteBridgeReply(received, actorPacketRelay.envelope, { ok: true, deferredResponse: true, actorPacketTarget });
        }
        return true;
      }
      const response = await this.options.actorPacketHandler(
        actorPacketRelay.actorId,
        [header, payload],
        true,
        remoteBoundSessionTarget,
        actorPacketRelay.actorRef
      );
      const actorPacketTarget = this.actorPacketTarget(actorPacketRelay.actorId);
      if (isReplyableRequestSeq(received.requestSeq)) {
        submitSpotRouteBridgeReply(received, actorPacketRelay.envelope, { ok: true, response, actorPacketTarget });
      }
      return true;
    } catch (error) {
      if (isReplyableRequestSeq(received.requestSeq)) {
        submitSpotRouteBridgeReply(received, actorPacketRelay.envelope, {
          ok: false,
          error: error instanceof Error ? error.message : String(error)
        });
        return true;
      }
      throw error;
    } finally {
      if (closeFrameMessages) {
        header.close();
        payload.close();
      }
    }
  }

  private actorPacketTarget(actorId: string): ReturnType<typeof encodeRemoteActorPacketTarget> {
    return encodeRemoteActorPacketTarget(this.options.actorPacketTargetProvider?.(actorId));
  }
}
