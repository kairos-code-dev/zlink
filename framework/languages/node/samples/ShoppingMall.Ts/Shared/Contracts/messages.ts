import { ZLinkPacket } from '@zlink-systems/framework';

const PacketNames = {
  startOrderReq: 'shoppingmall.start_order.req',
  continueOrderWorkflowReq: 'shoppingmall.continue_order_workflow.req',
  prepareInventoryReservedReq: 'shoppingmall.prepare_inventory_reserved.req',
  rebuildOrderProjectionReq: 'shoppingmall.rebuild_order_projection.req'
} as const;

@ZLinkPacket(PacketNames.startOrderReq)
class StartOrderReq {
  constructor(
    readonly cartId: string,
    readonly shippingAddressId: string,
    readonly paymentMethodId: string,
    readonly idempotencyKey: string
  ) {}
}

@ZLinkPacket(PacketNames.prepareInventoryReservedReq)
class PrepareInventoryReservedReq {
  constructor(
    readonly cartId: string,
    readonly shippingAddressId: string,
    readonly paymentMethodId: string,
    readonly idempotencyKey: string
  ) {}
}

@ZLinkPacket(PacketNames.continueOrderWorkflowReq)
class ContinueOrderWorkflowReq { constructor(readonly orderId: string) {} }

@ZLinkPacket(PacketNames.rebuildOrderProjectionReq)
class RebuildOrderProjectionReq { constructor(readonly orderId: string) {} }

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

export {
  OrderStatuses,
  PacketNames,
  StartOrderReq,
  PrepareInventoryReservedReq,
  ContinueOrderWorkflowReq,
  RebuildOrderProjectionReq
};
export type {
  ContinueOrderWorkflowRes,
  GetOrderStateRes,
  OrderState,
  RebuildOrderProjectionRes,
  ServerAssertionReq,
  ServerAssertionRes,
  StartOrderRes
};
