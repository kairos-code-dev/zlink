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

class FindCustomerActorReq { constructor(readonly customerId: string) {} }

type FindCustomerActorRes = {
  customerId: string;
  actorRef?: DeliveryDispatchActorRef;
};

class EnsureCustomerActorReq { constructor(readonly customerId: string) {} }

type EnsureCustomerActorRes = {
  customerId: string;
  actorRef: DeliveryDispatchActorRef;
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

class BindCourierSessionReq {
  constructor(
    readonly courierId: string,
    readonly actor?: DeliveryDispatchActorRef,
    readonly sessionRoute?: string
  ) {}
}

type BindCourierSessionRes = {
  courierId: string;
  actor: DeliveryDispatchActorRef;
  sessionRoute: string;
};

class FindCourierActorReq { constructor(readonly courierId: string) {} }

type FindCourierActorRes = {
  courierId: string;
  actor?: DeliveryDispatchActorRef;
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

class AssignDeliveryMsg {
  constructor(
    readonly deliveryId: string,
    readonly customerId: string,
    readonly pickupAddress: string,
    readonly dropoffAddress: string
  ) {}
}

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

class OfferDeliveryNotify {
  constructor(
    readonly courierId: string,
    readonly deliveryId: string,
    readonly pickupAddress: string,
    readonly dropoffAddress: string
  ) {}
}

class CourierDecisionMsg {
  constructor(
    readonly deliveryId: string,
    readonly courierId: string,
    readonly accepted: boolean,
    readonly reason?: string
  ) {}
}

class DeliveryStatusChangedReq {
  constructor(
    readonly deliveryId: string,
    readonly customerId: string,
    readonly status: Exclude<DeliveryStatus, 'Created'>,
    readonly occurredAt: string,
    readonly courierId?: string
  ) {}
}

type DeliveryStatusChangedRes = {
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

type ServerAssertionReq = {
  successfulDeliveryId: string;
  reassignedDeliveryId: string;
};

type ServerAssertionRes = {
  passed: boolean;
  evidence: string[];
};

const PacketNames = {
  assignDelivery: 'AssignDeliveryMsg',
  bindCourier: 'BindCourierReq',
  bindCourierResult: 'BindCourierRes',
  bindCourierSession: 'BindCourierSessionReq',
  bindCourierSessionResult: 'BindCourierSessionRes',
  createDeliveryRequest: 'CreateDeliveryReq',
  deliveryCreated: 'CreateDeliveryRes',
  deliveryStatusAck: 'DeliveryStatusChangedRes',
  deliveryStatusChanged: 'DeliveryStatusChangedReq',
  deliveryStatusNotify: 'DeliveryStatusNotify',
  deliveryStatusUpdated: 'DeliveryStatusUpdatedMsg',
  findCourierActor: 'FindCourierActorReq',
  courierActorFound: 'FindCourierActorRes',
  ensureCourierActor: 'EnsureCourierActorReq',
  courierActorEnsured: 'EnsureCourierActorRes',
  findCustomerActor: 'FindCustomerActorReq',
  customerActorFound: 'FindCustomerActorRes',
  ensureCustomerActor: 'EnsureCustomerActorReq',
  customerActorEnsured: 'EnsureCustomerActorRes',
  offerDelivery: 'OfferDeliveryReq',
  offerDeliveryNotify: 'OfferDeliveryNotify',
  offerDeliveryResult: 'OfferDeliveryRes',
  courierDecision: 'CourierDecisionMsg',
  serverAssertionReq: 'ServerAssertionReq',
  serverAssertionRes: 'ServerAssertionRes',
  subscribeDelivery: 'SubscribeDeliveryReq',
  subscribeDeliveryAccepted: 'SubscribeDeliveryRes'
} as const;

function assignDelivery(
  deliveryId: string,
  customerId: string,
  pickupAddress: string,
  dropoffAddress: string
): AssignDeliveryMsg {
  return new AssignDeliveryMsg(deliveryId, customerId, pickupAddress, dropoffAddress);
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
  status: DeliveryStatusChangedReq['status'],
  courierId?: string
): DeliveryStatusChangedReq {
  return new DeliveryStatusChangedReq(deliveryId, customerId, status, new Date().toISOString(), courierId);
}

function ensureCourierActor(courierId: string): EnsureCourierActorReq {
  return new EnsureCourierActorReq(courierId);
}

function findCourierActor(courierId: string): FindCourierActorReq {
  return new FindCourierActorReq(courierId);
}

function findCustomerActor(customerId: string): FindCustomerActorReq {
  return new FindCustomerActorReq(customerId);
}

function ensureCustomerActor(customerId: string): EnsureCustomerActorReq {
  return new EnsureCustomerActorReq(customerId);
}

function offerDelivery(courierId: string, deliveryId: string, pickupAddress: string, dropoffAddress: string): OfferDeliveryReq {
  return new OfferDeliveryReq(courierId, deliveryId, pickupAddress, dropoffAddress);
}

function subscribeDelivery(deliveryId: string): SubscribeDeliveryReq {
  return new SubscribeDeliveryReq(deliveryId);
}

export {
  DeliveryStatusNotify,
  OfferDeliveryNotify,
  DeliveryStatusUpdatedMsg,
  FindCustomerActorReq,
  EnsureCustomerActorReq,
  SubscribeDeliveryReq,
  BindCourierReq,
  BindCourierSessionReq,
  FindCourierActorReq,
  EnsureCourierActorReq,
  AssignDeliveryMsg,
  OfferDeliveryReq,
  DeliveryStatusChangedReq,
  CourierDecisionMsg,
  PacketNames,
  actorRefForMessage,
  actorRefFromMessage,
  assignDelivery,
  bindCourier,
  bindCourierSession,
  deliveryStatusChanged,
  ensureCourierActor,
  ensureCustomerActor,
  findCourierActor,
  findCustomerActor,
  offerDelivery,
  subscribeDelivery
};

export type {
  BindCourierRes,
  BindCourierSessionRes,
  CreateDeliveryReq,
  DeliveryDispatchActorRef,
  EnsureCustomerActorRes,
  FindCustomerActorRes,
  CreateDeliveryRes,
  DeliveryStatus,
  DeliveryStatusChangedRes,
  EnsureCourierActorRes,
  FindCourierActorRes,
  OfferDeliveryRes,
  ServerAssertionReq,
  ServerAssertionRes,
  SubscribeDeliveryRes
};
