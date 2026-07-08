import { Injectable } from '@nestjs/common';
import type { ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import { zlinkSpotPacketHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import type { RebuildOrderProjectionRes } from '../../../../../../../Shared/Contracts/messages';
import { OrderWorkflowService } from '../../../../../Application/OrderWorkflow/order-workflow-service';
import { OrderWorkflowSpot } from '../order-workflow-spot';

@Injectable()
@zlinkSpotPacketHandler({ packetName: PacketNames.rebuildOrderProjectionReq, spot: () => OrderWorkflowSpot })
class RebuildOrderProjectionHandler implements ZLinkSpotRequestHandler<OrderWorkflowSpot, { orderId: string }, RebuildOrderProjectionRes> {
  constructor(private readonly workflow: OrderWorkflowService) {}

  handle(spot: OrderWorkflowSpot, request: { orderId: string }): Promise<RebuildOrderProjectionRes> {
    void spot;
    return Promise.resolve(this.workflow.rebuild(request));
  }
}

export { RebuildOrderProjectionHandler };
