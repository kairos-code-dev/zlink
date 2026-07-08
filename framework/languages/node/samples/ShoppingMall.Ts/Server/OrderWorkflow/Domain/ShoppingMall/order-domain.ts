import { OrderStatuses } from '../../../../Shared/Contracts/messages';
import type { OrderState } from '../../../../Shared/Contracts/messages';
import type { OrderDomainEvent } from '../../../Shared/Domain/order-events';

function advanceOrder(state: OrderState): OrderDomainEvent {
  if (state.status === OrderStatuses.Failed) {
    return { type: 'OrderFailed', orderId: state.orderId, reason: state.reason ?? 'unknown' };
  }
  if (state.status === OrderStatuses.Confirmed) {
    return { type: 'OrderConfirmed', orderId: state.orderId };
  }
  if (state.status === OrderStatuses.InventoryReserved) {
    return { type: 'InventoryReserved', orderId: state.orderId };
  }
  return { type: 'OrderStarted', orderId: state.orderId };
}

export { advanceOrder };
