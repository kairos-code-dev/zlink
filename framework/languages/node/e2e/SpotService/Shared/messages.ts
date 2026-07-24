import type { SpotHandle } from '@zlink-systems/framework';

export const SpotServiceNames = {
  spotChannel: 'spot.service',
  controlChannel: 'spot.control',
  externalSpotChannel: 'spot.external.play-a',
  externalSpotChannelB: 'spot.external.play-b',
  externalClientChannel: 'spot.external.client',
  spotEventTopic: 'spot.service.events',
  streamNode: 'session-stream',
  tlsStreamNode: 'session-stream-tls',
  playSpotNode: 'play-node',
  multiSpotNodeA: 'multi-node-a',
  multiSpotNodeB: 'multi-node-b',
  spotOnlyMesh: 'spot-only.mesh',
  multiRouteChannelA: 'multi-route-a',
  multiRouteChannelB: 'multi-route-b',
  actorType: 'scenario-player',
  actorIdMetadata: 'actor-id'
} as const;

export interface CreateSpotReq {
  readonly spotRid: string;
}

export interface CreateSpotRes {
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly state: string;
}

export interface ScaleOutReadinessReq {
  readonly nodeRid: string;
  readonly timeoutMilliseconds?: number;
}

export interface ScaleOutReadinessRes {
  readonly nodeRid: string;
  readonly peerReady: boolean;
  readonly entrySpotReady: boolean;
  readonly capabilities: readonly string[];
}

export interface ScaleOutActorProbeReq {
  readonly actorId: string;
  readonly marker: string;
}

export interface ScaleOutActorProbeRes {
  readonly actorId: string;
  readonly nodeRid: string;
  readonly marker: string;
}

export interface CloseSpotReq {
  readonly spotRid: string;
}

export interface CloseSpotRes {
  readonly spotRid: string;
  readonly closed: boolean;
}

export interface StateReq {
  readonly operation: string;
  readonly delta: number;
}

export interface StateRes {
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly value: number;
}

export interface MultiNodeCreateSpotReq {
  readonly spotRid: string;
  readonly delta: number;
}

export interface MultiNodeCreateSpotRes {
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly state: string;
  readonly value: number;
}

export interface MultiNodeStateRouteReq {
  readonly spotRid: string;
  readonly delta: number;
}

export interface StateMsg {
  readonly marker: string;
}

export interface SpotOnlyMeshReq {
  readonly sourceSpotRid: string;
  readonly targetSpotRid: string;
  readonly marker: string;
}

export interface SpotOnlyMeshRes {
  readonly sourceSpotRid: string;
  readonly targetSpotRid: string;
  readonly targetValue: number;
  readonly marker: string;
}

export class SpotOnlyJoinReq {
  constructor(
    readonly targetSpotRid: string,
    readonly actorId: string,
    readonly marker: string
  ) {}
}

export interface SpotOnlyJoinRes {
  readonly targetSpotRid: string;
  readonly actorId: string;
  readonly accepted: boolean;
  readonly marker: string;
}

export interface StageProbeReq {
  readonly marker: string;
  readonly delta: number;
}

export interface StageTimerStartMsg {
  readonly name: string;
  readonly periodMs: number;
}

export interface SpotMsg {
  readonly marker: string;
}

export interface SpotOutboundMsg {
  readonly marker: string;
}

export interface SpotOutboundNegativeMsg {
  readonly marker: string;
}

export interface SpotOutboundRouteReq {
  readonly spotRid: string;
  readonly marker: string;
}

export interface SpotOutboundRouteRes {
  readonly spotRid: string;
  readonly marker: string;
  readonly accepted: boolean;
  readonly evidence: readonly string[];
}

export interface SpotToSpotReq {
  readonly targetSpotRid: string;
  readonly targetSpot: SpotHandle;
  readonly marker: string;
}

export interface SpotToSpotRes {
  readonly sourceSpotRid: string;
  readonly targetSpotRid: string;
  readonly targetValue: number;
}

export interface SpotToSpotRouteReq {
  readonly sourceSpotRid: string;
  readonly targetSpotRid: string;
  readonly marker: string;
}

export interface SpotToSpotTimeoutReq {
  readonly targetSpotRid: string;
  readonly targetSpot: SpotHandle;
  readonly marker: string;
}

export interface SpotToSpotTimeoutRes {
  readonly sourceSpotRid: string;
  readonly targetSpotRid: string;
  readonly failed: boolean;
}

export interface SpotToSpotTimeoutRouteReq {
  readonly sourceSpotRid: string;
  readonly targetSpotRid: string;
  readonly marker: string;
}

export interface SpotToSpotNegativeReq {
  readonly targetSpotRid: string;
  readonly targetSpot: SpotHandle;
  readonly marker: string;
}

export interface SpotToSpotNegativeRes {
  readonly sourceSpotRid: string;
  readonly targetSpotRid: string;
  readonly requestFailed: boolean;
}

