import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkSpot
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type {
  ZLinkRemoteActorPacketTarget,
  ZLinkRemoteBoundSessionTarget
} from '../actors';
import type { ZLinkActorResponseOptions } from './spot-actor-packet-dispatch';
import type {
  ZLinkRemoteActorJoinActor,
  ZLinkRoutedActorTransferProvider
} from './spot-remote-codec';
import type { ZLinkBackendSpotNode } from '../backend/contracts';

export interface ZLinkEntryActorRuntime {
  resolveActor(actorId: string): ZLinkActor | undefined;
  commitActorTransaction(actor: ZLinkActor, onJoined: () => Promise<void>): Promise<void>;
  destroyActor(
    node: ZLinkBackendSpotNode,
    entryNodeRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void>;
  routePacket(
    actorId: string,
    parts: readonly Message[],
    returnResponse?: boolean,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget
  ): Promise<{ readonly handled: boolean; readonly response?: unknown }>;
}

export interface ZLinkSpotActorTransferRuntime {
  getOrCreateRoutedActor(
    actorId: string,
    actorType: string,
    actorRef?: ActorRef,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    actorCreateRequest?: Message,
    signal?: AbortSignal
  ): Promise<ZLinkRemoteActorJoinActor>;
  materializeRoutedActor: ZLinkRoutedActorTransferProvider;
  claimNativeActorLocation(
    actor: ZLinkActor,
    spotRid: RoutingId,
    spotMeshName: string
  ): Promise<ZLinkNativeActorJoinSnapshot>;
  claimRoutedActorLocation(actor: ZLinkActor, spotRid: RoutingId, spotMeshName: string): Promise<void>;
  publishRoutedActorOwnership(actor: ZLinkActor): Promise<void>;
  commitRoutedActor(actor: ZLinkActor, spotRid: RoutingId, spot: ZLinkSpot): void;
  clearRoutedActor(actor: ZLinkActor): void;
  rollbackNativeActorJoin(
    actor: ZLinkActor,
    snapshot: ZLinkNativeActorJoinSnapshot
  ): Promise<void>;
  rollbackRoutedActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
  actorEntryNodeRid(actor: ZLinkActor): RoutingId | undefined;
}

export interface ZLinkNativeActorJoinSnapshot {
  readonly spotRid?: RoutingId;
  readonly spot?: ZLinkSpot;
  readonly spotMeshName?: string;
}

export interface ZLinkSpotBoundSessionRuntime {
  receiveRoutedBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    actorRef?: ActorRef,
    signal?: AbortSignal
  ): Promise<void>;
  receiveRoutedBoundSessionResponse(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    replyOptions: ZLinkActorResponseOptions,
    actorPacketTarget?: unknown,
    signal?: AbortSignal
  ): Promise<void>;
  receiveRoutedBoundSessionError(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    actorPacketTarget?: unknown,
    signal?: AbortSignal
  ): Promise<void>;
  receiveRemoteBoundSessionOwnership(payload: unknown): Promise<void>;
  rememberRemoteBoundSessionTarget(actorId: string, target: ZLinkRemoteBoundSessionTarget): void;
  resolveRemoteBoundSessionTarget(
    sourceNodeRid: RoutingId,
    sourceSessionRid: RoutingId
  ): ZLinkRemoteBoundSessionTarget | undefined;
  actorPacketTargetForState(actorId: string): ZLinkRemoteActorPacketTarget | undefined;
  sendActorResponse(
    actor: ZLinkActor,
    packetName: string,
    requestSeq: bigint,
    response: unknown,
    replyOptions: ZLinkActorResponseOptions,
    fallbackBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef,
    signal?: AbortSignal
  ): Promise<void>;
  sendActorError(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    fallbackBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    actorRef?: ActorRef,
    signal?: AbortSignal
  ): Promise<void>;
}

export interface ZLinkSpotActorHandoffRuntime {
  capture(
    actorId: string,
    parts: readonly Message[],
    returnResponse?: boolean,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> | undefined;
}
