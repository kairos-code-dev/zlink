import { RequestResult, SubmitResult } from '@zlink-systems/zlink';
import type {
  RawServiceIngressRecord,
  RawServiceMeshRuntime,
  RawServicePumpResult
} from './raw-service-mesh-runtime';
import {
  ActorLifecycleKind,
  OperationKind,
  ReceiveKind,
  type ActorControlPayload,
  type ActorJoinCompletionPayload,
  type ActorLocation,
  type ReceiveKindData
} from './service-runtime-contracts';
import type { ServiceMailboxRecord } from './service-mailbox';
import {
  ServiceStaleGenerationError,
  ServiceStatefulRegistry,
  ServiceTerminalOperationRegistry,
  type ServiceActorRef,
  type ServiceActorState,
  type ServiceSessionBinding,
  type ServiceSpotRef,
  type ServiceSpotState
} from './service-stateful-registry';
import {
  decodeStatefulHeader,
  decodeStatefulReply,
  encodeActorDestroyHeader,
  encodeActorHeader,
  encodeActorJoinHeader,
  encodeActorLookupHeader,
  encodeBoundSessionBindHeader,
  encodeBoundSessionSendHeader,
  encodeInstanceSpotActivationHeader,
  encodeInstanceSpotHeader,
  encodeLogicalMulticastHeader,
  encodeSpotHeader,
  encodeStatefulReply,
  M6bServiceWireCommand,
  sessionBindingFromWire,
  type ServiceActorRouteFence,
  type ServiceInstanceActivationTarget,
  type ServiceInstanceRouteFence,
  type ServiceSpotRouteFence,
  type ServiceStatefulReplyTail,
  type ServiceStatefulWireRecord
} from './service-stateful-wire-codec';
import {
  decodeApplicationPayload,
  encodeApplicationPayload,
  type ServiceApplicationPayload,
  ServiceWireProtocolError
} from './service-wire-m6a-codec';

const ACTOR_ROUTE_STALE = 21;
const ACTOR_ROUTE_NOT_FOUND = 1;

export interface ServiceStatefulResult {
  readonly terminalResult: number;
  readonly failureCode: number;
  readonly payload?: ServiceApplicationPayload;
  readonly kindData?: ReceiveKindData;
}

export interface ServiceStatefulPendingOperation {
  readonly id: bigint;
  readonly promise: Promise<ServiceStatefulResult>;
}

export interface ServiceStatefulMailboxData {
  readonly correlation?: bigint;
  readonly receiveKind: number;
  readonly operationKind: number;
  readonly sourceSpotRid?: string;
  readonly sourceActor?: ServiceActorRef;
  readonly sourceBindingGeneration?: bigint;
  readonly channelName?: string;
  readonly topic?: string;
  readonly targetSpot?: ServiceSpotRef;
  readonly targetActor?: ServiceActorRef;
  readonly kindData?: ReceiveKindData;
  readonly reply?: (
    terminalResult: number,
    failureCode: number,
    payload?: ServiceApplicationPayload,
    tail?: ServiceStatefulReplyTail
  ) => boolean;
}

export interface ServiceSessionDelivery {
  readonly binding: ServiceSessionBinding;
  readonly deliver: (sessionRid: string, payload: ServiceApplicationPayload) => boolean;
}

interface ServiceInstanceIntent {
  readonly instanceType: string;
  readonly route: ServiceInstanceRouteFence;
}

export interface ServiceInstanceActivationReservation {
  readonly attempt: bigint;
  readonly token: string;
}

export type ServiceInstanceAuthorityRead =
  | { readonly kind: 'missing' }
  | { readonly kind: 'ready'; readonly route: ServiceInstanceRouteFence };

export type ServiceInstanceAuthorityReserve =
  | { readonly kind: 'reserved'; readonly reservation: ServiceInstanceActivationReservation }
  | { readonly kind: 'ready'; readonly route: ServiceInstanceRouteFence };

/**
 * Synchronous authority port used by the raw ingress turn. An asynchronous
 * provider must complete outside this turn and re-enter with its result.
 */
export interface ServiceInstanceActivationAuthority {
  read(target: ServiceInstanceActivationTarget): ServiceInstanceAuthorityRead;
  reserve(
    target: ServiceInstanceActivationTarget,
    operation: { readonly high: bigint; readonly low: bigint },
    deadlineUnixMs: bigint
  ): ServiceInstanceAuthorityReserve;
  commit(
    target: ServiceInstanceActivationTarget,
    reservation: ServiceInstanceActivationReservation,
    spot: ServiceSpotState
  ): { readonly kind: 'committed' | 'lost'; readonly route: ServiceInstanceRouteFence };
  abort(target: ServiceInstanceActivationTarget, reservation: ServiceInstanceActivationReservation): void;
}

/**
 * Promise-based authority port used by real Location Store providers. Raw
 * ingress retains the envelope and resumes activation after each Store
 * operation completes; the binding callback itself never blocks.
 */
export interface ServiceAsyncInstanceActivationAuthority {
  read(target: ServiceInstanceActivationTarget): Promise<ServiceInstanceAuthorityRead>;
  reserve(
    target: ServiceInstanceActivationTarget,
    operation: { readonly high: bigint; readonly low: bigint },
    deadlineUnixMs: bigint
  ): Promise<ServiceInstanceAuthorityReserve>;
  commit(
    target: ServiceInstanceActivationTarget,
    reservation: ServiceInstanceActivationReservation,
    spot: ServiceSpotState
  ): Promise<{ readonly kind: 'committed' | 'lost'; readonly route: ServiceInstanceRouteFence }>;
  abort(
    target: ServiceInstanceActivationTarget,
    reservation: ServiceInstanceActivationReservation
  ): Promise<void>;
}

export class ServiceInstanceActivationRedirectError extends Error {
  constructor(readonly route: ServiceInstanceRouteFence) {
    super(`Instance Spot '${route.targetSpotRid}' is owned by '${route.targetNodeRid}'.`);
    this.name = 'ServiceInstanceActivationRedirectError';
  }
}

export interface ServiceLogicalMulticastDetail {
  readonly snapshotRemoteTargetCount: number;
  readonly admittedRemoteTargetCount: number;
  readonly droppedRemoteTargetCount: number;
  readonly unreachableRemoteTargetCount: number;
  readonly snapshotLocalSpotCount: number;
  readonly admittedLocalSpotCount: number;
  readonly droppedLocalSpotCount: number;
}

/**
 * Owns M6B routing, identity fences and terminal operations. Application turns
 * remain in the common mailbox, where a claimed Spot or Actor owner is serial.
 */
export class ServiceStatefulRuntime {
  readonly registry: ServiceStatefulRegistry;

  private readonly operations = new ServiceTerminalOperationRegistry<ServiceStatefulResult>();
  private readonly sessionDeliveries = new Map<string, ServiceSessionDelivery>();
  private readonly subscriptions = new Map<string, Set<string>>();
  private readonly instanceIntents = new Map<string, ServiceInstanceIntent>();
  private readonly admittedInstanceOperations = new Map<string, bigint>();
  private readonly actorRoutes = new Map<string, ServiceActorRouteFence>();
  private readonly spotRoutes = new Map<string, ServiceSpotRouteFence>();
  private nextSpotId = 1n;
  private nextSessionSequence = 1n;
  private nextInstanceOperation = 1n;
  private instanceAuthority?: ServiceInstanceActivationAuthority;
  private asyncInstanceAuthority?: ServiceAsyncInstanceActivationAuthority;
  private readonly pendingInstanceActivations = new Map<string, Promise<ServiceSpotState>>();
  private closed = false;

  constructor(
    private readonly raw: RawServiceMeshRuntime,
    readonly nodeRid: string,
    readonly nodeGeneration: bigint
  ) {
    this.registry = new ServiceStatefulRegistry(nodeRid, nodeGeneration);
    this.registry.createEntrySpot(nodeRid);
    raw.setServiceIngress(record => this.ingress(record));
  }

  createSpot(routingId?: string, kind: 'user' | 'instance' = 'user', stableType = kind): ServiceSpotState {
    this.requireOpen();
    return this.registry.createSpot(
      routingId ?? `spot-${this.nodeRid}-${this.nextSpotId++}`,
      kind,
      stableType
    );
  }

  entrySpot(): ServiceSpotState {
    return this.registry.createEntrySpot(this.nodeRid);
  }

  createActor(actorId: string, stableType = 'actor'): ServiceActorState {
    this.requireOpen();
    return this.registry.createActor(actorId, stableType);
  }

