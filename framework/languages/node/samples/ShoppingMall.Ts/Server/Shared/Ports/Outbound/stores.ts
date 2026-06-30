import type {
  ContinueOrderWorkflowReq,
  ContinueOrderWorkflowRes,
  GetOrderStateRes,
  RebuildOrderProjectionRes,
  SeedPendingIdempotencyReq,
  ServerAssertionRes,
  StartOrderRes,
  StartOrderWorkflowReq,
  StartOrderWorkflowRes
} from '../../../../Shared/Contracts/messages';
import type { IdempotencyReservation } from '../../Store/order-store';

interface CommerceOrderStorePort {
  reserveIdempotency(idempotencyKey: string): IdempotencyReservation;
  getOrder(orderId: string): GetOrderStateRes;
  deleteProjection(orderId: string): GetOrderStateRes;
  assertEvidence(
    successfulOrderId: string,
    pendingRecoveredOrderId: string,
    inventoryFailureOrderId: string,
    paymentFailureOrderId: string,
    scaleOutOrderId: string
  ): ServerAssertionRes;
}

interface OrderWorkflowStorePort {
  startWorkflow(request: StartOrderWorkflowReq): StartOrderWorkflowRes;
  continueWorkflow(request: ContinueOrderWorkflowReq): ContinueOrderWorkflowRes;
  rebuildProjection(orderId: string): RebuildOrderProjectionRes;
  seedPending(request: SeedPendingIdempotencyReq): StartOrderRes;
  getOrder(orderId: string): GetOrderStateRes;
  deleteProjection(orderId: string): GetOrderStateRes;
  assertEvidence(
    successfulOrderId: string,
    pendingRecoveredOrderId: string,
    inventoryFailureOrderId: string,
    paymentFailureOrderId: string,
    scaleOutOrderId: string
  ): ServerAssertionRes;
}

export type {
  CommerceOrderStorePort,
  OrderWorkflowStorePort
};
