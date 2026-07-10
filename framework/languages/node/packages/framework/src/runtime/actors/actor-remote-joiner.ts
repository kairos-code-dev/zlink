import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkActorJoinResult,
  ZLinkMessageSerializer
} from '../../contracts';
import { randomUUID } from 'node:crypto';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkEncodedPayload,
  ZLinkMessage,
  ZLinkSpotKind
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { Message as BindingMessage } from '@zlink-systems/zlink';
import type {
  ZLinkBackendActorJoinEntrySpotResult,
  ZLinkBackendActorJoinResult,
  ZLinkBackendActorRef,
  ZLinkBackendSpot,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import type { ZLinkLocationLifecycle } from '../locations';
import type {
  ZLinkSpotRouteResolver,
  ZLinkSpotRouteTarget
} from '../spots/spot-routing-internal';
import {
  ZLinkActorRuntimeState,
  toFrameworkActorRef,
  toFrameworkRoutingId
} from './actor-runtime-state';
import type { ZLinkActorJoinCoordinator } from './index';
import {
  REMOTE_ACTOR_JOIN_PACKET,
  REMOTE_ACTOR_JOIN_ADMISSION,
  REMOTE_ACTOR_JOIN_COMMIT,
  buildRemoteActorJoinRequestPayload,
  decodeWireRoutingId,
  type ZLinkRemoteActorJoinReply,
  type ZLinkRemoteActorJoinRequest
} from './actor-remote-wire';
import type { ZLinkActorTransferRegistry } from './actor-transfer-registry';
import { ZLinkPostCommitActorBinder } from './post-commit-actor-binder';

export interface ZLinkActorNativeJoinCoordinatorOptions {
  readonly node: ZLinkBackendSpotNode | (() => ZLinkBackendSpotNode);
  readonly spotRouteResolver?: ZLinkSpotRouteResolver;
  readonly routedTransport?: ZLinkActorRoutedJoinTransport;
  readonly locationLifecycle?: ZLinkLocationLifecycle;
  readonly remoteActorBinder?: (actorRef: ActorRef, signal?: AbortSignal, force?: boolean) => Promise<void>;
  readonly postCommitErrorReporter?: (error: unknown) => void;
  readonly actorTransferRegistry?: ZLinkActorTransferRegistry;
  readonly sourceActorLeaver?: (
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly sourceActorMoveStarter?: (actor: ZLinkActor, state: ZLinkActorRuntimeState) => Promise<void>;
  readonly sourceActorMoveCanceler?: (actor: ZLinkActor, state: ZLinkActorRuntimeState) => Promise<void>;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
}

export interface ZLinkActorRoutedJoinTransport {
  canRouteChannel?(routerChannelId: string): boolean;
  canRoutePacketChannel?(routerChannelId: string): boolean;
  send(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void>;
  request<TReply>(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply>;
  sendToSpot(
    spotRouteTarget: ZLinkSpotRouteTarget,
    message: unknown,
    options: { readonly packetName?: string; readonly signal?: AbortSignal }
  ): Promise<void>;
  requestRawToSpot?(
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: Message,
    options: { readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<readonly Message[]>;
  requestToSpot<TReply = unknown>(
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: unknown,
    options: { readonly packetName?: string; readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<TReply>;
  requestFromSpotToSpot?<TReply = unknown>(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: unknown,
    options: { readonly packetName?: string; readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<TReply>;
  requestRawFromSpotToSpot?(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: Message,
    options: { readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<readonly Message[]>;
}

export class ZLinkActorNativeJoinCoordinator implements ZLinkActorJoinCoordinator {
  private readonly postCommitBinder: ZLinkPostCommitActorBinder | undefined;

  constructor(private readonly options: ZLinkActorNativeJoinCoordinatorOptions) {
    this.postCommitBinder = options.remoteActorBinder === undefined
      ? undefined
      : new ZLinkPostCommitActorBinder({
          bind: (actorRef, force) => options.remoteActorBinder!(actorRef, undefined, force),
          reportError: options.postCommitErrorReporter
        });
  }

  async joinSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    spotRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    throwIfAborted(signal);
    const node = this.node();
    const actorRef = state.ensureNativeActorRef(node);
    const spotRouteTarget = this.options.spotRouteResolver === undefined
      ? undefined
      : await this.options.spotRouteResolver.resolve(spotRid, signal);
    const isRemoteJoin = spotRouteTarget !== undefined && String(spotRouteTarget.targetNodeRid) !== String(actorRef.nodeRid);
    if (isRemoteJoin) {
      if (!this.canUseRoutedTransport(spotRouteTarget)) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `Remote actor join for '${actor.actorId}' requires the two-phase routed transfer protocol.`
        );
      }
      return await this.joinRemoteSpot(actor, state, actorRef, spotRouteTarget, request, timeoutMs, signal);
    }
    const joinRequest = request;
    const { result, parts } = await new Promise<{
      result: ZLinkBackendActorJoinResult;
      parts: readonly Message[];
    }>((resolve, reject) => {
      if (signal?.aborted === true) {
        reject(new Error('The operation was aborted.'));
        return;
      }
      const submitted = node.joinActor(
          actorRef,
          spotRouteTarget?.targetNodeRid ?? actorRef.nodeRid,
          toBackendRoutingId(spotRid),
          joinRequest,
        (joinResult: ZLinkBackendActorJoinResult, replyParts: readonly Message[]) => {
          resolve({ result: joinResult, parts: replyParts });
        },
        timeoutMs
      );
      if (!submitted) {
        reject(new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `Actor join submit failed for '${actor.actorId}'.`
        ));
      }
    });

    if (result.result !== 0) {
      this.disposeParts(parts);
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor join failed for '${actor.actorId}' with '${result.result}'.`
      );
    }

    state.setNativeActorRef(result.actor);
    state.setJoinedSpot(toFrameworkRoutingId(result.joinedSpotRid));
    state.setRemoteActorPacketTarget(undefined);
    if (state.actorType !== undefined) {
      await this.options.locationLifecycle?.notifyActorJoinedSpot(
        state.actorType,
        actor.actorId,
        spotRouteTarget?.routerChannelId ?? '',
        toFrameworkRoutingId(result.joinedSpotRid)
      );
    }
    this.postCommitBinder?.bindEventually(toFrameworkActorRef(result.actor));
    try {
      return {
        accepted: true,
        actor: toFrameworkActorRef(result.actor),
        reply: parts[0]
      };
    } finally {
      this.disposeParts(parts.slice(1));
    }
  }

  private async joinRemoteSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    actorRef: ZLinkBackendActorRef,
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    const actorType = state.actorType;
    if (actorType === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor '${actor.actorId}' does not have an actor type for remote SPOT join.`
      );
    }
    const routedTransport = this.options.routedTransport;
    if (routedTransport === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor '${actor.actorId}' remote route transport is not configured.`
      );
    }
    const entrySpotRid = this.node().entrySpot().routingId;
    const boundSessionTarget = state.remoteBoundSessionTarget;
    const transferId = randomUUID();
    const admissionRequest = buildRemoteActorJoinRequestPayload({
      actorId: actor.actorId,
      actorType,
      actorRef,
      actorCreateRequest: state.createRequestPayload,
      request,
      targetSpotRid: spotRouteTarget.spotRid,
      routerChannelId: spotRouteTarget.routerChannelId,
      sourceSpotRid: boundSessionTarget?.spotRid ?? entrySpotRid,
      boundSessionTarget,
      phase: REMOTE_ACTOR_JOIN_ADMISSION,
      transferId
    });
    const admissionReply = await this.requestRemoteTransferWithRetry<
      ZLinkRemoteActorJoinReply & { readonly reply?: string }
    >(routedTransport, spotRouteTarget, admissionRequest, timeoutMs, signal);
    const admissionMessage = admissionReply.reply == null
      ? undefined
      : BindingMessage.from(Buffer.from(admissionReply.reply, 'base64'));
    if (!admissionReply.accepted) {
      return {
        accepted: false,
        reply: admissionMessage
      };
    }
    let sourceLeft = false;
    let moveStarted = false;
    let transfer;
    try {
      await this.options.sourceActorMoveStarter?.(actor, state);
      moveStarted = true;
      transfer = await this.options.actorTransferRegistry?.transferOut(actor, signal) ?? {
        state: ZLinkMessage.fromEncoded(ZLinkEncodedPayload.from(Buffer.alloc(0)))
      };
      await this.options.sourceActorLeaver?.(actor, state, signal);
      sourceLeft = true;
    } catch (error) {
      if (moveStarted && !sourceLeft) {
        await this.options.sourceActorMoveCanceler?.(actor, state);
      }
      admissionMessage?.close();
      throw error;
    }
    const transferPayload = transfer.state.toEncodedPayload(this.options.messageSerializers).data();
    const commitRequest = buildRemoteActorJoinRequestPayload({
      actorId: actor.actorId,
      actorType,
      actorRef,
      request,
      targetSpotRid: spotRouteTarget.spotRid,
      routerChannelId: spotRouteTarget.routerChannelId,
      sourceSpotRid: boundSessionTarget?.spotRid ?? entrySpotRid,
      boundSessionTarget,
      phase: REMOTE_ACTOR_JOIN_COMMIT,
      transferId,
      transferAdapterKey: transfer.adapterKey,
      transferState: Buffer.from(transferPayload)
    });
    try {
      const commitReply = await this.requestRemoteTransferWithRetry<ZLinkRemoteActorJoinReply>(
        routedTransport,
        spotRouteTarget,
        commitRequest,
        timeoutMs,
        signal
      );
      return await this.applyRemoteJoinResult(
        state,
        commitReply,
        spotRouteTarget,
        admissionMessage
      );
    } catch (error) {
      admissionMessage?.close();
      throw error;
    }
  }

  private async applyRemoteJoinResult(
    state: ZLinkActorRuntimeState,
    reply: ZLinkRemoteActorJoinReply,
    spotRouteTarget: ZLinkSpotRouteTarget,
    replyMessage: Message | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    const resultActor = {
      nodeRid: decodeWireRoutingId(reply.actorNodeRid, reply.actorNodeRidHex),
      actorId: reply.actorId,
      generation: BigInt(reply.actorGeneration)
    } as ActorRef;
    if (reply.accepted) {
      state.endMove();
      state.setNativeActorRef(resultActor as unknown as ZLinkBackendActorRef);
      state.setJoinedSpot(spotRouteTarget.spotRid);
      state.setRemoteActorPacketTarget({
        routerChannelId: spotRouteTarget.routerChannelId,
        targetNodeRid: spotRouteTarget.targetNodeRid,
        spotRid: spotRouteTarget.spotRid,
        spotKind: spotRouteTarget.spotKind
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
      this.postCommitBinder?.bindEventually(resultActor);
    }
    return {
      accepted: reply.accepted,
      actor: reply.accepted ? resultActor : undefined,
      reply: replyMessage
    };
  }

  private canUseRoutedTransport(spotRouteTarget: ZLinkSpotRouteTarget): boolean {
    const transport = this.options.routedTransport;
    if (transport === undefined) {
      return false;
    }
    if (transport.canRoutePacketChannel !== undefined) {
      return transport.canRoutePacketChannel(spotRouteTarget.routerChannelId) ||
        transport.requestRawToSpot !== undefined;
    }
    return transport.requestRawToSpot !== undefined ||
      transport.canRouteChannel?.(spotRouteTarget.routerChannelId) !== false;
  }

  private async requestRemoteTransfer<TReply>(
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
      const request = BindingMessage.from(Buffer.from(JSON.stringify(payload)));
      try {
        const parts = await transport.requestRawToSpot(target, request, { timeoutMs, signal });
        try {
          if (parts.length === 0) {
            throw new ZLinkFrameworkException(
              ZLinkFrameworkErrorKind.ActorRouteNotFound,
              'Remote actor transfer reply was empty.'
            );
          }
          return JSON.parse(parts[0].getString('utf8')) as TReply;
        } finally {
          this.disposeParts(parts);
        }
      } finally {
        request.close();
      }
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

  private async requestRemoteTransferWithRetry<TReply>(
    transport: ZLinkActorRoutedJoinTransport,
    target: ZLinkSpotRouteTarget,
    payload: unknown,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<TReply> {
    try {
      return await this.requestRemoteTransfer<TReply>(transport, target, payload, timeoutMs, signal);
    } catch (firstError) {
      throwIfAborted(signal);
      try {
        return await this.requestRemoteTransfer<TReply>(transport, target, payload, timeoutMs, signal);
      } catch (retryError) {
        throw new AggregateError([firstError, retryError], 'Remote actor transfer request failed twice.');
      }
    }
  }

  async joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    throwIfAborted(signal);
    const node = this.node();
    const actorRef = state.ensureNativeActorRef(node);
    const remoteEntry = String(nodeRid) === String(actorRef.nodeRid)
      ? undefined
      : await this.tryResolveRemoteEntry(nodeRid, signal);
    const isRemoteJoin = remoteEntry !== undefined && String(remoteEntry.targetNodeRid) !== String(actorRef.nodeRid);
    if (isRemoteJoin && this.canUseRoutedTransport(remoteEntry)) {
      const result = await this.joinRemoteSpot(
        actor,
        state,
        actorRef,
        {
          routerChannelId: remoteEntry.routerChannelId,
          targetNodeRid: remoteEntry.targetNodeRid,
          spotRid: remoteEntry.spotRid,
          spotKind: ZLinkSpotKind.Entry
        },
        request,
        timeoutMs,
        signal
      );
      if (result.accepted) {
        state.clearJoinedSpot();
        state.setRemoteActorPacketTarget(undefined);
        if (state.actorType !== undefined) {
          await this.options.locationLifecycle?.notifyActorLeftSpot(state.actorType, actor.actorId);
        }
      }
      return result;
    }
    const { result, parts } = await new Promise<{
      result: ZLinkBackendActorJoinEntrySpotResult;
      parts: readonly Message[];
    }>(
      (resolve, reject) => {
        const submitted = node.joinActorEntrySpot(
          actorRef,
          toBackendRoutingId(nodeRid),
          request,
          (entryResult, replyParts) => resolve({ result: entryResult, parts: replyParts }),
          timeoutMs
        );
        if (!submitted) {
          reject(new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            `Actor entry SPOT join submit failed for '${actor.actorId}'.`
          ));
        }
      }
    );

    if (result.result !== 0) {
      this.disposeParts(parts);
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor entry SPOT join failed for '${actor.actorId}' with '${result.result}'.`
      );
    }

    if (result.joinResultCode === 0) {
      state.setNativeActorRef(result.actor);
      state.clearJoinedSpot();
      if (state.actorType !== undefined) {
        await this.options.locationLifecycle?.notifyActorLeftSpot(state.actorType, actor.actorId);
      }
    }
    try {
      return {
        accepted: true,
        actor: toFrameworkActorRef(result.actor),
        reply: parts[0]
      };
    } finally {
      this.disposeParts(parts.slice(1));
    }
  }

  private disposeParts(parts: readonly Message[]): void {
    for (const part of parts) {
      part.close();
    }
  }

  private async tryResolveRemoteEntry(
    nodeRid: RoutingId,
    signal: AbortSignal | undefined
  ): Promise<ZLinkSpotRouteTarget | undefined> {
    if (this.options.spotRouteResolver === undefined) {
      return undefined;
    }
    try {
      return await this.options.spotRouteResolver.resolve(nodeRid, signal);
    } catch {
      return undefined;
    }
  }

  private node(): ZLinkBackendSpotNode {
    return typeof this.options.node === 'function'
      ? this.options.node()
      : this.options.node;
  }
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted === true) {
    throw new Error('The operation was aborted.');
  }
}

function toBackendRoutingId(routingId: RoutingId): ZLinkBackendActorRef['nodeRid'] {
  return routingId as unknown as ZLinkBackendActorRef['nodeRid'];
}
