import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { OrderWorkflowService } from '../Application/OrderWorkflow/order-workflow-service';
import { ensureOrderWorkflowSpot } from './start-order-handler';
import type { ZLinkRouteRequestContext, ZLinkRouteRequestHandler, ZLinkSpotManager } from '@zlink-systems/framework';
import type { ContinueOrderWorkflowReq, ContinueOrderWorkflowRes } from '../../../Shared/Contracts/messages';

class ContinueOrderWorkflowHandler implements ZLinkRouteRequestHandler<ContinueOrderWorkflowReq, ContinueOrderWorkflowRes> {
  constructor(
    @Inject(OrderWorkflowService) private readonly workflow: OrderWorkflowService,
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager
  ) {}

  async handle(request: ContinueOrderWorkflowReq, context: ZLinkRouteRequestContext): Promise<ContinueOrderWorkflowRes> {
    void context;
    await ensureOrderWorkflowSpot(this.spots, request.orderId);
    return this.workflow.continue(request);
  }
}

export {
  ContinueOrderWorkflowHandler
};
