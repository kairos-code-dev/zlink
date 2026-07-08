import { Inject, Injectable } from '@nestjs/common';
import type { ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import { zlinkSpotPacketHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import type { StartOrderReq, StartOrderRes } from '../../../../../../../Shared/Contracts/messages';
import { OrderWorkflowService } from '../../../../../Application/OrderWorkflow/order-workflow-service';
import { OrderWorkflowSpot } from '../order-workflow-spot';
import { SHOPPINGMALL_ROLE } from '../../../../../order-workflow-tokens';

@Injectable()
@zlinkSpotPacketHandler({ packetName: PacketNames.startOrderReq, spot: () => OrderWorkflowSpot })
class StartOrderWorkflowHandler implements ZLinkSpotRequestHandler<OrderWorkflowSpot, StartOrderReq, StartOrderRes> {
  constructor(
    private readonly workflow: OrderWorkflowService,
    @Inject(SHOPPINGMALL_ROLE) private readonly role: string
  ) {}

  handle(spot: OrderWorkflowSpot, request: StartOrderReq): Promise<StartOrderRes> {
    void spot;
    return Promise.resolve(this.workflow.start(request, this.role));
  }
}

export { StartOrderWorkflowHandler };
