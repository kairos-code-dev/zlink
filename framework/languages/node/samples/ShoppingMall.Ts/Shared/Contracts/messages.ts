interface StartOrderReq {
  cartId: string;
  shippingAddressId: string;
  paymentMethodId: string;
  idempotencyKey: string;
}

interface StartOrderRes {
  orderId: string;
  status: string;
}

interface GetOrderStateRes {
  state: OrderState;
}

interface OrderState {
  orderId: string;
  status: string;
  shippingAddressId?: string;
  reservationId?: string;
  paymentId?: string;
  reason?: string;
  amount?: number;
  currency?: string;
  updatedAtUnixMs: number;
}

interface ContinueOrderWorkflowRes {
  state: OrderState;
}

interface RebuildOrderProjectionRes {
  state: OrderState;
}

interface ServerAssertionReq {
  successfulOrderId: string;
  pendingRecoveredOrderId: string;
  concurrentOrderId: string;
  resumedOrderId: string;
  inventoryFailureOrderId: string;
  paymentFailureOrderId: string;
  scaleOutOrderId: string;
}

interface ServerAssertionRes {
  passed: boolean;
  evidence: string[];
}

const OrderStatuses = {
  Created: 'Created',
  InventoryReserved: 'InventoryReserved',
  PaymentAuthorized: 'PaymentAuthorized',
  Confirmed: 'Confirmed',
  Failed: 'Failed'
} as const;

const PacketNames = {
  startOrderReq: 'shoppingmall.start_order.req',
  continueOrderWorkflowReq: 'shoppingmall.continue_order_workflow.req',
  prepareInventoryReservedReq: 'shoppingmall.prepare_inventory_reserved.req',
  rebuildOrderProjectionReq: 'shoppingmall.rebuild_order_projection.req'
} as const;

export { OrderStatuses, PacketNames };
export type {
  ContinueOrderWorkflowRes,
  GetOrderStateRes,
  OrderState,
  RebuildOrderProjectionRes,
  ServerAssertionReq,
  ServerAssertionRes,
  StartOrderReq,
  StartOrderRes
};
