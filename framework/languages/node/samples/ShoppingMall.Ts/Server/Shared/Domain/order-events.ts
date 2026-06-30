import type { OrderLineInput } from '../../../Shared/Contracts/messages';

type OrderStartedEvent = {
  type: 'OrderStarted';
  eventId: string;
  sourceCommandId: string;
  orderId: string;
  cartId: string;
  shippingAddressId: string;
  lines: OrderLineInput[];
  amount: number;
  currency: string;
  createdAtUnixMs: number;
};

type InventoryReservedEvent = {
  type: 'InventoryReserved';
  eventId: string;
  orderId: string;
  reservationId: string;
  createdAtUnixMs: number;
};

type InventoryReservationFailedEvent = {
  type: 'InventoryReservationFailed';
  eventId: string;
  orderId: string;
  reason: string;
  createdAtUnixMs: number;
};

type PaymentAuthorizedEvent = {
  type: 'PaymentAuthorized';
  eventId: string;
  orderId: string;
  paymentId: string;
  createdAtUnixMs: number;
};

type PaymentFailedEvent = {
  type: 'PaymentFailed';
  eventId: string;
  orderId: string;
  reason: string;
  createdAtUnixMs: number;
};

type InventoryReleasedEvent = {
  type: 'InventoryReleased';
  eventId: string;
  orderId: string;
  reservationId: string;
  reason: string;
  createdAtUnixMs: number;
};

type OrderConfirmedEvent = {
  type: 'OrderConfirmed';
  eventId: string;
  orderId: string;
  createdAtUnixMs: number;
};

type OrderFailedEvent = {
  type: 'OrderFailed';
  eventId: string;
  orderId: string;
  reason: string;
  createdAtUnixMs: number;
};

type OrderDomainEvent =
  | OrderStartedEvent
  | InventoryReservedEvent
  | InventoryReservationFailedEvent
  | PaymentAuthorizedEvent
  | PaymentFailedEvent
  | InventoryReleasedEvent
  | OrderConfirmedEvent
  | OrderFailedEvent;

export type {
  InventoryReleasedEvent,
  InventoryReservationFailedEvent,
  InventoryReservedEvent,
  OrderConfirmedEvent,
  OrderDomainEvent,
  OrderFailedEvent,
  OrderStartedEvent,
  PaymentAuthorizedEvent,
  PaymentFailedEvent
};
