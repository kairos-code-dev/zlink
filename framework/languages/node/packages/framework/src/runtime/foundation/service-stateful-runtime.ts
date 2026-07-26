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
  encodeUserSpotCloseHeader,
  encodeUserSpotCreateHeader,
  M6bServiceWireCommand,
  M6bServiceWireFlag,
  sessionBindingFromWire,
  type ServiceActorRouteFence,
  type ServiceInstanceActivationTarget,
  type ServiceInstanceRouteFence,
  type ServiceSpotRouteFence,
  type ServiceStatefulReplyTail,
  type ServiceStatefulWireRecord,
  type ServiceUserSpotCloseRecord,
  type ServiceUserSpotCreateRecord
} from './service-stateful-wire-codec';
import {
  decodeApplicationPayload,
  encodeApplicationPayload,
  type ServiceApplicationPayload,
  ServiceWireProtocolError
} from './service-wire-m6a-codec';
import type {
  ServiceInstanceActivationRecoveryEnvelope
} from './service-instance-activation-recovery-codec';
import { validateServiceMetadataFrame } from './service-metadata-codec';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';

const ACTOR_ROUTE_STALE = 21;
const SPOT_MOVING = 34;
const USER_SPOT_OPERATION_CAPACITY = 65_536;
const USER_SPOT_OPERATION_REPLAY_RETENTION_MS = 5 * 60_000;
const ACTOR_ROUTE_NOT_FOUND = 1;

export interface ServiceStatefulResult {
  readonly terminalResult: number;
  readonly failureCode: number;
  readonly payload?: ServiceApplicationPayload;
  readonly kindData?: ReceiveKindData;
  readonly applicationMetadata?: Buffer;
}

export interface ServiceStatefulPendingOperation {
  readonly id: bigint;
  readonly promise: Promise<ServiceStatefulResult>;
}

export interface ServiceStatefulMailboxData {
  readonly correlation?: bigint;
  readonly receiveKind: number;
  readonly operationKind: number;
  readonly sourceSpotId?: string;
  readonly sourceActor?: ServiceActorRef;
  readonly sourceBindingGeneration?: bigint;
  readonly channelName?: string;
  readonly topic?: string;
  readonly targetSpot?: ServiceSpotRef;
  readonly targetActor?: ServiceActorRef;
  readonly kindData?: ReceiveKindData;
  readonly applicationMetadata?: Buffer;
  readonly onTerminalCompletion?: () => void | Promise<void>;
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

export interface ServiceInstanceApplicationLifecycle {
  materialize(target: ServiceInstanceActivationTarget, objectGeneration: bigint): Promise<void>;
  discard(target: ServiceInstanceActivationTarget): Promise<void>;
}

export interface ServicePendingInstanceActivation {
  readonly reservationId: string;
  readonly storeVersion: string;
  readonly objectGeneration: bigint;
  readonly authorityOwnerGeneration: bigint;
  readonly ownerId: string;
  readonly ownerLeaseGeneration: bigint;
  readonly meshName: string;
  readonly nodeRid: string;
  readonly nodeGeneration: bigint;
  readonly requestReference: string;
  readonly requestSha256: Uint8Array;
  readonly requestEncodedSize: bigint;
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
    activation: Omit<ServiceInstanceActivationRecoveryEnvelope, 'targetMeshName'>
  ): Promise<ServiceInstanceAuthorityReserve>;
  resume(
    target: ServiceInstanceActivationTarget,
    pending: ServicePendingInstanceActivation
  ): Promise<ServiceInstanceActivationReservation>;
  commit(
    target: ServiceInstanceActivationTarget,
    reservation: ServiceInstanceActivationReservation,
    spot: ServiceSpotState
  ): Promise<{ readonly kind: 'committed' | 'lost'; readonly route: ServiceInstanceRouteFence }>;
  complete(
    target: ServiceInstanceActivationTarget,
    route: ServiceInstanceRouteFence
  ): Promise<ServiceInstanceRouteFence>;
  abort(
    target: ServiceInstanceActivationTarget,
    reservation: ServiceInstanceActivationReservation
  ): Promise<void>;
}

export class ServiceInstanceActivationRedirectError extends Error {
  constructor(readonly route: ServiceInstanceRouteFence) {
    super(`Instance Spot '${route.targetSpotId}' is owned by '${route.targetNodeRid}'.`);
    this.name = 'ServiceInstanceActivationRedirectError';
  }
}

export interface ServiceUserSpotOperationResult {
  readonly terminalResult: number;
  readonly failureCode: number;
  readonly tail?: Extract<
    ServiceStatefulReplyTail,
    { readonly kind: 'userSpotCreate' | 'userSpotClose' }
  >;
  readonly payload?: ServiceApplicationPayload;
}

export interface ServiceUserSpotOperationHandler {
  create(record: ServiceUserSpotCreateRecord, signal: AbortSignal): Promise<ServiceUserSpotOperationResult>;
  close(record: ServiceUserSpotCloseRecord, signal: AbortSignal): Promise<ServiceUserSpotOperationResult>;
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
  private readonly spotRouteStoreVersions = new Map<string, string>();
  private nextSpotId = 1n;
  private nextSessionSequence = 1n;
  private nextInstanceOperation = 1n;
  private nextUserSpotOperation = 1n;
  private instanceAuthority?: ServiceInstanceActivationAuthority;
  private asyncInstanceAuthority?: ServiceAsyncInstanceActivationAuthority;
  private instanceApplicationLifecycle?: ServiceInstanceApplicationLifecycle;
  private userSpotOperationHandler?: ServiceUserSpotOperationHandler;
  private readonly admittedUserSpotOperations = new Map<string, {
    readonly request: string;
    readonly deadlineUnixMs: bigint;
    readonly result: Promise<ServiceUserSpotOperationResult>;
    settled: boolean;
  }>();
  private readonly pendingInstanceActivations = new Map<string, Promise<{
    readonly spot: ServiceSpotState;
    readonly route: ServiceInstanceRouteFence;
  }>>();
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

  restoreUserSpotAuthority(
    spotId: string,
    stableType: string,
    generation: bigint,
    authorityOwnerGeneration: bigint
  ): ServiceSpotState {
    this.requireOpen();
    return this.registry.restoreSpot(
      { spotId, generation },
      'user',
      stableType,
      authorityOwnerGeneration
    );
  }

  restoreSpotAuthority(
    spotId: string,
    objectKind: 'user_spot' | 'instance_spot',
    stableType: string,
    generation: bigint,
    authorityOwnerGeneration: bigint
  ): ServiceSpotState {
    this.requireOpen();
    return this.registry.restoreSpot(
      { spotId, generation },
      objectKind === 'user_spot' ? 'user' : 'instance',
      stableType,
      authorityOwnerGeneration
    );
  }

  entrySpot(): ServiceSpotState {
    return this.registry.createEntrySpot(this.nodeRid);
  }

  createActor(actorId: string, stableType = 'actor'): ServiceActorState {
    this.requireOpen();
    return this.registry.createActor(actorId, stableType);
  }

