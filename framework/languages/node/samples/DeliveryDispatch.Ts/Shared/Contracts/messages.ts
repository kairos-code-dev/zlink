type DeliveryStatus = 'Created' | 'Assigned' | 'Accepted' | 'Reassigned' | 'PickedUp' | 'Delivered' | 'Failed';

type DeliveryRecord = {
  deliveryId: string;
  customerId: string;
  pickupAddress: string;
  dropoffAddress: string;
  courierId?: string;
  status: DeliveryStatus;
};

type CreateDeliveryReq = {
  deliveryId: string;
  customerId: string;
  pickupAddress: string;
  dropoffAddress: string;
  packetName(): string;
};

type DeliveryCreated = {
  deliveryId: string;
};

type SubscribeDeliveryReq = {
  deliveryId: string;
  packetName(): string;
};

type SubscribeDeliveryAccepted = {
  deliveryId: string;
};

type AssignDeliveryReq = {
  deliveryId: string;
  courierId: string;
  packetName(): string;
};

type AdvanceDeliveryReq = {
  deliveryId: string;
  status: Exclude<DeliveryStatus, 'Created'>;
  courierId?: string;
  packetName(): string;
};

type DeliveryStatusNotify = {
  deliveryId: string;
  status: DeliveryStatus;
  courierId?: string;
  occurredAt: string;
};

type ServerAssertionReq = {
  successfulDeliveryId: string;
  reassignedDeliveryId: string;
  packetName(): string;
};

type ServerAssertionRes = {
  passed: boolean;
  evidence: string[];
};

const PacketNames = {
  createDeliveryReq: 'CreateDeliveryReq',
  deliveryCreated: 'DeliveryCreated',
  subscribeDeliveryReq: 'SubscribeDelivery',
  subscribeDeliveryAccepted: 'SubscribeDeliveryAccepted',
  assignDeliveryReq: 'AssignDeliveryReq',
  advanceDeliveryReq: 'AdvanceDeliveryReq',
  deliveryStatusNotify: 'DeliveryStatusNotify',
  serverAssertionReq: 'ServerAssertionReq',
  serverAssertionRes: 'ServerAssertionRes'
} as const;

function createDeliveryReq(
  deliveryId: string,
  customerId: string,
  pickupAddress: string,
  dropoffAddress: string
): CreateDeliveryReq {
  return { deliveryId, customerId, pickupAddress, dropoffAddress, packetName: () => PacketNames.createDeliveryReq };
}

function subscribeDeliveryReq(deliveryId: string): SubscribeDeliveryReq {
  return { deliveryId, packetName: () => PacketNames.subscribeDeliveryReq };
}

function assignDeliveryReq(deliveryId: string, courierId: string): AssignDeliveryReq {
  return { deliveryId, courierId, packetName: () => PacketNames.assignDeliveryReq };
}

function advanceDeliveryReq(
  deliveryId: string,
  status: AdvanceDeliveryReq['status'],
  courierId?: string
): AdvanceDeliveryReq {
  return { deliveryId, status, courierId, packetName: () => PacketNames.advanceDeliveryReq };
}

function serverAssertionReq(successfulDeliveryId: string, reassignedDeliveryId: string): ServerAssertionReq {
  return { successfulDeliveryId, reassignedDeliveryId, packetName: () => PacketNames.serverAssertionReq };
}

export {
  PacketNames,
  advanceDeliveryReq,
  assignDeliveryReq,
  createDeliveryReq,
  serverAssertionReq,
  subscribeDeliveryReq
};

export type {
  AdvanceDeliveryReq,
  AssignDeliveryReq,
  CreateDeliveryReq,
  DeliveryCreated,
  DeliveryRecord,
  DeliveryStatus,
  DeliveryStatusNotify,
  ServerAssertionReq,
  ServerAssertionRes,
  SubscribeDeliveryAccepted,
  SubscribeDeliveryReq
};
