import { Injectable } from '@nestjs/common';
import { PacketNames } from '../../../Shared/Contracts/messages';
import type { StartOrderReq, StartOrderRes } from '../../../Shared/Contracts/messages';
import { OrderWorkflowRouterPort } from './order-workflow-router-port';

@Injectable()
class StartOrderUseCase {
  constructor(private readonly workflowRouter: OrderWorkflowRouterPort) {}

  start(request: StartOrderReq): Promise<StartOrderRes> {
    this.reserveIdempotency(request.idempotencyKey);
    return this.workflowRouter.requestWorkflow<StartOrderRes>(request, PacketNames.startOrderReq, request.idempotencyKey);
  }

  reserveIdempotency(idempotencyKey: string): string {
    return idempotencyKey;
  }
}

export { StartOrderUseCase };
