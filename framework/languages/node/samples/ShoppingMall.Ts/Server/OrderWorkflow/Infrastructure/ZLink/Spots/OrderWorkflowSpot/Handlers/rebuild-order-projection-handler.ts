import { OrderWorkflowSpot } from '../order-workflow-spot';
import type { ZLinkHandlerContext, ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import type {
  RebuildOrderProjectionReq,
  RebuildOrderProjectionRes
} from '../../../../../../../Shared/Contracts/messages';

class RebuildOrderProjectionSpotHandler
  implements ZLinkSpotRequestHandler<OrderWorkflowSpot, RebuildOrderProjectionReq, RebuildOrderProjectionRes> {
  async handle(
    spot: OrderWorkflowSpot,
    request: RebuildOrderProjectionReq,
    context: ZLinkHandlerContext
  ): Promise<RebuildOrderProjectionRes> {
    void context;
    return spot.rebuildProjection(request);
  }
}

export {
  RebuildOrderProjectionSpotHandler
};
