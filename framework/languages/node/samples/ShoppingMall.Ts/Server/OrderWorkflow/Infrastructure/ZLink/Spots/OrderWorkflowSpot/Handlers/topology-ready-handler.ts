import { Injectable } from '@nestjs/common';
import type { ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import { zlinkSpotPacketHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import type { ShoppingMallTopologyReadyReq } from '../../../../../../../Shared/Contracts/messages';
import { OrderWorkflowSpot } from '../order-workflow-spot';

@Injectable()
@zlinkSpotPacketHandler({ packetName: PacketNames.topologyReadyReq, spot: () => OrderWorkflowSpot })
class ShoppingMallTopologyReadyHandler implements ZLinkSpotRequestHandler<OrderWorkflowSpot, ShoppingMallTopologyReadyReq, { ready: true }> {
  handle(spot: OrderWorkflowSpot, request: ShoppingMallTopologyReadyReq): Promise<{ ready: true }> {
    void spot;
    void request;
    return Promise.resolve({ ready: true });
  }
}

export { ShoppingMallTopologyReadyHandler };