  restoreActorAuthority(
    actorId: string,
    stableType: string,
    generation: bigint,
    authorityOwnerGeneration: bigint,
    spotId: string,
    spotGeneration: bigint,
    membershipEpoch: bigint
  ): ServiceActorState {
    this.requireOpen();
    return this.registry.restoreActor(
      { nodeRid: this.nodeRid, actorId, generation },
      stableType,
      { spotId, generation: spotGeneration },
      membershipEpoch,
      authorityOwnerGeneration
    );
  }

  discardRelocatedActor(actor: ServiceActorRef): void {
    this.requireOpen();
    this.registry.destroyActor(actor);
  }

  restoreActorSessionBinding(
    actorRef: ServiceActorRef,
    sessionNodeRid: string,
    sessionRid: string,
    bindingGeneration: bigint
  ): void {
    this.requireOpen();
    const actor = this.registry.actor(actorRef.actorId);
    if (
      actor === undefined
      || actor.ref.generation !== actorRef.generation
      || actor.ref.nodeRid !== actorRef.nodeRid
    ) {
      throw new ServiceStaleGenerationError('actor', actorRef.actorId);
    }
    const current = this.registry.binding(actor.ref);
    if (
      current?.bindingGeneration === bindingGeneration
      && current.sessionOwnerNodeRid === sessionNodeRid
      && current.sessionRid === sessionRid
    ) {
      return;
    }
    this.registry.installSessionBinding({
      actor: actor.ref,
      sessionRid,
      sessionOwnerNodeRid: sessionNodeRid,
      bindingGeneration,
      membershipEpoch: actor.membershipEpoch
    });
  }

  activateInstanceSpot(
    spotId: string,
    instanceType: string,
    attempt: bigint
  ): { readonly spot: ServiceSpotState; readonly created: boolean } {
    this.requireOpen();
    const result = this.registry.reserve('instanceSpot', spotId, instanceType, attempt);
    if (result.kind === 'existing') {
      if (result.spot === undefined) throw new Error('Instance Spot reservation lost its Spot state.');
      return { spot: result.spot, created: false };
    }
    if (result.kind === 'typeMismatch') {
      throw new TypeError(`Instance Spot '${spotId}' is assigned to another type.`);
    }
    if (result.kind === 'attemptStale') {
      throw new ServiceStaleGenerationError('spot', spotId);
    }
    const spot = this.registry.commitReservation(result.reservation);
    if (!('kind' in spot) || spot.kind !== 'instance') {
      throw new Error('Instance Spot reservation produced another object kind.');
    }
    return { spot, created: true };
  }

  registerInstanceIntent(instanceType: string, route: ServiceInstanceRouteFence): void {
    if (route.targetNodeRid !== this.nodeRid || route.targetNodeGeneration !== this.nodeGeneration) {
      throw new ServiceStaleGenerationError('spot', route.targetSpotId);
    }
    const current = this.instanceIntents.get(route.targetSpotId);
    if (current !== undefined) {
      if (current.route.authorityOwnerGeneration > route.authorityOwnerGeneration) {
        return;
      }
      if (current.route.authorityOwnerGeneration === route.authorityOwnerGeneration) {
        if (current.route.storeVersion === route.storeVersion) {
          if (current.instanceType === instanceType && sameInstanceRoute(current.route, route)) {
            return;
          }
          throw new ServiceStaleGenerationError('spot', route.targetSpotId);
        }
      }
    }
    this.instanceIntents.set(route.targetSpotId, Object.freeze({ instanceType, route: { ...route } }));
  }

