// SPDX-License-Identifier: MPL-2.0

// Shared handler + utility types the raw socket layer depends on, plus the
// re-exported fluent messaging operation contracts (via shared.ts).
export type * from './shared';

// Pull-dispatch value types and batch/claim/record contracts.
export {
  ReadyDomain,
  ReadyOwnerKind,
  ReceiveKind,
  OperationKind,
  ActorLifecycleKind,
  ActorJoinResult,
  MeshDestinationKind
} from './dispatch';
export type {
  ReadyDomainValue,
  ReadyOwnerKindValue,
  ReceiveKindValue,
  OperationKindValue,
  ActorLifecycleKindValue,
  ActorJoinResultValue,
  MeshDestinationKindValue,
  MeshOperationId,
  ActorTransferId,
  ActorRef,
  ActorLocation,
  ReadyRecord,
  ReceiveRequirements,
  ReceiveRecord,
  ReceiveKindData,
  ActorControlPayload,
  ActorJoinCompletionPayload,
  ActorLookupCompletionPayload,
  SendReadyPayload,
  ActorTransferControlPayload,
  DrainReadyResult,
  ClaimReceiveResult,
  ReadyBatch,
  Claim,
  ReceiveBatch
} from './dispatch';

// Actor transfer fence.
export { ActorTransferRole, ActorTransferPhase } from './transfer';
export type {
  ActorTransferRoleValue,
  ActorTransferPhaseValue,
  ActorTransferToken,
  ActorTransferPrepare,
  ActorTransferPrepareResult,
  PrepareActorTransferResult
} from './transfer';

// Mesh node.
export { MeshNodeState, MeshMonitorEventKind, MeshMonitorEventMask } from './mesh_node';
export type {
  MeshNodeStateValue,
  MeshNode,
  MeshNodeStatus,
  MeshPeerEntry,
  PeerChannels,
  ConnectPeerOptions,
  GetOrCreateSpotResult,
  MeshMonitorEvent,
  MeshMonitorStatus,
  MeshNodeMonitor
} from './mesh_node';

// Spot.
export { SpotKind, SubscriptionKind } from './spot';
export type { SpotKindValue, SubscriptionKindValue, Spot, SpotStatus } from './spot';

// Publisher.
export type { Publisher, MeshPublishDetail, MeshPublishResult } from './publisher';

// Stream session service.
export type {
  StreamSessionService,
  StreamSessionStatus,
  StreamSessionBinding
} from './stream_session';