export interface SpotToSpotNegativeRouteReq {
  readonly sourceSpotRid: string;
  readonly targetSpotRid: string;
  readonly marker: string;
}

export interface SpotPublishReq {
  readonly spotRid: string;
  readonly marker: string;
}

export interface SpotPublishRes {
  readonly operation: string;
  readonly publisherRid: string;
  readonly spotRid: string;
  readonly marker: string;
  readonly evidence: readonly string[];
}

export interface SpotPublishObserveRes {
  readonly operation: string;
  readonly spotRid: string;
  readonly marker: string;
  readonly received: boolean;
  readonly evidence: readonly string[];
}

export interface ChannelEchoReq {
  readonly value: string;
}

export interface ChannelEchoRes {
  readonly value: string;
}

export interface ChannelNotify {
  readonly marker: string;
}

export interface ChannelRouteReq {
  readonly targetNodeRid: string;
  readonly value: string;
}

export interface ChannelRouteRes {
  readonly value: string;
}

export interface SpotMixedRouteReq {
  readonly spotRid: string;
  readonly targetNodeRid: string;
  readonly channelValue: string;
  readonly delta: number;
}

export interface SpotMixedRouteRes {
  readonly spotRid: string;
  readonly channelReply: string;
  readonly spotValue: number;
}

export interface ControlPingReq {
  readonly value: string;
}

export interface ControlPingRes {
  readonly value: string;
  readonly nodeRid: string;
}

export interface AuthReq {
  readonly actorId: string;
  readonly displayName: string;
  readonly nodeRid: string;
}

export interface AuthRes {
  readonly actorId: string;
  readonly nodeRid: string;
  readonly generation?: string;
}

export interface EnsureActorReq {
  readonly actorId: string;
  readonly displayName: string;
  readonly nodeRid: string;
}

export interface EnsureActorRes {
  readonly actorId: string;
  readonly nodeRid: string;
  readonly generation: string;
}

export interface ActorPingReq {
  readonly value: string;
}

export interface SlowActorPingReq {
  readonly value: string;
  readonly delayMs: number;
}

export interface ActorPingRes {
  readonly actorId: string;
  readonly nodeRid: string;
  readonly spotRid: string;
  readonly value: string;
  readonly seen: number;
}

export class ActorPushReq {
  constructor(readonly value: string) {}
}

export class ActorPushNotify {
  constructor(
    readonly actorId: string,
    readonly value: string,
    readonly seen: number
  ) {}
}

export interface CrossRoleActorPushReq {
  readonly actorId: string;
  readonly nodeRid: string;
  readonly generation: string;
  readonly value: string;
}

export interface CrossRoleActorPushRes {
  readonly actorId: string;
  readonly nodeRid: string;
  readonly value: string;
  readonly delivered: boolean;
}

export interface MultiBindReq {
  readonly firstActorId: string;
  readonly secondActorId: string;
  readonly nodeRid: string;
}

export interface MultiBindRes {
  readonly boundCount: number;
}

export interface LogicalDisconnectReq {
  readonly actorId: string;
}

export interface LogicalDisconnectRes {
  readonly actorId: string;
  readonly remainingActorIds: readonly string[];
}

export interface SnapshotReq {
  readonly actorId: string;
}

export interface SnapshotRes {
  readonly actorId: string;
  readonly seen: number;
}

export interface DestroyActorReq {
  readonly actorId: string;
}

export interface DestroyActorRes {
  readonly actorId: string;
  readonly destroyed: boolean;
}

export interface UserSpotAuthReq {
  readonly spotRid: string;
  readonly actorId: string;
  readonly displayName: string;
  readonly nodeRid: string;
}

export interface JoinUserSpotActorReq {
  readonly spotRid: string;
  readonly actorId: string;
}

export interface JoinUserSpotActorRes {
  readonly spotRid: string;
  readonly actorId: string;
  readonly accepted: boolean;
  readonly generation: string;
}

export interface LeaveReq {
  readonly actorId: string;
}

export interface LeaveRes {
  readonly actorId: string;
  readonly accepted: boolean;
}

export interface ComplexActorReq {
  readonly displayName: string;
  readonly level: number;
  readonly tags: readonly string[];
  readonly attributes: Readonly<Record<string, string>>;
}

export interface ComplexActorRes {
  readonly actorId: string;
  readonly displayName: string;
  readonly level: number;
  readonly tags: readonly string[];
  readonly attributes: Readonly<Record<string, string>>;
}

export interface SpotStateRouteReq extends StateReq {
  readonly spotRid: string;
}

export interface SpotStateMsgReq {
  readonly spotRid: string;
  readonly marker: string;
}

export interface SpotStateMsgRes {
  readonly spotRid: string;
  readonly marker: string;
  readonly accepted: boolean;
  readonly evidence: readonly string[];
}

export interface SpotStageProbeReq extends StageProbeReq {
  readonly spotRid: string;
}

export interface SpotStageTimerReq {
  readonly spotRid: string;
  readonly name: string;
  readonly periodMs: number;
}