  activateInstanceSpot(
    spotRid: string,
    instanceType: string,
    attempt: bigint
  ): { readonly spot: ServiceSpotState; readonly created: boolean } {
    this.requireOpen();
    const result = this.registry.reserve('instanceSpot', spotRid, instanceType, attempt);
    if (result.kind === 'existing') {
      if (result.spot === undefined) throw new Error('Instance Spot reservation lost its Spot state.');
      return { spot: result.spot, created: false };
    }
    if (result.kind === 'typeMismatch') {
      throw new TypeError(`Instance Spot '${spotRid}' is assigned to another type.`);
    }
    if (result.kind === 'attemptStale') {
      throw new ServiceStaleGenerationError('spot', spotRid);
    }
    const spot = this.registry.commitReservation(result.reservation);
    if (!('kind' in spot) || spot.kind !== 'instance') {
      throw new Error('Instance Spot reservation produced another object kind.');
    }
    return { spot, created: true };
  }

  registerInstanceIntent(instanceType: string, route: ServiceInstanceRouteFence): void {
    if (route.targetNodeRid !== this.nodeRid || route.targetNodeGeneration !== this.nodeGeneration) {
      throw new ServiceStaleGenerationError('spot', route.targetSpotRid);
    }
    const current = this.instanceIntents.get(route.targetSpotRid);
    if (
      current !== undefined
      && (current.instanceType !== instanceType || !sameInstanceRoute(current.route, route))
    ) {
      throw new ServiceStaleGenerationError('spot', route.targetSpotRid);
    }
    this.instanceIntents.set(route.targetSpotRid, Object.freeze({ instanceType, route: { ...route } }));
  }

  registerInstanceActivationAuthority(authority: ServiceInstanceActivationAuthority): void {
    this.requireOpen();
    if (
      this.asyncInstanceAuthority !== undefined
      || this.instanceAuthority !== undefined && this.instanceAuthority !== authority
    ) {
      throw new Error('Instance activation authority is already registered.');
    }
    this.instanceAuthority = authority;
  }

  registerAsyncInstanceActivationAuthority(
    authority: ServiceAsyncInstanceActivationAuthority
  ): void {
    this.requireOpen();
    if (
      this.instanceAuthority !== undefined
      || this.asyncInstanceAuthority !== undefined
        && this.asyncInstanceAuthority !== authority
    ) {
      throw new Error('Instance activation authority is already registered.');
    }
    this.asyncInstanceAuthority = authority;
  }

  rememberActorRoute(route: ServiceActorRouteFence): void {
    if (route.actor.nodeRid.length === 0) {
      throw new TypeError('Actor route requires a target node.');
    }
    this.actorRoutes.set(actorKey(route.actor), Object.freeze({
      actor: Object.freeze({ ...route.actor }),
      targetNodeGeneration: route.targetNodeGeneration,
      authorityOwnerGeneration: route.authorityOwnerGeneration
    }));
  }

  rememberSpotRoute(route: ServiceSpotRouteFence): void {
    this.spotRoutes.set(spotKey(route.spot), Object.freeze({
      spot: Object.freeze({ ...route.spot }),
      targetNodeRid: route.targetNodeRid,
      targetNodeGeneration: route.targetNodeGeneration,
      authorityOwnerGeneration: route.authorityOwnerGeneration
    }));
  }

  actor(actorId: string): ServiceActorState | undefined {
    return this.registry.actor(actorId);
  }

  setSubscription(spot: ServiceSpotState, channelName: string, topicFilter: string): void {
    this.registry.requireSpot(spot.ref);
    if (spot.kind === 'instance') {
      throw new Error('Instance Spots do not support logical multicast subscriptions.');
    }
    const key = subscriptionKey(channelName, topicFilter);
    const subscriptions = this.subscriptions.get(spot.ref.spotRid) ?? new Set<string>();
    subscriptions.add(key);
    this.subscriptions.set(spot.ref.spotRid, subscriptions);
  }

  unsetSubscription(spot: ServiceSpotState, channelName: string, topicFilter: string): void {
    const subscriptions = this.subscriptions.get(spot.ref.spotRid);
    if (subscriptions === undefined) return;
    subscriptions.delete(subscriptionKey(channelName, topicFilter));
    if (subscriptions.size === 0) this.subscriptions.delete(spot.ref.spotRid);
  }

  clearSubscriptions(spotRid: string): void {
    this.subscriptions.delete(spotRid);
  }

  publishLogicalMulticast(
    channelName: string,
    topic: string,
    payload: ServiceApplicationPayload,
    sourceSpotRid = this.nodeRid
  ): ServiceLogicalMulticastDetail {
    const local = this.enqueueLogicalMulticast(channelName, topic, sourceSpotRid, payload);
    const targets = this.raw.topology.peers()
      .filter(peer => peer.descriptor.channels.some(channel => channel.name === channelName));
    let admittedRemoteTargetCount = 0;
    let unreachableRemoteTargetCount = 0;
    const header = encodeLogicalMulticastHeader(channelName, topic, sourceSpotRid);
    const payloadFrame = encodeApplicationPayload(payload);
    for (const target of targets) {
      if (this.raw.sendService(target.descriptor.nodeRoutingId, [header, payloadFrame])) {
        admittedRemoteTargetCount++;
      } else {
        unreachableRemoteTargetCount++;
      }
    }
    return {
      snapshotRemoteTargetCount: targets.length,
      admittedRemoteTargetCount,
      droppedRemoteTargetCount: targets.length - admittedRemoteTargetCount,
      unreachableRemoteTargetCount,
      snapshotLocalSpotCount: local.snapshot,
      admittedLocalSpotCount: local.admitted,
      droppedLocalSpotCount: local.snapshot - local.admitted
    };
  }

  sendToSpot(
    sourceSpotRid: string,
    targetNodeRid: string,
    targetSpot: ServiceSpotRef,
    _targetNodeGeneration: bigint,
    _authorityOwnerGeneration: bigint,
    payload: ServiceApplicationPayload
  ): number {
    const target = this.trySpotFence(targetNodeRid, targetSpot);
    if (target === undefined) return SubmitResult.NotFound;
    const header = encodeSpotHeader('spotSend', sourceSpotRid, target);
    return this.submitOneWay(targetNodeRid, [header, encodeApplicationPayload(payload)]);
  }

  requestToSpot(
    sourceSpotRid: string,
    targetNodeRid: string,
    targetSpot: ServiceSpotRef,
    _targetNodeGeneration: bigint,
    _authorityOwnerGeneration: bigint,
    payload: ServiceApplicationPayload,
    timeoutMs: number
  ): ServiceStatefulPendingOperation {
    const pending = this.operations.reserve(timeoutMs);
    const target = this.trySpotFence(targetNodeRid, targetSpot);
    if (target === undefined) {
      this.operations.reply(pending.id, {
        terminalResult: RequestResult.NotFound,
        failureCode: ACTOR_ROUTE_STALE
      });
      return pending;
    }
    const header = encodeSpotHeader('spotRequest', sourceSpotRid, target, pending.id);
    this.submitRequest(
      pending,
      targetNodeRid,
      [header, encodeApplicationPayload(payload)],
      timeoutMs,
      'spotRequest'
    );
    return pending;
  }

  sendToActor(
    target: ServiceActorRef,
    _targetNodeGeneration: bigint,
    _authorityOwnerGeneration: bigint,
    payload: ServiceApplicationPayload,
    sourceActor?: ServiceActorRef,
    boundSession?: { readonly sessionRid: string; readonly bindingGeneration: bigint }
  ): number {
    const route = this.tryActorFence(target);
    if (route === undefined) return SubmitResult.NotFound;
    const header = encodeActorHeader(
      'actorSend',
      route,
      undefined,
      sourceActor,
      boundSession === undefined
        ? undefined
        : {
            ...boundSession,
            sequence: this.nextSessionSequence++
          }
    );
    return this.submitOneWay(target.nodeRid, [header, encodeApplicationPayload(payload)]);
  }

  requestToActor(
    target: ServiceActorRef,
    _targetNodeGeneration: bigint,
    _authorityOwnerGeneration: bigint,
    payload: ServiceApplicationPayload,
    timeoutMs: number,
    sourceActor?: ServiceActorRef,
    boundSession?: { readonly sessionRid: string; readonly bindingGeneration: bigint }
  ): ServiceStatefulPendingOperation {
    const pending = this.operations.reserve(timeoutMs);
    const route = this.tryActorFence(target);
    if (route === undefined) {
      this.operations.reply(pending.id, {
        terminalResult: RequestResult.NotFound,
        failureCode: ACTOR_ROUTE_STALE
      });
      return pending;
    }
    const header = encodeActorHeader(
      'actorRequest',
      route,
      pending.id,
      sourceActor,
      boundSession === undefined
        ? undefined
        : {
            ...boundSession,
            sequence: this.nextSessionSequence++
          }
    );
    this.submitRequest(
      pending,
      target.nodeRid,
      [header, encodeApplicationPayload(payload)],
      timeoutMs,
      'actorRequest'
    );
    return pending;
  }

