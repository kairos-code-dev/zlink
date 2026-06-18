import { Inject } from '@nestjs/common';
import { OrderStore } from '../order-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type {
  GetOrderStateReq,
  GetOrderStateRes,
  RebuildOrderProjectionReq,
  RebuildOrderProjectionRes,
  SeedPendingIdempotencyReq,
  ServerAssertionReq,
  ServerAssertionRes,
  StartOrderRes
} from '../../../Shared/Contracts/messages';

class GetOrderStateHandler implements ZLinkRequestHandler<GetOrderStateReq, GetOrderStateRes> {
  constructor(@Inject(OrderStore) private readonly orders: OrderStore) {}

  async handle(request: GetOrderStateReq): Promise<GetOrderStateRes> {
    return this.orders.getOrder(request.orderId);
  }
}

class DeleteOrderProjectionHandler implements ZLinkRequestHandler<GetOrderStateReq, GetOrderStateRes> {
  constructor(@Inject(OrderStore) private readonly orders: OrderStore) {}

  async handle(request: GetOrderStateReq): Promise<GetOrderStateRes> {
    return this.orders.deleteProjection(request.orderId);
  }
}

class RebuildOrderProjectionHandler implements ZLinkRequestHandler<RebuildOrderProjectionReq, RebuildOrderProjectionRes> {
  constructor(@Inject(OrderStore) private readonly orders: OrderStore) {}

  async handle(request: RebuildOrderProjectionReq): Promise<RebuildOrderProjectionRes> {
    return this.orders.rebuildProjection(request.orderId);
  }
}

class SeedPendingIdempotencyHandler implements ZLinkRequestHandler<SeedPendingIdempotencyReq, StartOrderRes> {
  constructor(@Inject(OrderStore) private readonly orders: OrderStore) {}

  async handle(request: SeedPendingIdempotencyReq): Promise<StartOrderRes> {
    return this.orders.seedPending(request);
  }
}

class ServerAssertionHandler implements ZLinkRequestHandler<ServerAssertionReq, ServerAssertionRes> {
  constructor(@Inject(OrderStore) private readonly orders: OrderStore) {}

  async handle(request: ServerAssertionReq): Promise<ServerAssertionRes> {
    return this.orders.assertEvidence(
      request.successfulOrderId,
      request.pendingRecoveredOrderId,
      request.inventoryFailureOrderId,
      request.paymentFailureOrderId,
      request.scaleOutOrderId
    );
  }
}

export {
  DeleteOrderProjectionHandler,
  GetOrderStateHandler,
  RebuildOrderProjectionHandler,
  SeedPendingIdempotencyHandler,
  ServerAssertionHandler
};
