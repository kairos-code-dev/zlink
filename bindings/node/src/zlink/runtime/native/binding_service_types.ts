// SPDX-License-Identifier: MPL-2.0

// Raw marshaling shapes exchanged with the native addon for the RouteMesh
// 10.0.0 service layer (MeshNode + pull dispatch + spot/actor/stream_session).
// These mirror the C structs in core/include/zlink/service/*.h field-for-field.

export type NativeServiceHandle = unknown;

/** A 128-bit mesh operation id (zlink_mesh_operation_id_t). */
export interface MeshOperationIdRaw {
  high: bigint;
  low: bigint;
}

/** An actor reference (zlink_actor_ref_t). */
export interface ActorRefRaw {
  nodeRid: Buffer;
  actorId: string;
  generation: bigint;
}

/** Mesh node status snapshot (zlink_mesh_node_status_t). */
export interface MeshNodeStatusRaw {
  state: number;
  routingId: Buffer;
  meshName: string;
  localEndpoint: string;
  lifecycleGeneration: bigint;
  descriptorRevision: bigint;
  channelCount: number;
  configuredPeerCount: number;
  admittedPeerCount: number;
  drainingPeerCount: number;
  pendingApplicationMessages: bigint;
  pendingInfrastructureMessages: bigint;
  pendingBytes: bigint;
  multicastSubmitted: bigint;
  multicastDroppedTargets: bigint;
  lastError: number;
  lastChangedMs: bigint;
}

/** A mesh peer entry (zlink_mesh_peer_entry_t). */
export interface MeshPeerEntryRaw {
  connectionIntentId: bigint;
  source: number;
  state: number;
  routingId: Buffer;
  lifecycleGeneration: bigint;
  descriptorRevision: bigint;
  endpoint: string;
  channelCount: number;
  lastError: number;
  lastChangedMs: bigint;
}

export interface MeshMonitorEventRaw {
  kind: number;
  timestampMs: bigint;
  meshLifecycleGeneration: bigint;
  meshDescriptorRevision: bigint;
  meshState: number;
  peerRid: Buffer;
  peerLifecycleGeneration: bigint;
  peerDescriptorRevision: bigint;
  ownerKind: number;
  spotRid: Buffer;
  actor: ActorRefRaw;
  channelName: string;
  operationId: MeshOperationIdRaw;
  snapshotRemoteTargetCount: number;
  admittedRemoteTargetCount: number;
  droppedRemoteTargetCount: number;
  unreachableRemoteTargetCount: number;
  snapshotLocalSpotCount: number;
  admittedLocalSpotCount: number;
  droppedLocalSpotCount: number;
  resultCode: number;
  failureErrno: number;
}

export interface MeshMonitorStatusRaw {
  state: number;
  peerAdmitted: bigint;
  peerRejected: bigint;
  submittedMessages: bigint;
  completedOperations: bigint;
  backpressuredSubmits: bigint;
  multicastMessages: bigint;
  multicastDroppedTargets: bigint;
  activeClaims: bigint;
  pendingApplicationMessages: bigint;
  pendingInfrastructureMessages: bigint;
  pendingBytes: bigint;
}

/** Peer channel weights (zlink_mesh_node_peer_channels). */
export interface MeshPeerChannelsRaw {
  names: string[];
  weights: number[];
}

/** Publish accounting detail (zlink_mesh_publish_detail_t). */
export interface MeshPublishDetailRaw {
  snapshotRemoteTargetCount: number;
  admittedRemoteTargetCount: number;
  droppedRemoteTargetCount: number;
  unreachableRemoteTargetCount: number;
  snapshotLocalSpotCount: number;
  admittedLocalSpotCount: number;
  droppedLocalSpotCount: number;
}

/** Spot status snapshot (zlink_spot_status_t). */
export interface SpotStatusRaw {
  spotRid: Buffer;
  spotKind: number;
  lifecycleGeneration: bigint;
  pendingApplicationMessages: bigint;
  pendingInfrastructureMessages: bigint;
  pendingBytes: bigint;
  activeActorCount: number;
  draining: number;
  lastError: number;
  lastChangedMs: bigint;
}

/** Actor location (zlink_actor_location_t). */
export interface ActorLocationRaw {
  actor: ActorRefRaw;
  spotRid: Buffer;
  spotGeneration: bigint;
  membershipEpoch: bigint;
}

/** Result of meshNodeSpotGetOrNew. */
export interface SpotGetOrNewRaw {
  spot: NativeServiceHandle;
  created: boolean;
}

/** A ready-index record (zlink_mesh_ready_record_t). */
export interface MeshReadyRecordRaw {
  ownerKind: number;
  domain: number;
  spotRid: Buffer;
  actor: ActorRefRaw;
}

/** Receive-batch capacity requirements (zlink_mesh_receive_requirements_t). */
export interface MeshReceiveRequirementsRaw {
  messageCount: number;
  partCount: number;
  byteCount: number;
}

/** A 128-bit actor-transfer id (zlink_actor_transfer_id_t). */
export interface ActorTransferIdRaw {
  high: bigint;
  low: bigint;
}

