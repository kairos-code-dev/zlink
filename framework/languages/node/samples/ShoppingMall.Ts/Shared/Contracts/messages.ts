import { ZLinkPacket } from '@zlink-systems/framework';

const PacketNames = {
  startOrderWorkflowReq: 'StartOrderWorkflowReq',
  continueOrderWorkflowReq: 'ContinueOrderWorkflowReq',
  prepareInventoryReservedReq: 'PrepareInventoryReservedReq',
  prepareInventoryEffectReq: 'PrepareInventoryEffectReq',
  verifyExpectedVersionFenceReq: 'VerifyExpectedVersionFenceReq',
  topologyReadyReq: 'ShoppingMallTopologyReadyReq',
  rebuildOrderProjectionReq: 'RebuildOrderProjectionReq'
} as const;

interface OrderLineInput {
  sku: string;
  quantity: number;
}

interface StartOrderReq {
  cartId: string;
  shippingAddressId: string;
  paymentMethodId: string;
  idempotencyKey: string;
}

@ZLinkPacket(PacketNames.startOrderWorkflowReq)
class StartOrderWorkflowReq {
  constructor(
    readonly orderId: string,
    readonly cartId: string,
    readonly shippingAddressId: string,
    readonly paymentMethodId: string,
    readonly idempotencyKey: string,
    readonly lines: readonly OrderLineInput[],
    readonly amount: number,
    readonly currency: string
  ) {}
}

@ZLinkPacket(PacketNames.prepareInventoryReservedReq)
class PrepareInventoryReservedReq extends StartOrderWorkflowReq {}

@ZLinkPacket(PacketNames.prepareInventoryEffectReq)
class PrepareInventoryEffectReq extends StartOrderWorkflowReq {}

@ZLinkPacket(PacketNames.verifyExpectedVersionFenceReq)
class VerifyExpectedVersionFenceReq { constructor(readonly orderId: string) {} }

@ZLinkPacket(PacketNames.topologyReadyReq)
class ShoppingMallTopologyReadyReq { constructor(readonly probeId: string) {} }

@ZLinkPacket(PacketNames.continueOrderWorkflowReq)
class ContinueOrderWorkflowReq { constructor(readonly orderId: string) {} }

@ZLinkPacket(PacketNames.rebuildOrderProjectionReq)
class RebuildOrderProjectionReq { constructor(readonly orderId: string) {} }

interface StartOrderRes {
  orderId: string;
  status: string;
}

interface StartOrderWorkflowRes { state: OrderState; }
interface GetOrderStateRes { state: OrderState; }

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

interface ContinueOrderWorkflowRes { state: OrderState; }
interface RebuildOrderProjectionRes { state: OrderState; }
interface VerifyExpectedVersionFenceRes {
  rejected: boolean;
  expectedVersion: number;
  actualVersion: number;
  writerInstanceId?: string;
  ownerInstanceId?: string;
}

interface ServerAssertionReq {
  successfulOrderId: string;
  pendingRecoveredOrderId: string;
  concurrentOrderId: string;
  resumedOrderId: string;
  interruptedOrderId: string;
  inventoryFailureOrderId: string;
  paymentFailureOrderId: string;
  scaleOutOrderIds: readonly string[];
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
  ContinueOrderWorkflowReq,
  OrderStatuses,
  PacketNames,
  PrepareInventoryEffectReq,
  PrepareInventoryReservedReq,
  RebuildOrderProjectionReq,
  StartOrderWorkflowReq,
  ShoppingMallTopologyReadyReq,
  VerifyExpectedVersionFenceReq
};
export type {
  ContinueOrderWorkflowRes,
  GetOrderStateRes,
  OrderLineInput,
  OrderState,
  RebuildOrderProjectionRes,
  ServerAssertionReq,
  ServerAssertionRes,
  StartOrderReq,
  StartOrderRes,
  StartOrderWorkflowRes,
  VerifyExpectedVersionFenceRes
};
