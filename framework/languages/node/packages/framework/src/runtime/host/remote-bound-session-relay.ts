import { Message as BindingMessage } from '@zlink-systems/zlink';
import type { ActorRef, RoutingId, ZLinkActor } from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import type { ZLinkBackendSpotNode } from '../backend';
import {
  ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET,
  type ZLinkActorRoutedJoinTransport,
  type ZLinkRemoteActorPacketTarget,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import type { DefaultZLinkActorManager } from '../actors';
import type { ZLinkStreamBindingRuntime } from '../streams';
import type { ZLinkActorResponseOptions } from '../spots/spot-actor-packet-dispatch';
import {
  decodeRemoteBoundSessionErrorPayload,
  decodeRemoteBoundSessionResponsePayload,
  decodeRemoteBoundSessionSendPayload
} from '../actors/bound-session-wire';
import { encodeRemoteActorPacketTarget } from '../actors/actor-packet-relay-wire';
import { decodeRoutingId as decodeWireRoutingId } from '../routing-id';
import type { MeshRouterResolver } from './mesh-router-resolver';

export interface ZLinkRemoteBoundSessionRelayOptions {
  readonly requestTimeoutMs?: number;
  readonly routeTransport: ZLinkActorRoutedJoinTransport;
  readonly streamBindingRuntime: () => ZLinkStreamBindingRuntime;
  readonly actorManager: () => DefaultZLinkActorManager | undefined;
  readonly meshRouters: MeshRouterResolver;
  readonly primarySpotNode: () => ZLinkBackendSpotNode;
  readonly destroyedActorRefs: ReadonlyMap<string, ActorRef>;
  readonly boundSessionFactory: (actorId: string) => {
    send(message: unknown): {
      packetName(packetName: string): unknown;
      metadata(key: string, value: string): unknown;
      submit(signal?: AbortSignal): Promise<void>;
    };
  };
  readonly updateRemoteActorPacketTarget: (actorId: string, value: unknown) => void;
  readonly actorPacketTargetForState: (
    actorId: string,
    routerChannelIdHint?: string
  ) => ZLinkRemoteActorPacketTarget | undefined;
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
    actorRef?: ActorRef
  ): Promise<void> {
    const ownershipGeneration = (actorRef as (ActorRef & { ownershipGeneration?: bigint }) | undefined)
      ?.ownershipGeneration;
    const currentGeneration = this.actorOwnershipGenerations.get(actorId);
    if (
      currentGeneration !== undefined &&
      (ownershipGeneration === undefined || ownershipGeneration < currentGeneration)
    ) return;
    if (actorRef !== undefined) {
      await this.options.streamBindingRuntime().refreshActor(actorRef);
    }
    if (ownershipGeneration !== undefined) {
      this.actorOwnershipGenerations.set(actorId, ownershipGeneration);
    }
    const sent = this.options.streamBindingRuntime().sendLocalBoundSession(actorId, message, packetName, metadata);
    if (sent) {
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
    const metadata = new Map(Object.entries(send.metadata ?? {}));
    if (this.options.streamBindingRuntime().sendLocalBoundSession(
      send.actorId,
      send.message,
      send.boundPacketName,
      metadata
    )) {
      return { ok: true };
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
    if (typeof payload !== 'object' || payload === null) {
      throw new Error('Bound session ownership update payload must be an object.');
    }
    const value = payload as Record<string, unknown>;
    if (
      typeof value.actorId !== 'string' ||
      typeof value.actorNodeRid !== 'string' ||
      typeof value.actorGeneration !== 'string' ||
      typeof value.actorOwnershipGeneration !== 'string'
    ) {
      throw new Error('Bound session ownership update is missing actor identity or generation.');
    }
    const actorRef = {
      nodeRid: decodeWireRoutingId(value.actorNodeRid, value.actorNodeRidHex),
      actorId: value.actorId,
      generation: BigInt(value.actorGeneration)
    } as ActorRef;
    await this.options.streamBindingRuntime().refreshActor(actorRef);
    this.actorOwnershipGenerations.set(value.actorId, BigInt(value.actorOwnershipGeneration));
  }

  rememberRemoteBoundSessionTarget(actorId: string, target: ZLinkRemoteBoundSessionTarget): void {
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
    signal?: AbortSignal
  ): Promise<void> {
    const state = this.options.actorManager()?.getState(actor.actorId);
    const actorRef = state?.nativeActorRef as ActorRef | undefined;
    if (actorRef === undefined) {
      throw new Error(`Actor '${actor.actorId}' does not have a native actor ref.`);
    }
    if (this.options.streamBindingRuntime().sendLocalBoundSessionResponse(
      actor.actorId,
      packetName,
      requestSeq,
      response,
      replyOptions.metadata,
      replyOptions.compressPayload
    )) {
      return;
    }
    const remoteTarget = state?.remoteBoundSessionTarget;
    if (remoteTarget !== undefined) {
      await this.sendRemoteBoundSessionResponse(
        remoteTarget,
        actor.actorId,
        packetName,
        requestSeq,
        response,
        replyOptions,
        signal
      );
      return;
    }
    await this.options.streamBindingRuntime().sendNativeBoundSessionResponse(
      this.options.primarySpotNode(),
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
    const remoteTarget = state?.remoteBoundSessionTarget;
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
    const actorRef = (state?.nativeActorRef as ActorRef | undefined)
      ?? fallbackActorRef
      ?? this.options.destroyedActorRefs.get(actorId);
    if (actorRef === undefined) {
      throw new Error(`Actor '${actorId}' does not have a native actor ref.`);
    }
    await this.options.streamBindingRuntime().sendNativeBoundSessionError(
      this.options.primarySpotNode(),
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
    await this.sendRemoteBoundSessionControl(target, {
      packetName: ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET,
      actorId,
      boundPacketName: packetName,
      requestSeq: requestSeq.toString(),
      message,
      metadata: Object.fromEntries(replyOptions.metadata),
      compressPayload: replyOptions.compressPayload,
      actorPacketTarget
    }, signal);
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
    await this.sendRemoteBoundSessionControl(target, {
      packetName: ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET,
      actorId,
      boundPacketName: packetName,
      requestSeq: requestSeq.toString(),
      error: error instanceof Error
        ? { code: error.constructor.name, message: error.message }
        : error,
      metadata: Object.fromEntries(metadata),
      actorPacketTarget: encodeRemoteActorPacketTarget(
        this.options.actorPacketTargetForState(actorId, target.routerChannelId)
      )
    }, signal);
  }

  private async sendRemoteBoundSessionControl(
    target: ZLinkRemoteBoundSessionTarget,
    payload: Record<string, unknown>,
    signal?: AbortSignal
  ): Promise<void> {
    const rawPayload = BindingMessage.from(Buffer.from(JSON.stringify(payload)));
    try {
      const replyParts = await this.options.routeTransport.requestRawToSpot?.(
        {
          routerChannelId: target.routerChannelId,
          targetNodeRid: target.targetNodeRid,
          spotRid: target.spotRid,
          spotKind: ZLinkSpotKind.Entry
        },
        rawPayload,
        {
          timeoutMs: this.options.requestTimeoutMs,
          signal
        }
      );
      if (replyParts === undefined) {
        throw new Error('Remote bound session raw request transport is not available.');
      }
      for (const part of replyParts) {
        part.close();
      }
    } finally {
      rawPayload.close();
    }
  }
}
