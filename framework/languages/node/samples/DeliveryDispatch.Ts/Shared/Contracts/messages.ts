type DeliveryStatus = 'Created' | 'Assigned' | 'Accepted' | 'Reassigned' | 'PickedUp' | 'Delivered' | 'Failed';

type ActorRefSnapshot = {
  nodeRid: string;
  actorId: string;
  generation: number;
};

type CreateDeliveryRequest = {
  deliveryId: string;
  customerId: string;
  pickupAddress: string;
  dropoffAddress: string;
};

type DeliveryCreated = {
  deliveryId: string;
};

type EnsureCustomerActor = {
  customerId: string;
  packetName(): string;
};

type CustomerActorEnsured = {
  customerId: string;
  actor: ActorRefSnapshot;
};

type SubscribeDelivery = {
  deliveryId: string;
  packetName(): string;
};

type SubscribeDeliveryAccepted = {
  deliveryId: string;
};

type SubscribeCustomerToDelivery = {
  customerId: string;
  deliveryId: string;
  packetName(): string;
};

type CustomerDeliverySubscribed = {
  customerId: string;
  deliveryId: string;
};

type AssignDelivery = {
  deliveryId: string;
  customerId: string;
  pickupAddress: string;
  dropoffAddress: string;
  packetName(): string;
};

type AssignDeliveryResult = {
  deliveryId: string;
  courierId: string;
  accepted: boolean;
};

type OfferDelivery = {
  deliveryId: string;
  pickupAddress: string;
  dropoffAddress: string;
  packetName(): string;
};

type OfferDeliveryResult = {
  deliveryId: string;
  courierId: string;
  accepted: boolean;
  reason?: string;
};

type DeliveryStatusChanged = {
  deliveryId: string;
  status: Exclude<DeliveryStatus, 'Created'>;
  courierId?: string;
  occurredAt: string;
  packetName(): string;
};

type DeliveryStatusAck = {
  deliveryId: string;
  status: DeliveryStatus;
};

type DeliveryStatusNotify = {
  deliveryId: string;
  status: DeliveryStatus;
  courierId?: string;
  occurredAt: string;
};

type DeliverySpotCreate = {
  deliveryId: string;
};

type DeliverySpotCreated = {
  deliveryId: string;
};

type DeliverySpotJoin = {
  deliveryId: string;
  customerId: string;
};

type DeliverySpotJoined = {
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
  assignDelivery: 'AssignDelivery',
  assignDeliveryResult: 'AssignDeliveryResult',
  createDeliveryRequest: 'CreateDeliveryRequest',
  deliveryCreated: 'DeliveryCreated',
  deliveryStatusAck: 'DeliveryStatusAck',
  deliveryStatusChanged: 'DeliveryStatusChanged',
  deliveryStatusNotify: 'DeliveryStatusNotify',
  ensureCustomerActor: 'EnsureCustomerActor',
  customerActorEnsured: 'CustomerActorEnsured',
  offerDelivery: 'OfferDelivery',
  offerDeliveryResult: 'OfferDeliveryResult',
  serverAssertionReq: 'ServerAssertionReq',
  serverAssertionRes: 'ServerAssertionRes',
  subscribeCustomerToDelivery: 'SubscribeCustomerToDelivery',
  customerDeliverySubscribed: 'CustomerDeliverySubscribed',
  subscribeDelivery: 'SubscribeDelivery',
  subscribeDeliveryAccepted: 'SubscribeDeliveryAccepted'
} as const;

function assignDelivery(
  deliveryId: string,
  customerId: string,
  pickupAddress: string,
  dropoffAddress: string
): AssignDelivery {
  return { deliveryId, customerId, pickupAddress, dropoffAddress, packetName: () => PacketNames.assignDelivery };
}

function deliveryStatusChanged(
  deliveryId: string,
  status: DeliveryStatusChanged['status'],
  courierId?: string
): DeliveryStatusChanged {
  return {
    deliveryId,
    status,
    courierId,
    occurredAt: new Date().toISOString(),
    packetName: () => PacketNames.deliveryStatusChanged
  };
}

function ensureCustomerActor(customerId: string): EnsureCustomerActor {
  return { customerId, packetName: () => PacketNames.ensureCustomerActor };
}

function offerDelivery(deliveryId: string, pickupAddress: string, dropoffAddress: string): OfferDelivery {
  return { deliveryId, pickupAddress, dropoffAddress, packetName: () => PacketNames.offerDelivery };
}

function subscribeCustomerToDelivery(customerId: string, deliveryId: string): SubscribeCustomerToDelivery {
  return { customerId, deliveryId, packetName: () => PacketNames.subscribeCustomerToDelivery };
}

function subscribeDelivery(deliveryId: string): SubscribeDelivery {
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
  AssignDelivery,
  AssignDeliveryResult,
  CreateDeliveryRequest,
  CustomerActorEnsured,
  CustomerDeliverySubscribed,
  DeliveryCreated,
  DeliverySpotCreate,
  DeliverySpotCreated,
  DeliverySpotJoin,
  DeliverySpotJoined,
  DeliveryStatus,
  DeliveryStatusAck,
  DeliveryStatusChanged,
  DeliveryStatusNotify,
  EnsureCustomerActor,
  OfferDelivery,
  OfferDeliveryResult,
  ServerAssertionReq,
  ServerAssertionRes,
  SubscribeCustomerToDelivery,
  SubscribeDelivery,
  SubscribeDeliveryAccepted
};
