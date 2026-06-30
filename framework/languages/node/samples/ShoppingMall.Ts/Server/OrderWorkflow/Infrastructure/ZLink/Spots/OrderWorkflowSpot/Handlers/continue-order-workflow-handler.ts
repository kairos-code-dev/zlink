import { OrderWorkflowSpot } from '../order-workflow-spot';
import type { ZLinkHandlerContext, ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import type {
  ContinueOrderWorkflowReq,
  ContinueOrderWorkflowRes
} from '../../../../../../../Shared/Contracts/messages';

class ContinueOrderWorkflowSpotHandler
  implements ZLinkSpotRequestHandler<OrderWorkflowSpot, ContinueOrderWorkflowReq, ContinueOrderWorkflowRes> {
  async handle(
    spot: OrderWorkflowSpot,
    request: ContinueOrderWorkflowReq,
    context: ZLinkHandlerContext
  ): Promise<ContinueOrderWorkflowRes> {
    void context;
    return spot.continue(request);
  }
}

export {
  ContinueOrderWorkflowSpotHandler
};