  sendToInstanceSpot(
    route: ServiceInstanceRouteFence,
    payload: ServiceApplicationPayload,
    sourceSpotRid?: string
  ): number {
    return this.submitOneWay(route.targetNodeRid, [
      encodeInstanceSpotHeader(
        route,
        this.nodeGeneration,
        this.nodeRid,
        sourceSpotRid,
        'send',
        { high: 0n, low: 0n }
      ),
      encodeApplicationPayload(payload)
    ]);
  }

  sendToMissingInstanceSpot(
    target: ServiceInstanceActivationTarget,
    payload: ServiceApplicationPayload,
    deadlineUnixMs: bigint,
    sourceSpotRid?: string
  ): number {
    const operation = { high: this.nodeGeneration, low: this.nextInstanceOperation++ };
    return this.submitOneWay(target.targetNodeRid, [
      encodeInstanceSpotActivationHeader(
        target,
        this.nodeGeneration,
        this.nodeRid,
        sourceSpotRid,
        'send',
        operation,
        deadlineUnixMs
      ),
      encodeApplicationPayload(payload)
    ]);
  }

  requestToInstanceSpot(
    route: ServiceInstanceRouteFence,
    payload: ServiceApplicationPayload,
    timeoutMs: number,
    sourceSpotRid?: string
  ): ServiceStatefulPendingOperation {
    const pending = this.operations.reserve(timeoutMs);
    this.submitRequest(
      pending,
      route.targetNodeRid,
      [
        encodeInstanceSpotHeader(
          route,
          this.nodeGeneration,
          this.nodeRid,
          sourceSpotRid,
          'request',
          { high: 2n, low: pending.id },
          pending.id
        ),
        encodeApplicationPayload(payload)
      ],
      timeoutMs,
      'instanceSpotRequest'
    );
    return pending;
  }

  requestToMissingInstanceSpot(
    target: ServiceInstanceActivationTarget,
    payload: ServiceApplicationPayload,
    timeoutMs: number,
    sourceSpotRid?: string
  ): ServiceStatefulPendingOperation {
    const pending = this.operations.reserve(timeoutMs);
    const deadlineUnixMs = BigInt(Date.now() + timeoutMs);
    this.submitRequest(
      pending,
      target.targetNodeRid,
      [
        encodeInstanceSpotActivationHeader(
          target,
          this.nodeGeneration,
          this.nodeRid,
          sourceSpotRid,
          'request',
          { high: this.nodeGeneration, low: pending.id },
          deadlineUnixMs,
          pending.id
        ),
        encodeApplicationPayload(payload)
      ],
      timeoutMs,
      'instanceSpotRequest'
    );
    return pending;
  }

  lookupRemoteActor(
    targetNodeRid: string,
    actorId: string,
    timeoutMs: number
  ): ServiceStatefulPendingOperation {
    const pending = this.operations.reserve(timeoutMs);
    this.submitRequest(
      pending,
      targetNodeRid,
      [encodeActorLookupHeader(pending.id, actorId)],
      timeoutMs,
      'actorLookup'
    );
    return pending;
  }

  destroyActor(actor: ServiceActorRef, timeoutMs: number): ServiceStatefulPendingOperation {
    const pending = this.operations.reserve(timeoutMs);
    const local = actor.nodeRid === this.nodeRid;
    if (local) {
      queueMicrotask(() => {
        try {
          this.registry.destroyActor(actor);
          this.operations.reply(pending.id, { terminalResult: RequestResult.Ok, failureCode: 0 });
        } catch (error) {
          this.operations.reply(pending.id, failure(error));
        }
      });
      return pending;
    }
    const header = encodeActorDestroyHeader(pending.id, this.actorFence(actor));
    this.submitRequest(pending, actor.nodeRid, [header], timeoutMs, 'actorDestroy');
    return pending;
  }

  joinActor(
    actor: ServiceActorRef,
    targetNodeRid: string,
    targetSpot: ServiceSpotRef,
    targetSpotGeneration: bigint,
    payload: ServiceApplicationPayload | undefined,
    timeoutMs: number
  ): ServiceStatefulPendingOperation {
    const pending = this.operations.reserve(timeoutMs);
    const target = this.trySpotFence(targetNodeRid, {
      ...targetSpot,
      generation: targetSpotGeneration
    });
    const actorRoute = this.tryActorFence(actor);
    if (target === undefined || actorRoute === undefined) {
      this.operations.reply(pending.id, {
        terminalResult: RequestResult.NotFound,
        failureCode: ACTOR_ROUTE_STALE
      });
      return pending;
    }
    const header = encodeActorJoinHeader(
      pending.id,
      actorRoute,
      targetSpot.spotRid === targetNodeRid,
      target
    );
    this.submitRequest(
      pending,
      targetNodeRid,
      [
        header,
        ...(payload === undefined ? [] : [encodeApplicationPayload(payload)])
      ],
      timeoutMs,
      'actorJoin',
      actor
    );
    return pending;
  }

  leaveActor(
    actor: ServiceActorRef,
    expectedMembershipEpoch: bigint,
    timeoutMs: number
  ): ServiceStatefulPendingOperation {
    const pending = this.operations.reserve(timeoutMs);
    queueMicrotask(() => {
      try {
        const transition = this.registry.leaveActor(actor, expectedMembershipEpoch);
        this.enqueueActorControl(transition.actor.spot.spotRid, {
          kind: 'actorControl',
          lifecycleKind: ActorLifecycleKind.Left,
          previousActor: transition.actor.ref,
          currentActor: transition.actor.ref,
          previousSpotRid: transition.previousSpot.spotRid,
          currentSpotRid: transition.currentSpot.spotRid,
          previousSpotGeneration: transition.previousSpot.generation,
          currentSpotGeneration: transition.currentSpot.generation,
          previousMembershipEpoch: transition.previousMembershipEpoch,
          currentMembershipEpoch: transition.currentMembershipEpoch,
          resultCode: 0
        });
        this.operations.reply(pending.id, { terminalResult: RequestResult.Ok, failureCode: 0 });
      } catch (error) {
        this.operations.reply(pending.id, failure(error));
      }
    });
    return pending;
  }

  bindSession(
    sessionRid: string,
    actor: ServiceActorRef,
    timeoutMs: number,
    deliver: ServiceSessionDelivery['deliver']
  ): ServiceStatefulPendingOperation {
    const pending = this.operations.reserve(timeoutMs);
    const generation = this.nextSessionSequence++;
    const localBinding: ServiceSessionBinding = {
      actor,
      sessionRid,
      sessionOwnerNodeRid: this.nodeRid,
      bindingGeneration: generation,
      membershipEpoch: this.registry.actor(actor.actorId)?.membershipEpoch ?? 1n
    };
    this.sessionDeliveries.set(actorKey(actor), { binding: localBinding, deliver });
    if (actor.nodeRid === this.nodeRid) {
      try {
        const binding = this.registry.bindSession(actor, sessionRid, this.nodeRid);
        this.sessionDeliveries.set(actorKey(actor), { binding, deliver });
        this.operations.reply(pending.id, {
          terminalResult: RequestResult.Ok,
          failureCode: 0,
          kindData: undefined
        });
      } catch (error) {
        this.sessionDeliveries.delete(actorKey(actor));
        this.operations.reply(pending.id, failure(error));
      }
      return pending;
    }
    const header = encodeBoundSessionBindHeader(
      pending.id,
      this.actorFence(actor),
      sessionRid,
      { state: 'active', generation }
    );
    this.submitRequest(pending, actor.nodeRid, [header], timeoutMs, 'streamBind');
    void pending.promise.then(result => {
      if (result.terminalResult !== RequestResult.Ok) {
        this.sessionDeliveries.delete(actorKey(actor));
      }
    }, () => this.sessionDeliveries.delete(actorKey(actor)));
    return pending;
  }

  unbindSession(
    sessionRid: string,
    actor: ServiceActorRef,
    expectedBindingGeneration: bigint,
    timeoutMs: number
  ): ServiceStatefulPendingOperation {
    const pending = this.operations.reserve(timeoutMs);
    const delivery = this.sessionDeliveries.get(actorKey(actor));
    if (
      delivery === undefined
      || delivery.binding.sessionRid !== sessionRid
      || delivery.binding.bindingGeneration !== expectedBindingGeneration
    ) {
      this.operations.reply(pending.id, {
        terminalResult: RequestResult.NotFound,
        failureCode: ACTOR_ROUTE_STALE
      });
      return pending;
    }
    this.sessionDeliveries.delete(actorKey(actor));
    if (actor.nodeRid === this.nodeRid) {
      try {
        this.registry.unbindSession(actor, expectedBindingGeneration);
        this.operations.reply(pending.id, { terminalResult: RequestResult.Ok, failureCode: 0 });
      } catch (error) {
        this.operations.reply(pending.id, failure(error));
      }
      return pending;
    }
    const header = encodeBoundSessionBindHeader(
      pending.id,
      this.actorFence(actor),
      sessionRid,
      { state: 'tombstone', retiredGeneration: expectedBindingGeneration }
    );
    this.submitRequest(pending, actor.nodeRid, [header], timeoutMs, 'streamBind');
    return pending;
  }

