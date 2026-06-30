import type {
  RebuildOrderProjectionReq,
  RebuildOrderProjectionRes,
  SeedPendingIdempotencyReq,
  StartOrderRes,
  StartOrderWorkflowReq,
  StartOrderWorkflowRes
} from '../../../../Shared/Contracts/messages';

interface OrderWorkflowCommandPort {
  startOrder(request: StartOrderWorkflowReq): Promise<StartOrderWorkflowRes>;

  rebuildProjection(request: RebuildOrderProjectionReq): Promise<RebuildOrderProjectionRes>;

  seedPendingIdempotency(request: SeedPendingIdempotencyReq): Promise<StartOrderRes>;
}

export type {
  OrderWorkflowCommandPort
};
