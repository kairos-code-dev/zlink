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
  multiRouteChannelA: 'multi-route-a',
  multiRouteChannelB: 'multi-route-b',
  actorType: 'scenario-player',
  actorIdMetadata: 'actor-id'
} as const;

export interface CreateSpotReq {
  readonly spotRid: string;
}

export interface CreateSpotReply {
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly state: string;
}

export interface CloseSpotReq {
  readonly spotRid: string;
}

export interface CloseSpotReply {
  readonly spotRid: string;
  readonly closed: boolean;
}

export interface StateReq {
  readonly operation: string;
  readonly delta: number;
}

export interface StateReply {
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly value: number;
}

export interface MultiNodeCreateSpotReq {
  readonly spotRid: string;
  readonly delta: number;
}

export interface MultiNodeCreateSpotReply {
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly state: string;
  readonly value: number;
}

export interface MultiNodeStateRouteReq {
  readonly spotRid: string;
  readonly delta: number;
}

export interface StateCommand {
  readonly marker: string;
}

export interface StageProbeReq {
  readonly marker: string;
  readonly delta: number;
}

export interface StageTimerStartCommand {
  readonly name: string;
  readonly periodMs: number;
}

export interface SpotEvent {
  readonly marker: string;
}

export interface SpotOutboundReq {
  readonly marker: string;
}

export interface SpotOutboundNegativeReq {
  readonly marker: string;
}

export interface SpotOutboundRouteReq {
  readonly spotRid: string;
  readonly marker: string;
}

export interface SpotOutboundRouteReply {
  readonly spotRid: string;
  readonly marker: string;
  readonly accepted: boolean;
  readonly evidence: readonly string[];
}

export interface SpotToSpotReq {
  readonly targetSpotRid: string;
  readonly marker: string;
}

export interface SpotToSpotReply {
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
  readonly marker: string;
}

export interface SpotToSpotTimeoutReply {
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
  readonly marker: string;
}