  sessionBindings(sessionRid: string): readonly ServiceSessionBinding[] {
    return [...this.sessionDeliveries.values()]
      .map(value => value.binding)
      .filter(binding => binding.sessionRid === sessionRid);
  }

  allSessionBindings(): readonly ServiceSessionBinding[] {
    return [...this.sessionDeliveries.values()].map(value => value.binding);
  }

  sendSessionToActor(
    sessionRid: string,
    actor: ServiceActorRef,
    payload: ServiceApplicationPayload
  ): number {
    const delivery = this.sessionDeliveries.get(actorKey(actor));
    if (delivery === undefined || delivery.binding.sessionRid !== sessionRid) {
      return SubmitResult.NotFound;
    }
    return this.sendToActor(
      actor,
      this.peerGeneration(actor.nodeRid),
      actor.generation,
      payload,
      undefined,
      {
        sessionRid,
        bindingGeneration: delivery.binding.bindingGeneration
      }
    );
  }

  sendBoundSession(
    actor: ServiceActorRef,
    expectedBindingGeneration: bigint,
    payload: ServiceApplicationPayload
  ): number {
    let binding: ServiceSessionBinding;
    try {
      binding = this.registry.validateBoundSession(actor, expectedBindingGeneration);
    } catch {
      return SubmitResult.InvalidState;
    }
    const header = encodeBoundSessionSendHeader(
      this.actorFence(actor),
      expectedBindingGeneration
    );
    return this.submitOneWay(
      binding.sessionOwnerNodeRid,
      [header, encodeApplicationPayload(payload)]
    );
  }

  close(): void {
    if (this.closed) return;
    this.closed = true;
    this.operations.close();
    this.sessionDeliveries.clear();
    this.actorRoutes.clear();
    this.spotRoutes.clear();
  }

  private ingress(record: RawServiceIngressRecord): RawServicePumpResult | undefined {
    if (
      record.command < M6bServiceWireCommand.spotSend
      || record.command > M6bServiceWireCommand.instanceSpot
    ) {
      return undefined;
    }
    let decoded: ServiceStatefulWireRecord;
    try {
      decoded = decodeStatefulHeader(record.parts[0]!);
      const hasPayload = [
        'spotSend',
        'spotRequest',
        'logicalMulticast',
        'actorSend',
        'actorRequest',
        'actorJoin',
        'boundSessionSend'
      ].includes(decoded.kind);
      if (
        record.parts.length < 1
        || record.parts.length > 2
        || (hasPayload && decoded.kind !== 'actorJoin' && record.parts.length !== 2)
      ) {
        return 'protocolError';
      }
      const payload = record.parts.length === 2
        ? decodeApplicationPayload(record.parts[1]!)
        : undefined;
      try {
        return this.handleIngress(record, decoded, payload);
      } catch (error) {
        const correlation = statefulCorrelation(decoded);
        if (correlation !== undefined) {
          const result = failure(error);
          this.replyWire(record, correlation, result.terminalResult, result.failureCode);
          return 'infrastructure';
        }
        if (error instanceof ServiceStaleGenerationError) return 'protocolError';
        throw error;
      }
    } catch (error) {
      if (error instanceof ServiceWireProtocolError) return 'protocolError';
      throw error;
    }
  }

  private handleIngress(
    ingress: RawServiceIngressRecord,
    record: ServiceStatefulWireRecord,
    payload: ServiceApplicationPayload | undefined
  ): RawServicePumpResult {
    switch (record.kind) {
      case 'spotSend':
      case 'spotRequest':
        this.validateSpotFence(record.target);
        return this.enqueueApplication(
          ingress,
          `spot:${record.target.spot.spotRid}`,
          payload!,
          {
            receiveKind: record.kind === 'spotSend' ? ReceiveKind.SpotSend : ReceiveKind.SpotRequest,
            operationKind: record.kind === 'spotRequest' ? OperationKind.SpotRequest : 0,
            ...(record.correlation === undefined ? {} : { correlation: record.correlation }),
            sourceSpotRid: record.sourceSpotRid,
            targetSpot: record.target.spot,
            ...(record.kind === 'spotRequest'
              ? { reply: this.replyPort(ingress, record.correlation, 'spotRequest') }
              : {})
          }
        );
      case 'actorSend':
      case 'actorRequest':
        this.validateActorFence(record.target);
        return this.enqueueApplication(
          ingress,
          `actor:${record.target.actor.actorId}\0${record.target.actor.generation}`,
          payload!,
          {
            receiveKind: record.kind === 'actorSend' ? ReceiveKind.ActorSend : ReceiveKind.ActorRequest,
            operationKind: record.kind === 'actorRequest' ? OperationKind.ActorRequest : 0,
            ...(record.correlation === undefined ? {} : { correlation: record.correlation }),
            ...(record.sourceActor === undefined
              ? {}
              : { sourceActor: { ...record.sourceActor, nodeRid: ingress.sourceRoutingId } }),
            ...(record.boundSession === undefined
              ? {}
              : { sourceBindingGeneration: record.boundSession.bindingGeneration }),
            targetActor: record.target.actor,
            ...(record.kind === 'actorRequest'
              ? { reply: this.replyPort(ingress, record.correlation, 'actorRequest') }
              : {})
          }
        );
      case 'logicalMulticast':
        this.enqueueLogicalMulticast(
          record.channelName,
          record.topic,
          record.sourceSpotRid,
          payload!,
          ingress.sourceRoutingId
        );
        return 'application';
      case 'actorLookup':
        return this.replyLookup(ingress, record);
      case 'actorDestroy':
        return this.replyDestroy(ingress, record);
      case 'actorJoin':
        this.validateSpotFence(record.target);
        return this.enqueueActorJoin(ingress, record, payload);
      case 'boundSessionBind':
        return this.replySessionBind(ingress, record);
      case 'boundSessionSend':
        return this.deliverBoundSession(ingress, record, payload!);
      case 'instanceSpot':
        return this.enqueueInstanceSpot(ingress, record, payload!);
    }
  }

  private enqueueInstanceSpot(
    ingress: RawServiceIngressRecord,
    record: Extract<ServiceStatefulWireRecord, { readonly kind: 'instanceSpot' }>,
    payload: ServiceApplicationPayload
  ): RawServicePumpResult {
    if (record.activation === 'missing' && this.asyncInstanceAuthority !== undefined) {
      void this.continueMissingInstanceActivation(ingress, record, payload);
      return 'infrastructure';
    }
    const spot = this.requireInstanceActivation(ingress, record);
    return this.enqueueActivatedInstanceSpot(ingress, record, payload, spot);
  }

  private enqueueActivatedInstanceSpot(
    ingress: RawServiceIngressRecord,
    record: Extract<ServiceStatefulWireRecord, { readonly kind: 'instanceSpot' }>,
    payload: ServiceApplicationPayload,
    spot: ServiceSpotState,
    localReply?: NonNullable<ServiceStatefulMailboxData['reply']>
  ): RawServicePumpResult {
    let operationKey: string | undefined;
    if (record.activation === 'missing') {
      const now = BigInt(Date.now());
      for (const [key, deadline] of this.admittedInstanceOperations) {
        if (deadline < now) this.admittedInstanceOperations.delete(key);
      }
      operationKey = instanceOperationKey(record);
      if (this.admittedInstanceOperations.has(operationKey)) return 'application';
    }
    const result = this.enqueueApplication(
      ingress,
      `spot:${spot.ref.spotRid}`,
      payload,
      {
        receiveKind: ReceiveKind.InstanceSpotActivation,
        operationKind: record.operationKind === 'request' ? OperationKind.InstanceSpotRequest : 0,
        ...(record.operationKind === 'request' ? { correlation: record.replyRouteId } : {}),
        ...(record.sourceSpotRid === undefined ? {} : { sourceSpotRid: record.sourceSpotRid }),
        targetSpot: spot.ref,
        ...(record.operationKind === 'request'
          ? {
              reply: localReply ?? this.replyPort(
                ingress,
                record.replyRouteId,
                'instanceSpotRequest'
              )
            }
          : {})
      }
    );
    if (operationKey !== undefined && result === 'application' && record.activation === 'missing') {
      this.admittedInstanceOperations.set(operationKey, record.deadlineUnixMs);
    }
    return result;
  }

