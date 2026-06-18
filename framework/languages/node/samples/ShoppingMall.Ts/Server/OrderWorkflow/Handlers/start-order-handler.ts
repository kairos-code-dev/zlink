import { Inject } from '@nestjs/common';
import { OrderStore } from '../order-store';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { StartOrderReq, StartOrderRes } from '../../../Shared/Contracts/messages';

class StartOrderHandler implements ZLinkRequestHandler<StartOrderReq, StartOrderRes> {
  constructor(@Inject(OrderStore) private readonly orders: OrderStore) {}

  async handle(request: StartOrderReq): Promise<StartOrderRes> {
    return this.orders.startOrder(request);
  }
}

export {
  StartOrderHandler
};
