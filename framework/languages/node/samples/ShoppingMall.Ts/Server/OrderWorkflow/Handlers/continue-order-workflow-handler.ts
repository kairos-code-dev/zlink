import { Inject } from '@nestjs/common';
import { OrderStore } from '../order-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { ContinueOrderWorkflowReq, ContinueOrderWorkflowRes } from '../../../Shared/Contracts/messages';

class ContinueOrderWorkflowHandler implements ZLinkRequestHandler<ContinueOrderWorkflowReq, ContinueOrderWorkflowRes> {
  constructor(@Inject(OrderStore) private readonly orders: OrderStore) {}

  async handle(request: ContinueOrderWorkflowReq): Promise<ContinueOrderWorkflowRes> {
    return this.orders.continueWorkflow(request.orderId);
  }
}

export {
  ContinueOrderWorkflowHandler
};
