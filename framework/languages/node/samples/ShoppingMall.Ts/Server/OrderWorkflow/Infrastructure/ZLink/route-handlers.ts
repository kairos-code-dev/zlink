import { Inject, Injectable } from '@nestjs/common';
import type {
  SpotHandle,
  ZLinkRequestHandler,
  ZLinkSpotHandleResolver,
  ZLinkSpotManager,
  ZLinkSpotOutbound
} from '@zlink-systems/framework';
import {
  ZLINK_SPOT_HANDLE_RESOLVER,
  ZLINK_SPOT_MANAGER,
  ZLINK_SPOT_OUTBOUND,
  zlinkRequestHandler
} from '@zlink-systems/nestjs';
import {
  ContinueOrderWorkflowReq,
  OrderStatuses,
  PacketNames,
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
import { SampleNames } from '../../../../Shared/Configuration/sample-names';
import { OrderWorkflowService } from '../../Application/OrderWorkflow/order-workflow-service';
import { SHOPPINGMALL_ROLE } from '../../order-workflow-tokens';
import { OrderWorkflowSpot } from './Spots/OrderWorkflowSpot/order-workflow-spot';

@Injectable()
@zlinkRequestHandler('workflow', PacketNames.startOrderWorkflowReq)
class StartOrderWorkflowRouteHandler implements ZLinkRequestHandler<StartOrderWorkflowReq, StartOrderWorkflowRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotRefs: ZLinkSpotHandleResolver
  ) {}

  async handle(request: StartOrderWorkflowReq): Promise<StartOrderWorkflowRes> {
    const spot = await resolveOrderSpot(this.spots, this.spotRefs, request.orderId);
    const started = await requestSpot<StartOrderWorkflowRes>(this.outbound, spot, new StartOrderWorkflowReq(
      request.orderId, request.cartId, request.shippingAddressId, request.paymentMethodId,
      request.idempotencyKey, request.lines, request.amount, request.currency
    ));
    if (started.state.status === OrderStatuses.Created) {
      void requestSpot<ContinueOrderWorkflowRes>(this.outbound, spot, new ContinueOrderWorkflowReq(request.orderId))
        .catch((error: unknown) => console.error(`shoppingmall background continue failed order=${request.orderId}: ${error instanceof Error ? error.message : String(error)}`));
    }
    return started;
  }
}

@Injectable()
@zlinkRequestHandler('workflow', PacketNames.prepareInventoryReservedReq)
class PrepareInventoryReservedRouteHandler implements ZLinkRequestHandler<PrepareInventoryReservedReq, StartOrderWorkflowRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotRefs: ZLinkSpotHandleResolver
  ) {}

  async handle(request: PrepareInventoryReservedReq): Promise<StartOrderWorkflowRes> {
    const spot = await resolveOrderSpot(this.spots, this.spotRefs, request.orderId);
    const prepared = await requestSpot<StartOrderWorkflowRes>(this.outbound, spot, new PrepareInventoryReservedReq(
      request.orderId, request.cartId, request.shippingAddressId, request.paymentMethodId,
      request.idempotencyKey, request.lines, request.amount, request.currency
    ));
    return prepared;
  }
}

@Injectable()
@zlinkRequestHandler('workflow', PacketNames.prepareInventoryEffectReq)
class PrepareInventoryEffectRouteHandler implements ZLinkRequestHandler<PrepareInventoryEffectReq, StartOrderWorkflowRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotRefs: ZLinkSpotHandleResolver
  ) {}

  async handle(request: PrepareInventoryEffectReq): Promise<StartOrderWorkflowRes> {
    const spot = await resolveOrderSpot(this.spots, this.spotRefs, request.orderId);
    return requestSpot(this.outbound, spot, new PrepareInventoryEffectReq(
      request.orderId, request.cartId, request.shippingAddressId, request.paymentMethodId,
      request.idempotencyKey, request.lines, request.amount, request.currency
    ));
  }
}

@Injectable()
@zlinkRequestHandler('workflow', PacketNames.continueOrderWorkflowReq)
class ContinueOrderWorkflowRouteHandler implements ZLinkRequestHandler<ContinueOrderWorkflowReq, ContinueOrderWorkflowRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotRefs: ZLinkSpotHandleResolver
  ) {}

  async handle(request: ContinueOrderWorkflowReq): Promise<ContinueOrderWorkflowRes> {
    return requestSpot(this.outbound, await resolveOrderSpot(this.spots, this.spotRefs, request.orderId), new ContinueOrderWorkflowReq(request.orderId));
  }
}

@Injectable()
@zlinkRequestHandler('workflow', PacketNames.rebuildOrderProjectionReq)
class RebuildOrderProjectionRouteHandler implements ZLinkRequestHandler<RebuildOrderProjectionReq, RebuildOrderProjectionRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotRefs: ZLinkSpotHandleResolver
  ) {}

  async handle(request: RebuildOrderProjectionReq): Promise<RebuildOrderProjectionRes> {
    return requestSpot(this.outbound, await resolveOrderSpot(this.spots, this.spotRefs, request.orderId), new RebuildOrderProjectionReq(request.orderId));
  }
}

@Injectable()
@zlinkRequestHandler('workflow', PacketNames.verifyExpectedVersionFenceReq)
class VerifyExpectedVersionFenceRouteHandler implements ZLinkRequestHandler<VerifyExpectedVersionFenceReq, VerifyExpectedVersionFenceRes> {
  constructor(
    private readonly workflow: OrderWorkflowService,
    @Inject(SHOPPINGMALL_ROLE) private readonly role: string
  ) {}

  async handle(request: VerifyExpectedVersionFenceReq): Promise<VerifyExpectedVersionFenceRes> {
    return this.workflow.verifyExpectedVersionFence(request, this.role);
  }
}

async function resolveOrderSpot(spots: ZLinkSpotManager, spotRefs: ZLinkSpotHandleResolver, orderId: string): Promise<SpotHandle> {
  await spots.getOrCreate(OrderWorkflowSpot, orderId, { orderId });
  const spot = await spotRefs.resolveSpotHandle(orderId);
  if (spot === undefined) throw new Error(`Order workflow spot '${orderId}' was not resolved.`);
  return spot;
}

function requestSpot<T>(outbound: ZLinkSpotOutbound, spot: SpotHandle, body: object): Promise<T> {
  return outbound.requestToSpot(spot, body).timeout(SampleNames.requestTimeout).submit<T>();
}

export {
  ContinueOrderWorkflowRouteHandler,
  PrepareInventoryEffectRouteHandler,
  PrepareInventoryReservedRouteHandler,
  RebuildOrderProjectionRouteHandler,
  StartOrderWorkflowRouteHandler,
  VerifyExpectedVersionFenceRouteHandler
};