export interface SpotStageTimerRes {
  readonly spotRid: string;
  readonly name: string;
  readonly started: boolean;
  readonly evidence: readonly string[];
}

export interface SpotMissingHandlerReq {
  readonly spotRid: string;
}

export interface SpotMissingHandlerRes {
  readonly spotRid: string;
  readonly failed: boolean;
  readonly evidence: readonly string[];
}

export interface SpotMissingMsgReq {
  readonly spotRid: string;
  readonly marker: string;
}

export interface SpotMissingMsgRes {
  readonly spotRid: string;
  readonly marker: string;
  readonly sent: boolean;
  readonly evidence: readonly string[];
}

export interface SpotMissingTargetReq {
  readonly spotRid: string;
}

export interface SpotMissingTargetRes {
  readonly spotRid: string;
  readonly failed: boolean;
  readonly evidence: readonly string[];
}

export interface SpotMissingTargetMsgReq {
  readonly spotRid: string;
  readonly marker: string;
}

export interface SpotMissingTargetMsgRes {
  readonly spotRid: string;
  readonly marker: string;
  readonly sent: boolean;
  readonly evidence: readonly string[];
}

export interface SlowSpotReq {
  readonly marker: string;
  readonly delayMs: number;
}

export interface SlowSpotRes {
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly marker: string;
}

export interface SpotSlowRouteReq {
  readonly spotRid: string;
  readonly marker: string;
  readonly delayMs: number;
  readonly timeoutMs: number;
}

export interface SpotSlowRouteRes {
  readonly spotRid: string;
  readonly marker: string;
  readonly timedOut: boolean;
}

export interface SpotWorkerStartReq {
  readonly spotRid: string;
  readonly marker: string;
  readonly delayMs: number;
}

export interface WorkerStartRes {
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly marker: string;
}

export interface SpotWorkerCompleteReq {
  readonly spotRid: string;
  readonly marker: string;
}

export class SpotAdminReq {
  constructor(
    readonly operation: 'publish' | 'worker' | 'idleTimer' | 'timer' | 'overrunTimer',
    readonly marker?: string,
    readonly name?: string,
    readonly periodMs?: number,
    readonly delayMs?: number,
    readonly policy?: 'SkipLateTicks' | 'CatchUpBounded' | 'DelayNextTick'
  ) {}
}

export interface SpotAdminRes {
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly marker?: string;
}

export interface SpotWorkerCompleteRes {
  readonly spotRid: string;
  readonly marker: string;
  readonly completed: boolean;
  readonly evidence: readonly string[];
}

export interface SpotTimerStartReq {
  readonly spotRid: string;
  readonly name: string;
  readonly periodMs: number;
}

export interface SpotTimerStartRes {
  readonly spotRid: string;
  readonly name: string;
  readonly started: boolean;
  readonly evidence: readonly string[];
}

export interface SpotIdleCloseReq {
  readonly spotRid: string;
  readonly name: string;
  readonly periodMs: number;
}

export interface SpotIdleCloseRes {
  readonly spotRid: string;
  readonly name: string;
  readonly closed: boolean;
  readonly evidence: readonly string[];
}

export interface SpotOverrunStartReq {
  readonly spotRid: string;
  readonly name: string;
  readonly policy: string;
  readonly periodMs: number;
}

export interface SpotOverrunStartRes {
  readonly spotRid: string;
  readonly name: string;
  readonly policy: string;
  readonly started: boolean;
  readonly evidence: readonly string[];
}

export interface SpotTypeMismatchReq {
  readonly spotRid: string;
}

export interface SpotTypeMismatchRes {
  readonly spotRid: string;
  readonly failed: boolean;
  readonly errorKind: string;
  readonly state: string;
}

export interface EvidenceWaitReq {
  readonly containsAll: readonly string[];
  readonly timeoutMilliseconds?: number;
}

export type SpotServicePacketType<T extends object> = new () => T;

export function spotServicePacket<T extends object>(type: SpotServicePacketType<T>, value: T): T {
  return Object.assign(new type(), value);
}

export class CreateSpotReq {}
export class StateReq {}
export class StateMsg {}
export class StageProbeReq {}
export class StageTimerStartMsg {}
export class SpotMsg {}
export class SpotOutboundMsg {}
export class SpotOutboundNegativeMsg {}
export class SpotToSpotReq {}
export class SpotToSpotTimeoutReq {}
export class SpotToSpotNegativeReq {}
export class SlowSpotReq {}
export class ChannelEchoReq {}
export class ChannelNotify {}
export class CrossRoleActorPushReq {}
export class ControlPingReq {}
export class EnsureActorReq {}
export class ScaleOutActorProbeReq {}
export class MissingSpotReq {
  declare readonly operation: string;
  declare readonly delta: number;
}
export class MissingSpotMsg {
  declare readonly marker: string;
}
export class MissingChannelReq {
  declare readonly value: string;
}
export class MissingChannelNotify {
  declare readonly marker: string;
}
