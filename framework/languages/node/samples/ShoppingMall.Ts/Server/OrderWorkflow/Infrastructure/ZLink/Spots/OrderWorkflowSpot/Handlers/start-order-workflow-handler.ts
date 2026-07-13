import { Inject, Injectable } from '@nestjs/common';
import type { ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import { zlinkSpotPacketHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import type { StartOrderWorkflowReq, StartOrderWorkflowRes } from '../../../../../../../Shared/Contracts/messages';
import { OrderWorkflowService } from '../../../../../Application/OrderWorkflow/order-workflow-service';
import { OrderWorkflowSpot } from '../order-workflow-spot';
import { SHOPPINGMALL_ROLE } from '../../../../../order-workflow-tokens';

@Injectable()
@zlinkSpotPacketHandler({ packetName: PacketNames.startOrderWorkflowReq, spot: () => OrderWorkflowSpot })
class StartOrderWorkflowHandler implements ZLinkSpotRequestHandler<OrderWorkflowSpot, StartOrderWorkflowReq, StartOrderWorkflowRes> {
  constructor(
    private readonly workflow: OrderWorkflowService,
    @Inject(SHOPPINGMALL_ROLE) private readonly role: string
  ) {}

  handle(spot: OrderWorkflowSpot, request: StartOrderWorkflowReq): Promise<StartOrderWorkflowRes> {
    void spot;
    return Promise.resolve(this.workflow.start(request, this.role));
  }
}

export { StartOrderWorkflowHandler };
