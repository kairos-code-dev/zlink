export const SpotActorTransferNames = {
  mesh: 'spot-actor-transfer',
  actorTypeStateful: 'transfer-stateful',
  actorTypeEmptyState: 'transfer-empty-state',
  actorTypeNoAdapter: 'transfer-no-adapter',
  actorTypeFailTransferOut: 'transfer-fail-out',
  actorTypeFailLeave: 'transfer-fail-leave',
  actorTypeFailTransferIn: 'transfer-fail-in',
  packetJoin: 'JoinTargetReq',
  packetProbe: 'ProbeReq',
  packetHandoff: 'HandoffProbe',
  packetBoundPush: 'BoundPushReq',
  packetBoundNotify: 'BoundPushNotify',
  packetBindActor: 'BindActorSessionReq'
} as const;

export interface ActorCreateReq { actorId: string; actorType: string; stateVersion: number }
export interface ActorCreateRes { actorId: string; actorType: string; nodeRid: string; generation: string }
export interface CreateSpotReq { spotRid: string; mode?: string }
export interface CreateSpotRes { spotRid: string; nodeRid: string; state: string }
export interface GateReleaseRes { key: string; released: boolean }
export interface JoinTargetReq { scenario: string; targetSpotRid: string; expectedMode?: string; transferId?: string }
export interface JoinTargetRes {
  scenario: string;
  actorId: string;
  accepted: boolean;
  sourceNodeRid: string;
  targetSpotRid: string;
  stateVersion: number;
  errorKind?: string;
}
export interface ProbeReq {
  scenario: string;
  marker: string;
  delayMs?: number;
  requestTimeoutMs?: number;
}
export interface ProbeRes {
  scenario: string;
  actorId: string;
  spotRid: string;
  nodeRid: string;
  stateVersion: number;
  marker: string;
}
export interface BindActorSessionReq {
  scenario: string;
  actorId: string;
  nodeRid?: string;
  generation?: string;
  transferId?: string;
}
export interface BindActorSessionRes { scenario: string; actorId: string; nodeRid: string; generation: string }
export interface BoundPushReq { scenario: string; marker: string }
export interface BoundPushRes extends ProbeRes {}
export interface BoundPushNotify extends ProbeRes {}
export interface EvidenceWaitReq { containsAll: readonly string[]; timeoutMilliseconds?: number }
export interface ActorRefSnapshotRes { actorId: string; nodeRid: string; generation: string }
export interface TransferStateDto { actorId: string; actorType: string; stateVersion: number }
export interface ActorEvidence {
  scenario: string;
  actorId: string;
  kind: string;
  value: string;
  nodeRid: string;
  atNs: string;
  sequence: number;
  transferId?: string;
}
