import type { OrderDomainEvent } from '../../../Shared/Domain/order-events';
import type { OrderState, StartOrderWorkflowReq } from '../../../../Shared/Contracts/messages';

function orderStarted(request: StartOrderWorkflowReq, nowUnixMs: number): OrderDomainEvent {
  return {
    type: 'OrderStarted',
    eventId: eventId('started', request.orderId),
    sourceCommandId: request.idempotencyKey,
    orderId: request.orderId,
    cartId: request.cartId,
    shippingAddressId: request.shippingAddressId,
    lines: request.lines,
    amount: request.amount,
    currency: request.currency,
    createdAtUnixMs: nowUnixMs
  };
}

function stateFromStarted(event: OrderDomainEvent): OrderState {
  if (event.type !== 'OrderStarted') {
    throw new Error(`Expected OrderStarted event, got ${event.type}`);
  }
  return {
    orderId: event.orderId,
    status: 'Created',
    shippingAddressId: event.shippingAddressId,
    amount: event.amount,
    currency: event.currency,
    updatedAtUnixMs: event.createdAtUnixMs
  };
}

function advanceOrder(
  state: OrderState,
  paymentMethodId: string,
  cartId: string,
  amount: number,
  currency: string,
  nowUnixMs: number
): { state: OrderState; events: OrderDomainEvent[] } {
  if (cartId.includes('inventory-fail')) {
    const reason = 'inventory unavailable';
    return {
      state: { ...state, status: 'Failed', reason, updatedAtUnixMs: nowUnixMs },
      events: [
        {
          type: 'InventoryReservationFailed',
          eventId: eventId('inventory', state.orderId),
          orderId: state.orderId,
          reason,
          createdAtUnixMs: nowUnixMs
        },
        {
          type: 'OrderFailed',
          eventId: eventId('failed', state.orderId),
          orderId: state.orderId,
          reason,
          createdAtUnixMs: nowUnixMs
        }
      ]
    };
  }

  const reservationId = `reservation-${state.orderId}`;
  if (paymentMethodId.includes('decline')) {
    const reason = 'payment declined';
    return {
      state: { ...state, status: 'Failed', reservationId, reason, updatedAtUnixMs: nowUnixMs },
      events: [
        {
          type: 'InventoryReserved',
          eventId: eventId('inventory', state.orderId),
          orderId: state.orderId,
          reservationId,
          createdAtUnixMs: nowUnixMs
        },
        {
          type: 'PaymentFailed',
          eventId: eventId('payment', state.orderId),
          orderId: state.orderId,
          reason,
          createdAtUnixMs: nowUnixMs
        },
        {
          type: 'InventoryReleased',
          eventId: eventId('release', state.orderId),
          orderId: state.orderId,
          reservationId,
          reason,
          createdAtUnixMs: nowUnixMs
        },
        {
          type: 'OrderFailed',
          eventId: eventId('failed', state.orderId),
          orderId: state.orderId,
          reason,
          createdAtUnixMs: nowUnixMs
        }
      ]
    };
  }

  const paymentId = `payment-${state.orderId}`;
  return {
    state: {
      ...state,
      status: 'Confirmed',
      reservationId,
      paymentId,
      amount,
      currency,
      updatedAtUnixMs: nowUnixMs
    },
    events: [
      {
        type: 'InventoryReserved',
        eventId: eventId('inventory', state.orderId),
        orderId: state.orderId,
        reservationId,
        createdAtUnixMs: nowUnixMs
      },
      {
        type: 'PaymentAuthorized',
        eventId: eventId('payment', state.orderId),
        orderId: state.orderId,
        paymentId,
        createdAtUnixMs: nowUnixMs
      },
      {
        type: 'OrderConfirmed',
        eventId: eventId('confirmed', state.orderId),
        orderId: state.orderId,
        createdAtUnixMs: nowUnixMs
      }
    ]
  };
}

function eventId(prefix: string, orderId: string): string {
  return `${prefix}-${orderId}-${Date.now().toString(36)}`;
}

export {
  advanceOrder,
  orderStarted,
  stateFromStarted
};
