type CheckoutStatus = 'CartCreated' | 'PaymentAuthorized' | 'InventoryReserved' | 'OrderConfirmed';

type CheckoutState = {
  checkoutId: string;
  customerId: string;
  status: CheckoutStatus;
};

type StartCheckoutReq = {
  customerId: string;
  packetName(): string;
};

type CheckoutStepReq = {
  checkoutId: string;
  packetName(): string;
};

const PacketNames = {
  startCheckoutReq: 'StartCheckoutReq',
  authorizePaymentReq: 'AuthorizePaymentReq',
  reserveInventoryReq: 'ReserveInventoryReq',
  confirmOrderReq: 'ConfirmOrderReq',
  checkoutStateNotify: 'CheckoutStateNotify'
} as const;

function startCheckoutReq(customerId: string): StartCheckoutReq {
  return { customerId, packetName: () => PacketNames.startCheckoutReq };
}

function authorizePaymentReq(checkoutId: string): CheckoutStepReq {
  return { checkoutId, packetName: () => PacketNames.authorizePaymentReq };
}

function reserveInventoryReq(checkoutId: string): CheckoutStepReq {
  return { checkoutId, packetName: () => PacketNames.reserveInventoryReq };
}

function confirmOrderReq(checkoutId: string): CheckoutStepReq {
  return { checkoutId, packetName: () => PacketNames.confirmOrderReq };
}

export {
  PacketNames,
  authorizePaymentReq,
  confirmOrderReq,
  reserveInventoryReq,
  startCheckoutReq
};

export type {
  CheckoutStepReq,
  CheckoutState,
  CheckoutStatus,
  StartCheckoutReq
};