  private async continueMissingInstanceActivation(
    ingress: RawServiceIngressRecord,
    record: Extract<
      ServiceStatefulWireRecord,
      { readonly kind: 'instanceSpot'; readonly activation: 'missing' }
    >,
    payload: ServiceApplicationPayload,
    localReply?: NonNullable<ServiceStatefulMailboxData['reply']>
  ): Promise<void> {
    try {
      this.validateInstanceIngress(ingress, record);
      const spot = await this.activateMissingInstanceAsync(record);
      if (this.closed) return;
      this.enqueueActivatedInstanceSpot(ingress, record, payload, spot, localReply);
    } catch (error) {
      if (record.operationKind !== 'request') return;
      const result = failure(error);
      if (localReply !== undefined) {
        localReply(result.terminalResult, result.failureCode);
      } else {
        if (record.replyRouteId === undefined) return;
        this.replyWire(
          ingress,
          record.replyRouteId,
          result.terminalResult,
          result.failureCode
        );
      }
    }
  }

  private requireInstanceActivation(
    ingress: RawServiceIngressRecord,
    record: Extract<ServiceStatefulWireRecord, { readonly kind: 'instanceSpot' }>
  ): ServiceSpotState {
    this.validateInstanceIngress(ingress, record);
    if (record.activation === 'missing') {
      return this.activateMissingInstance(record);
    }
    if (
      record.route.targetNodeRid !== this.nodeRid
      || record.route.targetNodeGeneration !== this.nodeGeneration
    ) {
      throw new ServiceStaleGenerationError('spot', record.route.targetSpotRid);
    }
    const intent = this.instanceIntents.get(record.route.targetSpotRid);
    if (intent === undefined || !sameInstanceRoute(intent.route, record.route)) {
      throw new ServiceStaleGenerationError('spot', record.route.targetSpotRid);
    }
    const activation = this.activateInstanceSpot(
      record.route.targetSpotRid,
      intent.instanceType,
      record.route.authorityOwnerGeneration
    );
    if (activation.spot.ref.generation !== record.route.objectGeneration) {
      throw new ServiceStaleGenerationError('spot', record.route.targetSpotRid);
    }
    return activation.spot;
  }

  private validateInstanceIngress(
    ingress: RawServiceIngressRecord,
    record: Extract<ServiceStatefulWireRecord, { readonly kind: 'instanceSpot' }>
  ): void {
    if (
      record.sourceNodeRid !== ingress.sourceRoutingId
      || record.sourceNodeGeneration !== this.peerGeneration(record.sourceNodeRid)
    ) {
      throw new ServiceStaleGenerationError(
        'spot',
        record.activation === 'ready'
          ? record.route.targetSpotRid
          : record.target.targetSpotRid
      );
    }
  }

  private activateMissingInstance(
    record: Extract<
      ServiceStatefulWireRecord,
      { readonly kind: 'instanceSpot'; readonly activation: 'missing' }
    >
  ): ServiceSpotState {
    const target = record.target;
    if (
      target.targetNodeRid !== this.nodeRid
      || target.targetNodeGeneration !== this.nodeGeneration
      || record.deadlineUnixMs < BigInt(Date.now())
    ) {
      throw new ServiceStaleGenerationError('spot', target.targetSpotRid);
    }
    const authority = this.instanceAuthority;
    if (authority === undefined) {
      throw new Error('Instance activation authority is not registered.');
    }

    const local = this.registry.spot(target.targetSpotRid);
    const current = authority.read(target);
    if (local !== undefined) {
      if (
        local.kind !== 'instance'
        || local.stableType !== target.stableType
        || current.kind !== 'ready'
        || !routeMatchesLocal(current.route, local, this.nodeRid, this.nodeGeneration)
      ) {
        throw new ServiceStaleGenerationError('spot', target.targetSpotRid);
      }
      return local;
    }
    if (current.kind === 'ready') {
      throw new ServiceInstanceActivationRedirectError(current.route);
    }

    const reserved = authority.reserve(target, record.operation, record.deadlineUnixMs);
    if (reserved.kind === 'ready') {
      throw new ServiceInstanceActivationRedirectError(reserved.route);
    }

    let activation: { readonly spot: ServiceSpotState; readonly created: boolean };
    try {
      activation = this.activateInstanceSpot(
        target.targetSpotRid,
        target.stableType,
        reserved.reservation.attempt
      );
    } catch (error) {
      authority.abort(target, reserved.reservation);
      throw error;
    }
    const committed = authority.commit(target, reserved.reservation, activation.spot);
    if (committed.kind === 'lost') {
      if (activation.created) this.registry.closeSpot(activation.spot.ref);
      throw new ServiceInstanceActivationRedirectError(committed.route);
    }
    if (!routeMatchesLocal(
      committed.route,
      activation.spot,
      this.nodeRid,
      this.nodeGeneration
    )) {
      if (activation.created) this.registry.closeSpot(activation.spot.ref);
      throw new ServiceStaleGenerationError('spot', target.targetSpotRid);
    }
    return activation.spot;
  }

  private activateMissingInstanceAsync(
    record: Extract<
      ServiceStatefulWireRecord,
      { readonly kind: 'instanceSpot'; readonly activation: 'missing' }
    >
  ): Promise<ServiceSpotState> {
    const key = instanceOperationKey(record);
    const pending = this.pendingInstanceActivations.get(key);
    if (pending !== undefined) return pending;
    const activation = this.runMissingInstanceActivation(record);
    this.pendingInstanceActivations.set(key, activation);
    const clear = () => {
      if (this.pendingInstanceActivations.get(key) === activation) {
        this.pendingInstanceActivations.delete(key);
      }
    };
    void activation.then(clear, clear);
    return activation;
  }

  private async runMissingInstanceActivation(
    record: Extract<
      ServiceStatefulWireRecord,
      { readonly kind: 'instanceSpot'; readonly activation: 'missing' }
    >
  ): Promise<ServiceSpotState> {
    const target = record.target;
    if (
      target.targetNodeRid !== this.nodeRid
      || target.targetNodeGeneration !== this.nodeGeneration
      || record.deadlineUnixMs < BigInt(Date.now())
    ) {
      throw new ServiceStaleGenerationError('spot', target.targetSpotRid);
    }
    const authority = this.asyncInstanceAuthority;
    if (authority === undefined) {
      throw new Error('Async Instance activation authority is not registered.');
    }

    const local = this.registry.spot(target.targetSpotRid);
    const current = await authority.read(target);
    if (local !== undefined) {
      if (
        local.kind !== 'instance'
        || local.stableType !== target.stableType
        || current.kind !== 'ready'
        || !routeMatchesLocal(current.route, local, this.nodeRid, this.nodeGeneration)
      ) {
        throw new ServiceStaleGenerationError('spot', target.targetSpotRid);
      }
      return local;
    }
    if (current.kind === 'ready') {
      throw new ServiceInstanceActivationRedirectError(current.route);
    }

    const reserved = await authority.reserve(
      target,
      record.operation,
      record.deadlineUnixMs
    );
    if (reserved.kind === 'ready') {
      throw new ServiceInstanceActivationRedirectError(reserved.route);
    }
    if (this.closed || record.deadlineUnixMs < BigInt(Date.now())) {
      await authority.abort(target, reserved.reservation);
      throw new ServiceStaleGenerationError('spot', target.targetSpotRid);
    }

    let activation: { readonly spot: ServiceSpotState; readonly created: boolean };
    try {
      activation = this.activateInstanceSpot(
        target.targetSpotRid,
        target.stableType,
        reserved.reservation.attempt
      );
    } catch (error) {
      await authority.abort(target, reserved.reservation);
      throw error;
    }
    let committed: Awaited<ReturnType<ServiceAsyncInstanceActivationAuthority['commit']>>;
    try {
      committed = await authority.commit(target, reserved.reservation, activation.spot);
    } catch (error) {
      if (activation.created) this.registry.closeSpot(activation.spot.ref);
      throw error;
    }
    if (committed.kind === 'lost') {
      if (activation.created) this.registry.closeSpot(activation.spot.ref);
      throw new ServiceInstanceActivationRedirectError(committed.route);
    }
    if (!routeMatchesLocal(
      committed.route,
      activation.spot,
      this.nodeRid,
      this.nodeGeneration
    )) {
      if (activation.created) this.registry.closeSpot(activation.spot.ref);
      throw new ServiceStaleGenerationError('spot', target.targetSpotRid);
    }
    return activation.spot;
  }

