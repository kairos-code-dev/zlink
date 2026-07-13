import { Injectable } from '@nestjs/common';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { Inject } from '@nestjs/common';
import { SampleNames } from '../../../../Shared/Configuration/sample-names';
import { OrderWorkflowRouterPort } from '../../Application/order-workflow-router-port';
import {
  ContinueOrderWorkflowReq,
  PrepareInventoryEffectReq,
  PrepareInventoryReservedReq,
  RebuildOrderProjectionReq,
  StartOrderWorkflowReq,
  VerifyExpectedVersionFenceReq
} from '../../../../Shared/Contracts/messages';
import type {
  ContinueOrderWorkflowRes,
  RebuildOrderProjectionRes,
  StartOrderWorkflowRes,
  VerifyExpectedVersionFenceRes
} from '../../../../Shared/Contracts/messages';

@Injectable()
class ZLinkOrderWorkflowRouter implements OrderWorkflowRouterPort {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient) {}

  start(request: StartOrderWorkflowReq): Promise<StartOrderWorkflowRes> {
    return this.request(request);
  }

  prepareInventory(request: StartOrderWorkflowReq): Promise<StartOrderWorkflowRes> {
    return this.request(new PrepareInventoryReservedReq(
      request.orderId,
      request.cartId,
      request.shippingAddressId,
      request.paymentMethodId,
      request.idempotencyKey,
      request.lines,
      request.amount,
      request.currency
    ));
  }

  prepareInventoryEffect(request: StartOrderWorkflowReq): Promise<StartOrderWorkflowRes> {
    return this.request(new PrepareInventoryEffectReq(
      request.orderId, request.cartId, request.shippingAddressId, request.paymentMethodId,
      request.idempotencyKey, request.lines, request.amount, request.currency
    ));
  }

  continue(orderId: string): Promise<ContinueOrderWorkflowRes> {
    return this.request(new ContinueOrderWorkflowReq(orderId));
  }

  rebuild(orderId: string): Promise<RebuildOrderProjectionRes> {
    return this.request(new RebuildOrderProjectionReq(orderId));
  }

  async verifyExpectedVersionFence(orderId: string): Promise<VerifyExpectedVersionFenceRes> {
    for (let attempt = 0; attempt < 16; attempt++) {
      const result = await this.request<VerifyExpectedVersionFenceRes>(new VerifyExpectedVersionFenceReq(orderId));
      if (result.rejected) return result;
    }
    throw new Error(`Expected-version fence was not exercised by a second workflow instance for '${orderId}'.`);
  }

  private request<TResponse>(payload: object): Promise<TResponse> {
    return this.channels
      .requestToChannel(SampleNames.orderWorkflowChannel, payload)
      .timeout(SampleNames.requestTimeout)
      .submit<TResponse>();
  }

}

export { ZLinkOrderWorkflowRouter };
