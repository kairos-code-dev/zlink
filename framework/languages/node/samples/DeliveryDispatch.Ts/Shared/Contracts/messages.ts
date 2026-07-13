import type { ActorRef, RoutingId } from '@zlink-systems/framework';

type DeliveryStatus = 'Created' | 'Assigned' | 'Accepted' | 'Reassigned' | 'PickedUp' | 'Delivered' | 'Failed';

type CreateDeliveryReq = {
  deliveryId: string;
  customerId: string;
  pickupAddress: string;
  dropoffAddress: string;
};

type CreateDeliveryRes = {
  deliveryId: string;
};

class EnsureCustomerActorReq { constructor(readonly customerId: string) {} }

type EnsureCustomerActorRes = {
  customerId: string;
  actor: DeliveryDispatchActorRef;
};

class SubscribeDeliveryReq { constructor(readonly deliveryId: string) {} }

type SubscribeDeliveryRes = {
  deliveryId: string;
};

class BindCourierReq { constructor(readonly courierId: string, readonly sessionRoute: string) {} }

type BindCourierRes = {
  courierId: string;
  actor: DeliveryDispatchActorRef;
  sessionRoute: string;
};

class BindCourierSessionReq { constructor(readonly courierId: string) {} }

type BindCourierSessionRes = {
  courierId: string;
  actor: DeliveryDispatchActorRef;
  sessionRoute: string;
};

class EnsureCourierActorReq { constructor(readonly courierId: string) {} }

type EnsureCourierActorRes = {
  courierId: string;
  actor: DeliveryDispatchActorRef;
};

type DeliveryDispatchActorRef = {
  nodeRid: string;
  actorId: string;
  generation: number;
};

class SubscribeCustomerToDeliveryReq { constructor(readonly customerId: string, readonly deliveryId: string) {} }

type SubscribeCustomerToDeliveryRes = {
  customerId: string;
  deliveryId: string;
};

class AssignDeliveryReq {
  constructor(
    readonly deliveryId: string,
    readonly customerId: string,
    readonly pickupAddress: string,
    readonly dropoffAddress: string
  ) {}
}

type AssignDeliveryRes = {
  deliveryId: string;
  courierId: string;
  accepted: boolean;
};

class OfferDeliveryReq {
  constructor(
    readonly courierId: string,
    readonly deliveryId: string,
    readonly pickupAddress: string,
    readonly dropoffAddress: string
  ) {}
}

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

class DeliveryStatusReq {
  constructor(
    readonly deliveryId: string,
    readonly customerId: string,
    readonly status: Exclude<DeliveryStatus, 'Created'>,
    readonly occurredAt: string,
    readonly courierId?: string
  ) {}
}

type DeliveryStatusRes = {
  deliveryId: string;
  status: DeliveryStatus;
};

class DeliveryStatusNotify {
  constructor(
    readonly deliveryId: string,
    readonly status: DeliveryStatus,
    readonly occurredAt: string,
    readonly courierId?: string
  ) {}
}

class DeliveryStatusUpdatedMsg {
  constructor(
    readonly deliveryId: string,
    readonly customerId: string,
    readonly status: Exclude<DeliveryStatus, 'Created'>,
    readonly occurredAt: string,
    readonly courierId?: string
  ) {}
}

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
  deliveryStatusUpdated: 'DeliveryStatusUpdatedMsg',
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
  return new AssignDeliveryReq(deliveryId, customerId, pickupAddress, dropoffAddress);
}

function bindCourier(courierId: string, sessionRoute: string): BindCourierReq {
  return new BindCourierReq(courierId, sessionRoute);
}

function bindCourierSession(courierId: string): BindCourierSessionReq {
  return new BindCourierSessionReq(courierId);
}

function actorRefForMessage(actor: ActorRef): DeliveryDispatchActorRef {
  return {
    nodeRid: String(actor.nodeRid),
    actorId: actor.actorId,
    generation: Number(actor.generation)
  };
}

function actorRefFromMessage(actor: DeliveryDispatchActorRef): ActorRef {
  return {
    nodeRid: actor.nodeRid as unknown as RoutingId,
    actorId: actor.actorId,
    generation: BigInt(actor.generation)
  };
}

function deliveryStatusChanged(
  deliveryId: string,
  customerId: string,
  status: DeliveryStatusReq['status'],
  courierId?: string
): DeliveryStatusReq {
  return new DeliveryStatusReq(deliveryId, customerId, status, new Date().toISOString(), courierId);
}

function ensureCourierActor(courierId: string): EnsureCourierActorReq {
  return new EnsureCourierActorReq(courierId);
}

function ensureCustomerActor(customerId: string): EnsureCustomerActorReq {
  return new EnsureCustomerActorReq(customerId);
}

function offerDelivery(courierId: string, deliveryId: string, pickupAddress: string, dropoffAddress: string): OfferDeliveryReq {
  return new OfferDeliveryReq(courierId, deliveryId, pickupAddress, dropoffAddress);
}

function subscribeCustomerToDelivery(customerId: string, deliveryId: string): SubscribeCustomerToDeliveryReq {
  return new SubscribeCustomerToDeliveryReq(customerId, deliveryId);
}

function subscribeDelivery(deliveryId: string): SubscribeDeliveryReq {
  return new SubscribeDeliveryReq(deliveryId);
}

export {
  DeliveryStatusNotify,
  DeliveryStatusUpdatedMsg,
  EnsureCustomerActorReq,
  SubscribeDeliveryReq,
  BindCourierReq,
  BindCourierSessionReq,
  EnsureCourierActorReq,
  SubscribeCustomerToDeliveryReq,
  AssignDeliveryReq,
  OfferDeliveryReq,
  DeliveryStatusReq,
  PacketNames,
  actorRefForMessage,
  actorRefFromMessage,
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
  AssignDeliveryRes,
  BindCourierRes,
  BindCourierSessionRes,
  CourierDecisionMsg,
  CreateDeliveryReq,
  DeliveryDispatchActorRef,
  EnsureCustomerActorRes,
  SubscribeCustomerToDeliveryRes,
  CreateDeliveryRes,
  DeliverySpotCreateReq,
  DeliverySpotCreateRes,
  DeliverySpotJoinReq,
  DeliverySpotJoinRes,
  DeliveryStatus,
  DeliveryStatusRes,
  EnsureCourierActorRes,
  OfferDeliveryNotify,
  OfferDeliveryRes,
  ReassignDelivery,
  ServerAssertionReq,
  ServerAssertionRes,
  SubscribeDeliveryRes
};