  private enqueueActorJoin(
    ingress: RawServiceIngressRecord,
    record: Extract<ServiceStatefulWireRecord, { readonly kind: 'actorJoin' }>,
    payload: ServiceApplicationPayload | undefined
  ): RawServicePumpResult {
    const current = this.registry.actor(record.actor.actor.actorId);
    const previousEpoch = current?.membershipEpoch ?? 0n;
    const actor: ServiceActorRef = {
      ...record.actor.actor,
      nodeRid: this.nodeRid
    };
    const control: ActorControlPayload = {
      kind: 'actorControl',
      lifecycleKind: ActorLifecycleKind.Joined,
      previousActor: current?.ref ?? record.actor.actor,
      currentActor: actor,
      previousSpotRid: current?.spot.spotRid ?? null,
      currentSpotRid: record.target.spot.spotRid,
      previousSpotGeneration: current?.spot.generation ?? 0n,
      currentSpotGeneration: record.target.spot.generation,
      previousMembershipEpoch: previousEpoch,
      currentMembershipEpoch: previousEpoch + 1n,
      resultCode: 0
    };
    const application = payload ?? emptyPayload();
    return this.enqueueApplication(
      ingress,
      `spot:${record.target.spot.spotRid}`,
      application,
      {
        receiveKind: ReceiveKind.SpotControl,
        operationKind: OperationKind.ActorJoin,
        correlation: record.correlation,
        targetSpot: record.target.spot,
        targetActor: actor,
        kindData: control,
        reply: (terminalResult, failureCode, replyPayload, tail) => {
          if (terminalResult === RequestResult.Ok && tail?.kind === 'actorJoin' && tail.joinResult === 0) {
            this.commitJoinedActor(record.actor.actor, record.target.spot, previousEpoch + 1n);
          }
          return this.replyPort(ingress, record.correlation, 'actorJoin')(
            terminalResult,
            failureCode,
            replyPayload,
            tail
          );
        }
      }
    );
  }

  private replyLookup(
    ingress: RawServiceIngressRecord,
    record: Extract<ServiceStatefulWireRecord, { readonly kind: 'actorLookup' }>
  ): RawServicePumpResult {
    const actor = this.registry.actor(record.actorId);
    if (actor === undefined) {
      this.replyWire(ingress, record.correlation, RequestResult.NotFound, ACTOR_ROUTE_NOT_FOUND);
    } else {
      this.replyWire(ingress, record.correlation, RequestResult.Ok, 0, undefined, {
        kind: 'actorLookup',
        actor: actor.ref,
        spot: actor.spot,
        membershipEpoch: actor.membershipEpoch,
        authorityOwnerGeneration: actor.authorityOwnerGeneration
      });
    }
    return 'infrastructure';
  }

  private replyDestroy(
    ingress: RawServiceIngressRecord,
    record: Extract<ServiceStatefulWireRecord, { readonly kind: 'actorDestroy' }>
  ): RawServicePumpResult {
    try {
      this.validateActorFence(record.actor);
      this.registry.destroyActor(record.actor.actor);
      this.replyWire(ingress, record.correlation, RequestResult.Ok, 0);
    } catch (error) {
      const result = failure(error);
      this.replyWire(ingress, record.correlation, result.terminalResult, result.failureCode);
    }
    return 'infrastructure';
  }

  private replySessionBind(
    ingress: RawServiceIngressRecord,
    record: Extract<ServiceStatefulWireRecord, { readonly kind: 'boundSessionBind' }>
  ): RawServicePumpResult {
    try {
      const actor = this.validateActorFence(record.actor);
      if (record.binding.state === 'active') {
        const binding = sessionBindingFromWire(
          actor.ref,
          record.sessionRid,
          ingress.sourceRoutingId,
          record.binding.generation,
          actor.membershipEpoch
        );
        this.registry.installSessionBinding(binding);
        this.replyWire(ingress, record.correlation, RequestResult.Ok, 0, undefined, {
          kind: 'streamBind',
          bindingGeneration: binding.bindingGeneration,
          authorityOwnerGeneration: actor.authorityOwnerGeneration
        });
      } else {
        this.registry.unbindSession(actor.ref, record.binding.retiredGeneration);
        this.replyWire(ingress, record.correlation, RequestResult.Ok, 0);
      }
    } catch (error) {
      const result = failure(error);
      this.replyWire(ingress, record.correlation, result.terminalResult, result.failureCode);
    }
    return 'infrastructure';
  }

  private deliverBoundSession(
    _ingress: RawServiceIngressRecord,
    record: Extract<ServiceStatefulWireRecord, { readonly kind: 'boundSessionSend' }>,
    payload: ServiceApplicationPayload
  ): RawServicePumpResult {
    const delivery = this.sessionDeliveries.get(actorKey(record.actor.actor));
    if (
      delivery === undefined
      || delivery.binding.bindingGeneration !== record.expectedBindingGeneration
      || !delivery.deliver(delivery.binding.sessionRid, payload)
    ) {
      return 'protocolError';
    }
    return 'application';
  }

  private enqueueApplication(
    ingress: RawServiceIngressRecord,
    owner: string,
    payload: ServiceApplicationPayload,
    stateful: ServiceStatefulMailboxData
  ): RawServicePumpResult {
    return this.raw.mailbox.tryEnqueue({
      owner,
      domain: 'application',
      parts: [ingress.parts[0]!, encodeApplicationPayload(payload)],
      sourceRoutingId: ingress.sourceRoutingId,
      ...(ingress.requestSequence === undefined ? {} : { requestSequence: ingress.requestSequence }),
      ...(stateful.correlation === undefined ? {} : { correlation: stateful.correlation }),
      stateful
    })
      ? 'application'
      : 'protocolError';
  }

  private enqueueLogicalMulticast(
    channelName: string,
    topic: string,
    sourceSpotRid: string,
    payload: ServiceApplicationPayload,
    sourceNodeRid = this.nodeRid
  ): { readonly snapshot: number; readonly admitted: number } {
    const targets = [...this.subscriptions.entries()]
      .filter(([, values]) => [...values].some(value => {
        const separator = value.indexOf('\0');
        return value.slice(0, separator) === channelName
          && topicMatches(value.slice(separator + 1), topic);
      }))
      .map(([spotRid]) => spotRid)
      .sort();
    let admitted = 0;
    for (const spotRid of targets) {
      const spot = this.registry.spot(spotRid);
      if (spot === undefined) continue;
      if (this.raw.mailbox.tryEnqueue({
        owner: `spot:${spotRid}`,
        domain: 'application',
        parts: [
          encodeLogicalMulticastHeader(channelName, topic, sourceSpotRid),
          encodeApplicationPayload(payload)
        ],
        sourceRoutingId: sourceNodeRid,
        stateful: {
          receiveKind: ReceiveKind.SpotMulticast,
          operationKind: 0,
          sourceSpotRid,
          channelName,
          topic,
          targetSpot: spot.ref
        } satisfies ServiceStatefulMailboxData
      })) {
        admitted++;
      }
    }
    return { snapshot: targets.length, admitted };
  }

  private enqueueActorControl(spotRid: string, control: ActorControlPayload): void {
    const header = Buffer.from([0x5a, 0x4d, 1, M6bServiceWireCommand.actorJoined, 0]);
    this.raw.mailbox.tryEnqueue({
      owner: `spot:${spotRid}`,
      domain: 'application',
      parts: [header],
      sourceRoutingId: this.nodeRid,
      stateful: {
        receiveKind: ReceiveKind.SpotControl,
        operationKind: 0,
        targetSpot: this.registry.spot(spotRid)?.ref,
        kindData: control
      } satisfies ServiceStatefulMailboxData
    });
  }

  private replyPort(
    ingress: RawServiceIngressRecord,
    correlation: bigint | undefined,
    operationKind: 'spotRequest' | 'actorRequest' | 'actorJoin'
      | 'instanceSpotRequest'
  ): NonNullable<ServiceStatefulMailboxData['reply']> {
    if (correlation === undefined) throw new ServiceWireProtocolError('Request correlation is missing.');
    return (terminalResult, failureCode, payload, tail) => {
      this.replyWire(ingress, correlation, terminalResult, failureCode, payload, tail);
      void operationKind;
      return true;
    };
  }

  private replyWire(
    ingress: RawServiceIngressRecord,
    correlation: bigint,
    terminalResult: number,
    failureCode: number,
    payload?: ServiceApplicationPayload,
    tail?: ServiceStatefulReplyTail
  ): void {
    this.raw.replyService(ingress, [
      encodeStatefulReply(correlation, terminalResult, failureCode, tail),
      ...(payload === undefined ? [] : [encodeApplicationPayload(payload)])
    ]);
  }

  private submitOneWay(targetNodeRid: string, parts: readonly Buffer[]): number {
    this.requireOpen();
    if (targetNodeRid === this.nodeRid) {
      const result = this.ingress({
        command: parts[0]![3]!,
        flags: parts[0]![4]!,
        sourceRoutingId: this.nodeRid,
        parts
      });
      return result === 'application' || result === 'infrastructure'
        ? SubmitResult.Ok
        : SubmitResult.InvalidState;
    }
    return this.raw.sendService(targetNodeRid, parts)
      ? SubmitResult.Ok
      : SubmitResult.NotConnected;
  }

