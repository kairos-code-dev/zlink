type OrderDomainEvent =
  | { readonly type: 'OrderStarted'; readonly orderId: string }
  | { readonly type: 'InventoryReserved'; readonly orderId: string }
  | { readonly type: 'PaymentAuthorized'; readonly orderId: string }
  | { readonly type: 'OrderConfirmed'; readonly orderId: string }
  | { readonly type: 'OrderFailed'; readonly orderId: string; readonly reason: string };

export type { OrderDomainEvent };
