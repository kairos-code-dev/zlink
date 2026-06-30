import { OrderWorkflowSpot } from '../order-workflow-spot';
import type { ZLinkHandlerContext, ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import type {
  StartOrderWorkflowReq,
  StartOrderWorkflowRes
} from '../../../../../../../Shared/Contracts/messages';

class StartOrderWorkflowSpotHandler
  implements ZLinkSpotRequestHandler<OrderWorkflowSpot, StartOrderWorkflowReq, StartOrderWorkflowRes> {
  async handle(
    spot: OrderWorkflowSpot,
    request: StartOrderWorkflowReq,
    context: ZLinkHandlerContext
  ): Promise<StartOrderWorkflowRes> {
    void context;
    return spot.start(request);
  }
}

export {
  StartOrderWorkflowSpotHandler
};
