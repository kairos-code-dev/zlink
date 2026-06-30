import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { OrderWorkflowService } from '../Application/OrderWorkflow/order-workflow-service';
import { ensureOrderWorkflowSpot } from './start-order-handler';
import type { ZLinkRouteRequestContext, ZLinkRouteRequestHandler, ZLinkSpotManager } from '@zlink-systems/framework';
import type {
  GetOrderStateReq,
  GetOrderStateRes,
  RebuildOrderProjectionReq,
  RebuildOrderProjectionRes,
  SeedPendingIdempotencyReq,
  ServerAssertionReq,
  ServerAssertionRes,
  StartOrderRes
} from '../../../Shared/Contracts/messages';

class GetOrderStateHandler implements ZLinkRouteRequestHandler<GetOrderStateReq, GetOrderStateRes> {
  constructor(@Inject(OrderWorkflowService) private readonly workflow: OrderWorkflowService) {}

  async handle(request: GetOrderStateReq, context: ZLinkRouteRequestContext): Promise<GetOrderStateRes> {
    void context;
    return this.workflow.get(request);
  }
}

class DeleteOrderProjectionHandler implements ZLinkRouteRequestHandler<GetOrderStateReq, GetOrderStateRes> {
  constructor(@Inject(OrderWorkflowService) private readonly workflow: OrderWorkflowService) {}

  async handle(request: GetOrderStateReq, context: ZLinkRouteRequestContext): Promise<GetOrderStateRes> {
    void context;
    return this.workflow.deleteProjection(request);
  }
}

class RebuildOrderProjectionHandler implements ZLinkRouteRequestHandler<RebuildOrderProjectionReq, RebuildOrderProjectionRes> {
  constructor(
    @Inject(OrderWorkflowService) private readonly workflow: OrderWorkflowService,
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager
  ) {}

  async handle(request: RebuildOrderProjectionReq, context: ZLinkRouteRequestContext): Promise<RebuildOrderProjectionRes> {
    void context;
    await ensureOrderWorkflowSpot(this.spots, request.orderId);
    return this.workflow.rebuildProjection(request);
  }
}

class SeedPendingIdempotencyHandler implements ZLinkRouteRequestHandler<SeedPendingIdempotencyReq, StartOrderRes> {
  constructor(@Inject(OrderWorkflowService) private readonly workflow: OrderWorkflowService) {}

  async handle(request: SeedPendingIdempotencyReq, context: ZLinkRouteRequestContext): Promise<StartOrderRes> {
    void context;
    return this.workflow.seedPending(request);
  }
}

class ServerAssertionHandler implements ZLinkRouteRequestHandler<ServerAssertionReq, ServerAssertionRes> {
  constructor(@Inject(OrderWorkflowService) private readonly workflow: OrderWorkflowService) {}

  async handle(request: ServerAssertionReq, context: ZLinkRouteRequestContext): Promise<ServerAssertionRes> {
    void context;
    return this.workflow.assertEvidence(request);
  }
}

export {
  DeleteOrderProjectionHandler,
  GetOrderStateHandler,
  RebuildOrderProjectionHandler,
  SeedPendingIdempotencyHandler,
  ServerAssertionHandler
};
