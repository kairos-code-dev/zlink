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

type BindCourierReq = {
  courierId: string;
  sessionRoute: string;
  packetName(): string;
};

type BindCourierRes = {
  courierId: string;
  actor: ActorRefSnapshot;
  sessionRoute: string;
};

type BindCourierSessionReq = {
  courierId: string;
  packetName(): string;
};

type BindCourierSessionRes = {
  courierId: string;
  actor: ActorRefSnapshot;
  sessionRoute: string;
};

type EnsureCourierActorReq = {
  courierId: string;
  packetName(): string;
};

type EnsureCourierActorRes = {
  courierId: string;
  actor: ActorRefSnapshot;
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
  courierId: string;
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

type OfferDeliveryNotify = {
  courierId: string;
  deliveryId: string;
  pickupAddress: string;
  dropoffAddress: string;
};

type CourierDecisionMsg = {
  deliveryId: string;
  courierId: string;
  accepted: boolean;
  reason?: string;
};

type ReassignDelivery = {
  deliveryId: string;
  previousCourierId: string;
  nextCourierId: string;
  reason: string;
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
  bindCourier: 'BindCourierReq',
  bindCourierResult: 'BindCourierRes',
  bindCourierSession: 'BindCourierSessionReq',
  bindCourierSessionResult: 'BindCourierSessionRes',
  createDeliveryRequest: 'CreateDeliveryReq',
  deliveryCreated: 'CreateDeliveryRes',
  deliveryStatusAck: 'DeliveryStatusRes',
  deliveryStatusChanged: 'DeliveryStatusReq',
  deliveryStatusNotify: 'DeliveryStatusNotify',
  ensureCourierActor: 'EnsureCourierActorReq',
  courierActorEnsured: 'EnsureCourierActorRes',
  ensureCustomerActor: 'EnsureCustomerActorReq',
  customerActorEnsured: 'EnsureCustomerActorRes',
  offerDelivery: 'OfferDeliveryReq',
  offerDeliveryNotify: 'OfferDeliveryNotify',
  offerDeliveryResult: 'OfferDeliveryRes',
  courierDecision: 'CourierDecisionMsg',
  reassignDelivery: 'ReassignDelivery',
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

function bindCourier(courierId: string, sessionRoute: string): BindCourierReq {
  return { courierId, sessionRoute, packetName: () => PacketNames.bindCourier };
}

function bindCourierSession(courierId: string): BindCourierSessionReq {
  return { courierId, packetName: () => PacketNames.bindCourierSession };
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

function ensureCourierActor(courierId: string): EnsureCourierActorReq {
  return { courierId, packetName: () => PacketNames.ensureCourierActor };
}

function ensureCustomerActor(customerId: string): EnsureCustomerActorReq {
  return { customerId, packetName: () => PacketNames.ensureCustomerActor };
}

function offerDelivery(courierId: string, deliveryId: string, pickupAddress: string, dropoffAddress: string): OfferDeliveryReq {
  return { courierId, deliveryId, pickupAddress, dropoffAddress, packetName: () => PacketNames.offerDelivery };
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
  bindCourier,
  bindCourierSession,
  deliveryStatusChanged,
  ensureCourierActor,
  ensureCustomerActor,
  offerDelivery,
  subscribeCustomerToDelivery,
  subscribeDelivery
};

export type {
  ActorRefSnapshot,
  AssignDeliveryReq,
  AssignDeliveryRes,
  BindCourierReq,
  BindCourierRes,
  BindCourierSessionReq,
  BindCourierSessionRes,
  CourierDecisionMsg,
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
  EnsureCourierActorReq,
  EnsureCourierActorRes,
  EnsureCustomerActorReq,
  OfferDeliveryReq,
  OfferDeliveryNotify,
  OfferDeliveryRes,
  ReassignDelivery,
  ServerAssertionReq,
  ServerAssertionRes,
  SubscribeCustomerToDeliveryReq,
  SubscribeDeliveryReq,
  SubscribeDeliveryRes
};
