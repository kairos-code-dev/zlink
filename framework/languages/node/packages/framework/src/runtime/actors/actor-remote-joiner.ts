import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkActorJoinResult
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
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
  buildRemoteActorJoinRequestPayload,
  decodeWireRoutingId,
  type ZLinkRemoteActorJoinReply,
  type ZLinkRemoteActorJoinRequest
} from './actor-remote-wire';

export interface ZLinkActorNativeJoinCoordinatorOptions {
  readonly node: ZLinkBackendSpotNode;
  readonly spotRouteResolver?: ZLinkSpotRouteResolver;
  readonly routedTransport?: ZLinkActorRoutedJoinTransport;
  readonly locationLifecycle?: ZLinkLocationLifecycle;
  readonly remoteActorBinder?: (actorRef: ActorRef, signal?: AbortSignal, force?: boolean) => Promise<void>;
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
  constructor(private readonly options: ZLinkActorNativeJoinCoordinatorOptions) {}

  async joinSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    spotRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    throwIfAborted(signal);
    const actorRef = state.ensureNativeActorRef(this.options.node);
    const spotRouteTarget = this.options.spotRouteResolver === undefined
      ? undefined
      : await this.options.spotRouteResolver.resolve(spotRid, signal);
    const isRemoteJoin = spotRouteTarget !== undefined && String(spotRouteTarget.targetNodeRid) !== String(actorRef.nodeRid);
    if (isRemoteJoin && this.canUseRoutedTransport(spotRouteTarget)) {
      return await this.joinRemoteSpot(actor, state, actorRef, spotRouteTarget, request, timeoutMs, signal);
    }
    const joinRequest = isRemoteJoin
      ? this.encodeRemoteNativeJoinRequest(actor, state, request)
      : request;
    const { result, parts } = await new Promise<{
      result: ZLinkBackendActorJoinResult;
      parts: readonly Message[];
    }>((resolve, reject) => {
      if (signal?.aborted === true) {
        reject(new Error('The operation was aborted.'));
        return;
      }
      const submitted = this.options.node.joinActor(
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
    await this.options.remoteActorBinder?.(toFrameworkActorRef(result.actor), signal, !isRemoteJoin);
    try {
      return {
        accepted: true,
        actor: toFrameworkActorRef(result.actor),
        reply: parts[0]
      };
    } finally {
      this.disposeParts(parts.slice(1));
      if (joinRequest !== request) {
        joinRequest.close();
      }
    }
  }

  private encodeRemoteNativeJoinRequest(actor: ZLinkActor, state: ZLinkActorRuntimeState, request: Message): Message {
    const actorType = state.actorType;
    const actorRef = state.nativeActorRef;
    if (actorType === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor '${actor.actorId}' does not have an actor type for remote SPOT join.`
      );
    }
    return BindingMessage.from(Buffer.from(JSON.stringify(buildRemoteActorJoinRequestPayload({
      actorType,
      actorRef,
      actorCreateRequest: state.createRequestPayload,
      request
    }))));
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
    if (this.canUseRoutedTransport(spotRouteTarget)) {
      const routedTransport = this.options.routedTransport;
      if (routedTransport === undefined) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `Actor '${actor.actorId}' remote route transport is not configured.`
        );
      }
      const entrySpotRid = this.options.node.entrySpot().routingId;
      const boundSessionTarget = state.remoteBoundSessionTarget;
      const joinRequest = buildRemoteActorJoinRequestPayload({
        actorId: actor.actorId,
        actorType,
        actorRef,
        actorCreateRequest: state.createRequestPayload,
        request,
        targetSpotRid: spotRouteTarget.spotRid,
        routerChannelId: spotRouteTarget.routerChannelId,
        sourceSpotRid: boundSessionTarget?.spotRid ?? entrySpotRid,
        boundSessionTarget
      });
      const reply = typeof routedTransport.requestToSpot === 'function'
        ? await routedTransport.requestToSpot<ZLinkRemoteActorJoinReply & { readonly reply?: string }>(
            spotRouteTarget,
            joinRequest,
            {
              packetName: REMOTE_ACTOR_JOIN_PACKET,
              timeoutMs,
              signal
            }
          )
        : await routedTransport.request<ZLinkRemoteActorJoinReply & { readonly reply?: string }>(
            spotRouteTarget.routerChannelId,
            spotRouteTarget.targetNodeRid,
            REMOTE_ACTOR_JOIN_PACKET,
            joinRequest,
            timeoutMs,
            signal
          );
      return await this.applyRemoteJoinResult(
        state,
        reply,
        spotRouteTarget,
        reply.reply == null ? undefined : BindingMessage.from(Buffer.from(reply.reply, 'base64')),
        signal
      );
    }
    const entrySpotRid = this.options.node.entrySpot().routingId;
    const boundSessionTarget = state.remoteBoundSessionTarget;
    const joinPayload = buildRemoteActorJoinRequestPayload({
      actorId: actor.actorId,
      actorType,
      actorRef,
      actorCreateRequest: state.createRequestPayload,
      request,
      routerChannelId: spotRouteTarget.routerChannelId,
      sourceSpotRid: boundSessionTarget?.spotRid ?? entrySpotRid,
      boundSessionTarget
    });
    const transport = this.options.routedTransport;
    if (transport?.requestRawToSpot !== undefined) {
      const payload = BindingMessage.from(Buffer.from(JSON.stringify(joinPayload)));
      try {
        const parts = await transport.requestRawToSpot(spotRouteTarget, payload, { timeoutMs, signal });
        try {
          if (parts.length === 0) {
            throw new ZLinkFrameworkException(
              ZLinkFrameworkErrorKind.ActorRouteNotFound,
              `Remote actor join reply was empty for '${actor.actorId}'.`
            );
          }
          const reply = JSON.parse(parts[0].getString('utf8')) as ZLinkRemoteActorJoinReply;
          return await this.applyRemoteJoinResult(state, reply, spotRouteTarget, parts[1], signal);
        } finally {
          parts[0]?.close();
          this.disposeParts(parts.slice(2));
        }
      } finally {
        payload.close();
      }
    }
    const {
      request: _encodedRequest,
      ...headerPayload
    } = joinPayload as ZLinkRemoteActorJoinRequest & { readonly request?: string };
    const header = BindingMessage.from(Buffer.from(JSON.stringify(headerPayload)));
    const outbound = this.options.node.getOrCreateSpot(`__zlink.actor.join.${String(actorRef.nodeRid)}`).spot;
    try {
      const parts = await new Promise<readonly Message[]>((resolve, reject) => {
        if (signal?.aborted === true) {
          reject(new Error('The operation was aborted.'));
          return;
        }
        const submitted = outbound.requestToSpot(
          spotRouteTarget.targetNodeRid,
          spotRouteTarget.spotRid,
          [header, request],
          (result, replyParts) => {
            if (result !== 0) {
              reject(new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
            `Remote actor join failed for '${actor.actorId}' to SPOT '${spotRouteTarget.spotRid}' with result ${result}.`
              ));
              return;
            }
            resolve(replyParts as readonly Message[]);
          },
          0,
          timeoutMs
        );
        if (!submitted) {
          reject(new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            `Remote actor join submit failed for '${actor.actorId}'.`
          ));
        }
      });
      try {
        if (parts.length === 0) {
          throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            `Remote actor join reply was empty for '${actor.actorId}'.`
          );
        }
        const reply = JSON.parse(parts[0].getString('utf8')) as ZLinkRemoteActorJoinReply;
        return await this.applyRemoteJoinResult(state, reply, spotRouteTarget, parts[1], signal);
      } finally {
        parts[0]?.close();
        this.disposeParts(parts.slice(2));
      }
    } finally {
      header.close();
    }
  }

  private async applyRemoteJoinResult(
    state: ZLinkActorRuntimeState,
    reply: ZLinkRemoteActorJoinReply,
    spotRouteTarget: ZLinkSpotRouteTarget,
    replyMessage: Message | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    const resultActor = {
      nodeRid: decodeWireRoutingId(reply.actorNodeRid, reply.actorNodeRidHex),
      actorId: reply.actorId,
      generation: BigInt(reply.actorGeneration)
    } as ActorRef;
    if (reply.accepted) {
      state.setNativeActorRef(resultActor as unknown as ZLinkBackendActorRef);
      state.setJoinedSpot(spotRouteTarget.spotRid);
      state.setRemoteActorPacketTarget({
        routerChannelId: spotRouteTarget.routerChannelId,
        targetNodeRid: spotRouteTarget.targetNodeRid,
        spotRid: spotRouteTarget.spotRid,
        spotKind: spotRouteTarget.spotKind
      });
      if (state.actorType !== undefined) {
        await this.options.locationLifecycle?.notifyActorJoinedSpot(
          state.actorType,
          reply.actorId,
          spotRouteTarget.routerChannelId,
          spotRouteTarget.spotRid
        );
      }
      await this.options.remoteActorBinder?.(resultActor, signal, true);
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
      return transport.canRoutePacketChannel(spotRouteTarget.routerChannelId);
    }
    return transport.canRouteChannel?.(spotRouteTarget.routerChannelId) !== false;
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
    const actorRef = state.ensureNativeActorRef(this.options.node);
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
        const submitted = this.options.node.joinActorEntrySpot(
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
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted === true) {
    throw new Error('The operation was aborted.');
  }
}

function toBackendRoutingId(routingId: RoutingId): ZLinkBackendActorRef['nodeRid'] {
  return routingId as unknown as ZLinkBackendActorRef['nodeRid'];
}
