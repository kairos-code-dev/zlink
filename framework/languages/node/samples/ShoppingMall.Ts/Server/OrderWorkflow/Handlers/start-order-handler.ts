import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { OrderWorkflowService } from '../Application/OrderWorkflow/order-workflow-service';
import { OrderWorkflowSpot } from '../Infrastructure/ZLink/Spots/OrderWorkflowSpot/order-workflow-spot';
import type { ZLinkRouteRequestContext, ZLinkRouteRequestHandler, ZLinkSpotManager } from '@zlink-systems/framework';
import type { StartOrderWorkflowReq, StartOrderWorkflowRes } from '../../../Shared/Contracts/messages';

class StartOrderHandler implements ZLinkRouteRequestHandler<StartOrderWorkflowReq, StartOrderWorkflowRes> {
  constructor(
    @Inject(OrderWorkflowService) private readonly workflow: OrderWorkflowService,
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager
  ) {}

  async handle(request: StartOrderWorkflowReq, context: ZLinkRouteRequestContext): Promise<StartOrderWorkflowRes> {
    void context;
    await ensureOrderWorkflowSpot(this.spots, request.orderId);
    return this.workflow.start(request);
  }
}

export {
  ensureOrderWorkflowSpot,
  StartOrderHandler
};

async function ensureOrderWorkflowSpot(spots: ZLinkSpotManager, orderId: string): Promise<void> {
  await spots.getOrCreate(OrderWorkflowSpot, orderId, { orderId });
}
