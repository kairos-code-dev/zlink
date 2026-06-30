type OrderStatus = 'Created' | 'Confirmed' | 'Failed';
type Packetized = {
  packetName(): string;
};

type OrderState = {
  orderId: string;
  status: OrderStatus;
  shippingAddressId?: string;
  reservationId?: string;
  paymentId?: string;
  reason?: string;
  amount?: number;
  currency?: string;
  updatedAtUnixMs: number;
};

type OrderLineInput = {
  sku: string;
  quantity: number;
};

type StartOrderReq = {
  cartId: string;
  shippingAddressId: string;
  paymentMethodId: string;
  idempotencyKey: string;
};

type StartOrderRes = {
  orderId: string;
  status: OrderStatus;
};

type StartOrderWorkflowReq = Packetized & {
  orderId: string;
  cartId: string;
  shippingAddressId: string;
  paymentMethodId: string;
  idempotencyKey: string;
  lines: OrderLineInput[];
  amount: number;
  currency: string;
};

type StartOrderWorkflowRes = {
  state: OrderState;
};

type GetOrderStateReq = Packetized & {
  orderId: string;
};

type GetOrderStateRes = {
  state: OrderState;
};

type ContinueOrderWorkflowReq = Packetized & {
  orderId: string;
};

type ContinueOrderWorkflowRes = {
  state: OrderState;
};

type RebuildOrderProjectionReq = Packetized & {
  orderId: string;
};

type RebuildOrderProjectionRes = {
  state: OrderState;
};

type SeedPendingIdempotencyReq = Packetized & {
  idempotencyKey: string;
  orderId: string;
  ownerInstanceId: string;
};

type ServerAssertionReq = Packetized & {
  successfulOrderId: string;
  pendingRecoveredOrderId: string;
  inventoryFailureOrderId: string;
  paymentFailureOrderId: string;
  scaleOutOrderId: string;
};

type ServerAssertionRes = {
  passed: boolean;
  evidence: string[];
};

const PacketNames = {
  startOrderWorkflowReq: 'StartOrderWorkflowReq',
  getOrderStateReq: 'GetOrderStateReq',
  continueOrderWorkflowReq: 'ContinueOrderWorkflowReq',
  rebuildOrderProjectionReq: 'RebuildOrderProjectionReq',
  deleteOrderProjectionReq: 'DeleteOrderProjectionReq',
  seedPendingIdempotencyReq: 'SeedPendingIdempotencyReq',
  serverAssertionReq: 'ServerAssertionReq',
  orderStateNotify: 'OrderStateNotify'
} as const;

function startOrderWorkflowReq(
  orderId: string,
  cartId: string,
  shippingAddressId: string,
  paymentMethodId: string,
  idempotencyKey: string,
  lines: OrderLineInput[],
  amount: number,
  currency: string
): StartOrderWorkflowReq {
  return {
    orderId,
    cartId,
    shippingAddressId,
    paymentMethodId,
    idempotencyKey,
    lines,
    amount,
    currency,
    packetName: () => PacketNames.startOrderWorkflowReq
  };
}

function getOrderStateReq(orderId: string): GetOrderStateReq {
  return { orderId, packetName: () => PacketNames.getOrderStateReq };
}

function continueOrderWorkflowReq(orderId: string): ContinueOrderWorkflowReq {
  return { orderId, packetName: () => PacketNames.continueOrderWorkflowReq };
}

function rebuildOrderProjectionReq(orderId: string): RebuildOrderProjectionReq {
  return { orderId, packetName: () => PacketNames.rebuildOrderProjectionReq };
}

function deleteOrderProjectionReq(orderId: string): GetOrderStateReq {
  return { orderId, packetName: () => PacketNames.deleteOrderProjectionReq };
}

function seedPendingIdempotencyReq(
  idempotencyKey: string,
  orderId: string,
  ownerInstanceId: string
): SeedPendingIdempotencyReq {
  return { idempotencyKey, orderId, ownerInstanceId, packetName: () => PacketNames.seedPendingIdempotencyReq };
}

function serverAssertionReq(
  successfulOrderId: string,
  pendingRecoveredOrderId: string,
  inventoryFailureOrderId: string,
  paymentFailureOrderId: string,
  scaleOutOrderId: string
): ServerAssertionReq {
  return {
    successfulOrderId,
    pendingRecoveredOrderId,
    inventoryFailureOrderId,
    paymentFailureOrderId,
    scaleOutOrderId,
    packetName: () => PacketNames.serverAssertionReq
  };
}

export {
  PacketNames,
  continueOrderWorkflowReq,
  deleteOrderProjectionReq,
  getOrderStateReq,
  rebuildOrderProjectionReq,
  seedPendingIdempotencyReq,
  serverAssertionReq,
  startOrderWorkflowReq
};

export type {
  ContinueOrderWorkflowReq,
  ContinueOrderWorkflowRes,
  GetOrderStateReq,
  GetOrderStateRes,
  OrderLineInput,
  OrderState,
  OrderStatus,
  RebuildOrderProjectionReq,
  RebuildOrderProjectionRes,
  SeedPendingIdempotencyReq,
  ServerAssertionReq,
  ServerAssertionRes,
  StartOrderReq,
  StartOrderRes,
  StartOrderWorkflowReq,
  StartOrderWorkflowRes
};
