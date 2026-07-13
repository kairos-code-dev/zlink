import { Injectable } from '@nestjs/common';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { Inject } from '@nestjs/common';
import { orderWorkflowChannelFor, SampleNames } from '../../../../Shared/Configuration/sample-names';
import { OrderWorkflowRouterPort } from '../../Application/order-workflow-router-port';
import {
  ContinueOrderWorkflowReq,
  PrepareInventoryReservedReq,
  RebuildOrderProjectionReq,
  StartOrderReq
} from '../../../../Shared/Contracts/messages';
import type {
  ContinueOrderWorkflowRes,
  RebuildOrderProjectionRes,
  StartOrderRes
} from '../../../../Shared/Contracts/messages';

@Injectable()
class ZLinkOrderWorkflowRouter implements OrderWorkflowRouterPort {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient) {}

  start(request: StartOrderReq): Promise<StartOrderRes> {
    return this.request(new StartOrderReq(
      request.cartId,
      request.shippingAddressId,
      request.paymentMethodId,
      request.idempotencyKey
    ), request.idempotencyKey);
  }

  prepareInventory(request: StartOrderReq): Promise<StartOrderRes> {
    return this.request(new PrepareInventoryReservedReq(
      request.cartId,
      request.shippingAddressId,
      request.paymentMethodId,
      request.idempotencyKey
    ), request.idempotencyKey);
  }

  continue(orderId: string): Promise<ContinueOrderWorkflowRes> {
    return this.request(new ContinueOrderWorkflowReq(orderId), orderId);
  }

  rebuild(orderId: string): Promise<RebuildOrderProjectionRes> {
    return this.request(new RebuildOrderProjectionReq(orderId), orderId);
  }

  private request<TResponse>(payload: object, ownerKey: string): Promise<TResponse> {
    return this.channels
      .requestToChannel(orderWorkflowChannelFor(this.workflowRouteRid(ownerKey)), payload)
      .timeout(SampleNames.requestTimeout)
      .submit<TResponse>();
  }

  workflowRouteRid(payload: string): string {
    const last = payload.charCodeAt(payload.length - 1);
    return Number.isFinite(last) && last % 2 === 0 ? SampleNames.workflowB : SampleNames.workflowA;
  }
}

export { ZLinkOrderWorkflowRouter };
