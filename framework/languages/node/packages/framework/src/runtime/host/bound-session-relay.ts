import { Message as BindingMessage } from '@zlink-systems/zlink';
import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkRouteRequestContext,
  ZLinkRouteSendContext,
  ZLinkSessionActor
} from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkBackendActorRef, ZLinkBackendSpotNode } from '../backend';
import {
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
  ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET,
  type ZLinkActorRoutedJoinTransport,
  type ZLinkRemoteActorPacketTarget,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import type { ZLinkActorResponseOptions } from '../spots/spot-actor-packet-dispatch';
import type { DefaultZLinkActorManager } from '../actors';
import type { DefaultZLinkSpotManager, ZLinkSpotNodeRuntimeManager } from '../spots';
import type { ZLinkBoundSessionResponseTarget, ZLinkStreamBindingRuntime } from '../streams';
import {
  decodeStreamHeader,
  encodeStreamHeader,
  messageToBytes,
  type ZLinkStreamFrameHeader,
  ZLinkStreamCodec,
  ZLinkStreamHeaderFlags,
  ZLinkStreamMessageKind
} from '../streams/protocol';
import {
  decodeRemoteActorJoinPayload,
  decodeRemoteActorPacketRelayPayload,
  encodeRemoteActorPacketTarget,
  normalizeRuntimeRoutingId,
  streamMetadataMap
} from '../spots/route-wire-codec';
import type { MeshRouterResolver } from './mesh-router-resolver';
import { ZLinkRemoteActorPacketTargetStore } from './remote-actor-packet-target-store';
import { ZLinkRemoteBoundSessionRelay } from './remote-bound-session-relay';

export interface ZLinkBoundSessionRelayOptions {
  readonly requestTimeoutMs?: number;
  readonly routeTransport: ZLinkActorRoutedJoinTransport;
  readonly streamBindingRuntime: () => ZLinkStreamBindingRuntime;
  readonly meshRouters: MeshRouterResolver;
  readonly actorManager: () => DefaultZLinkActorManager | undefined;
  readonly spotManager: () => DefaultZLinkSpotManager | undefined;
  readonly spotNodeRuntime: () => ZLinkSpotNodeRuntimeManager | undefined;
  readonly primarySpotNode: () => ZLinkBackendSpotNode;
  readonly destroyedActorRefs: ReadonlyMap<string, ActorRef>;
  readonly errorSink: () => { reportRuntimeTaskException(taskName: string, error: unknown): void };
  readonly boundSessionFactory: (actorId: string) => {
    send(message: unknown): {
      packetName(packetName: string): unknown;
      metadata(key: string, value: string): unknown;
      submit(signal?: AbortSignal): Promise<void>;
    };
  };
}

export class ZLinkBoundSessionRelay {
  private readonly actorPacketTargets: ZLinkRemoteActorPacketTargetStore;
  private readonly boundSessions: ZLinkRemoteBoundSessionRelay;

  constructor(private readonly options: ZLinkBoundSessionRelayOptions) {
    this.actorPacketTargets = new ZLinkRemoteActorPacketTargetStore({
      actorManager: options.actorManager,
      streamBindingRuntime: options.streamBindingRuntime,
      meshRouters: options.meshRouters,
      primaryNodeRid: () => options.spotNodeRuntime()?.primaryNode?.routingId as RoutingId | undefined
    });
    this.boundSessions = new ZLinkRemoteBoundSessionRelay({
      requestTimeoutMs: options.requestTimeoutMs,
      routeTransport: options.routeTransport,
      streamBindingRuntime: options.streamBindingRuntime,
      actorManager: options.actorManager,
      primarySpotNode: options.primarySpotNode,
      destroyedActorRefs: options.destroyedActorRefs,
      boundSessionFactory: options.boundSessionFactory,
      updateRemoteActorPacketTarget: (actorId, value) => this.updateRemoteActorPacketTarget(actorId, value),
      actorPacketTargetForState: (actorId, routerChannelIdHint) =>
        this.actorPacketTargetForState(actorId, routerChannelIdHint)
    });
  }

  async receiveRoutedBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>
  ): Promise<void> {
    await this.boundSessions.receiveRoutedBoundSession(actorId, message, packetName, metadata);
  }

  async receiveRoutedBoundSessionResponse(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    replyOptions: ZLinkActorResponseOptions,
    actorPacketTarget?: unknown
  ): Promise<void> {
    await this.boundSessions.receiveRoutedBoundSessionResponse(
      actorId,
      packetName,
      requestSeq,
      message,
      replyOptions,
      actorPacketTarget
    );
  }

  async receiveRoutedBoundSessionError(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    actorPacketTarget?: unknown
  ): Promise<void> {
    await this.boundSessions.receiveRoutedBoundSessionError(
      actorId,
      packetName,
      requestSeq,
      error,
      metadata,
      actorPacketTarget
    );
  }

  async receiveRemoteBoundSessionSend(payload: unknown): Promise<{ readonly ok: boolean }> {
    return await this.boundSessions.receiveRemoteBoundSessionSend(payload);
  }

  async receiveRemoteBoundSessionResponse(payload: unknown): Promise<{ readonly ok: boolean }> {
    return await this.boundSessions.receiveRemoteBoundSessionResponse(payload);
  }

  async receiveRemoteBoundSessionError(payload: unknown): Promise<{ readonly ok: boolean }> {
    return await this.boundSessions.receiveRemoteBoundSessionError(payload);
  }

  async receiveRemoteActorJoin(
    payload: unknown,
    routeContext: ZLinkRouteRequestContext
  ): Promise<{
    readonly accepted: boolean;
    readonly actorNodeRid: string;
    readonly actorNodeRidHex?: string;
    readonly actorId: string;
    readonly actorGeneration: string;
    readonly reply?: string;
  }> {
    const join = decodeRemoteActorJoinPayload(payload);
    const actorManager = this.requireActorManager();
    const actor = await actorManager.getOrCreateActor(join.actorId, join.actorType);
    const state = actorManager.getState(join.actorId);
    if (state === undefined) {
      throw new Error(`Actor '${join.actorId}' state was not created.`);
    }
    const actorRef = {
      nodeRid: normalizeRuntimeRoutingId(join.actorNodeRid),
      actorId: join.actorId,
      generation: BigInt(join.actorGeneration)
    };
    state.setNativeActorRef(actorRef as unknown as ZLinkBackendActorRef);
    state.setRemoteBoundSessionTarget({
      routerChannelId: join.boundSessionRouterChannelId ?? join.routerChannelId ?? routeContext.channelName ?? '',
      targetNodeRid: normalizeRuntimeRoutingId(join.boundSessionTargetNodeRid ?? routeContext.sourceNodeRid),
      spotRid: normalizeRuntimeRoutingId(join.boundSessionSpotRid ?? join.sourceSpotRid ?? routeContext.sourceNodeRid)
    });
    const request = BindingMessage.from(Buffer.from(join.request, 'base64'));
    try {
      const response = await this.requireSpotManager().admitActorJoin(
        join.spotRid as RoutingId,
        actor,
        request,
        (spot) => state.setJoinedSpot(join.spotRid as RoutingId, spot)
      );
      const reply = response.reply as Message | undefined;
      return {
        accepted: response.accepted,
        actorNodeRid: join.actorNodeRid,
        actorNodeRidHex: join.actorNodeRidHex,
        actorId: join.actorId,
        actorGeneration: join.actorGeneration,
        reply: reply?.data().toString('base64')
      };
    } finally {
      request.close();
    }
  }

  async sendActorResponse(
    actor: ZLinkActor,
    packetName: string,
    requestSeq: bigint,
    response: unknown,
    replyOptions: ZLinkActorResponseOptions,
    signal?: AbortSignal
  ): Promise<void> {
    await this.boundSessions.sendActorResponse(actor, packetName, requestSeq, response, replyOptions, signal);
  }

  async sendActorError(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    fallbackActorRef?: ActorRef,
    signal?: AbortSignal
  ): Promise<void> {
    await this.boundSessions.sendActorError(actorId, packetName, requestSeq, error, metadata, fallbackActorRef, signal);
  }

  async notifyBoundActorDisconnected(
    actor: ZLinkSessionActor,
    signal?: AbortSignal
  ): Promise<void> {
    const state = this.options.actorManager()?.getState(actor.actorId);
    const currentRemoteBoundSessionTarget =
      state?.spotRid === undefined ? undefined : state.remoteBoundSessionTarget;
    const currentRemoteActorPacketTarget =
      state?.spotRid === undefined ? undefined : state.remoteActorPacketTarget;
    const remoteTarget = currentRemoteBoundSessionTarget
      ?? currentRemoteActorPacketTarget
      ?? (state?.spotRid === undefined ? undefined : this.actorPacketTargets.cachedTargetForActor(actor));
    if (remoteTarget !== undefined) {
      await this.notifyRemoteActorDisconnected(actor.actorId, remoteTarget, signal);
      return;
    }
    const actorRefTarget = this.actorPacketTargets.targetForActorRef(actor.ref);
    if (actorRefTarget !== undefined) {
      await this.notifyRemoteActorDisconnected(actor.actorId, actorRefTarget, signal);
      return;
    }
    if (state?.spotRid !== undefined && state.actor !== undefined) {
      const handled = await this.requireSpotManager().notifyJoinedSpotActorDisconnected(
        state.spotRid,
        state.actor,
        signal
      );
      if (handled) {
        return;
      }
    }
    await this.notifyActorDisconnectedById(actor.actorId, signal);
  }

  clearRemoteActorPacketTarget(actorId: string): void {
    this.actorPacketTargets.clear(actorId);
  }

  async notifyActorDisconnectedById(actorId: string, signal?: AbortSignal): Promise<void> {
    const state = this.options.actorManager()?.getState(actorId);
    const remoteTarget = state?.remoteBoundSessionTarget ?? state?.remoteActorPacketTarget;
    if (remoteTarget !== undefined) {
      await this.notifyRemoteActorDisconnected(actorId, remoteTarget, signal);
      return;
    }
    await this.notifyLocalActorDisconnectedById(actorId, signal);
  }

  async notifyLocalActorDisconnectedById(actorId: string, signal?: AbortSignal): Promise<void> {
    const state = this.options.actorManager()?.getState(actorId);
    if (state?.spotRid !== undefined && state.actor !== undefined) {
      const handled = await this.requireSpotManager().notifyJoinedSpotActorDisconnected(
        state.spotRid,
        state.actor,
        signal
      );
      if (handled) {
        return;
      }
    }
    const localActor = state?.actor;
    if (localActor === undefined) {
      throw new Error(`Actor '${actorId}' does not have a local actor instance.`);
    }
    await this.requireSpotNodeRuntime().notifyPrimaryEntrySpotActorDisconnected(localActor, signal);
  }

  async receiveRemoteActorPacketRelay(
    payload: unknown,
    routeContext: ZLinkRouteSendContext
  ): Promise<{
    readonly ok: boolean;
    readonly error?: unknown;
    readonly response?: unknown;
    readonly deferredResponse?: boolean;
    readonly actorPacketTarget?: unknown;
  }> {
    const relay = decodeRemoteActorPacketRelayPayload(payload);
    const remoteBoundSessionTarget: ZLinkRemoteBoundSessionTarget | undefined =
      relay.routerChannelId === undefined
        ? undefined
        : {
            routerChannelId: relay.routerChannelId,
            targetNodeRid: normalizeRuntimeRoutingId(relay.boundSessionTargetNodeRid ?? routeContext.sourceNodeRid),
            spotRid: normalizeRuntimeRoutingId(relay.boundSessionSpotRid ?? routeContext.sourceNodeRid)
          };
    const header = BindingMessage.from(Buffer.from(relay.header, 'base64'));
    const body = BindingMessage.from(Buffer.from(relay.payload, 'base64'));
    let closeFrameMessages = true;
    try {
      const frameHeader = decodeStreamHeader(messageToBytes(header));
      if (frameHeader.name === ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET) {
        await this.notifyLocalActorDisconnectedById(relay.actorId);
        return {
          ok: true,
          actorPacketTarget: encodeRemoteActorPacketTarget(
            this.actorPacketTargetForState(relay.actorId, relay.routerChannelId)
          )
        };
      }
      const state = this.options.actorManager()?.getState(relay.actorId);
      if (frameHeader.kind === ZLinkStreamMessageKind.Request && frameHeader.requestSeq !== undefined) {
        const dispatch = state?.spotRid === undefined
          ? this.requireSpotNodeRuntime().dispatchEntryActorPacket(
              relay.actorId,
              [header, body],
              false,
              remoteBoundSessionTarget
            )
          : this.requireSpotManager().dispatchRoutedActorPacket(
              state.spotRid,
              relay.actorId,
              [header, body],
              false,
              remoteBoundSessionTarget
            );
        closeFrameMessages = false;
        void dispatch.catch((error) =>
          this.options.errorSink().reportRuntimeTaskException('remote actor packet relay', error)
        ).finally(() => {
          header.close();
          body.close();
        });
        return {
          ok: true,
          deferredResponse: true,
          actorPacketTarget: encodeRemoteActorPacketTarget(
            this.actorPacketTargetForState(relay.actorId, relay.routerChannelId)
          )
        };
      }
      const response = state?.spotRid === undefined
        ? await this.requireSpotNodeRuntime().dispatchEntryActorPacket(
            relay.actorId,
            [header, body],
            true,
            remoteBoundSessionTarget
          )
        : await this.requireSpotManager().dispatchRoutedActorPacket(
            state.spotRid,
            relay.actorId,
            [header, body],
            true,
            remoteBoundSessionTarget
          );
      return {
        ok: true,
        response,
        actorPacketTarget: encodeRemoteActorPacketTarget(
          this.actorPacketTargetForState(relay.actorId, relay.routerChannelId)
        )
      };
    } catch (error) {
      return {
        ok: false,
        error: error instanceof Error ? error.message : String(error)
      };
    } finally {
      if (closeFrameMessages) {
        header.close();
        body.close();
      }
    }
  }

  async relayActorPacket(
    actor: ZLinkSessionActor,
    frameHeader: ZLinkStreamFrameHeader,
    payload: Message,
    signal?: AbortSignal
  ): Promise<boolean> {
    if (await this.relayRemoteActorPacket(actor, frameHeader, payload, signal)) {
      return true;
    }
    return await this.relayLocalActorPacket(actor, frameHeader, payload, signal);
  }

  async relayRemoteActorPacket(
    actor: ZLinkSessionActor,
    frameHeader: ZLinkStreamFrameHeader,
    payload: Message,
    signal?: AbortSignal
  ): Promise<boolean> {
    const responseTarget = this.options.streamBindingRuntime().captureBoundSessionResponseTarget(actor);
    const remoteTarget = this.options.actorManager()?.getState(actor.actorId)?.remoteActorPacketTarget
      ?? this.actorPacketTargets.cachedTargetForActor(actor);
    if (remoteTarget === undefined) {
      return false;
    }
    const localNodeRid = this.options.spotNodeRuntime()?.primaryNode?.routingId as RoutingId | undefined;
    const request = {
      packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
      actorId: actor.actorId,
      routerChannelId: remoteTarget.routerChannelId,
      boundSessionTargetNodeRid: localNodeRid === undefined ? undefined : String(localNodeRid),
      boundSessionSpotRid: localNodeRid === undefined ? undefined : String(localNodeRid),
      header: Buffer.from(encodeStreamHeader(frameHeader)).toString('base64'),
      payload: Buffer.from(messageToBytes(payload)).toString('base64')
    };
    const remoteAddress = {
      routerChannelId: remoteTarget.routerChannelId,
      targetNodeRid: remoteTarget.targetNodeRid,
      spotRid: remoteTarget.spotRid,
      spotKind: remoteTarget.spotKind ?? ZLinkSpotKind.User
    };
    if (frameHeader.kind === ZLinkStreamMessageKind.Send || frameHeader.requestSeq === undefined) {
      await this.options.routeTransport.sendToSpot(remoteAddress, request, {
        packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
        signal
      });
      return true;
    }
    try {
      let reply: {
        readonly ok?: boolean;
        readonly error?: unknown;
        readonly response?: unknown;
        readonly deferredResponse?: boolean;
        readonly actorPacketTarget?: unknown;
      };
      const payload = BindingMessage.from(Buffer.from(JSON.stringify(request)));
      try {
        const parts = await this.options.routeTransport.requestRawToSpot?.(remoteAddress, payload, {
          timeoutMs: this.options.requestTimeoutMs,
          signal
        });
        if (parts === undefined) {
          throw new Error(`Remote actor packet relay raw request is not available for '${actor.actorId}'.`);
        }
        try {
          if (parts.length === 0) {
            throw new Error(`Remote actor packet relay reply was empty for '${actor.actorId}'.`);
          }
          reply = JSON.parse(parts[0].getString('utf8')) as {
            readonly ok?: boolean;
            readonly error?: unknown;
            readonly response?: unknown;
            readonly deferredResponse?: boolean;
            readonly actorPacketTarget?: unknown;
          };
        } finally {
          parts.forEach((part) => part.close());
        }
      } finally {
        payload.close();
      }
      const actorPacketTarget = this.actorPacketTargets.decodeFromWire(reply.actorPacketTarget);
      if (
        actorPacketTarget !== undefined &&
        (localNodeRid === undefined || !routingIdsEqual(actorPacketTarget.targetNodeRid, localNodeRid))
      ) {
        this.actorPacketTargets.rememberActorTarget(actor, actorPacketTarget);
      } else if (reply.ok !== false) {
        this.actorPacketTargets.clear(actor.actorId);
      }
      if (reply.deferredResponse === true && reply.ok !== false) {
        return true;
      }
      if (reply.ok === false) {
        const sent = this.sendCapturedOrCurrentBoundSessionError(
          responseTarget,
          actor.actorId,
          frameHeader.name,
          frameHeader.requestSeq,
          reply.error ?? 'Remote actor request failed.',
          streamMetadataMap(frameHeader.metadata)
        );
        if (!sent) {
          throw new Error(`Actor '${actor.actorId}' local bound session error response route is not ready.`);
        }
        return true;
      }
      const sent = this.sendCapturedOrCurrentBoundSessionResponse(
        responseTarget,
        actor.actorId,
        frameHeader.name,
        frameHeader.requestSeq,
        reply.response,
        streamMetadataMap(frameHeader.metadata)
      );
      if (!sent) {
        throw new Error(`Actor '${actor.actorId}' local bound session response route is not ready.`);
      }
    } finally {
      void request;
    }
    return true;
  }

  actorPacketTargetForState(
    actorId: string,
    routerChannelIdHint?: string
  ): ZLinkRemoteActorPacketTarget | undefined {
    return this.actorPacketTargets.targetForState(actorId, routerChannelIdHint);
  }

  private async relayLocalActorPacket(
    actor: ZLinkSessionActor,
    frameHeader: ZLinkStreamFrameHeader,
    payload: Message,
    signal?: AbortSignal
  ): Promise<boolean> {
    void signal;
    const state = this.options.actorManager()?.getState(actor.actorId);
    const spotRid = state?.spotRid as RoutingId | undefined;
    const hasActiveSpot = spotRid !== undefined && this.options.spotManager()?.hasActiveSpot(spotRid) === true;
    if (!hasActiveSpot) {
      return false;
    }
    const actorRef = state?.nativeActorRef as ActorRef | undefined;
    const localNodeRid = this.options.spotNodeRuntime()?.primaryNode?.routingId as RoutingId | undefined;
    if (
      actorRef?.nodeRid !== undefined &&
      localNodeRid !== undefined &&
      !routingIdsEqual(actorRef.nodeRid as RoutingId, localNodeRid)
    ) {
      return false;
    }
    const responseTarget = this.options.streamBindingRuntime().captureBoundSessionResponseTarget(actor);
    const header = BindingMessage.from(Buffer.from(encodeStreamHeader(frameHeader)));
    try {
      if (frameHeader.kind === ZLinkStreamMessageKind.Request && frameHeader.requestSeq !== undefined) {
        try {
          const response = await this.requireSpotManager().dispatchRoutedActorPacket(
            spotRid,
            actor.actorId,
            [header, payload],
            true
          );
          const sent = this.sendCapturedOrCurrentBoundSessionResponse(
            responseTarget,
            actor.actorId,
            frameHeader.name,
            frameHeader.requestSeq,
            response,
            streamMetadataMap(frameHeader.metadata)
          );
          if (!sent) {
            throw new Error(`Actor '${actor.actorId}' local bound session response route is not ready.`);
          }
        } catch (error) {
          const sent = this.sendCapturedOrCurrentBoundSessionError(
            responseTarget,
            actor.actorId,
            frameHeader.name,
            frameHeader.requestSeq,
            error,
            streamMetadataMap(frameHeader.metadata)
          );
          if (!sent) {
            throw error;
          }
        }
        return true;
      }
      await this.requireSpotManager().dispatchRoutedActorPacket(
        spotRid,
        actor.actorId,
        [header, payload],
        false
      );
      return true;
    } finally {
      header.close();
    }
  }

  private updateRemoteActorPacketTarget(actorId: string, value: unknown): void {
    this.actorPacketTargets.updateFromWire(actorId, value);
  }

  async notifyRemoteActorDisconnected(
    actorId: string,
    remoteTarget: ZLinkRemoteBoundSessionTarget | ZLinkRemoteActorPacketTarget,
    signal?: AbortSignal
  ): Promise<void> {
    const spotKind: ZLinkSpotKind | undefined =
      'spotKind' in remoteTarget ? remoteTarget.spotKind as ZLinkSpotKind | undefined : undefined;
    const header: ZLinkStreamFrameHeader = {
      kind: ZLinkStreamMessageKind.Send,
      codec: ZLinkStreamCodec.Raw,
      flags: ZLinkStreamHeaderFlags.None,
      name: ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET,
      metadata: new Map()
    };
    const payload = {
      packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
      actorId,
      routerChannelId: remoteTarget.routerChannelId,
      header: Buffer.from(encodeStreamHeader(header)).toString('base64'),
      payload: Buffer.alloc(0).toString('base64')
    };
    await this.options.routeTransport.sendToSpot(
      {
        routerChannelId: remoteTarget.routerChannelId,
        targetNodeRid: remoteTarget.targetNodeRid,
        spotRid: remoteTarget.spotRid,
        spotKind: spotKind ?? ZLinkSpotKind.Entry
      },
      payload,
      {
        packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
        signal
      }
    );
  }

  private sendCapturedOrCurrentBoundSessionResponse(
    target: ZLinkBoundSessionResponseTarget | undefined,
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    response: unknown,
    metadata: ReadonlyMap<string, string>
  ): boolean {
    return target?.sendResponse(packetName, requestSeq, response, metadata)
      ?? this.options.streamBindingRuntime().sendLocalBoundSessionResponse(
        actorId,
        packetName,
        requestSeq,
        response,
        metadata,
        false
      );
  }

  private sendCapturedOrCurrentBoundSessionError(
    target: ZLinkBoundSessionResponseTarget | undefined,
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>
  ): boolean {
    return target?.sendError(packetName, requestSeq, error, metadata)
      ?? this.options.streamBindingRuntime().sendLocalBoundSessionError(
        actorId,
        packetName,
        requestSeq,
        error,
        metadata
      );
  }

  private requireActorManager(): DefaultZLinkActorManager {
    const manager = this.options.actorManager();
    if (manager === undefined) {
      throw new Error('Actor manager runtime is not started.');
    }
    return manager;
  }

  private requireSpotNodeRuntime(): ZLinkSpotNodeRuntimeManager {
    const runtime = this.options.spotNodeRuntime();
    if (runtime === undefined) {
      throw new Error('SPOT node runtime is not started.');
    }
    return runtime;
  }

  private requireSpotManager(): DefaultZLinkSpotManager {
    const manager = this.options.spotManager();
    if (manager === undefined) {
      throw new Error('SPOT manager runtime is not started.');
    }
    return manager;
  }
}

function routingIdsEqual(left: RoutingId, right: RoutingId): boolean {
  const leftHex = (left as { toHex?: () => string }).toHex?.();
  const rightHex = (right as { toHex?: () => string }).toHex?.();
  if (leftHex !== undefined && rightHex !== undefined) {
    return leftHex === rightHex;
  }
  return String(left) === String(right);
}
