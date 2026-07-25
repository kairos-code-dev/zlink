import { Message as BindingMessage } from '@zlink-systems/zlink';
import type {
  ActorRef,
  RoutingId,
  ZLinkRouteMessageContext,
  ZLinkSessionActor
} from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import {
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
  ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET,
  type ZLinkActorRoutedJoinTransport,
  type ZLinkRemoteActorPacketTarget,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import type { DefaultZLinkActorManager } from '../actors';
import {
  decodeRemoteActorSessionBinding,
  decodeRemoteActorPacketRelayPayload,
  encodeRemoteActorSessionBinding,
  encodeRemoteActorPacketRelayPayload,
  encodeRemoteActorPacketTarget,
  ZLINK_REMOTE_ACTOR_SESSION_BIND_PACKET
} from '../actors/actor-packet-relay-wire';
import { requestRoutedJsonReply } from '../actors/actor-routed-json-request';
import { streamMetadataMap } from '../actors/bound-session-wire';
import { normalizeRoutingId, routingIdsEqual } from '../routing-id';
import type { DefaultZLinkSpotManager, ZLinkSpotNodeRuntimeManager } from '../spots';
import type { ZLinkBoundSessionResponseTarget } from '../streams';
import type {
  ZLinkBoundSessionResponsePort,
  ZLinkStreamActorLookupPort
} from '../streams/stream-binding-runtime-ports';
import {
  decodeStreamHeader,
  encodeStreamHeader,
  messageToBytes,
  type ZLinkStreamFrameHeader,
  ZLinkStreamCodec,
  ZLinkStreamHeaderFlags,
  ZLinkStreamMessageKind
} from '../streams/protocol';
import type { MeshRouterResolver } from './mesh-router-resolver';
import { ZLinkRemoteActorPacketTargetStore } from './remote-actor-packet-target-store';

export interface ZLinkActorPacketRelayOptions {
  readonly requestTimeoutMs?: number;
  readonly routeTransport: ZLinkActorRoutedJoinTransport;
  readonly streamBindingRuntime: () => ZLinkBoundSessionResponsePort & ZLinkStreamActorLookupPort;
  readonly meshRouters: MeshRouterResolver;
  readonly actorManager: () => DefaultZLinkActorManager | undefined;
  readonly spotManager: () => DefaultZLinkSpotManager | undefined;
  readonly spotNodeRuntime: () => ZLinkSpotNodeRuntimeManager | undefined;
  readonly errorSink: () => { reportRuntimeTaskException(taskName: string, error: unknown): void };
}

export class ZLinkActorPacketRelay {
  private readonly targets: ZLinkRemoteActorPacketTargetStore;

  constructor(private readonly options: ZLinkActorPacketRelayOptions) {
    this.targets = new ZLinkRemoteActorPacketTargetStore({
      actorManager: options.actorManager,
      streamBindingRuntime: options.streamBindingRuntime,
      meshRouters: options.meshRouters,
      primaryNodeRid: () => {
        const node = options.spotNodeRuntime()?.primaryMeshNode;
        return node === undefined ? undefined : String(node.status().routingId);
      }
    });
  }

  async notifyBoundActorDisconnected(actor: ZLinkSessionActor, signal?: AbortSignal): Promise<void> {
    const state = this.options.actorManager()?.getState(actor.actorId);
    const currentRemoteBoundSessionTarget =
      state?.spotId === undefined ? undefined : state.remoteBoundSessionTarget;
    const currentRemoteActorPacketTarget =
      state?.spotId === undefined ? undefined : state.remoteActorPacketTarget;
    const remoteTarget = currentRemoteBoundSessionTarget
      ?? currentRemoteActorPacketTarget
      ?? (state?.spotId === undefined ? undefined : this.targets.cachedTargetForActor(actor));
    if (remoteTarget !== undefined) {
      await this.notifyRemoteActorDisconnected(actor.actorId, remoteTarget, signal);
      return;
    }
    const actorRefTarget = this.targets.targetForActorRef(actor.ref);
    if (actorRefTarget !== undefined) {
      await this.notifyRemoteActorDisconnected(actor.actorId, actorRefTarget, signal);
      return;
    }
    if (state?.spotId !== undefined && state.actor !== undefined) {
      const handled = await this.requireSpotManager().notifyJoinedSpotActorDisconnected(
        state.spotId,
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
    this.targets.clear(actorId);
  }

  updateRemoteActorPacketTarget(actorId: string, value: unknown): void {
    this.targets.updateFromWire(actorId, value);
  }

  actorPacketTargetForState(
    actorId: string,
    routerChannelIdHint?: string
  ): ZLinkRemoteActorPacketTarget | undefined {
    return this.targets.targetForState(actorId, routerChannelIdHint);
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
    if (state?.spotId !== undefined && state.actor !== undefined) {
      const handled = await this.requireSpotManager().notifyJoinedSpotActorDisconnected(
        state.spotId,
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
    _routeContext: ZLinkRouteMessageContext
  ): Promise<{
    readonly ok: boolean;
    readonly error?: unknown;
    readonly response?: unknown;
    readonly deferredResponse?: boolean;
    readonly actorPacketTarget?: unknown;
  }> {
    const relay = decodeRemoteActorPacketRelayPayload(payload);
    const remoteBoundSessionTarget: ZLinkRemoteBoundSessionTarget | undefined =
      relay.routerChannelId === undefined ||
      relay.boundSessionTargetNodeRid === undefined ||
      relay.boundSessionSpotId === undefined
        ? undefined
        : {
            routerChannelId: relay.routerChannelId,
            targetNodeRid: normalizeRoutingId(relay.boundSessionTargetNodeRid),
            spotId: normalizeRoutingId(relay.boundSessionSpotId)
          };
    const header = BindingMessage.from(Buffer.from(relay.header, 'base64'));
    const body = BindingMessage.from(Buffer.from(relay.payload, 'base64'));
    let closeFrameMessages = true;
    try {
      const frameHeader = decodeStreamHeader(messageToBytes(header));
      if (frameHeader.name === ZLINK_REMOTE_ACTOR_SESSION_BIND_PACKET) {
        if (frameHeader.kind !== ZLinkStreamMessageKind.Send) {
          throw new Error('Remote actor session binding requires a send frame.');
        }
        const { sessionNodeRid, sessionRid } = decodeRemoteActorSessionBinding(messageToBytes(body));
        if (!routingIdsEqual(sessionNodeRid, _routeContext.sourceNodeRid)) {
          throw new Error('Remote actor session binding source did not match the declared session node.');
        }
        const state = this.options.actorManager()?.getState(relay.actorId);
        const actorRef = state?.nativeActorRef;
        if (state === undefined || actorRef === undefined) {
          throw new Error(`Actor '${relay.actorId}' does not have a concrete actor ref.`);
        }
        if (this.requireSpotNodeRuntime().primaryMeshNode === undefined) {
          throw new Error('MeshNode actor runtime is not started.');
        }
        const target = this.options.meshRouters.remoteBoundSessionTargetForSource(sessionNodeRid)
          ?? (relay.routerChannelId === undefined
            ? undefined
            : {
                routerChannelId: relay.routerChannelId,
                targetNodeRid: sessionNodeRid,
                spotId: sessionNodeRid
              });
        if (target === undefined) {
          throw new Error('Remote actor session binding did not declare a return router.');
        }
        state.setRemoteBoundSessionTarget({ ...target, sessionNodeRid, sessionRid });
        return { ok: true, response: { acknowledged: true } };
      }
      if (frameHeader.name === ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET) {
        this.options.actorManager()?.getState(relay.actorId)?.setRemoteBoundSessionTarget(undefined);
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
        const dispatch = state?.spotId === undefined
          ? this.requireSpotNodeRuntime().dispatchEntryActorPacket(
              relay.actorId,
              [header, body],
              false,
              remoteBoundSessionTarget
            )
          : this.requireSpotManager().dispatchRoutedActorPacket(
              state.spotId,
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
      const response = state?.spotId === undefined
        ? await this.requireSpotNodeRuntime().dispatchEntryActorPacket(
            relay.actorId,
            [header, body],
            true,
            remoteBoundSessionTarget
          )
        : await this.requireSpotManager().dispatchRoutedActorPacket(
            state.spotId,
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

  async confirmRemoteSessionBinding(
    actor: ActorRef,
    sessionNodeRid: RoutingId,
    sessionRid: RoutingId,
    signal?: AbortSignal
  ): Promise<void> {
    const target = this.targets.targetForActorRef(actor);
    if (target === undefined) return;
    const header = encodeStreamHeader({
      kind: ZLinkStreamMessageKind.Send,
      codec: ZLinkStreamCodec.Json,
      flags: ZLinkStreamHeaderFlags.None,
      name: ZLINK_REMOTE_ACTOR_SESSION_BIND_PACKET,
      metadata: new Map()
    });
    const request = encodeRemoteActorPacketRelayPayload({
      actorId: actor.actorId,
      routerChannelId: target.routerChannelId,
      boundSessionTargetNodeRid: String(sessionNodeRid),
      boundSessionSpotId: String(sessionNodeRid),
      bindingActorRef: actor,
      header,
      payload: encodeRemoteActorSessionBinding({ sessionNodeRid, sessionRid })
    });
    const reply = await requestRoutedJsonReply<{
      readonly ok?: boolean;
      readonly error?: unknown;
      readonly acknowledged?: boolean;
      readonly response?: { readonly acknowledged?: boolean };
    }>(
      this.options.routeTransport,
      { ...target, spotKind: target.spotKind ?? ZLinkSpotKind.Entry },
      request,
      { timeoutMs: this.options.requestTimeoutMs, signal },
      `Remote actor session binding is not available for '${actor.actorId}'.`,
      (parts) => JSON.parse(parts[0]?.getString('utf8') ?? '{}')
    );
    if (reply.ok === false || (reply.response?.acknowledged !== true && reply.acknowledged !== true)) {
      throw new Error(
        `Actor '${actor.actorId}' did not acknowledge its remote session binding: ${JSON.stringify(reply)}`
      );
    }
  }

  async relayRemoteActorPacket(
    actor: ZLinkSessionActor,
    frameHeader: ZLinkStreamFrameHeader,
    payload: Message,
    signal?: AbortSignal
  ): Promise<boolean> {
    const responseTarget = this.options.streamBindingRuntime().captureBoundSessionResponseTarget(actor);
    const remoteTarget = this.options.actorManager()?.getState(actor.actorId)?.remoteActorPacketTarget
      ?? this.targets.cachedTargetForActor(actor);
    if (remoteTarget === undefined) {
      return false;
    }
    const localNode = this.options.spotNodeRuntime()?.primaryMeshNode;
    const localNodeRid = localNode === undefined ? undefined : String(localNode.status().routingId);
    const request = encodeRemoteActorPacketRelayPayload({
      actorId: actor.actorId,
      routerChannelId: remoteTarget.routerChannelId,
      boundSessionTargetNodeRid: localNodeRid === undefined ? undefined : String(localNodeRid),
      boundSessionSpotId: localNodeRid === undefined ? undefined : String(localNodeRid),
      header: encodeStreamHeader(frameHeader),
      payload: messageToBytes(payload)
    });
    const remoteAddress = {
      routerChannelId: remoteTarget.routerChannelId,
      targetNodeRid: remoteTarget.targetNodeRid,
      spotId: remoteTarget.spotId,
      spotKind: remoteTarget.spotKind ?? ZLinkSpotKind.User
    };
    if (frameHeader.kind === ZLinkStreamMessageKind.Send || frameHeader.requestSeq === undefined) {
      await this.options.routeTransport.sendToSpot(remoteAddress, request, {
        packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
        signal
      });
      return true;
    }
    let reply: {
      readonly ok?: boolean;
      readonly error?: unknown;
      readonly response?: unknown;
      readonly deferredResponse?: boolean;
      readonly actorPacketTarget?: unknown;
    };
    reply = await requestRoutedJsonReply(
      this.options.routeTransport,
      remoteAddress,
      request,
      {
        timeoutMs: this.options.requestTimeoutMs,
        signal
      },
      `Remote actor packet relay raw request is not available for '${actor.actorId}'.`,
      (parts) => {
        if (parts.length === 0) {
          throw new Error(`Remote actor packet relay reply was empty for '${actor.actorId}'.`);
        }
        return JSON.parse(parts[0].getString('utf8')) as typeof reply;
      }
    );
    const actorPacketTarget = this.targets.decodeFromWire(reply.actorPacketTarget);
    if (
      actorPacketTarget !== undefined &&
      (localNodeRid === undefined || !routingIdsEqual(actorPacketTarget.targetNodeRid, localNodeRid))
    ) {
      this.targets.rememberActorTarget(actor, actorPacketTarget);
    } else if (reply.ok !== false) {
      this.targets.clear(actor.actorId);
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
    return true;
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
    const payload = encodeRemoteActorPacketRelayPayload({
      actorId,
      routerChannelId: remoteTarget.routerChannelId,
      header: encodeStreamHeader(header),
      payload: Buffer.alloc(0)
    });
    await this.options.routeTransport.requestToSpot(
      {
        routerChannelId: remoteTarget.routerChannelId,
        targetNodeRid: remoteTarget.targetNodeRid,
        spotId: remoteTarget.spotId,
        spotKind: spotKind ?? ZLinkSpotKind.Entry
      },
      payload,
      {
        packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
        timeoutMs: this.options.requestTimeoutMs,
        signal
      }
    );
  }

  private async relayLocalActorPacket(
    actor: ZLinkSessionActor,
    frameHeader: ZLinkStreamFrameHeader,
    payload: Message,
    signal?: AbortSignal
  ): Promise<boolean> {
    void signal;
    const state = this.options.actorManager()?.getState(actor.actorId);
    const spotId = state?.spotId as RoutingId | undefined;
    const hasActiveSpot = spotId !== undefined && this.options.spotManager()?.hasActiveSpot(spotId) === true;
    if (!hasActiveSpot) {
      return false;
    }
    const actorRef = state?.nativeActorRef as ActorRef | undefined;
    const localNode = this.options.spotNodeRuntime()?.primaryMeshNode;
    const localNodeRid = localNode === undefined ? undefined : String(localNode.status().routingId);
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
            spotId,
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
        spotId,
        actor.actorId,
        [header, payload],
        false
      );
      return true;
    } finally {
      header.close();
    }
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
