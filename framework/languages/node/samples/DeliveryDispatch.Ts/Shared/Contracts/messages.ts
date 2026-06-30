type DeliveryStatus = 'Created' | 'Assigned' | 'Accepted' | 'Reassigned' | 'PickedUp' | 'Delivered' | 'Failed';

type ActorRefSnapshot = {
  nodeRid: string;
  actorId: string;
  generation: number;
};

type CreateDeliveryReq = {
  deliveryId: string;
  customerId: string;
  pickupAddress: string;
  dropoffAddress: string;
};

type CreateDeliveryRes = {
  deliveryId: string;
};

type EnsureCustomerActorReq = {
  customerId: string;
  packetName(): string;
};

type EnsureCustomerActorRes = {
  customerId: string;
  actor: ActorRefSnapshot;
};

type SubscribeDeliveryReq = {
  deliveryId: string;
  packetName(): string;
};

type SubscribeDeliveryRes = {
  deliveryId: string;
};

type SubscribeCustomerToDeliveryReq = {
  customerId: string;
  deliveryId: string;
  packetName(): string;
};

type SubscribeCustomerToDeliveryRes = {
  customerId: string;
  deliveryId: string;
};

type AssignDeliveryReq = {
  deliveryId: string;
  customerId: string;
  pickupAddress: string;
  dropoffAddress: string;
  packetName(): string;
};

type AssignDeliveryRes = {
  deliveryId: string;
  courierId: string;
  accepted: boolean;
};

type OfferDeliveryReq = {
  deliveryId: string;
  pickupAddress: string;
  dropoffAddress: string;
  packetName(): string;
};

type OfferDeliveryRes = {
  deliveryId: string;
  courierId: string;
  accepted: boolean;
  reason?: string;
};

type DeliveryStatusReq = {
  deliveryId: string;
  status: Exclude<DeliveryStatus, 'Created'>;
  courierId?: string;
  occurredAt: string;
  packetName(): string;
};

type DeliveryStatusRes = {
  deliveryId: string;
  status: DeliveryStatus;
};

type DeliveryStatusNotify = {
  deliveryId: string;
  status: DeliveryStatus;
  courierId?: string;
  occurredAt: string;
};

type DeliverySpotCreateReq = {
  deliveryId: string;
};

type DeliverySpotCreateRes = {
  deliveryId: string;
};

type DeliverySpotJoinReq = {
  deliveryId: string;
  customerId: string;
};

type DeliverySpotJoinRes = {
  deliveryId: string;
  customerId: string;
};

type ServerAssertionReq = {
  successfulDeliveryId: string;
  reassignedDeliveryId: string;
};

type ServerAssertionRes = {
  passed: boolean;
  evidence: string[];
};

const PacketNames = {
  assignDelivery: 'AssignDeliveryReq',
  assignDeliveryResult: 'AssignDeliveryRes',
  createDeliveryRequest: 'CreateDeliveryReq',
  deliveryCreated: 'CreateDeliveryRes',
  deliveryStatusAck: 'DeliveryStatusRes',
  deliveryStatusChanged: 'DeliveryStatusReq',
  deliveryStatusNotify: 'DeliveryStatusNotify',
  ensureCustomerActor: 'EnsureCustomerActorReq',
  customerActorEnsured: 'EnsureCustomerActorRes',
  offerDelivery: 'OfferDeliveryReq',
  offerDeliveryResult: 'OfferDeliveryRes',
  serverAssertionReq: 'ServerAssertionReq',
  serverAssertionRes: 'ServerAssertionRes',
  subscribeCustomerToDelivery: 'SubscribeCustomerToDeliveryReq',
  customerDeliverySubscribed: 'SubscribeCustomerToDeliveryRes',
  subscribeDelivery: 'SubscribeDeliveryReq',
  subscribeDeliveryAccepted: 'SubscribeDeliveryRes'
} as const;

function assignDelivery(
  deliveryId: string,
  customerId: string,
  pickupAddress: string,
  dropoffAddress: string
): AssignDeliveryReq {
  return { deliveryId, customerId, pickupAddress, dropoffAddress, packetName: () => PacketNames.assignDelivery };
}

function deliveryStatusChanged(
  deliveryId: string,
  status: DeliveryStatusReq['status'],
  courierId?: string
): DeliveryStatusReq {
  return {
    deliveryId,
    status,
    courierId,
    occurredAt: new Date().toISOString(),
    packetName: () => PacketNames.deliveryStatusChanged
  };
}

function ensureCustomerActor(customerId: string): EnsureCustomerActorReq {
  return { customerId, packetName: () => PacketNames.ensureCustomerActor };
}

function offerDelivery(deliveryId: string, pickupAddress: string, dropoffAddress: string): OfferDeliveryReq {
  return { deliveryId, pickupAddress, dropoffAddress, packetName: () => PacketNames.offerDelivery };
}

function subscribeCustomerToDelivery(customerId: string, deliveryId: string): SubscribeCustomerToDeliveryReq {
  return { customerId, deliveryId, packetName: () => PacketNames.subscribeCustomerToDelivery };
}

function subscribeDelivery(deliveryId: string): SubscribeDeliveryReq {
  return { deliveryId, packetName: () => PacketNames.subscribeDelivery };
}

export {
  PacketNames,
  assignDelivery,
  deliveryStatusChanged,
  ensureCustomerActor,
  offerDelivery,
  subscribeCustomerToDelivery,
  subscribeDelivery
};

export type {
  ActorRefSnapshot,
  AssignDeliveryReq,
  AssignDeliveryRes,
  CreateDeliveryReq,
  EnsureCustomerActorRes,
  SubscribeCustomerToDeliveryRes,
  CreateDeliveryRes,
  DeliverySpotCreateReq,
  DeliverySpotCreateRes,
  DeliverySpotJoinReq,
  DeliverySpotJoinRes,
  DeliveryStatus,
  DeliveryStatusRes,
  DeliveryStatusReq,
  DeliveryStatusNotify,
  EnsureCustomerActorReq,
  OfferDeliveryReq,
  OfferDeliveryRes,
  ServerAssertionReq,
  ServerAssertionRes,
  SubscribeCustomerToDeliveryReq,
  SubscribeDeliveryReq,
  SubscribeDeliveryRes
};
