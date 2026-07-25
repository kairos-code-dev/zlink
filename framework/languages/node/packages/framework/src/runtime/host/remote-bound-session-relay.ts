import type { ActorRef, RoutingId, ZLinkActor } from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import type { ZLinkBackendActorSessionNode } from '../backend';
import {
  type ZLinkActorRoutedJoinTransport,
  type ZLinkRemoteActorPacketTarget,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import type { DefaultZLinkActorManager } from '../actors';
import type {
  ZLinkRemoteBoundSessionPort,
  ZLinkStreamActorLookupPort
} from '../streams/stream-binding-runtime-ports';
import type { DefaultZLinkBoundSession } from '../streams/session-context';
import type { ZLinkActorResponseOptions } from '../spots/spot-actor-packet-dispatch';
import {
  decodeRemoteBoundSessionErrorPayload,
  decodeRemoteBoundSessionOwnershipPayload,
  decodeRemoteBoundSessionResponsePayload,
  decodeRemoteBoundSessionSendPayload,
  encodeRemoteBoundSessionErrorPayload,
  encodeRemoteBoundSessionResponsePayload
} from '../actors/bound-session-wire';
import { encodeRemoteActorPacketTarget } from '../actors/actor-packet-relay-wire';
import { requestRoutedJson } from '../actors/actor-routed-json-request';
import {
  decodeRoutingId as decodeWireRoutingId,
  routingIdsEqual
} from '../routing-id';
import type { MeshRouterResolver } from './mesh-router-resolver';

export interface ZLinkRemoteBoundSessionRelayOptions {
  readonly requestTimeoutMs?: number;
  readonly routeTransport: ZLinkActorRoutedJoinTransport;
  readonly streamBindingRuntime: () => ZLinkRemoteBoundSessionPort & ZLinkStreamActorLookupPort;
  readonly actorManager: () => DefaultZLinkActorManager | undefined;
  readonly meshRouters: MeshRouterResolver;
  readonly primarySpotNode?: () => ZLinkBackendActorSessionNode;
  readonly destroyedActorRefs: ReadonlyMap<string, ActorRef>;
  readonly boundSessionFactory: (actorId: string) => DefaultZLinkBoundSession;
  readonly updateRemoteActorPacketTarget: (actorId: string, value: unknown) => void;
  readonly actorPacketTargetForState: (
    actorId: string,
    routerChannelIdHint?: string
  ) => ZLinkRemoteActorPacketTarget | undefined;
  readonly reportOwnershipRefreshError?: (actorId: string, error: unknown) => void;
}

export class ZLinkRemoteBoundSessionRelay {
  private readonly actorOwnershipGenerations = new Map<string, bigint>();

  constructor(private readonly options: ZLinkRemoteBoundSessionRelayOptions) {
  }

  async receiveRoutedBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    actorRef?: ActorRef,
    actorPacketTarget?: unknown
  ): Promise<void> {
    this.options.updateRemoteActorPacketTarget(actorId, actorPacketTarget);
    const ownershipGeneration = (actorRef as (ActorRef & { ownershipGeneration?: bigint }) | undefined)
      ?.ownershipGeneration;
    const currentGeneration = this.actorOwnershipGenerations.get(actorId);
    if (
      currentGeneration !== undefined &&
      (ownershipGeneration === undefined || ownershipGeneration < currentGeneration)
    ) return;
    if (actorRef !== undefined) {
      await this.options.streamBindingRuntime().rebindActor(actorRef);
    }
    if (ownershipGeneration !== undefined) {
      this.actorOwnershipGenerations.set(actorId, ownershipGeneration);
    }
    const sent = this.options.streamBindingRuntime().sendLocalBoundSession(actorId, message, packetName, metadata);
    if (sent) {
      return;
    }
    if (this.options.actorManager()?.getState(actorId)?.remoteBoundSessionTarget === undefined) {
      return;
    }
    const call = this.options.boundSessionFactory(actorId).send(message);
    if (packetName !== undefined) {
      call.packetName(packetName);
    }
    for (const [key, value] of metadata) {
      call.metadata(key, value);
    }
    await call.submit();
  }

  async receiveRoutedBoundSessionResponse(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    replyOptions: ZLinkActorResponseOptions,
    actorPacketTarget?: unknown
  ): Promise<void> {
    this.options.updateRemoteActorPacketTarget(actorId, actorPacketTarget);
    this.options.streamBindingRuntime().sendLocalBoundSessionResponse(
      actorId,
      packetName,
      requestSeq,
      message,
      replyOptions.metadata,
      replyOptions.compressPayload
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
    this.options.updateRemoteActorPacketTarget(actorId, actorPacketTarget);
    this.options.streamBindingRuntime().sendLocalBoundSessionError(
      actorId,
      packetName,
      requestSeq,
      error,
      metadata
    );
  }

  async receiveRemoteBoundSessionSend(payload: unknown): Promise<{ readonly ok: boolean }> {
    const send = decodeRemoteBoundSessionSendPayload(payload);
    this.options.updateRemoteActorPacketTarget(send.actorId, send.actorPacketTarget);
    const metadata = new Map(Object.entries(send.metadata ?? {}));
    if (this.options.streamBindingRuntime().sendLocalBoundSession(
      send.actorId,
      send.message,
      send.boundPacketName,
      metadata
    )) {
      return { ok: true };
    }
    if (this.options.actorManager()?.getState(send.actorId)?.remoteBoundSessionTarget === undefined) {
      return { ok: false };
    }
    const call = this.options.boundSessionFactory(send.actorId).send(send.message);
    if (send.boundPacketName !== undefined) {
      call.packetName(send.boundPacketName);
    }
    for (const [key, value] of metadata) {
      call.metadata(key, value);
    }
    await call.submit();
    return { ok: true };
  }

  async receiveRemoteBoundSessionResponse(payload: unknown): Promise<{ readonly ok: boolean }> {
    const response = decodeRemoteBoundSessionResponsePayload(payload);
    this.options.updateRemoteActorPacketTarget(response.actorId, response.actorPacketTarget);
    const sent = this.options.streamBindingRuntime().sendLocalBoundSessionResponse(
      response.actorId,
      response.boundPacketName,
      response.requestSeq,
      response.message,
      new Map(Object.entries(response.metadata ?? {})),
      response.compressPayload
    );
    return { ok: sent };
  }

  async receiveRemoteBoundSessionError(payload: unknown): Promise<{ readonly ok: boolean }> {
    const response = decodeRemoteBoundSessionErrorPayload(payload);
    this.options.updateRemoteActorPacketTarget(response.actorId, response.actorPacketTarget);
    const sent = this.options.streamBindingRuntime().sendLocalBoundSessionError(
      response.actorId,
      response.boundPacketName,
      response.requestSeq,
      response.error,
      new Map(Object.entries(response.metadata ?? {}))
    );
    return { ok: sent };
  }

  async receiveRemoteBoundSessionOwnership(payload: unknown): Promise<void> {
    const value = decodeRemoteBoundSessionOwnershipPayload(payload);
    const actorRef = {
      nodeRid: decodeWireRoutingId(value.actorNodeRid, value.actorNodeRidHex),
      actorId: value.actorId,
      generation: BigInt(value.actorGeneration)
    } as ActorRef;
    this.options.updateRemoteActorPacketTarget(value.actorId, value.actorPacketTarget);
    const current = this.options.streamBindingRuntime().find(value.actorId)?.ref;
    if (
      current !== undefined &&
      current.actorId === actorRef.actorId &&
      current.generation === actorRef.generation &&
      routingIdsEqual(current.nodeRid, actorRef.nodeRid)
    ) {
      this.actorOwnershipGenerations.set(value.actorId, BigInt(value.actorOwnershipGeneration));
      return;
    }
    this.actorOwnershipGenerations.set(value.actorId, BigInt(value.actorOwnershipGeneration));
    void this.options.streamBindingRuntime().refreshActor(actorRef).catch((error) => {
      this.options.reportOwnershipRefreshError?.(value.actorId, error);
    });
  }

  rememberRemoteBoundSessionTarget(actorId: string, target: ZLinkRemoteBoundSessionTarget | undefined): void {
    this.options.actorManager()?.getState(actorId)?.setRemoteBoundSessionTarget(target);
  }

  resolveRemoteBoundSessionTarget(
    sourceNodeRid: RoutingId,
    _sourceSessionRid: RoutingId
  ): ZLinkRemoteBoundSessionTarget | undefined {
    return this.options.meshRouters.remoteBoundSessionTargetForSource(sourceNodeRid);
  }

  actorPacketTargetForState(
    actorId: string,
    routerChannelIdHint?: string
  ): ZLinkRemoteActorPacketTarget | undefined {
    return this.options.actorPacketTargetForState(actorId, routerChannelIdHint);
  }

  clearOwnership(actorId: string): void {
    this.actorOwnershipGenerations.delete(actorId);
  }

  async sendActorResponse(
    actor: ZLinkActor,
    packetName: string,
    requestSeq: bigint,
    response: unknown,
    replyOptions: ZLinkActorResponseOptions,
    fallbackBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef,
    signal?: AbortSignal
  ): Promise<void> {
    const state = this.options.actorManager()?.getState(actor.context.actorId);
    if (this.options.streamBindingRuntime().sendLocalBoundSessionResponse(
      actor.context.actorId,
      packetName,
      requestSeq,
      response,
      replyOptions.metadata,
      replyOptions.compressPayload
    )) {
      return;
    }
    const stateActorRef = state?.nativeActorRef as ActorRef | undefined;
    const actorRef = stateActorRef === undefined
      ? fallbackActorRef
      : {
          ...stateActorRef,
          bindingGeneration: state!.boundSessionBindingGeneration
        } as ActorRef;
    const remoteTarget = fallbackBoundSessionTarget ?? state?.remoteBoundSessionTarget;
    if (remoteTarget !== undefined) {
      await this.sendRemoteBoundSessionResponse(
        remoteTarget,
        actor.context.actorId,
        packetName,
        requestSeq,
        response,
        replyOptions,
        signal
      );
      return;
    }
    if (actorRef === undefined) {
      throw new Error(`Actor '${actor.context.actorId}' does not have a native actor ref.`);
    }
    const primarySpotNode = this.options.primarySpotNode?.();
    if (primarySpotNode === undefined) {
      throw new Error('Native bound-session response requires the RouteMesh stream-session service.');
    }
    await this.options.streamBindingRuntime().sendNativeBoundSessionResponse(
      primarySpotNode,
      actorRef,
      packetName,
      requestSeq,
      response,
      replyOptions.metadata,
      replyOptions.compressPayload,
      signal
    );
  }

  async sendActorError(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    fallbackBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef,
    signal?: AbortSignal
  ): Promise<void> {
    if (this.options.streamBindingRuntime().sendLocalBoundSessionError(
      actorId,
      packetName,
      requestSeq,
      error,
      metadata
    )) {
      return;
    }
    const state = this.options.actorManager()?.getState(actorId);
    const stateActorRef = state?.nativeActorRef as ActorRef | undefined;
    const actorRef = (stateActorRef === undefined
      ? fallbackActorRef
      : {
          ...stateActorRef,
          bindingGeneration: state!.boundSessionBindingGeneration
        } as ActorRef)
      ?? this.options.destroyedActorRefs.get(actorId);
    const remoteTarget = fallbackBoundSessionTarget ?? state?.remoteBoundSessionTarget;
    if (remoteTarget !== undefined) {
      await this.sendRemoteBoundSessionError(
        remoteTarget,
        actorId,
        packetName,
        requestSeq,
        error,
        metadata,
        signal
      );
      return;
    }
    if (actorRef === undefined) {
      throw new Error(`Actor '${actorId}' does not have a native actor ref.`);
    }
    const primarySpotNode = this.options.primarySpotNode?.();
    if (primarySpotNode === undefined) {
      throw new Error('Native bound-session error response requires the RouteMesh stream-session service.');
    }
    await this.options.streamBindingRuntime().sendNativeBoundSessionError(
      primarySpotNode,
      actorRef,
      packetName,
      requestSeq,
      error,
      metadata,
      signal
    );
  }

  private async sendRemoteBoundSessionResponse(
    target: ZLinkRemoteBoundSessionTarget,
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    replyOptions: ZLinkActorResponseOptions,
    signal?: AbortSignal
  ): Promise<void> {
    const actorPacketTarget = encodeRemoteActorPacketTarget(
      this.options.actorPacketTargetForState(actorId, target.routerChannelId)
    );
    await this.sendRemoteBoundSessionControl(target, encodeRemoteBoundSessionResponsePayload({
      actorId,
      boundPacketName: packetName,
      requestSeq,
      message,
      metadata: replyOptions.metadata,
      compressPayload: replyOptions.compressPayload,
      actorPacketTarget
    }), signal);
  }

  private async sendRemoteBoundSessionError(
    target: ZLinkRemoteBoundSessionTarget,
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    await this.sendRemoteBoundSessionControl(target, encodeRemoteBoundSessionErrorPayload({
      actorId,
      boundPacketName: packetName,
      requestSeq,
      error,
      metadata,
      actorPacketTarget: encodeRemoteActorPacketTarget(
        this.options.actorPacketTargetForState(actorId, target.routerChannelId)
      )
    }), signal);
  }

  private async sendRemoteBoundSessionControl(
    target: ZLinkRemoteBoundSessionTarget,
    payload: Record<string, unknown>,
    signal?: AbortSignal
  ): Promise<void> {
    await requestRoutedJson(
      this.options.routeTransport,
      {
        routerChannelId: target.routerChannelId,
        targetNodeRid: target.targetNodeRid,
        spotId: target.spotId,
        spotKind: ZLinkSpotKind.Entry
      },
      payload,
      { timeoutMs: this.options.requestTimeoutMs, signal },
      'Remote bound session raw request transport is not available.'
    );
  }
}
