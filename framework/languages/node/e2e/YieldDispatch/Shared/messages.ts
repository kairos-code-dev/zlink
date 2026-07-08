export const YieldDispatchNames = {
  controlChannel: 'yield.control',
  delayChannel: 'yield.delay',
  spotChannel: 'yield.spot',
  spotRouteChannel: 'yield.spot.route',
  streamNode: 'yield.stream',
  actorType: 'yield.actor',
  actorIdMetadata: 'actor-id',
  spotRidMetadata: 'spot-rid',
  targetNodeRidMetadata: 'target-node-rid'
} as const;

export interface DelayReq {
  readonly requestId: string;
  readonly delayMs: number;
  readonly marker: string;
}

export interface DelayRes {
  readonly requestId: string;
  readonly marker: string;
  readonly nodeRid: string;
}

export interface YieldShutdownScenarioReq {
  readonly requestId: string;
  readonly spotRid: string;
  readonly delayMs: number;
}

export interface YieldShutdownRecoveryReq {
  readonly requestId: string;
  readonly spotRid: string;
}

export interface YieldScenarioRes {
  readonly operation: string;
  readonly spotRid: string;
  readonly evidence: readonly string[];
}

import type { SpotRef } from '@zlink-systems/framework';

export interface HoldMsg {
  readonly requestId: string;
  readonly delayMs: number;
}

export interface YieldMsg {
  readonly requestId: string;
  readonly delayMs: number;
  readonly correlationId: string;
}

export interface YieldReq {
  readonly requestId: string;
  readonly delayMs: number;
  readonly correlationId: string;
}

export interface RemoteSpotYieldReq {
  readonly requestId: string;
  readonly targetSpotRid: string;
  readonly targetSpot?: SpotRef;
  readonly delayMs: number;
}

export interface RemoteSpotYieldMsg {
  readonly requestId: string;
  readonly targetSpotRid: string;
  readonly targetSpot?: SpotRef;
  readonly delayMs: number;
}

export interface WorkerYieldMsg {
  readonly requestId: string;
  readonly delayMs: number;
}

export interface YieldTimeoutMsg {
  readonly requestId: string;
  readonly delayMs: number;
  readonly timeoutMs: number;
}

export interface YieldCancelMsg {
  readonly requestId: string;
  readonly delayMs: number;
  readonly cancelAfterMs: number;
}

export interface TimerStartMsg {
  readonly requestId: string;
  readonly timerName: string;
  readonly mode: string;
  readonly periodMs: number;
  readonly delayMs: number;
}

export interface TimerStopMsg {
  readonly requestId: string;
}

export interface ProbeMsg {
  readonly requestId: string;
  readonly marker: string;
}

export interface EnsureSpotReq {
  readonly spotRid: string;
}

export interface EnsureSpotRes {
  readonly spotRid: string;
  readonly nodeRid: string;
}

export interface YieldEvidenceWaitReq {
  readonly requestId: string;
  readonly marker: string;
  readonly timeoutMilliseconds?: number;
}

export interface YieldEvidenceReq {
  readonly requestId: string;
}

export interface YieldEvidenceRes {
  readonly requestId: string;
  readonly evidence: readonly string[];
}

export interface BindYieldActorsReq {
  readonly spotRid: string;
  readonly actorIds: readonly string[];
}

export interface BindYieldActorsRes {
  readonly spotRid: string;
  readonly actors: readonly YieldActorBinding[];
}

export interface YieldActorBinding {
  readonly actorId: string;
  readonly nodeRid: string;
  readonly generation: string;
}

export interface ActorYieldReq {
  readonly requestId: string;
  readonly delayMs: number;
}

export interface ActorFastReq {
  readonly requestId: string;
  readonly marker: string;
}

export interface ActorFastMsg {
  readonly requestId: string;
  readonly marker: string;
}

export interface ActorJoinYieldReq {
  readonly requestId: string;
  readonly targetSpotRid: string;
}

export interface ActorPushYieldReq {
  readonly requestId: string;
  readonly delayMs: number;
  readonly value: string;
}

export interface ActorPushNotify {
  readonly actorId: string;
  readonly requestId: string;
  readonly value: string;
  readonly nodeRid: string;
}

export interface ActorYieldRes {
  readonly scenarioId: string;
  readonly requestId: string;
  readonly actorId: string;
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly marker: string;
}

export interface YieldDispatchRes {
  readonly scenarioId: string;
  readonly requestId: string;
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly marker: string;
}