  forgetInstanceIntent(
    spotId: string,
    authorityOwnerGeneration: bigint,
    storeVersion?: string
  ): void {
    const current = this.instanceIntents.get(spotId);
    if (
      current?.route.authorityOwnerGeneration === authorityOwnerGeneration
      && (storeVersion === undefined || current.route.storeVersion === storeVersion)
    ) {
      this.instanceIntents.delete(spotId);
    }
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

  registerInstanceApplicationLifecycle(
    lifecycle: ServiceInstanceApplicationLifecycle
  ): void {
    if (this.instanceApplicationLifecycle !== undefined) {
      throw new Error('Instance application lifecycle is already registered.');
    }
    this.instanceApplicationLifecycle = lifecycle;
  }

  registerUserSpotOperationHandler(handler: ServiceUserSpotOperationHandler): void {
    this.requireOpen();
    if (
      this.userSpotOperationHandler !== undefined
      && this.userSpotOperationHandler !== handler
    ) {
      throw new Error('User Spot operation handler is already registered.');
    }
    this.userSpotOperationHandler = handler;
  }

  requestUserSpotCreate(
    targetNodeRid: string,
    request: Omit<ServiceUserSpotCreateRecord, 'kind' | 'correlation' | 'operation'>,
    timeoutMs: number
  ): Promise<ServiceUserSpotOperationResult> {
    const correlation = this.nextUserSpotOperation++;
    const operation = {
      high: this.nodeGeneration,
      low: correlation
    };
    return this.requestUserSpotOperation(
      targetNodeRid,
      encodeUserSpotCreateHeader({
        ...request,
        sourceNodeRid: this.nodeRid,
        sourceNodeGeneration: this.nodeGeneration,
        correlation,
        operation
      }),
      correlation,
      'userSpotCreate',
      timeoutMs
    );
  }

  requestUserSpotClose(
    targetNodeRid: string,
    request: Omit<ServiceUserSpotCloseRecord, 'kind' | 'correlation' | 'operation'>,
    timeoutMs: number
  ): Promise<ServiceUserSpotOperationResult> {
    const correlation = this.nextUserSpotOperation++;
    const operation = {
      high: this.nodeGeneration,
      low: correlation
    };
    return this.requestUserSpotOperation(
      targetNodeRid,
      encodeUserSpotCloseHeader({
        ...request,
        sourceNodeRid: this.nodeRid,
        sourceNodeGeneration: this.nodeGeneration,
        correlation,
        operation
      }),
      correlation,
      'userSpotClose',
      timeoutMs
    );
  }

  async recoverInstanceActivation(
    envelope: ServiceInstanceActivationRecoveryEnvelope,
    route: ServiceInstanceRouteFence
  ): Promise<void> {
    const target = envelope.target;
    if (
      target.targetNodeRid !== this.nodeRid
      || target.targetNodeGeneration !== this.nodeGeneration
      || target.targetSpotId !== route.targetSpotId
      || route.targetNodeRid !== this.nodeRid
      || route.targetNodeGeneration !== this.nodeGeneration
    ) {
      throw new ServiceStaleGenerationError('spot', target.targetSpotId);
    }
    const authority = this.asyncInstanceAuthority;
    if (authority === undefined) {
      throw new Error('Async Instance activation authority is not registered.');
    }
    await this.instanceApplicationLifecycle?.materialize(target, route.objectGeneration);
    const spot = this.registry.spot(target.targetSpotId)
      ?? this.registry.restoreSpot(
        {
          spotId: target.targetSpotId,
          generation: route.objectGeneration
        },
        'instance',
        target.stableType,
        route.authorityOwnerGeneration
      );
    if (
      spot.kind !== 'instance'
      || spot.stableType !== target.stableType
      || spot.ref.generation !== route.objectGeneration
      || spot.authorityOwnerGeneration !== route.authorityOwnerGeneration
    ) {
      throw new ServiceStaleGenerationError('spot', target.targetSpotId);
    }
    this.publishCommittedInstanceRoute(target.stableType, route);
    const record = {
      kind: 'instanceSpot' as const,
      activation: 'missing' as const,
      target,
      sourceNodeGeneration: envelope.sourceNodeGeneration,
      sourceNodeRid: envelope.sourceNodeRid,
      ...(envelope.sourceSpotId === undefined
        ? {}
        : { sourceSpotId: envelope.sourceSpotId }),
      operationKind: envelope.operationKind,
      operation: envelope.operation,
      deadlineUnixMs: envelope.deadlineUnixMs,
      ...(envelope.replyRouteId === undefined ? {} : { replyRouteId: envelope.replyRouteId })
    };
    const admitted = this.enqueueActivatedInstanceSpot(
      {
        command: M6bServiceWireCommand.instanceSpot,
        flags: envelope.metadataFrame === undefined ? 0 : M6bServiceWireFlag.metadata,
        sourceRoutingId: envelope.sourceNodeRid,
        parts: [
          encodeInstanceSpotActivationHeader(
            target,
            envelope.sourceNodeGeneration,
            envelope.sourceNodeRid,
            envelope.sourceSpotId,
            envelope.operationKind,
            envelope.operation,
            envelope.deadlineUnixMs,
            envelope.replyRouteId,
            envelope.metadataFrame !== undefined
          )
        ]
      },
      record,
      envelope.applicationPayload,
      spot,
      undefined,
      this.activationTerminalCompletion(target, route),
      envelope.metadataFrame === undefined
        ? undefined
        : validateServiceMetadataFrame(envelope.metadataFrame)
    );
    if (admitted !== 'application') {
      throw new Error('Recovered Instance activation was not admitted to the local queue.');
    }
  }

  async recoverPendingInstanceActivation(
    envelope: ServiceInstanceActivationRecoveryEnvelope,
    pending: ServicePendingInstanceActivation
  ): Promise<void> {
    const target = envelope.target;
    if (
      target.targetNodeRid !== this.nodeRid
      || target.targetNodeGeneration !== this.nodeGeneration
      || pending.nodeRid !== this.nodeRid
      || pending.nodeGeneration !== this.nodeGeneration
      || pending.meshName !== envelope.targetMeshName
    ) {
      throw new ServiceStaleGenerationError('spot', target.targetSpotId);
    }
    const authority = this.asyncInstanceAuthority;
    if (authority === undefined) {
      throw new Error('Async Instance activation authority is not registered.');
    }
    const reservation = await authority.resume(target, pending);
    await this.instanceApplicationLifecycle?.materialize(target, pending.objectGeneration);
    const spot = this.registry.restoreSpot(
      {
        spotId: target.targetSpotId,
        generation: pending.objectGeneration
      },
      'instance',
      target.stableType,
      pending.authorityOwnerGeneration
    );
    const committed = await authority.commit(target, reservation, spot);
    if (committed.kind !== 'committed') {
      this.registry.closeSpot(spot.ref);
      throw new ServiceInstanceActivationRedirectError(committed.route);
    }
    await this.recoverInstanceActivation(envelope, committed.route);
  }

  async completeRecoveredInstanceActivation(
    target: ServiceInstanceActivationTarget,
    route: ServiceInstanceRouteFence
  ): Promise<ServiceInstanceRouteFence> {
    const authority = this.asyncInstanceAuthority;
    if (authority === undefined) {
      throw new Error('Async Instance activation authority is not registered.');
    }
    const released = await authority.complete(target, route);
    this.publishCommittedInstanceRoute(target.stableType, released);
    return released;
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

  rememberSpotRoute(route: ServiceSpotRouteFence, storeVersion = ''): void {
    const key = spotKey(route.spot);
    const current = this.spotRoutes.get(key);
    if (current !== undefined) {
      if (current.authorityOwnerGeneration > route.authorityOwnerGeneration) {
        return;
      }
      if (current.authorityOwnerGeneration === route.authorityOwnerGeneration) {
        if (this.spotRouteStoreVersions.get(key) === storeVersion) {
          if (sameSpotRoute(current, route)) {
            return;
          }
          throw new ServiceStaleGenerationError('spot', route.spot.spotId);
        }
      }
    }
    this.spotRoutes.set(key, Object.freeze({
      spot: Object.freeze({ ...route.spot }),
      targetNodeRid: route.targetNodeRid,
      targetNodeGeneration: route.targetNodeGeneration,
      authorityOwnerGeneration: route.authorityOwnerGeneration
    }));
    this.spotRouteStoreVersions.set(key, storeVersion);
  }

  forgetSpotRoute(
    spot: ServiceSpotRef,
    authorityOwnerGeneration: bigint,
    storeVersion?: string
  ): void {
    const key = spotKey(spot);
    const current = this.spotRoutes.get(key);
    if (
      current?.authorityOwnerGeneration === authorityOwnerGeneration
      && (storeVersion === undefined || this.spotRouteStoreVersions.get(key) === storeVersion)
    ) {
      this.spotRoutes.delete(key);
      this.spotRouteStoreVersions.delete(key);
    }
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
    const subscriptions = this.subscriptions.get(spot.ref.spotId) ?? new Set<string>();
    subscriptions.add(key);
    this.subscriptions.set(spot.ref.spotId, subscriptions);
  }

  unsetSubscription(spot: ServiceSpotState, channelName: string, topicFilter: string): void {
    const subscriptions = this.subscriptions.get(spot.ref.spotId);
    if (subscriptions === undefined) return;
    subscriptions.delete(subscriptionKey(channelName, topicFilter));
    if (subscriptions.size === 0) this.subscriptions.delete(spot.ref.spotId);
  }

  clearSubscriptions(spotId: string): void {
    this.subscriptions.delete(spotId);
  }

  publishLogicalMulticast(
    channelName: string,
    topic: string,
    payload: ServiceApplicationPayload,
    sourceSpotId = this.nodeRid
  ): void {
    this.enqueueLogicalMulticast(channelName, topic, sourceSpotId, payload);
    const targets = this.raw.topology.peers()
      .filter(peer => peer.descriptor.channels.some(
        channel => channel.name === channelName && channel.weight > 0
      ));
    const header = encodeLogicalMulticastHeader(channelName, topic, sourceSpotId);
    const payloadFrame = encodeApplicationPayload(payload);
    for (const target of targets) {
      // Remote admission ends at the source outbound transport queue. The
      // receiver's Spot queue and handler completion are not publish results.
      this.raw.sendService(target.descriptor.nodeRoutingId, [header, payloadFrame]);
    }
  }

  sendToSpot(
    sourceSpotId: string,
    targetNodeRid: string,
    targetSpot: ServiceSpotRef,
    _targetNodeGeneration: bigint,
    _authorityOwnerGeneration: bigint,
    payload: ServiceApplicationPayload
  ): number {
    const target = this.trySpotFence(targetNodeRid, targetSpot);
    if (target === undefined) return SubmitResult.NotFound;
    const header = encodeSpotHeader('spotSend', sourceSpotId, target);
    return this.submitOneWay(targetNodeRid, [header, encodeApplicationPayload(payload)]);
  }

  requestToSpot(
    sourceSpotId: string,
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
    const header = encodeSpotHeader('spotRequest', sourceSpotId, target, pending.id);
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
    sourceSpotId?: string,
    metadataFrame?: Uint8Array
  ): number {
    return this.submitOneWay(route.targetNodeRid, instanceOperationParts([
      encodeInstanceSpotHeader(
        route,
        this.nodeGeneration,
        this.nodeRid,
        sourceSpotId,
        'send',
        { high: 0n, low: 0n },
        undefined,
        metadataFrame !== undefined
      ),
      encodeApplicationPayload(payload)
    ], metadataFrame));
  }

  sendToMissingInstanceSpot(
    target: ServiceInstanceActivationTarget,
    payload: ServiceApplicationPayload,
    deadlineUnixMs: bigint,
    sourceSpotId?: string,
    metadataFrame?: Uint8Array
  ): number {
    const operation = { high: this.nodeGeneration, low: this.nextInstanceOperation++ };
    return this.submitOneWay(target.targetNodeRid, instanceOperationParts([
      encodeInstanceSpotActivationHeader(
        target,
        this.nodeGeneration,
        this.nodeRid,
        sourceSpotId,
        'send',
        operation,
        deadlineUnixMs,
        undefined,
        metadataFrame !== undefined
      ),
      encodeApplicationPayload(payload)
    ], metadataFrame));
  }

  requestToInstanceSpot(
    route: ServiceInstanceRouteFence,
    payload: ServiceApplicationPayload,
    timeoutMs: number,
    sourceSpotId?: string,
    metadataFrame?: Uint8Array
  ): ServiceStatefulPendingOperation {
    const pending = this.operations.reserve(timeoutMs);
    this.submitRequest(
      pending,
      route.targetNodeRid,
      instanceOperationParts([
        encodeInstanceSpotHeader(
          route,
          this.nodeGeneration,
          this.nodeRid,
          sourceSpotId,
          'request',
          { high: 2n, low: pending.id },
          pending.id,
          metadataFrame !== undefined
        ),
        encodeApplicationPayload(payload)
      ], metadataFrame),
      timeoutMs,
      'instanceSpotRequest'
    );
    return pending;
  }

  requestToMissingInstanceSpot(
    target: ServiceInstanceActivationTarget,
    payload: ServiceApplicationPayload,
    timeoutMs: number,
    sourceSpotId?: string,
    metadataFrame?: Uint8Array
  ): ServiceStatefulPendingOperation {
    const pending = this.operations.reserve(timeoutMs);
    const deadlineUnixMs = BigInt(Date.now() + timeoutMs);
    this.submitRequest(
      pending,
      target.targetNodeRid,
      instanceOperationParts([
        encodeInstanceSpotActivationHeader(
          target,
          this.nodeGeneration,
          this.nodeRid,
          sourceSpotId,
          'request',
          { high: this.nodeGeneration, low: pending.id },
          deadlineUnixMs,
          pending.id,
          metadataFrame !== undefined
        ),
        encodeApplicationPayload(payload)
      ], metadataFrame),
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
      targetSpot.spotId === targetNodeRid,
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
        this.enqueueActorControl(transition.actor.spot.spotId, {
          kind: 'actorControl',
          lifecycleKind: ActorLifecycleKind.Left,
          previousActor: transition.actor.ref,
          currentActor: transition.actor.ref,
          previousSpotId: transition.previousSpot.spotId,
          currentSpotId: transition.currentSpot.spotId,
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
    this.instanceIntents.clear();
    this.admittedUserSpotOperations.clear();
    this.actorRoutes.clear();
    this.spotRoutes.clear();
    this.spotRouteStoreVersions.clear();
  }

  private ingress(record: RawServiceIngressRecord): RawServicePumpResult | undefined {
    if (
      record.command < M6bServiceWireCommand.spotSend
      || (
        record.command > M6bServiceWireCommand.instanceSpot
        && record.command !== M6bServiceWireCommand.userSpotCreate
        && record.command !== M6bServiceWireCommand.userSpotClose
      )
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
      const instanceMetadata = decoded.kind === 'instanceSpot'
        && (record.flags & M6bServiceWireFlag.metadata) !== 0;
      const userSpotInfrastructure = decoded.kind === 'userSpotCreate'
        || decoded.kind === 'userSpotClose';
      if (
        record.parts.length < 1
        || record.parts.length > (instanceMetadata ? 3 : 2)
        || (hasPayload && decoded.kind !== 'actorJoin' && record.parts.length !== 2)
        || (
          decoded.kind === 'instanceSpot'
          && record.parts.length !== (instanceMetadata ? 3 : 2)
        )
        || (userSpotInfrastructure && record.parts.length !== 1)
      ) {
        return 'protocolError';
      }
      const metadataFrame = instanceMetadata
        ? validateServiceMetadataFrame(record.parts[1]!)
        : undefined;
      const payload = record.parts.length >= 2
        ? decodeApplicationPayload(record.parts[record.parts.length - 1]!)
        : undefined;
      try {
        return this.handleIngress(record, decoded, payload, metadataFrame);
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
    payload: ServiceApplicationPayload | undefined,
    metadataFrame?: Buffer
  ): RawServicePumpResult {
    switch (record.kind) {
      case 'spotSend':
      case 'spotRequest':
        this.validateSpotFence(record.target);
        return this.enqueueApplication(
          ingress,
          `spot:${record.target.spot.spotId}`,
          payload!,
          {
            receiveKind: record.kind === 'spotSend' ? ReceiveKind.SpotSend : ReceiveKind.SpotRequest,
            operationKind: record.kind === 'spotRequest' ? OperationKind.SpotRequest : 0,
            ...(record.correlation === undefined ? {} : { correlation: record.correlation }),
            sourceSpotId: record.sourceSpotId,
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
          record.sourceSpotId,
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
        return this.enqueueInstanceSpot(ingress, record, payload!, metadataFrame);
      case 'userSpotCreate':
      case 'userSpotClose':
        return this.handleUserSpotOperation(ingress, record);
    }
  }

  private enqueueInstanceSpot(
    ingress: RawServiceIngressRecord,
    record: Extract<ServiceStatefulWireRecord, { readonly kind: 'instanceSpot' }>,
    payload: ServiceApplicationPayload,
    metadataFrame?: Buffer
  ): RawServicePumpResult {
    if (record.activation === 'missing' && this.asyncInstanceAuthority !== undefined) {
      void this.continueMissingInstanceActivation(ingress, record, payload, undefined, metadataFrame);
      return 'infrastructure';
    }
    const spot = this.requireInstanceActivation(ingress, record);
    return this.enqueueActivatedInstanceSpot(ingress, record, payload, spot, undefined, undefined, metadataFrame);
  }

  private enqueueActivatedInstanceSpot(
    ingress: RawServiceIngressRecord,
    record: Extract<ServiceStatefulWireRecord, { readonly kind: 'instanceSpot' }>,
    payload: ServiceApplicationPayload,
    spot: ServiceSpotState,
    localReply?: NonNullable<ServiceStatefulMailboxData['reply']>,
    onTerminalCompletion?: NonNullable<ServiceStatefulMailboxData['onTerminalCompletion']>,
    metadataFrame?: Buffer
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
      `spot:${spot.ref.spotId}`,
      payload,
      {
        receiveKind: ReceiveKind.InstanceSpotActivation,
        operationKind: record.operationKind === 'request' ? OperationKind.InstanceSpotRequest : 0,
        ...(record.operationKind === 'request' ? { correlation: record.replyRouteId } : {}),
        ...(record.sourceSpotId === undefined ? {} : { sourceSpotId: record.sourceSpotId }),
        targetSpot: spot.ref,
        ...(metadataFrame === undefined ? {} : { applicationMetadata: metadataFrame }),
        ...(onTerminalCompletion === undefined ? {} : { onTerminalCompletion }),
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
    localReply?: NonNullable<ServiceStatefulMailboxData['reply']>,
    metadataFrame?: Buffer
  ): Promise<void> {
    try {
      this.validateInstanceIngress(ingress, record);
      const activation = await this.activateMissingInstanceAsync(record, payload, metadataFrame);
      if (this.closed) return;
      const admitted = this.enqueueActivatedInstanceSpot(
        ingress,
        record,
        payload,
        activation.spot,
        localReply,
        this.activationTerminalCompletion(record.target, activation.route),
        metadataFrame
      );
      if (admitted !== 'application') {
        throw new Error('Activated Instance message was not admitted to the local queue.');
      }
    } catch (error) {
      let terminalError = error;
      if (error instanceof ServiceInstanceActivationRedirectError && !this.closed) {
        try {
          await this.relayMissingInstanceActivation(
            record,
            payload,
            error.route,
            ingress,
            localReply,
            metadataFrame
          );
          return;
        } catch (relayError) {
          terminalError = relayError;
        }
      }
      if (record.operationKind !== 'request') return;
      const result = failure(terminalError);
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

  private activationTerminalCompletion(
    target: ServiceInstanceActivationTarget,
    route: ServiceInstanceRouteFence
  ): () => Promise<void> {
    let completion: Promise<void> | undefined;
    return () => completion ??= (async () => {
      const released = await this.asyncInstanceAuthority?.complete(target, route);
      if (released !== undefined) {
        this.publishCommittedInstanceRoute(target.stableType, released);
      }
    })();
  }

  private async relayMissingInstanceActivation(
    record: Extract<
      ServiceStatefulWireRecord,
      { readonly kind: 'instanceSpot'; readonly activation: 'missing' }
    >,
    payload: ServiceApplicationPayload,
    route: ServiceInstanceRouteFence,
    ingress: RawServiceIngressRecord,
    localReply?: NonNullable<ServiceStatefulMailboxData['reply']>,
    metadataFrame?: Buffer
  ): Promise<void> {
    const remainingMs = Number(record.deadlineUnixMs - BigInt(Date.now()));
    if (remainingMs <= 0) {
      throw new ServiceStaleGenerationError('spot', route.targetSpotId);
    }
    const redirectedTarget: ServiceInstanceActivationTarget = {
      ...record.target,
      targetNodeRid: route.targetNodeRid,
      targetNodeGeneration: route.targetNodeGeneration,
      targetSpotId: route.targetSpotId
    };
    if (record.operationKind === 'send') {
      const submitted = this.submitOneWay(route.targetNodeRid, instanceOperationParts([
        encodeInstanceSpotActivationHeader(
          redirectedTarget,
          this.nodeGeneration,
          this.nodeRid,
          record.sourceSpotId,
          'send',
          record.operation,
          record.deadlineUnixMs,
          undefined,
          metadataFrame !== undefined
        ),
        encodeApplicationPayload(payload)
      ], metadataFrame));
      if (submitted !== SubmitResult.Ok) {
        throw new Error(`Instance activation redirect was not admitted: ${submitted}.`);
      }
      return;
    }

    const pending = this.operations.reserve(remainingMs);
    this.submitRequest(
      pending,
      route.targetNodeRid,
      instanceOperationParts([
        encodeInstanceSpotActivationHeader(
          redirectedTarget,
          this.nodeGeneration,
          this.nodeRid,
          record.sourceSpotId,
          'request',
          record.operation,
          record.deadlineUnixMs,
          pending.id,
          metadataFrame !== undefined
        ),
        encodeApplicationPayload(payload)
      ], metadataFrame),
      remainingMs,
      'instanceSpotRequest'
    );
    const result = await pending.promise;
    if (localReply !== undefined) {
      localReply(result.terminalResult, result.failureCode, result.payload);
      return;
    }
    if (record.replyRouteId !== undefined) {
      this.replyWire(
        ingress,
        record.replyRouteId,
        result.terminalResult,
        result.failureCode,
        result.payload
      );
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
      throw new ServiceStaleGenerationError('spot', record.route.targetSpotId);
    }
    const intent = this.instanceIntents.get(record.route.targetSpotId);
    if (intent === undefined || !sameInstanceRoute(intent.route, record.route)) {
      throw new ServiceStaleGenerationError('spot', record.route.targetSpotId);
    }
    const spot = this.registry.spot(record.route.targetSpotId)
      ?? this.registry.restoreSpot(
        {
          spotId: record.route.targetSpotId,
          generation: record.route.objectGeneration
        },
        'instance',
        intent.instanceType,
        record.route.authorityOwnerGeneration
      );
    if (
      spot.state !== 'ready'
      || spot.kind !== 'instance'
      || spot.stableType !== intent.instanceType
      || spot.ref.generation !== record.route.objectGeneration
      || spot.authorityOwnerGeneration !== record.route.authorityOwnerGeneration
    ) {
      throw new ServiceStaleGenerationError('spot', record.route.targetSpotId);
    }
    return spot;
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
          ? record.route.targetSpotId
          : record.target.targetSpotId
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
      throw new ServiceStaleGenerationError('spot', target.targetSpotId);
    }
    const authority = this.instanceAuthority;
    if (authority === undefined) {
      throw new Error('Instance activation authority is not registered.');
    }

    const local = this.registry.spot(target.targetSpotId);
    const current = authority.read(target);
    if (local !== undefined) {
      if (
        local.kind !== 'instance'
        || local.stableType !== target.stableType
        || current.kind !== 'ready'
        || !routeMatchesLocal(current.route, local, this.nodeRid, this.nodeGeneration)
      ) {
        throw new ServiceStaleGenerationError('spot', target.targetSpotId);
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
        target.targetSpotId,
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
      throw new ServiceStaleGenerationError('spot', target.targetSpotId);
    }
    return activation.spot;
  }

  private activateMissingInstanceAsync(
    record: Extract<
      ServiceStatefulWireRecord,
      { readonly kind: 'instanceSpot'; readonly activation: 'missing' }
    >,
    payload: ServiceApplicationPayload,
    metadataFrame?: Buffer
  ): Promise<{ readonly spot: ServiceSpotState; readonly route: ServiceInstanceRouteFence }> {
    const key = instanceOperationKey(record);
    const pending = this.pendingInstanceActivations.get(key);
    if (pending !== undefined) return pending;
    const activation = this.runMissingInstanceActivation(record, payload, metadataFrame);
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
    >,
    payload: ServiceApplicationPayload,
    metadataFrame?: Buffer
  ): Promise<{ readonly spot: ServiceSpotState; readonly route: ServiceInstanceRouteFence }> {
    const target = record.target;
    if (
      target.targetNodeRid !== this.nodeRid
      || target.targetNodeGeneration !== this.nodeGeneration
      || record.deadlineUnixMs < BigInt(Date.now())
    ) {
      throw new ServiceStaleGenerationError('spot', target.targetSpotId);
    }
    const authority = this.asyncInstanceAuthority;
    if (authority === undefined) {
      throw new Error('Async Instance activation authority is not registered.');
    }

    const local = this.registry.spot(target.targetSpotId);
    const current = await authority.read(target);
    if (local !== undefined) {
      if (
        local.kind !== 'instance'
        || local.stableType !== target.stableType
        || current.kind !== 'ready'
        || !routeMatchesLocal(current.route, local, this.nodeRid, this.nodeGeneration)
      ) {
        throw new ServiceStaleGenerationError('spot', target.targetSpotId);
      }
      return { spot: local, route: current.route };
    }
    if (current.kind === 'ready') {
      throw new ServiceInstanceActivationRedirectError(current.route);
    }

    const reserved = await authority.reserve({
      target,
      sourceNodeRid: record.sourceNodeRid,
      sourceNodeGeneration: record.sourceNodeGeneration,
      ...(record.sourceSpotId === undefined ? {} : { sourceSpotId: record.sourceSpotId }),
      operationKind: record.operationKind,
      operation: record.operation,
      ...(record.replyRouteId === undefined ? {} : { replyRouteId: record.replyRouteId }),
      deadlineUnixMs: record.deadlineUnixMs,
      ...(metadataFrame === undefined ? {} : { metadataFrame }),
      applicationPayload: payload
    });
    if (reserved.kind === 'ready') {
      throw new ServiceInstanceActivationRedirectError(reserved.route);
    }
    if (this.closed || record.deadlineUnixMs < BigInt(Date.now())) {
      await authority.abort(target, reserved.reservation);
      throw new ServiceStaleGenerationError('spot', target.targetSpotId);
    }

    let activation: { readonly spot: ServiceSpotState; readonly created: boolean };
    try {
      await this.instanceApplicationLifecycle?.materialize(target, reserved.reservation.attempt);
      activation = this.activateInstanceSpot(
        target.targetSpotId,
        target.stableType,
        reserved.reservation.attempt
      );
    } catch (error) {
      await authority.abort(target, reserved.reservation);
      await this.instanceApplicationLifecycle?.discard(target);
      throw error;
    }
    let committed: Awaited<ReturnType<ServiceAsyncInstanceActivationAuthority['commit']>>;
    try {
      committed = await authority.commit(target, reserved.reservation, activation.spot);
    } catch (error) {
      if (activation.created) this.registry.closeSpot(activation.spot.ref);
      await this.instanceApplicationLifecycle?.discard(target);
      throw error;
    }
    if (committed.kind === 'lost') {
      if (activation.created) this.registry.closeSpot(activation.spot.ref);
      await this.instanceApplicationLifecycle?.discard(target);
      throw new ServiceInstanceActivationRedirectError(committed.route);
    }
    if (!routeMatchesLocal(
      committed.route,
      activation.spot,
      this.nodeRid,
      this.nodeGeneration
    )) {
      if (activation.created) this.registry.closeSpot(activation.spot.ref);
      await this.instanceApplicationLifecycle?.discard(target);
      throw new ServiceStaleGenerationError('spot', target.targetSpotId);
    }
    this.publishCommittedInstanceRoute(target.stableType, committed.route);
    return { spot: activation.spot, route: committed.route };
  }

  private publishCommittedInstanceRoute(
    stableType: string,
    route: ServiceInstanceRouteFence
  ): void {
    this.rememberSpotRoute({
      spot: {
        spotId: route.targetSpotId,
        generation: route.objectGeneration
      },
      targetNodeRid: route.targetNodeRid,
      targetNodeGeneration: route.targetNodeGeneration,
      authorityOwnerGeneration: route.authorityOwnerGeneration
    }, route.storeVersion);
    this.registerInstanceIntent(stableType, route);
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
      previousSpotId: current?.spot.spotId ?? null,
      currentSpotId: record.target.spot.spotId,
      previousSpotGeneration: current?.spot.generation ?? 0n,
      currentSpotGeneration: record.target.spot.generation,
      previousMembershipEpoch: previousEpoch,
      currentMembershipEpoch: previousEpoch + 1n,
      resultCode: 0
    };
    const application = payload ?? emptyPayload();
    return this.enqueueApplication(
      ingress,
      `spot:${record.target.spot.spotId}`,
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
        this.enqueueActorBindingControl(binding);
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
    sourceSpotId: string,
    payload: ServiceApplicationPayload,
    sourceNodeRid = this.nodeRid
  ): void {
    const targets = [...this.subscriptions.entries()]
      .filter(([, values]) => [...values].some(value => {
        const separator = value.indexOf('\0');
        return value.slice(0, separator) === channelName
          && topicMatches(value.slice(separator + 1), topic);
      }))
      .map(([spotId]) => spotId)
      .sort();
    for (const spotId of targets) {
      const spot = this.registry.spot(spotId);
      if (spot === undefined) continue;
      this.raw.mailbox.tryEnqueue({
        owner: `spot:${spotId}`,
        domain: 'application',
        parts: [
          encodeLogicalMulticastHeader(channelName, topic, sourceSpotId),
          encodeApplicationPayload(payload)
        ],
        sourceRoutingId: sourceNodeRid,
        stateful: {
          receiveKind: ReceiveKind.SpotMulticast,
          operationKind: 0,
          sourceSpotId,
          channelName,
          topic,
          targetSpot: spot.ref
        } satisfies ServiceStatefulMailboxData
      });
    }
  }

  private enqueueActorControl(spotId: string, control: ActorControlPayload): void {
    const header = Buffer.from([0x5a, 0x4d, 1, M6bServiceWireCommand.actorJoined, 0]);
    this.raw.mailbox.tryEnqueue({
      owner: `spot:${spotId}`,
      domain: 'application',
      parts: [header],
      sourceRoutingId: this.nodeRid,
      stateful: {
        receiveKind: ReceiveKind.SpotControl,
        operationKind: 0,
        targetSpot: this.registry.spot(spotId)?.ref,
        kindData: control
      } satisfies ServiceStatefulMailboxData
    });
  }

  private enqueueActorBindingControl(
    binding: ServiceSessionBinding
  ): void {
    const actor = binding.actor;
    const header = Buffer.from([0x5a, 0x4d, 1, M6bServiceWireCommand.boundSessionBind, 0]);
    this.raw.mailbox.tryEnqueue({
      owner: `actor:${actor.actorId}\0${actor.generation}`,
      domain: 'application',
      parts: [header],
      sourceRoutingId: this.nodeRid,
      stateful: {
        receiveKind: ReceiveKind.ActorBinding,
        operationKind: 0,
        targetActor: actor,
        kindData: {
          kind: 'actorBinding',
          actor,
          bindingGeneration: binding.bindingGeneration,
          sessionNodeRid: binding.sessionOwnerNodeRid as never,
          sessionRid: binding.sessionRid as never
        }
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

  private handleUserSpotOperation(
    ingress: RawServiceIngressRecord,
    record: ServiceUserSpotCreateRecord | ServiceUserSpotCloseRecord
  ): RawServicePumpResult {
    if (
      ingress.requestSequence === undefined
      || record.sourceNodeRid !== ingress.sourceRoutingId
      || record.sourceNodeGeneration !== this.peerGeneration(ingress.sourceRoutingId)
    ) {
      return 'protocolError';
    }
    const target = record.kind === 'userSpotCreate'
      ? record.reservation
      : record.target;
    if (
      target.targetNodeRid !== this.nodeRid
      || target.targetNodeGeneration !== this.nodeGeneration
    ) {
      this.replyWire(
        ingress,
        record.correlation,
        RequestResult.Conflict,
        SPOT_MOVING
      );
      return 'infrastructure';
    }
    const operationKey = [
      record.sourceNodeRid,
      record.sourceNodeGeneration,
      record.operation.high,
      record.operation.low
    ].join('\0');
    const encodedRequest = userSpotOperationFingerprint(record);
    let admitted = this.admittedUserSpotOperations.get(operationKey);
    if (
      admitted?.settled === true
      && operationReplayExpired(admitted.deadlineUnixMs)
    ) {
      this.admittedUserSpotOperations.delete(operationKey);
      admitted = undefined;
    }
    if (admitted !== undefined && admitted.request !== encodedRequest) {
      return 'protocolError';
    }
    if (admitted === undefined) {
      if (record.deadlineUnixMs < BigInt(Date.now())) {
        this.replyWire(
          ingress,
          record.correlation,
          RequestResult.TimedOut,
          0
        );
        return 'infrastructure';
      }
      if (this.admittedUserSpotOperations.size >= USER_SPOT_OPERATION_CAPACITY) {
        this.releaseExpiredUserSpotOperations();
      }
      if (this.admittedUserSpotOperations.size >= USER_SPOT_OPERATION_CAPACITY) {
        this.replyWire(
          ingress,
          record.correlation,
          RequestResult.Busy,
          0
        );
        return 'infrastructure';
      }
      const handler = this.userSpotOperationHandler;
      const result = handler === undefined
        ? Promise.resolve<ServiceUserSpotOperationResult>({
            terminalResult: RequestResult.InvalidState,
            failureCode: 0
          })
        : this.executeUserSpotOperation(handler, record);
      admitted = {
        request: encodedRequest,
        deadlineUnixMs: record.deadlineUnixMs,
        result,
        settled: false
      };
      this.admittedUserSpotOperations.set(operationKey, admitted);
      void result.finally(() => {
        const current = this.admittedUserSpotOperations.get(operationKey);
        if (current?.result === result) current.settled = true;
      }).catch(() => undefined);
    }
    void admitted.result.then(
      result => {
        if (this.closed) return;
        this.raw.replyService(ingress, [
          encodeStatefulReply(
            record.correlation,
            result.terminalResult,
            result.failureCode,
            result.tail
          ),
          ...(result.payload === undefined
            ? []
            : [encodeApplicationPayload(result.payload)])
        ]);
      },
      error => {
        if (this.closed) return;
        const failed = failure(error);
        this.replyWire(
          ingress,
          record.correlation,
          failed.terminalResult,
          failed.failureCode
        );
      }
    );
    return 'infrastructure';
  }

  private releaseExpiredUserSpotOperations(): void {
    for (const [key, operation] of this.admittedUserSpotOperations) {
      if (operation.settled && operationReplayExpired(operation.deadlineUnixMs)) {
        this.admittedUserSpotOperations.delete(key);
      }
    }
  }

  private async executeUserSpotOperation(
    handler: ServiceUserSpotOperationHandler,
    record: ServiceUserSpotCreateRecord | ServiceUserSpotCloseRecord
  ): Promise<ServiceUserSpotOperationResult> {
    const deadline = userSpotDeadline(record.deadlineUnixMs);
    try {
      const result = record.kind === 'userSpotCreate'
        ? await handler.create(record, deadline.signal)
        : await handler.close(record, deadline.signal);
      if (
        result.terminalResult === RequestResult.Ok
        && (
          result.tail === undefined
          || result.tail.kind !== record.kind
        )
      ) {
        throw new ServiceWireProtocolError('User Spot success result omits its operation tail.');
      }
      return result;
    } catch (error) {
      const failed = failure(error);
      return failed;
    } finally {
      deadline.close();
    }
  }

  private async requestUserSpotOperation(
    targetNodeRid: string,
    header: Buffer,
    correlation: bigint,
    operationKind: 'userSpotCreate' | 'userSpotClose',
    timeoutMs: number
  ): Promise<ServiceUserSpotOperationResult> {
    this.requireOpen();
    const parts = await this.raw.requestService(targetNodeRid, [header], timeoutMs);
    if (parts.length < 1 || parts.length > 2) {
      throw new ServiceWireProtocolError('Invalid User Spot reply parts.');
    }
    const decoded = decodeStatefulReply(parts[0]!, correlation, operationKind);
    if (
      decoded.terminalResult !== RequestResult.Ok
      && parts.length !== 1
    ) {
      throw new ServiceWireProtocolError('Failed User Spot reply carries a payload.');
    }
    if (
      operationKind === 'userSpotClose'
      && parts.length !== 1
    ) {
      throw new ServiceWireProtocolError('User Spot close reply carries a payload.');
    }
    return {
      terminalResult: decoded.terminalResult,
      failureCode: decoded.failureCode,
      ...(decoded.tail === undefined ? {} : { tail: decoded.tail as never }),
      ...(parts.length === 2
        ? { payload: decodeApplicationPayload(parts[1]!) }
        : {})
    };
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
        `spot:${decoded.target.spot.spotId}`,
        payload!,
        {
          receiveKind: ReceiveKind.SpotRequest,
          operationKind: OperationKind.SpotRequest,
          correlation: decoded.correlation,
          sourceSpotId: decoded.sourceSpotId,
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
        previousSpotId: previous?.spot.spotId ?? null,
        currentSpotId: decoded.target.spot.spotId,
        previousSpotGeneration: previous?.spot.generation ?? 0n,
        currentSpotGeneration: decoded.target.spot.generation,
        previousMembershipEpoch: previous?.membershipEpoch ?? 0n,
        currentMembershipEpoch: (previous?.membershipEpoch ?? 0n) + 1n,
        resultCode: 0
      };
      this.enqueueApplication(
        ingress,
        `spot:${decoded.target.spot.spotId}`,
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
          `spot:${activation.ref.spotId}`,
          payload!,
          {
            receiveKind: ReceiveKind.InstanceSpotActivation,
            operationKind: OperationKind.InstanceSpotRequest,
            correlation: pending.id,
            ...(decoded.sourceSpotId === undefined ? {} : { sourceSpotId: decoded.sourceSpotId }),
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
      if (
        operationKind === 'actorJoin'
        && actor !== undefined
        && decoded.terminalResult === RequestResult.Ok
        && decoded.tail?.kind === 'actorJoin'
        && decoded.tail.joinResult === 0
      ) {
        this.enqueueRemoteSourceLeave(actor);
      }
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

  private enqueueRemoteSourceLeave(actor: ServiceActorRef): void {
    const current = this.registry.requireActor(actor);
    const transition = this.registry.leaveActor(actor, current.membershipEpoch);
    this.enqueueActorControl(transition.currentSpot.spotId, {
      kind: 'actorControl',
      lifecycleKind: ActorLifecycleKind.Left,
      previousActor: transition.actor.ref,
      currentActor: transition.actor.ref,
      previousSpotId: transition.previousSpot.spotId,
      currentSpotId: transition.currentSpot.spotId,
      previousSpotGeneration: transition.previousSpot.generation,
      currentSpotGeneration: transition.currentSpot.generation,
      previousMembershipEpoch: transition.previousMembershipEpoch,
      currentMembershipEpoch: transition.currentMembershipEpoch,
      resultCode: 0
    });
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
          spotId: tail.spot.spotId,
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
          spotId: spot?.spotId ?? null,
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
      throw new ServiceStaleGenerationError('spot', fence.spot.spotId);
    }
    const spot = this.registry.requireSpot(fence.spot);
    if (spot.authorityOwnerGeneration !== fence.authorityOwnerGeneration) {
      throw new ServiceStaleGenerationError('spot', fence.spot.spotId);
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
    const local = targetNodeRid === this.nodeRid ? this.registry.spot(spot.spotId) : undefined;
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
    const previousActor = current?.ref ?? actor;
    const previousSpot = current?.spot;
    const previousMembershipEpoch = current?.membershipEpoch ?? membershipEpoch - 1n;
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
    const committed = this.registry.requireActor({
      ...actor,
      nodeRid: this.nodeRid
    });
    this.enqueueActorControl(spot.spotId, {
      kind: 'actorControl',
      lifecycleKind: ActorLifecycleKind.Joined,
      previousActor,
      currentActor: committed.ref,
      previousSpotId: previousSpot?.spotId ?? null,
      currentSpotId: spot.spotId,
      previousSpotGeneration: previousSpot?.generation ?? 0n,
      currentSpotGeneration: spot.generation,
      previousMembershipEpoch,
      currentMembershipEpoch: committed.membershipEpoch,
      resultCode: 0
    });
  }

  private requireOpen(): void {
    if (this.closed) throw new Error('Stateful runtime is closed.');
  }
}

function userSpotDeadline(deadlineUnixMs: bigint): {
  readonly signal: AbortSignal;
  close(): void;
} {
  const controller = new AbortController();
  const delay = Number(deadlineUnixMs - BigInt(Date.now()));
  const timeout = setTimeout(
    () => controller.abort(new Error('User Spot operation deadline exceeded.')),
    Math.max(0, Math.min(delay, 0x7fff_ffff))
  );
  return {
    signal: controller.signal,
    close: () => clearTimeout(timeout)
  };
}

function operationReplayExpired(deadlineUnixMs: bigint): boolean {
  return BigInt(Date.now()) > deadlineUnixMs
    + BigInt(USER_SPOT_OPERATION_REPLAY_RETENTION_MS);
}

function userSpotOperationFingerprint(
  record: ServiceUserSpotCreateRecord | ServiceUserSpotCloseRecord
): string {
  const { correlation: _correlation, ...semantic } = record;
  return JSON.stringify(
    semantic,
    (_key, value: unknown) => typeof value === 'bigint' ? `${value}n` : value
  );
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
    spotId: actor.spot.spotId,
    spotGeneration: actor.spot.generation,
    membershipEpoch: actor.membershipEpoch
  };
}

function failure(error: unknown): ServiceStatefulResult {
  if (error instanceof ServiceStaleGenerationError) {
    return { terminalResult: RequestResult.NotFound, failureCode: ACTOR_ROUTE_STALE };
  }
  if (error instanceof ZLinkFrameworkException) {
    if (error.kind === ZLinkFrameworkErrorKind.DeadlineExceeded) {
      return { terminalResult: RequestResult.TimedOut, failureCode: 0 };
    }
    if (
      error.kind === ZLinkFrameworkErrorKind.SpotGenerationStale
      || error.kind === ZLinkFrameworkErrorKind.SpotMoving
    ) {
      return {
        terminalResult: RequestResult.Conflict,
        failureCode: error.code + 1
      };
    }
    return {
      terminalResult: RequestResult.InternalError,
      failureCode: error.code + 1
    };
  }
  return { terminalResult: RequestResult.InternalError, failureCode: 17 };
}

function actorKey(actor: ServiceActorRef): string {
  return `${actor.actorId}\0${actor.generation}`;
}

function spotKey(spot: ServiceSpotRef): string {
  return `${spot.spotId}\0${spot.generation}`;
}

function sameActorRef(left: ServiceActorRef, right: ServiceActorRef): boolean {
  return left.nodeRid === right.nodeRid
    && left.actorId === right.actorId
    && left.generation === right.generation;
}

function sameSpotRef(left: ServiceSpotRef, right: ServiceSpotRef): boolean {
  return left.spotId === right.spotId && left.generation === right.generation;
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
    && left.targetSpotId === right.targetSpotId
    && left.objectGeneration === right.objectGeneration
    && left.ownerId === right.ownerId
    && left.authorityOwnerGeneration === right.authorityOwnerGeneration
    && left.leaseGeneration === right.leaseGeneration
    && left.storeVersion === right.storeVersion;
}

function sameSpotRoute(left: ServiceSpotRouteFence, right: ServiceSpotRouteFence): boolean {
  return sameSpotRef(left.spot, right.spot)
    && left.targetNodeRid === right.targetNodeRid
    && left.targetNodeGeneration === right.targetNodeGeneration
    && left.authorityOwnerGeneration === right.authorityOwnerGeneration;
}

function routeMatchesLocal(
  route: ServiceInstanceRouteFence,
  spot: ServiceSpotState,
  nodeRid: string,
  nodeGeneration: bigint
): boolean {
  return route.targetNodeRid === nodeRid
    && route.targetNodeGeneration === nodeGeneration
    && route.targetSpotId === spot.ref.spotId
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

function instanceOperationParts(
  headerAndPayload: readonly Buffer[],
  metadataFrame?: Uint8Array
): readonly Buffer[] {
  if (headerAndPayload.length !== 2) {
    throw new TypeError('Instance operation requires one header and one payload frame.');
  }
  if (metadataFrame === undefined) return headerAndPayload;
  return [
    headerAndPayload[0]!,
    validateServiceMetadataFrame(metadataFrame),
    headerAndPayload[1]!
  ];
}

export function statefulMailboxData(record: ServiceMailboxRecord): ServiceStatefulMailboxData | undefined {
  return record.stateful as ServiceStatefulMailboxData | undefined;
}
