import { Inject } from '@nestjs/common';
import { OrderStore } from '../../../Shared/Store/order-store';
import type {
  ContinueOrderWorkflowReq,
  ContinueOrderWorkflowRes,
  GetOrderStateReq,
  GetOrderStateRes,
  RebuildOrderProjectionReq,
  RebuildOrderProjectionRes,
  SeedPendingIdempotencyReq,
  ServerAssertionReq,
  ServerAssertionRes,
  StartOrderRes,
  StartOrderWorkflowReq,
  StartOrderWorkflowRes
} from '../../../../Shared/Contracts/messages';
import type { OrderWorkflowStorePort } from '../../../Shared/Ports/Outbound/stores';

class OrderWorkflowService {
  constructor(@Inject(OrderStore) private readonly orders: OrderWorkflowStorePort) {}

  start(request: StartOrderWorkflowReq): StartOrderWorkflowRes {
    return this.orders.startWorkflow(request);
  }

  continue(request: ContinueOrderWorkflowReq): ContinueOrderWorkflowRes {
    return this.orders.continueWorkflow(request);
  }

  get(request: GetOrderStateReq): GetOrderStateRes {
    return this.orders.getOrder(request.orderId);
  }

  deleteProjection(request: GetOrderStateReq): GetOrderStateRes {
    return this.orders.deleteProjection(request.orderId);
  }

  rebuildProjection(request: RebuildOrderProjectionReq): RebuildOrderProjectionRes {
    return this.orders.rebuildProjection(request.orderId);
  }

  seedPending(request: SeedPendingIdempotencyReq): StartOrderRes {
    return this.orders.seedPending(request);
  }

  assertEvidence(request: ServerAssertionReq): ServerAssertionRes {
    return this.orders.assertEvidence(
      request.successfulOrderId,
      request.pendingRecoveredOrderId,
      request.inventoryFailureOrderId,
      request.paymentFailureOrderId,
      request.scaleOutOrderId
    );
  }
}

export {
  OrderWorkflowService
};