export interface SpotToSpotNegativeReply {
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

export interface SpotPublishReply {
  readonly operation: string;
  readonly publisherRid: string;
  readonly spotRid: string;
  readonly marker: string;
  readonly evidence: readonly string[];
}

export interface SpotPublishObserveReply {
  readonly operation: string;
  readonly spotRid: string;
  readonly marker: string;
  readonly received: boolean;
  readonly evidence: readonly string[];
}

export interface ChannelEchoReq {
  readonly value: string;
}

export interface ChannelEchoReply {
  readonly value: string;
}

export interface ChannelNotify {
  readonly marker: string;
}

export interface ChannelRouteReq {
  readonly targetNodeRid: string;
  readonly value: string;
}

export interface ChannelRouteReply {
  readonly value: string;
}

export interface SpotMixedRouteReq {
  readonly spotRid: string;
  readonly targetNodeRid: string;
  readonly channelValue: string;
  readonly delta: number;
}

export interface SpotMixedRouteReply {
  readonly spotRid: string;
  readonly channelReply: string;
  readonly spotValue: number;
}

export interface ControlPingReq {
  readonly value: string;
}

export interface ControlPingReply {
  readonly value: string;
  readonly nodeRid: string;
}

export interface AuthReq {
  readonly actorId: string;
  readonly displayName: string;
  readonly nodeRid: string;
}

export interface AuthReply {
  readonly actorId: string;
  readonly nodeRid: string;
}

export interface EnsureActorReq {
  readonly actorId: string;
  readonly displayName: string;
  readonly nodeRid: string;
}

export interface EnsureActorReply {
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

export interface ActorPingReply {
  readonly actorId: string;
  readonly nodeRid: string;
  readonly spotRid: string;
  readonly value: string;
  readonly seen: number;
}

export interface ActorPushReq {
  readonly value: string;
}

export interface ActorPushNotify {
  readonly actorId: string;
  readonly value: string;
  readonly seen: number;
}

export interface MultiBindReq {
  readonly firstActorId: string;
  readonly secondActorId: string;
  readonly nodeRid: string;
}

export interface MultiBindReply {
  readonly boundCount: number;
}

export interface SnapshotReq {
  readonly actorId: string;
}

export interface SnapshotReply {
  readonly actorId: string;
  readonly seen: number;
}

export interface DestroyActorReq {
  readonly actorId: string;
}

export interface DestroyActorReply {
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

export interface JoinUserSpotActorReply {
  readonly spotRid: string;
  readonly actorId: string;
  readonly accepted: boolean;
  readonly generation: string;
}

export interface LeaveReq {
  readonly actorId: string;
}

export interface LeaveReply {
  readonly actorId: string;
  readonly accepted: boolean;
}

export interface ComplexActorReq {
  readonly displayName: string;
  readonly level: number;
  readonly tags: readonly string[];
  readonly attributes: Readonly<Record<string, string>>;
}

export interface ComplexActorReply {
  readonly actorId: string;
  readonly displayName: string;
  readonly level: number;
  readonly tags: readonly string[];
  readonly attributes: Readonly<Record<string, string>>;
}

export interface SpotStateRouteReq extends StateReq {
  readonly spotRid: string;
}

export interface SpotStateCommandReq {
  readonly spotRid: string;
  readonly marker: string;
}

export interface SpotStateCommandReply {
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

export interface SpotStageTimerReply {
  readonly spotRid: string;
  readonly name: string;
  readonly started: boolean;
  readonly evidence: readonly string[];
}

export interface SpotMissingHandlerReq {
  readonly spotRid: string;
}

export interface SpotMissingHandlerReply {
  readonly spotRid: string;
  readonly failed: boolean;
  readonly evidence: readonly string[];
}

export interface SpotMissingCommandReq {
  readonly spotRid: string;
  readonly marker: string;
}

export interface SpotMissingCommandReply {
  readonly spotRid: string;
  readonly marker: string;
  readonly sent: boolean;
  readonly evidence: readonly string[];
}

export interface SpotMissingTargetReq {
  readonly spotRid: string;
}

export interface SpotMissingTargetReply {
  readonly spotRid: string;
  readonly failed: boolean;
  readonly evidence: readonly string[];
}

export interface SpotMissingTargetCommandReq {
  readonly spotRid: string;
  readonly marker: string;
}

export interface SpotMissingTargetCommandReply {
  readonly spotRid: string;
  readonly marker: string;
  readonly sent: boolean;
  readonly evidence: readonly string[];
}

export interface SlowSpotReq {
  readonly marker: string;
  readonly delayMs: number;
}

export interface SlowSpotReply {
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

export interface SpotSlowRouteReply {
  readonly spotRid: string;
  readonly marker: string;
  readonly timedOut: boolean;
}

export interface SpotWorkerStartReq {
  readonly spotRid: string;
  readonly marker: string;
  readonly delayMs: number;
}

export interface WorkerStartReply {
  readonly spotRid: string;
  readonly nodeRid: string;
  readonly marker: string;
}

export interface SpotWorkerCompleteReq {
  readonly spotRid: string;
  readonly marker: string;
}

export interface SpotWorkerCompleteReply {
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

export interface SpotTimerStartReply {
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

export interface SpotIdleCloseReply {
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

export interface SpotOverrunStartReply {
  readonly spotRid: string;
  readonly name: string;
  readonly policy: string;
  readonly started: boolean;
  readonly evidence: readonly string[];
}

export interface SpotTypeMismatchReq {
  readonly spotRid: string;
}

export interface SpotTypeMismatchReply {
  readonly spotRid: string;
  readonly failed: boolean;
  readonly errorKind: string;
  readonly state: string;
}

export interface EvidenceWaitRequest {
  readonly containsAll: readonly string[];
  readonly timeoutMilliseconds?: number;
}
