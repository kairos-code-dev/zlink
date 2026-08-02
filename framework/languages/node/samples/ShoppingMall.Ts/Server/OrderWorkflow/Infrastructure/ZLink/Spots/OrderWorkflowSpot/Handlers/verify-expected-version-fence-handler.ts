import { Inject, Injectable } from '@nestjs/common';
import { zlinkSpotPacketHandler } from '@zlink-systems/nestjs';
import type { ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import type {
  VerifyExpectedVersionFenceReq,
  VerifyExpectedVersionFenceRes
} from '../../../../../../Shared/Internal/shoppingmall-workflow-messages';
import { OrderWorkflowService } from '../../../../../Application/OrderWorkflow/order-workflow-service';
import { SHOPPINGMALL_ROLE } from '../../../../../order-workflow-tokens';
import { OrderWorkflowSpot } from '../order-workflow-spot';

@Injectable()
@zlinkSpotPacketHandler({ spot: () => OrderWorkflowSpot, packetName: 'VerifyExpectedVersionFenceReq' })
class VerifyExpectedVersionFenceHandler
  implements ZLinkSpotRequestHandler<
    OrderWorkflowSpot,
    VerifyExpectedVersionFenceReq,
    VerifyExpectedVersionFenceRes
  > {
  constructor(
    private readonly workflow: OrderWorkflowService,
    @Inject(SHOPPINGMALL_ROLE) private readonly role: string
  ) {}

  handle(
    spot: OrderWorkflowSpot,
    request: VerifyExpectedVersionFenceReq
  ): Promise<VerifyExpectedVersionFenceRes> {
    void spot;
    return Promise.resolve(this.workflow.verifyExpectedVersionFence(request, this.role));
  }
}

export { VerifyExpectedVersionFenceHandler };