  private submitRequest(
    pending: ServiceStatefulPendingOperation,
    targetNodeRid: string,
    parts: readonly Buffer[],
    timeoutMs: number,
    operationKind: 'spotRequest' | 'actorRequest' | 'actorLookup' | 'actorDestroy' | 'actorJoin' | 'streamBind'
      | 'instanceSpotRequest',
    actor?: ServiceActorRef
  ): void {
    if (targetNodeRid === this.nodeRid) {
      const localIngress: RawServiceIngressRecord = {
        command: parts[0]![3]!,
        flags: parts[0]![4]!,
        sourceRoutingId: this.nodeRid,
        requestSequence: pending.id,
        parts
      };
      try {
        this.submitLocalRequest(localIngress, pending, operationKind, actor);
      } catch (error) {
        this.operations.reply(pending.id, failure(error));
      }
      return;
    }
    void this.raw.requestService(targetNodeRid, parts, timeoutMs).then(
      reply => this.completeRemoteReply(pending, targetNodeRid, operationKind, actor, reply),
      error => this.operations.fail(pending.id, error)
    );
  }

  private submitLocalRequest(
    ingress: RawServiceIngressRecord,
    pending: ServiceStatefulPendingOperation,
    operationKind: 'spotRequest' | 'actorRequest' | 'actorLookup' | 'actorDestroy' | 'actorJoin' | 'streamBind'
      | 'instanceSpotRequest',
    actor?: ServiceActorRef
  ): void {
    const decoded = decodeStatefulHeader(ingress.parts[0]!);
    const payload = ingress.parts.length < 2
      ? undefined
      : decodeApplicationPayload(ingress.parts[1]);
    if (decoded.kind === 'actorLookup') {
      const found = this.registry.actor(decoded.actorId);
      this.operations.reply(pending.id, found === undefined
        ? { terminalResult: RequestResult.NotFound, failureCode: ACTOR_ROUTE_NOT_FOUND }
        : {
            terminalResult: RequestResult.Ok,
            failureCode: 0,
            kindData: lookupKindData(found)
          });
      return;
    }
    if (decoded.kind === 'actorDestroy') {
      try {
        this.registry.destroyActor(decoded.actor.actor);
        this.operations.reply(pending.id, { terminalResult: RequestResult.Ok, failureCode: 0 });
      } catch (error) {
        this.operations.reply(pending.id, failure(error));
      }
      return;
    }
    const localReply: ServiceStatefulMailboxData['reply'] = (
      terminalResult,
      failureCode,
      replyPayload,
      tail
    ) => this.operations.reply(pending.id, this.resultFromReply(
      terminalResult,
      failureCode,
      replyPayload,
      tail,
      this.nodeRid,
      actor
    ));
    if (decoded.kind === 'spotRequest') {
      this.validateSpotFence(decoded.target);
      this.enqueueApplication(
        ingress,
        `spot:${decoded.target.spot.spotRid}`,
        payload!,
        {
          receiveKind: ReceiveKind.SpotRequest,
          operationKind: OperationKind.SpotRequest,
          correlation: decoded.correlation,
          sourceSpotRid: decoded.sourceSpotRid,
          targetSpot: decoded.target.spot,
          reply: localReply
        }
      );
      return;
    }
    if (decoded.kind === 'actorRequest') {
      this.validateActorFence(decoded.target);
      this.enqueueApplication(
        ingress,
        `actor:${decoded.target.actor.actorId}\0${decoded.target.actor.generation}`,
        payload!,
        {
          receiveKind: ReceiveKind.ActorRequest,
          operationKind: OperationKind.ActorRequest,
          correlation: decoded.correlation,
          targetActor: decoded.target.actor,
          ...(decoded.sourceActor === undefined ? {} : { sourceActor: decoded.sourceActor }),
          ...(decoded.boundSession === undefined
            ? {}
            : { sourceBindingGeneration: decoded.boundSession.bindingGeneration }),
          reply: localReply
        }
      );
      return;
    }
    if (decoded.kind === 'actorJoin') {
      this.validateSpotFence(decoded.target);
      const previous = this.registry.actor(decoded.actor.actor.actorId);
      const control: ActorControlPayload = {
        kind: 'actorControl',
        lifecycleKind: ActorLifecycleKind.Joined,
        previousActor: previous?.ref ?? decoded.actor.actor,
        currentActor: decoded.actor.actor,
        previousSpotRid: previous?.spot.spotRid ?? null,
        currentSpotRid: decoded.target.spot.spotRid,
        previousSpotGeneration: previous?.spot.generation ?? 0n,
        currentSpotGeneration: decoded.target.spot.generation,
        previousMembershipEpoch: previous?.membershipEpoch ?? 0n,
        currentMembershipEpoch: (previous?.membershipEpoch ?? 0n) + 1n,
        resultCode: 0
      };
      this.enqueueApplication(
        ingress,
        `spot:${decoded.target.spot.spotRid}`,
        payload ?? emptyPayload(),
        {
          receiveKind: ReceiveKind.SpotControl,
          operationKind: OperationKind.ActorJoin,
          correlation: decoded.correlation,
          targetSpot: decoded.target.spot,
          targetActor: decoded.actor.actor,
          kindData: control,
          reply: (terminalResult, failureCode, replyPayload, tail) => {
            if (terminalResult === RequestResult.Ok && tail?.kind === 'actorJoin' && tail.joinResult === 0) {
              this.commitJoinedActor(
                decoded.actor.actor,
                decoded.target.spot,
                control.currentMembershipEpoch
              );
            }
            return localReply(terminalResult, failureCode, replyPayload, tail);
          }
        }
      );
      return;
    }
    if (decoded.kind === 'boundSessionBind') {
      try {
        const current = this.validateActorFence(decoded.actor);
        if (decoded.binding.state === 'active') {
          this.registry.installSessionBinding(sessionBindingFromWire(
            current.ref,
            decoded.sessionRid,
            this.nodeRid,
            decoded.binding.generation,
            current.membershipEpoch
          ));
        } else {
          this.registry.unbindSession(current.ref, decoded.binding.retiredGeneration);
        }
        this.operations.reply(pending.id, { terminalResult: RequestResult.Ok, failureCode: 0 });
      } catch (error) {
        this.operations.reply(pending.id, failure(error));
      }
      return;
    }
    if (decoded.kind === 'instanceSpot') {
      if (decoded.activation === 'missing' && this.asyncInstanceAuthority !== undefined) {
        void this.continueMissingInstanceActivation(
          ingress,
          decoded,
          payload!,
          localReply
        );
        return;
      }
      try {
        const activation = this.requireInstanceActivation(ingress, decoded);
        this.enqueueApplication(
          ingress,
          `spot:${activation.ref.spotRid}`,
          payload!,
          {
            receiveKind: ReceiveKind.InstanceSpotActivation,
            operationKind: OperationKind.InstanceSpotRequest,
            correlation: pending.id,
            ...(decoded.sourceSpotRid === undefined ? {} : { sourceSpotRid: decoded.sourceSpotRid }),
            targetSpot: activation.ref,
            reply: localReply
          }
        );
      } catch (error) {
        this.operations.reply(pending.id, failure(error));
      }
      return;
    }
    this.operations.reply(pending.id, {
      terminalResult: RequestResult.ProtocolError,
      failureCode: 0
    });
    void operationKind;
  }

  private completeRemoteReply(
    pending: ServiceStatefulPendingOperation,
    targetNodeRid: string,
    operationKind: 'spotRequest' | 'actorRequest' | 'actorLookup' | 'actorDestroy' | 'actorJoin' | 'streamBind'
      | 'instanceSpotRequest',
    actor: ServiceActorRef | undefined,
    reply: readonly Buffer[]
  ): void {
    try {
      if (reply.length < 1 || reply.length > 2) throw new ServiceWireProtocolError('Invalid M6B reply parts.');
      const decoded = decodeStatefulReply(reply[0]!, pending.id, operationKind);
      const payload = reply.length < 2 ? undefined : decodeApplicationPayload(reply[1]);
      this.operations.reply(pending.id, this.resultFromReply(
        decoded.terminalResult,
        decoded.failureCode,
        payload,
        decoded.tail,
        targetNodeRid,
        actor
      ));
    } catch (error) {
      this.operations.fail(pending.id, error);
    }
  }

