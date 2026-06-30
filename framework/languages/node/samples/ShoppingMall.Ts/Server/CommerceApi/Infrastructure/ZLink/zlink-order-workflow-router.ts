import { SampleNames } from '../../../../Shared/Configuration/sample-names';
import type { ZLinkRouteClient } from '@zlink-systems/framework';
import type {
  RebuildOrderProjectionReq,
  RebuildOrderProjectionRes,
  SeedPendingIdempotencyReq,
  StartOrderRes,
  StartOrderWorkflowReq,
  StartOrderWorkflowRes
} from '../../../../Shared/Contracts/messages';
import type { OrderWorkflowCommandPort } from '../../Ports/Outbound/workflow-ports';

class ZLinkOrderWorkflowRouter implements OrderWorkflowCommandPort {
  constructor(private readonly routes: ZLinkRouteClient) {}

  startOrder(request: StartOrderWorkflowReq): Promise<StartOrderWorkflowRes> {
    return this.requestWorkflow(request);
  }

  rebuildProjection(request: RebuildOrderProjectionReq): Promise<RebuildOrderProjectionRes> {
    return this.requestWorkflow(request);
  }

  seedPendingIdempotency(request: SeedPendingIdempotencyReq): Promise<StartOrderRes> {
    return this.requestWorkflow(request);
  }

  private async requestWorkflow<TResponse>(payload: unknown): Promise<TResponse> {
    let lastError: unknown;
    for (let attempt = 0; attempt < 40; attempt += 1) {
      try {
        return await this.routes
          .request(SampleNames.orderWorkflowRouteChannel, workflowRouteRid(payload), payload)
          .submit<TResponse>();
      } catch (error) {
        lastError = error;
        await delay(250);
      }
    }
    throw lastError instanceof Error ? lastError : new Error(String(lastError));
  }
}

function workflowRouteRid(payload: unknown): string {
  const orderId = typeof payload === 'object' && payload !== null && 'orderId' in payload
    ? String((payload as { orderId: unknown }).orderId)
    : 'workflow-a';
  const match = /(\d+)$/.exec(orderId);
  if (match === null) {
    return 'workflow-a';
  }
  return Number(match[1]) % 2 === 0 ? 'workflow-b' : 'workflow-a';
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

export {
  ZLinkOrderWorkflowRouter
};
