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

export interface DelayReply {
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

export interface YieldScenarioResult {
  readonly operation: string;
  readonly spotRid: string;
  readonly evidence: readonly string[];
}

export interface HoldCommand {
  readonly requestId: string;
  readonly delayMs: number;
}

export interface YieldCommand {
  readonly requestId: string;
  readonly delayMs: number;
  readonly correlationId: string;
}

export interface RemoteSpotYieldReq {
  readonly requestId: string;
  readonly targetSpotRid: string;
  readonly delayMs: number;
}

export interface RemoteSpotYieldCommand {
  readonly requestId: string;
  readonly targetSpotRid: string;
  readonly delayMs: number;
}

export interface WorkerYieldCommand {
  readonly requestId: string;
  readonly delayMs: number;
}

export interface YieldTimeoutCommand {
  readonly requestId: string;
  readonly delayMs: number;
  readonly timeoutMs: number;
}

export interface YieldCancelCommand {
  readonly requestId: string;
  readonly delayMs: number;
  readonly cancelAfterMs: number;
}

export interface TimerStartCommand {
  readonly requestId: string;
  readonly timerName: string;
  readonly mode: string;
  readonly periodMs: number;
  readonly delayMs: number;
}

export interface TimerStopCommand {
  readonly requestId: string;
}

export interface ProbeCommand {
  readonly requestId: string;
  readonly marker: string;
}

export interface EnsureSpotReq {
  readonly spotRid: string;
}

export interface EnsureSpotReply {
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

export interface YieldEvidenceReply {
  readonly requestId: string;
  readonly evidence: readonly string[];
}

export interface BindYieldActorsReq {
  readonly spotRid: string;
  readonly actorIds: readonly string[];
}

export interface BindYieldActorsReply {
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

export interface ActorYieldReply {
  readonly scenarioId: string;
  readonly requestId: string;
  readonly actorId: string;
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly marker: string;
}

export interface YieldDispatchReply {
  readonly scenarioId: string;
  readonly requestId: string;
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly marker: string;
}
