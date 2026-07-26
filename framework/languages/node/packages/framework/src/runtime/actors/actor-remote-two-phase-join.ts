import { randomUUID } from 'node:crypto';
import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkMessageSerializer
} from '../../contracts';
import type { ZLinkActorJoinRuntimeResult } from './actor-runtime-contracts';
import {
  ZLinkEncodedPayload,
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkMessage
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { Message as BindingMessage } from '@zlink-systems/zlink';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import type { ZLinkLocationLifecycle } from '../locations';
import { throwIfAborted } from '../abort';
import type { ZLinkSpotRouteTarget } from '../spots/spot-routing-internal';
import type { ZLinkActorRoutedJoinTransport } from './actor-routed-join-transport';
import { requestRoutedJsonReply } from './actor-routed-json-request';
import type { ZLinkActorSourceTransfer } from './actor-source-transfer';
import { ZLinkActorRuntimeState } from './actor-runtime-state';
import {
  REMOTE_ACTOR_JOIN_ADMISSION,
  REMOTE_ACTOR_JOIN_COMMIT,
  REMOTE_ACTOR_JOIN_PACKET,
  buildRemoteActorJoinRequestPayload,
  decodeWireRoutingId,
  type ZLinkRemoteActorJoinReply
} from './actor-remote-wire';
import type { ZLinkPostCommitActorBinder } from './post-commit-actor-binder';
import type { ZLinkPostCommitActorLocation } from './post-commit-actor-location';

export interface ZLinkRemoteTwoPhaseActorJoinOptions {
  readonly routedTransport?: ZLinkActorRoutedJoinTransport;
  readonly locationLifecycle?: ZLinkLocationLifecycle;
  readonly postCommitLocation?: ZLinkPostCommitActorLocation;
  readonly sourceTransfer?: ZLinkActorSourceTransfer;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly postCommitBinder?: ZLinkPostCommitActorBinder;
}

/** Owns the admission/commit protocol and the resulting remote actor state. */
export class ZLinkRemoteTwoPhaseActorJoin {
  constructor(private readonly options: ZLinkRemoteTwoPhaseActorJoinOptions) {}

  canJoin(target: ZLinkSpotRouteTarget): boolean {
    const transport = this.options.routedTransport;
    if (transport === undefined) {
      return false;
    }
    if (transport.canRoutePacketChannel !== undefined) {
      return transport.canRoutePacketChannel(target.routerChannelId) ||
        transport.requestRawToSpot !== undefined;
    }
    return transport.requestRawToSpot !== undefined ||
      transport.canRouteChannel?.(target.routerChannelId) !== false;
  }

  async joinSpot(
    node: ZLinkBackendSpotNode,
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    actorRef: ZLinkBackendActorRef,
    target: ZLinkSpotRouteTarget,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinRuntimeResult<Message>> {
    const actorType = state.actorType;
    if (actorType === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor '${actor.context.actorId}' does not have an actor type for remote SPOT join.`
      );
    }
    const transport = this.options.routedTransport;
    if (transport === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor '${actor.context.actorId}' remote route transport is not configured.`
      );
    }

    const entrySpotId = node.entrySpot().routingId;
    const currentBoundSessionTarget = state.remoteBoundSessionTarget ?? state.boundSessionTransferTarget;
    const boundSessionTarget = currentBoundSessionTarget === undefined
      ? undefined
      : {
          ...currentBoundSessionTarget,
          previousAuthorityOwnerGeneration:
            currentBoundSessionTarget.previousAuthorityOwnerGeneration ?? state.locationGeneration,
          previousOwnerLeaseGeneration:
            currentBoundSessionTarget.previousOwnerLeaseGeneration ?? state.ownerLeaseGeneration
        };
    const transferId = randomUUID();
    const admissionRequest = buildRemoteActorJoinRequestPayload({
      actorId: actor.context.actorId,
      actorType,
      actorRef,
      expectedMembershipEpoch: state.spotMembershipEpoch,
      actorEntryNodeRid: state.entryNodeRid ?? actorRef.nodeRid as unknown as RoutingId,
      actorCreateRequest: state.createRequestPayload,
      request,
      targetSpotId: target.spotId,
      routerChannelId: target.routerChannelId,
      sourceSpotId: boundSessionTarget?.spotId ?? entrySpotId,
      boundSessionTarget,
      phase: REMOTE_ACTOR_JOIN_ADMISSION,
      transferId
    });
    const admissionReply = await this.requestWithRetry<
      ZLinkRemoteActorJoinReply & { readonly reply?: string }
    >(transport, target, admissionRequest, timeoutMs, signal);
    const admissionMessage = admissionReply.reply == null
      ? undefined
      : BindingMessage.from(Buffer.from(admissionReply.reply, 'base64'));
    if (!admissionReply.accepted) {
      return { accepted: false, reply: admissionMessage };
    }

    let transfer;
    try {
      transfer = await this.options.sourceTransfer?.prepareSource(actor, state, signal) ?? {
        state: ZLinkMessage.fromEncoded(ZLinkEncodedPayload.from(Buffer.alloc(0))),
        handoffBacklog: [],
        commit() {},
        async rollback() {}
      };
    } catch (error) {
      admissionMessage?.close();
      throw error;
    }

    const committedBoundSessionTarget = state.remoteBoundSessionTarget ?? state.boundSessionTransferTarget;

    const commitRequest = buildRemoteActorJoinRequestPayload({
      actorId: actor.context.actorId,
      actorType,
      actorRef,
      expectedMembershipEpoch: state.spotMembershipEpoch,
      actorEntryNodeRid: state.entryNodeRid ?? actorRef.nodeRid as unknown as RoutingId,
      request,
      targetSpotId: target.spotId,
      routerChannelId: target.routerChannelId,
      sourceSpotId: boundSessionTarget?.spotId ?? entrySpotId,
      boundSessionTarget: committedBoundSessionTarget,
      phase: REMOTE_ACTOR_JOIN_COMMIT,
      transferId,
      transferAdapterKey: transfer.adapterKey,
      transferState: Buffer.from(
        transfer.state.toEncodedPayload(this.options.messageSerializers).data()
      ),
      handoffBacklog: transfer.handoffBacklog
    });
    try {
      const commitReply = await this.requestWithRetry<ZLinkRemoteActorJoinReply>(
        transport,
        target,
        commitRequest,
        timeoutMs,
        signal
      );
      if (commitReply.accepted) {
        transfer.commit(
          target,
          actorRefFromReply(commitReply),
          commitReply.handoffResults ?? []
        );
      } else await transfer.rollback();
      return await this.applyResult(state, commitReply, target, admissionMessage);
    } catch (error) {
      await transfer.rollback();
      admissionMessage?.close();
      throw error;
    }
  }

  async joinEntrySpot(
    node: ZLinkBackendSpotNode,
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    actorRef: ZLinkBackendActorRef,
    target: ZLinkSpotRouteTarget,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinRuntimeResult<Message>> {
    const result = await this.joinSpot(node, actor, state, actorRef, target, request, timeoutMs, signal);
    if (result.accepted) {
      state.clearJoinedSpot();
      state.setRemoteActorPacketTarget(undefined);
    }
    return result;
  }

  private async applyResult(
    state: ZLinkActorRuntimeState,
    reply: ZLinkRemoteActorJoinReply,
    target: ZLinkSpotRouteTarget,
    replyMessage: Message | undefined
  ): Promise<ZLinkActorJoinRuntimeResult<Message>> {
    const resultActor = actorRefFromReply(reply);
    if (reply.accepted) {
      state.endMove();
      state.setNativeActorRef(resultActor as unknown as ZLinkBackendActorRef);
      state.setJoinedSpot(target.spotId);
      state.setRemoteActorPacketTarget({
        routerChannelId: target.routerChannelId,
        targetNodeRid: target.targetNodeRid,
        spotId: target.spotId,
        spotKind: target.spotKind
      });
      if (state.actorType !== undefined && state.ownsLocation) {
        const actorType = state.actorType;
        const cleanup = this.options.locationLifecycle?.releaseActorEventually(actorType, reply.actorId);
        if (cleanup === undefined) {
          state.markLocationReleased();
        } else {
          void cleanup.then(() => state.markLocationReleased());
        }
      }
      await this.options.postCommitBinder?.bind(resultActor);
    }
    return {
      accepted: reply.accepted,
      actor: reply.accepted ? resultActor : undefined,
      reply: replyMessage
    };
  }

  private async request<TReply>(
    transport: ZLinkActorRoutedJoinTransport,
    target: ZLinkSpotRouteTarget,
    payload: unknown,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<TReply> {
    if (
      transport.canRoutePacketChannel?.(target.routerChannelId) === true &&
      typeof transport.requestToSpot === 'function'
    ) {
      return await transport.requestToSpot<TReply>(target, payload, {
        packetName: REMOTE_ACTOR_JOIN_PACKET,
        timeoutMs,
        signal
      });
    }
    if (transport.requestRawToSpot !== undefined) {
      return await requestRoutedJsonReply(
        transport,
        target,
        payload,
        { timeoutMs, signal },
        'Remote actor transfer raw request transport is not available.',
        (parts) => {
          if (parts.length === 0) {
            throw new ZLinkFrameworkException(
              ZLinkFrameworkErrorKind.ActorRouteNotFound,
              'Remote actor transfer reply was empty.'
            );
          }
          return JSON.parse(parts[0].getString('utf8')) as TReply;
        }
      );
    }
    return await transport.request<TReply>(
      target.routerChannelId,
      target.targetNodeRid,
      REMOTE_ACTOR_JOIN_PACKET,
      payload,
      timeoutMs,
      signal
    );
  }

  private async requestWithRetry<TReply>(
    transport: ZLinkActorRoutedJoinTransport,
    target: ZLinkSpotRouteTarget,
    payload: unknown,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<TReply> {
    try {
      return await this.request<TReply>(transport, target, payload, timeoutMs, signal);
    } catch (firstError) {
      throwIfAborted(signal);
      try {
        return await this.request<TReply>(transport, target, payload, timeoutMs, signal);
      } catch (retryError) {
        throw new AggregateError([firstError, retryError], 'Remote actor transfer request failed twice.');
      }
    }
  }
}

function actorRefFromReply(reply: ZLinkRemoteActorJoinReply): ActorRef {
  return {
    nodeRid: decodeWireRoutingId(reply.actorNodeRid, reply.actorNodeRidHex),
    actorId: reply.actorId,
    generation: BigInt(reply.actorGeneration)
  } as ActorRef;
}