  private resultFromReply(
    terminalResult: number,
    failureCode: number,
    payload: ServiceApplicationPayload | undefined,
    tail: ServiceStatefulReplyTail | undefined,
    targetNodeRid: string,
    actor: ServiceActorRef | undefined
  ): ServiceStatefulResult {
    let kindData: ReceiveKindData | undefined;
    if (tail?.kind === 'actorLookup') {
      const resolvedActor = { ...tail.actor, nodeRid: targetNodeRid };
      this.rememberActorRoute({
        actor: resolvedActor,
        targetNodeGeneration: this.peerGeneration(targetNodeRid),
        authorityOwnerGeneration: tail.authorityOwnerGeneration
      });
      kindData = {
        kind: 'actorLookupCompletion',
        location: {
          actor: resolvedActor,
          spotRid: tail.spot.spotRid,
          spotGeneration: tail.spot.generation,
          membershipEpoch: tail.membershipEpoch
        }
      };
    } else if (tail?.kind === 'actorJoin' && actor !== undefined) {
      const joinedActor = { ...actor, nodeRid: targetNodeRid };
      const spot = tail.spot;
      kindData = {
        kind: 'actorJoinCompletion',
        joinResult: tail.joinResult,
        actor: joinedActor,
        location: {
          actor: joinedActor,
          spotRid: spot?.spotRid ?? null,
          spotGeneration: spot?.generation ?? 0n,
          membershipEpoch: tail.membershipEpoch ?? 1n
        }
      } satisfies ActorJoinCompletionPayload;
    }
    return {
      terminalResult,
      failureCode,
      ...(payload === undefined ? {} : { payload }),
      ...(kindData === undefined ? {} : { kindData })
    };
  }

  private validateActorFence(fence: ServiceActorRouteFence): ServiceActorState {
    if (fence.actor.nodeRid !== this.nodeRid || fence.targetNodeGeneration !== this.nodeGeneration) {
      throw new ServiceStaleGenerationError('actor', fence.actor.actorId);
    }
    const actor = this.registry.requireActor(fence.actor);
    if (actor.authorityOwnerGeneration !== fence.authorityOwnerGeneration) {
      throw new ServiceStaleGenerationError('actor', fence.actor.actorId);
    }
    return actor;
  }

  private validateSpotFence(fence: ServiceSpotRouteFence): ServiceSpotState {
    if (fence.targetNodeRid !== this.nodeRid || fence.targetNodeGeneration !== this.nodeGeneration) {
      throw new ServiceStaleGenerationError('spot', fence.spot.spotRid);
    }
    const spot = this.registry.requireSpot(fence.spot);
    if (spot.authorityOwnerGeneration !== fence.authorityOwnerGeneration) {
      throw new ServiceStaleGenerationError('spot', fence.spot.spotRid);
    }
    return spot;
  }

  private actorFence(actor: ServiceActorRef): ServiceActorRouteFence {
    const route = this.tryActorFence(actor);
    if (route === undefined) throw new ServiceStaleGenerationError('actor', actor.actorId);
    return route;
  }

  private tryActorFence(actor: ServiceActorRef): ServiceActorRouteFence | undefined {
    const local = actor.nodeRid === this.nodeRid ? this.registry.actor(actor.actorId) : undefined;
    if (local !== undefined && sameActorRef(local.ref, actor)) {
      return {
        actor,
        targetNodeGeneration: this.nodeGeneration,
        authorityOwnerGeneration: local.authorityOwnerGeneration
      };
    }
    const route = this.actorRoutes.get(actorKey(actor));
    return route !== undefined && sameActorRef(route.actor, actor) ? route : undefined;
  }

  private trySpotFence(
    targetNodeRid: string,
    spot: ServiceSpotRef
  ): ServiceSpotRouteFence | undefined {
    const local = targetNodeRid === this.nodeRid ? this.registry.spot(spot.spotRid) : undefined;
    if (local !== undefined && sameSpotRef(local.ref, spot)) {
      return {
        spot,
        targetNodeRid,
        targetNodeGeneration: this.nodeGeneration,
        authorityOwnerGeneration: local.authorityOwnerGeneration
      };
    }
    const route = this.spotRoutes.get(spotKey(spot));
    return route !== undefined
      && route.targetNodeRid === targetNodeRid
      && sameSpotRef(route.spot, spot)
      ? route
      : undefined;
  }

  private peerGeneration(nodeRid: string): bigint {
    if (nodeRid === this.nodeRid) return this.nodeGeneration;
    const peer = this.raw.topology.peer(nodeRid);
    if (peer === undefined) {
      throw new Error(`Node '${nodeRid}' has no admitted lifecycle generation.`);
    }
    return peer.descriptor.lifecycleGeneration;
  }

  private commitJoinedActor(actor: ServiceActorRef, spot: ServiceSpotRef, membershipEpoch: bigint): void {
    const current = this.registry.actor(actor.actorId);
    if (current === undefined) {
      this.registry.restoreActor(
        { ...actor, nodeRid: this.nodeRid },
        'actor',
        spot,
        membershipEpoch
      );
    } else {
      this.registry.joinActor(current.ref, spot);
    }
  }

  private requireOpen(): void {
    if (this.closed) throw new Error('Stateful runtime is closed.');
  }
}

function lookupKindData(actor: ServiceActorState): ReceiveKindData {
  return {
    kind: 'actorLookupCompletion',
    location: actorLocation(actor)
  };
}

function actorLocation(actor: ServiceActorState): ActorLocation {
  return {
    actor: actor.ref,
    spotRid: actor.spot.spotRid,
    spotGeneration: actor.spot.generation,
    membershipEpoch: actor.membershipEpoch
  };
}

function failure(error: unknown): ServiceStatefulResult {
  return error instanceof ServiceStaleGenerationError
    ? { terminalResult: RequestResult.NotFound, failureCode: ACTOR_ROUTE_STALE }
    : { terminalResult: RequestResult.InternalError, failureCode: 0 };
}

function actorKey(actor: ServiceActorRef): string {
  return `${actor.actorId}\0${actor.generation}`;
}

function spotKey(spot: ServiceSpotRef): string {
  return `${spot.spotRid}\0${spot.generation}`;
}

function sameActorRef(left: ServiceActorRef, right: ServiceActorRef): boolean {
  return left.nodeRid === right.nodeRid
    && left.actorId === right.actorId
    && left.generation === right.generation;
}

function sameSpotRef(left: ServiceSpotRef, right: ServiceSpotRef): boolean {
  return left.spotRid === right.spotRid && left.generation === right.generation;
}

function statefulCorrelation(record: ServiceStatefulWireRecord): bigint | undefined {
  if ('correlation' in record) return record.correlation;
  return record.kind === 'instanceSpot' && record.operationKind === 'request'
    ? record.replyRouteId
    : undefined;
}

function sameInstanceRoute(left: ServiceInstanceRouteFence, right: ServiceInstanceRouteFence): boolean {
  return left.targetNodeRid === right.targetNodeRid
    && left.targetNodeGeneration === right.targetNodeGeneration
    && left.targetSpotRid === right.targetSpotRid
    && left.objectGeneration === right.objectGeneration
    && left.ownerId === right.ownerId
    && left.authorityOwnerGeneration === right.authorityOwnerGeneration
    && left.leaseGeneration === right.leaseGeneration
    && left.storeVersion === right.storeVersion;
}

function routeMatchesLocal(
  route: ServiceInstanceRouteFence,
  spot: ServiceSpotState,
  nodeRid: string,
  nodeGeneration: bigint
): boolean {
  return route.targetNodeRid === nodeRid
    && route.targetNodeGeneration === nodeGeneration
    && route.targetSpotRid === spot.ref.spotRid
    && route.objectGeneration === spot.ref.generation
    && route.authorityOwnerGeneration === spot.authorityOwnerGeneration;
}

function instanceOperationKey(
  record: Extract<ServiceStatefulWireRecord, { readonly kind: 'instanceSpot' }>
): string {
  return `${record.sourceNodeRid}\0${record.sourceNodeGeneration}\0`
    + `${record.operation.high}\0${record.operation.low}`;
}

function subscriptionKey(channelName: string, topicFilter: string): string {
  if (channelName.length === 0 || topicFilter.length === 0) {
    throw new TypeError('Channel name and topic filter must be non-empty.');
  }
  return `${channelName}\0${topicFilter}`;
}

function topicMatches(filter: string, topic: string): boolean {
  if (filter === '*' || filter === '#') return true;
  if (filter.endsWith('*')) return topic.startsWith(filter.slice(0, -1));
  return filter === topic;
}

function emptyPayload(): ServiceApplicationPayload {
  return {
    packetName: 'ZLinkFrameworkEmpty',
    contentType: 'application/octet-stream',
    payload: Buffer.alloc(0)
  };
}

export function statefulMailboxData(record: ServiceMailboxRecord): ServiceStatefulMailboxData | undefined {
  return record.stateful as ServiceStatefulMailboxData | undefined;
}