/** Raw typed kind_data emitted by the addon for a receive record. */
export type MeshKindDataRaw =
  | ActorControlKindDataRaw
  | ActorJoinCompletionKindDataRaw
  | ActorLookupCompletionKindDataRaw
  | SendReadyKindDataRaw
  | TransferControlKindDataRaw;

export interface ActorControlKindDataRaw {
  kind: 'actorControl';
  lifecycleKind: number;
  previousActor: ActorRefRaw;
  currentActor: ActorRefRaw;
  previousSpotRid: Buffer;
  currentSpotRid: Buffer;
  previousSpotGeneration: bigint;
  currentSpotGeneration: bigint;
  previousMembershipEpoch: bigint;
  currentMembershipEpoch: bigint;
  resultCode: number;
}

export interface ActorJoinCompletionKindDataRaw {
  kind: 'actorJoinCompletion';
  joinResult: number;
  actor: ActorRefRaw;
  location: ActorLocationRaw;
}

export interface ActorLookupCompletionKindDataRaw {
  kind: 'actorLookupCompletion';
  location: ActorLocationRaw;
}

export interface SendReadyKindDataRaw {
  kind: 'sendReady';
  destinationKind: number;
  targetNodeRid: Buffer;
  targetSpotRid: Buffer;
  targetActor: ActorRefRaw;
  channelName: string | null;
}

export interface TransferControlKindDataRaw {
  kind: 'transferControl';
  phase: number;
  role: number;
  transferId: ActorTransferIdRaw;
  actor: ActorRefRaw;
  membershipEpoch: bigint;
  finalSequence: bigint;
  resultCode: number;
  failureErrno: number;
}

/** A materialized receive record (zlink_mesh_receive_record_t + its parts). */
export interface MeshReceiveRecordRaw {
  kind: number;
  domain: number;
  sourceNodeRid: Buffer;
  sourceSpotRid: Buffer;
  sourceActor: ActorRefRaw;
  operationId: MeshOperationIdRaw;
  operationKind: number;
  /** Opaque reply token (32 bytes), or an empty Buffer when the record is not replyable. */
  replyToken: Buffer;
  channelName: string | null;
  topic: string | null;
  applicationMetadata: Buffer | null;
  /** Typed kind_data for this record kind, or null when the kind carries none. */
  kindData: MeshKindDataRaw | null;
  terminalResult: number;
  failureErrno: number;
  parts: Buffer[];
}

/** Result of meshNodeDrainReady. */
export interface MeshDrainReadyRaw {
  /** false when the drain would block and no records were produced. */
  ok: boolean;
  hasResidue: boolean;
  records: MeshReadyRecordRaw[];
}

/** Result of meshClaimRecvBatch. */
export interface MeshClaimRecvRaw {
  /** false when the receive would block. */
  ok: boolean;
  /** Set when the batch capacity was insufficient; retry with a larger batch. */
  required: MeshReceiveRequirementsRaw | null;
  records: MeshReceiveRecordRaw[];
}

/** Stream-session service status (zlink_stream_session_status_t). */
export interface StreamSessionStatusRaw {
  state: number;
  lifecycleGeneration: bigint;
  sessionCount: bigint;
  bindingCount: bigint;
  pendingMessageCount: bigint;
  pendingByteCount: bigint;
  lastError: number;
}

/** A stream-session binding (zlink_stream_session_binding_t). */
export interface StreamSessionBindingRaw {
  sessionRid: Buffer;
  actor: ActorRefRaw;
  bindingGeneration: bigint;
  membershipEpoch: bigint;
}

/** Ready-domain handler installed via meshNodeSetReadyHandler. Returns the domain mask to drain. */
export type NativeReadyHandler = (readyDomains: number) => number;

/** Inputs to meshNodeActorTransferPrepare (zlink_actor_transfer_prepare_t). */
export interface ActorTransferPrepareRaw {
  role: number;
  transferId: ActorTransferIdRaw;
  actor: ActorRefRaw;
  expectedMembershipEpoch: bigint;
  peerNodeRid: Buffer;
  finalSequence: bigint;
  reserveMessageCount: bigint;
  reserveByteCount: bigint;
}

/** Result detail from meshNodeActorTransferPrepare (zlink_actor_transfer_prepare_result_t). */
export interface ActorTransferPrepareResultRaw {
  role: number;
  transferId: ActorTransferIdRaw;
  actor: ActorRefRaw;
  finalSequence: bigint;
  reserveMessageCount: bigint;
  reserveByteCount: bigint;
}

/** Result of meshNodeActorTransferPrepare: the opaque token bytes and result detail. */
export interface ActorTransferPrepareOutcomeRaw {
  token: Buffer;
  result: ActorTransferPrepareResultRaw;
}

/** Options passed to meshNodeNew. */
export interface MeshNodeNewOptions {
  meshName?: string;
  trustProfile?: string;
}

/** Options passed to meshNodeConnectPeer. */
export interface MeshConnectPeerOptions {
  endpoint: string;
  expectedRid?: Buffer;
}
