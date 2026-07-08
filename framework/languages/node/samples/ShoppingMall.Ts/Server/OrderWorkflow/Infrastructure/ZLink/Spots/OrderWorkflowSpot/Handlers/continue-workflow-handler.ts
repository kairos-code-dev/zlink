import { Inject, Injectable } from '@nestjs/common';
import type { ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import { zlinkSpotPacketHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import type { ContinueOrderWorkflowRes } from '../../../../../../../Shared/Contracts/messages';
import { OrderWorkflowService } from '../../../../../Application/OrderWorkflow/order-workflow-service';
import { OrderWorkflowSpot } from '../order-workflow-spot';
import { SHOPPINGMALL_ROLE } from '../../../../../order-workflow-tokens';

@Injectable()
@zlinkSpotPacketHandler({ packetName: PacketNames.continueOrderWorkflowReq, spot: () => OrderWorkflowSpot })
class ContinueWorkflowHandler implements ZLinkSpotRequestHandler<OrderWorkflowSpot, { orderId: string }, ContinueOrderWorkflowRes> {
  constructor(
    private readonly workflow: OrderWorkflowService,
    @Inject(SHOPPINGMALL_ROLE) private readonly role: string
  ) {}

  handle(spot: OrderWorkflowSpot, request: { orderId: string }): Promise<ContinueOrderWorkflowRes> {
    void spot;
    return Promise.resolve(this.workflow.continue(request, this.role));
  }
}

export { ContinueWorkflowHandler };
