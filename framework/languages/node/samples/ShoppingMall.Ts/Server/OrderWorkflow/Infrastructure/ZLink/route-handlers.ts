import { Inject, Injectable } from '@nestjs/common';
import type {
  ZLinkRequestHandler,
  ZLinkSpotManager,
  ZLinkSpotOutbound,
  ZLinkSpotRefResolver
} from '@zlink-systems/framework';
import {
  ZLINK_SPOT_MANAGER,
  ZLINK_SPOT_OUTBOUND,
  ZLINK_SPOT_REF_RESOLVER,
  zlinkRequestHandler
} from '@zlink-systems/nestjs';
import { PacketNames } from '../../../../Shared/Contracts/messages';
import type {
  ContinueOrderWorkflowRes,
  RebuildOrderProjectionRes,
  StartOrderReq,
  StartOrderRes
} from '../../../../Shared/Contracts/messages';
import { SampleNames } from '../../../../Shared/Configuration/sample-names';
import { OrderWorkflowSpot } from './Spots/OrderWorkflowSpot/order-workflow-spot';

@Injectable()
@zlinkRequestHandler('workflow', PacketNames.startOrderReq)
class StartOrderWorkflowRouteHandler implements ZLinkRequestHandler<StartOrderReq, StartOrderRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound,
    @Inject(ZLINK_SPOT_REF_RESOLVER) private readonly spotRefs: ZLinkSpotRefResolver
  ) {}

  handle(request: StartOrderReq): Promise<StartOrderRes> {
    return requestOrderWorkflowSpot(this.spots, this.outbound, this.spotRefs, orderIdForStartRequest(request), request, PacketNames.startOrderReq);
  }
}

@Injectable()
@zlinkRequestHandler('workflow', PacketNames.prepareInventoryReservedReq)
class PrepareInventoryReservedRouteHandler implements ZLinkRequestHandler<StartOrderReq, StartOrderRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound,
    @Inject(ZLINK_SPOT_REF_RESOLVER) private readonly spotRefs: ZLinkSpotRefResolver
  ) {}

  handle(request: StartOrderReq): Promise<StartOrderRes> {
    return requestOrderWorkflowSpot(this.spots, this.outbound, this.spotRefs, orderIdForStartRequest(request), request, PacketNames.prepareInventoryReservedReq);
  }
}

@Injectable()
@zlinkRequestHandler('workflow', PacketNames.continueOrderWorkflowReq)
class ContinueOrderWorkflowRouteHandler implements ZLinkRequestHandler<{ orderId: string }, ContinueOrderWorkflowRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound,
    @Inject(ZLINK_SPOT_REF_RESOLVER) private readonly spotRefs: ZLinkSpotRefResolver
  ) {}

  handle(request: { orderId: string }): Promise<ContinueOrderWorkflowRes> {
    return requestOrderWorkflowSpot(this.spots, this.outbound, this.spotRefs, request.orderId, request, PacketNames.continueOrderWorkflowReq);
  }
}

@Injectable()
@zlinkRequestHandler('workflow', PacketNames.rebuildOrderProjectionReq)
class RebuildOrderProjectionRouteHandler implements ZLinkRequestHandler<{ orderId: string }, RebuildOrderProjectionRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound,
    @Inject(ZLINK_SPOT_REF_RESOLVER) private readonly spotRefs: ZLinkSpotRefResolver
  ) {}

  handle(request: { orderId: string }): Promise<RebuildOrderProjectionRes> {
    return requestOrderWorkflowSpot(this.spots, this.outbound, this.spotRefs, request.orderId, request, PacketNames.rebuildOrderProjectionReq);
  }
}

async function requestOrderWorkflowSpot<TResponse>(
  spots: ZLinkSpotManager,
  outbound: ZLinkSpotOutbound,
  spotRefs: ZLinkSpotRefResolver,
  orderId: string,
  body: unknown,
  packetName: string
): Promise<TResponse> {
  await spots.getOrCreate(OrderWorkflowSpot, orderId, { orderId });
  const spot = await spotRefs.resolveSpotRef(orderId);
  if (spot === undefined) {
    throw new Error(`Order workflow spot '${orderId}' was not resolved.`);
  }
  return outbound
    .requestToSpot(spot, body)
    .packetName(packetName)
    .timeout(SampleNames.requestTimeout)
    .submit<TResponse>();
}

function orderIdForStartRequest(request: StartOrderReq): string {
  return request.idempotencyKey.replace(/-\d+$/, '-0001');
}

export {
  ContinueOrderWorkflowRouteHandler,
  PrepareInventoryReservedRouteHandler,
  RebuildOrderProjectionRouteHandler,
  StartOrderWorkflowRouteHandler
};
