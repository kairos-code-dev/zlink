import { Injectable } from '@nestjs/common';
import type { StartOrderReq, StartOrderRes } from '../../../Shared/Contracts/messages';
import { OrderWorkflowRouterPort } from './order-workflow-router-port';

@Injectable()
class StartOrderUseCase {
  constructor(private readonly workflowRouter: OrderWorkflowRouterPort) {}

  start(request: StartOrderReq): Promise<StartOrderRes> {
    this.reserveIdempotency(request.idempotencyKey);
    return this.workflowRouter.start(request);
  }

  reserveIdempotency(idempotencyKey: string): string {
    return idempotencyKey;
  }
}

export { StartOrderUseCase };
