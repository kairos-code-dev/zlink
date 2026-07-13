export const AutomaticTurnDispatchNames = {
  controlChannel: 'await.control',
  delayChannel: 'await.delay',
  spotChannel: 'await.spot',
  spotRouteChannel: 'await.spot.route',
  streamNode: 'await.stream',
  actorType: 'await.actor',
  actorIdMetadata: 'actor-id',
  spotRidMetadata: 'spot-rid',
  targetNodeRidMetadata: 'target-node-rid'
} as const;

export class DelayReq {
  constructor(
    readonly requestId: string,
    readonly delayMs: number,
    readonly marker: string
  ) {}
}

export interface DelayRes {
  readonly requestId: string;
  readonly marker: string;
  readonly nodeRid: string;
}

export interface AwaitShutdownScenarioReq {
  readonly requestId: string;
  readonly spotRid: string;
  readonly delayMs: number;
}

export interface AwaitShutdownRecoveryReq {
  readonly requestId: string;
  readonly spotRid: string;
}

export interface AwaitScenarioRes {
  readonly operation: string;
  readonly spotRid: string;
  readonly evidence: readonly string[];
}

import type { SpotHandle } from '@zlink-systems/framework';

export interface HoldMsg {
  readonly requestId: string;
  readonly delayMs: number;
}

export interface AwaitMsg {
  readonly requestId: string;
  readonly delayMs: number;
  readonly correlationId: string;
}

export interface AwaitReq {
  readonly requestId: string;
  readonly delayMs: number;
  readonly correlationId: string;
}

export interface RemoteSpotAwaitReq {
  readonly requestId: string;
  readonly targetSpotRid: string;
  readonly targetSpot?: SpotHandle;
  readonly delayMs: number;
}

export interface RemoteSpotAwaitMsg {
  readonly requestId: string;
  readonly targetSpotRid: string;
  readonly targetSpot?: SpotHandle;
  readonly delayMs: number;
}

export interface WorkerAwaitMsg {
  readonly requestId: string;
  readonly delayMs: number;
}

export interface AwaitTimeoutMsg {
  readonly requestId: string;
  readonly delayMs: number;
  readonly timeoutMs: number;
}

export interface AwaitCancelMsg {
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

export interface AwaitEvidenceWaitReq {
  readonly requestId: string;
  readonly marker: string;
  readonly timeoutMilliseconds?: number;
}

export interface AwaitEvidenceReq {
  readonly requestId: string;
}

export interface AwaitEvidenceRes {
  readonly requestId: string;
  readonly evidence: readonly string[];
}

export interface BindAwaitActorsReq {
  readonly spotRid: string;
  readonly actorIds: readonly string[];
}

export interface BindAwaitActorsRes {
  readonly spotRid: string;
  readonly actors: readonly AwaitActorBinding[];
}

export interface AwaitActorBinding {
  readonly actorId: string;
  readonly nodeRid: string;
  readonly generation: string;
}

export interface ActorAwaitReq {
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

export interface ActorJoinAwaitReq {
  readonly requestId: string;
  readonly targetSpotRid: string;
}

export interface ActorPushAwaitReq {
  readonly requestId: string;
  readonly delayMs: number;
  readonly value: string;
}

export class ActorPushNotify {
  constructor(
    readonly actorId: string,
    readonly requestId: string,
    readonly value: string,
    readonly nodeRid: string
  ) {}
}

export interface ActorAwaitRes {
  readonly scenarioId: string;
  readonly requestId: string;
  readonly actorId: string;
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly marker: string;
}

export interface AutomaticTurnDispatchRes {
  readonly scenarioId: string;
  readonly requestId: string;
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly marker: string;
}

// Stream decoding produces plain objects. These nominal constructors restore the
// packet descriptor before a decoded packet is forwarded through framework calls.
export class AwaitShutdownScenarioReq {}
export class AwaitShutdownRecoveryReq {}
export class HoldMsg {}
export class AwaitMsg {}
export class AwaitReq {}
export class RemoteSpotAwaitReq {}
export class RemoteSpotAwaitMsg {}
export class WorkerAwaitMsg {}
export class AwaitTimeoutMsg {}
export class AwaitCancelMsg {}
export class TimerStartMsg {}
export class TimerStopMsg {}
export class ProbeMsg {}
export class EnsureSpotReq {}
export class AwaitEvidenceWaitReq {}
export class AwaitEvidenceReq {}
export class BindAwaitActorsReq {}
export class ActorAwaitReq {}
export class ActorFastReq {}
export class ActorFastMsg {}
export class ActorJoinAwaitReq {}
export class ActorPushAwaitReq {}
