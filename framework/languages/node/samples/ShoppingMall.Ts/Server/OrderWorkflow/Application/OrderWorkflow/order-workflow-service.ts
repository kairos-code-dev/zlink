import { Injectable } from '@nestjs/common';
import { OrderStore } from '../../../Shared/Store/order-store';
import type {
  ContinueOrderWorkflowRes,
  RebuildOrderProjectionRes,
  StartOrderReq,
  StartOrderRes
} from '../../../../Shared/Contracts/messages';
import { advanceOrder } from '../../Domain/ShoppingMall/order-domain';

@Injectable()
class OrderWorkflowService {
  constructor(private readonly store: OrderStore) {}

  start(request: StartOrderReq, role: string): StartOrderRes {
    const response = this.store.startOrder(request, role);
    advanceOrder(this.store.getOrder(response.orderId).state);
    return response;
  }

  prepareInventoryReserved(request: StartOrderReq, role: string): StartOrderRes {
    const response = this.store.createInventoryReserved(request, role);
    advanceOrder(this.store.getOrder(response.orderId).state);
    return response;
  }

  continue(request: { orderId: string }, role: string): ContinueOrderWorkflowRes {
    const response = this.store.continueOrder(request.orderId, role);
    advanceOrder(response.state);
    return response;
  }

  rebuild(request: { orderId: string }): RebuildOrderProjectionRes {
    return this.store.rebuildProjection(request.orderId);
  }
}

export { OrderWorkflowService };
