import { Inject } from '@nestjs/common';
import { OrderWorkflowService } from '../../../../Application/OrderWorkflow/order-workflow-service';
import { PacketNames } from '../../../../../../Shared/Contracts/messages';
import { ContinueOrderWorkflowSpotHandler } from './Handlers/continue-order-workflow-handler';
import { RebuildOrderProjectionSpotHandler } from './Handlers/rebuild-order-projection-handler';
import { StartOrderWorkflowSpotHandler } from './Handlers/start-order-workflow-handler';
import type { ZLinkMessage, ZLinkSpot, ZLinkSpotContext } from '@zlink-systems/framework';
import type {
  ContinueOrderWorkflowReq,
  ContinueOrderWorkflowRes,
  RebuildOrderProjectionReq,
  RebuildOrderProjectionRes,
  StartOrderWorkflowReq,
  StartOrderWorkflowRes
} from '../../../../../../Shared/Contracts/messages';

class OrderWorkflowSpot implements ZLinkSpot {
  readonly context!: ZLinkSpotContext;
  private orderId = '';

  constructor(@Inject(OrderWorkflowService) private readonly workflow: OrderWorkflowService) {}

  configure(): void {
    this.context.handlers.packet(PacketNames.startOrderWorkflowReq, StartOrderWorkflowSpotHandler);
    this.context.handlers.packet(PacketNames.continueOrderWorkflowReq, ContinueOrderWorkflowSpotHandler);
    this.context.handlers.packet(PacketNames.rebuildOrderProjectionReq, RebuildOrderProjectionSpotHandler);
  }

  async onCreate(request: ZLinkMessage): Promise<{ accepted: boolean }> {
    const payload = request.decode<{ orderId?: string }>(Object as never);
    this.orderId = typeof payload.orderId === 'string' ? payload.orderId : String(this.context.spotRid);
    console.error(`shoppingmall order spot ready order=${this.orderId} spot=${String(this.context.spotRid)}`);
    return { accepted: true };
  }

  start(request: StartOrderWorkflowReq): StartOrderWorkflowRes {
    return this.workflow.start(request);
  }

  continue(request: ContinueOrderWorkflowReq): ContinueOrderWorkflowRes {
    return this.workflow.continue(request);
  }

  rebuildProjection(request: RebuildOrderProjectionReq): RebuildOrderProjectionRes {
    return this.workflow.rebuildProjection(request);
  }
}

export {
  OrderWorkflowSpot
};
