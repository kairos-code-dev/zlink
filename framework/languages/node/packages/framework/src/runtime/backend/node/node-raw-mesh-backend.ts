import {
  Message,
  RoutingId as BindingRoutingId,
  RequestResult,
  SubmitResult,
  type MessageLike,
  type RequestResult as RequestResultValue,
  type StreamSocket,
  type SubmitResult as SubmitResultValue
} from '@zlink-systems/zlink';
import type {
  MeshOperationId,
  MeshPeerEntry,
  MeshPublisher,
  ReadyBatch,
  ReadyRecord,
  ReceiveBatch,
  ReceiveRecord,
  ServiceSpot,
  StreamSessionService
} from '../../foundation/service-runtime-contracts';
import {
  OperationKind,
  ReadyDomain,
  ReadyOwnerKind,
  ReceiveKind
} from '../../foundation/service-runtime-contracts';
import {
  RawServiceMeshRuntime,
  type RawServiceRequestResult
} from '../../foundation/raw-service-mesh-runtime';
import {
  ServiceStatefulRuntime,
  statefulMailboxData,
  type ServiceAsyncInstanceActivationAuthority,
  type ServiceInstanceApplicationLifecycle,
  type ServicePendingInstanceActivation,
  type ServiceUserSpotOperationHandler,
  type ServiceUserSpotOperationResult,
  type ServiceStatefulMailboxData,
  type ServiceStatefulPendingOperation,
  type ServiceStatefulResult
} from '../../foundation/service-stateful-runtime';
import type {
  ServiceActorRef,
  ServiceSpotState
} from '../../foundation/service-stateful-registry';
import type {
  ServiceInstanceActivationTarget,
  ServiceInstanceRouteFence,
  ServiceSpotRouteFence,
  ServiceUserSpotCloseRecord,
  ServiceUserSpotCreateRecord
} from '../../foundation/service-stateful-wire-codec';
import { encodeServiceMetadataFrame } from '../../foundation/service-metadata-codec';
import type {
  ServiceInstanceActivationRecoveryEnvelope
} from '../../foundation/service-instance-activation-recovery-codec';
import type {
  ServiceMailboxClaim,
  ServiceMailboxRecord
} from '../../foundation/service-mailbox';
import type {
  ServiceChannelDescriptor,
  ServiceNodeDescriptor
} from '../../foundation/service-topology-registry';
import type {
  RoutingId
} from '../../../contracts';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendMeshNode
} from '../contracts';

const MULTIPART_PACKET_NAME = 'ZLinkFrameworkMultipart';
const MULTIPART_CONTENT_TYPE = 'application/x-zlink-multipart';
const MAX_DRAIN_RECORDS = 64;

/**
 * M6A MeshNode backend. Stateful Spot/Actor entry points stay explicit until
 * their owning M6B runtime is connected; topology and node/channel dispatch do
 * not depend on those entry points.
 */
export class ZLinkNodeRawMeshBackend implements ZLinkBackendMeshNode {
  readonly nativeInstance = this;

  private readonly channels = new Map<string, number>();
  private readonly peerIntents = new Map<bigint, {
    readonly endpoint: string;
    readonly nodeRoutingId: string;
  }>();
  private readonly completions: PendingCompletion[] = [];
  private readonly routingId: string;
  private bindEndpoint?: string;
  private runtime?: RawServiceMeshRuntime;
  private stateful?: ServiceStatefulRuntime;
  private readyHandler?: (domains: number) => number;
  private pollTimer?: NodeJS.Timeout;
  private nextPeerIntent = 1n;
  private closed = false;
  private objectRole: ServiceNodeDescriptor['objectRole'] = 'none';
  private placementWeight = 100;
  private activeCapacityLimit = 10_000;
  private pendingCapacityLimit = 128;
  private objectCapabilities: readonly string[] = [];

  constructor(
    private readonly meshName: string,
    routingId: string | undefined
  ) {
    if (meshName.length === 0) throw new TypeError('MeshName must be non-empty.');
    if (routingId === undefined || routingId.length === 0) {
      throw new TypeError('M6A raw MeshNode requires a routing id.');
    }
    this.routingId = routingId;
  }

  configureObjectPlacement(options: {
    readonly role: ServiceNodeDescriptor['objectRole'];
    readonly placementWeight: number;
    readonly activeCapacityLimit: number;
    readonly pendingCapacityLimit: number;
    readonly objectCapabilities: readonly string[];
  }): void {
    this.requireNotStarted();
    this.objectRole = options.role;
    this.placementWeight = requirePublicWeight(
      options.placementWeight,
      'placementWeight'
    );
    this.activeCapacityLimit = requirePositivePlacementValue(
      options.activeCapacityLimit,
      'activeCapacityLimit'
    );
    this.pendingCapacityLimit = requirePositivePlacementValue(
      options.pendingCapacityLimit,
      'pendingCapacityLimit'
    );
    this.objectCapabilities = [...new Set(options.objectCapabilities)].sort();
  }

  selectObjectPlacement(stableType: string) {
    const descriptor = this.requireRuntime().topology.selectObjectPlacement(stableType);
    return descriptor === undefined
      ? undefined
      : {
          targetNodeRid: descriptor.nodeRoutingId,
          targetNodeGeneration: descriptor.lifecycleGeneration,
          descriptorVersion: descriptor.descriptorRevision.toString()
        };
  }

  sendToMissingInstanceSpot(
    target: ServiceInstanceActivationTarget,
    parts: MessageLike | readonly MessageLike[],
    deadlineUnixMs: bigint,
    sourceSpotId?: string,
    metadata?: ReadonlyMap<string, string>
  ): SubmitResult {
    const result = this.requireStateful().sendToMissingInstanceSpot(
      target,
      encodeMultipart(parts),
      deadlineUnixMs,
      sourceSpotId,
      metadata === undefined ? undefined : encodeServiceMetadataFrame(metadata)
    );
    return result as SubmitResult;
  }

  requestToMissingInstanceSpot(
    target: ServiceInstanceActivationTarget,
    parts: MessageLike | readonly MessageLike[],
    timeoutMs: number,
    sourceSpotId?: string,
    metadata?: ReadonlyMap<string, string>
  ): MeshOperationId {
    return this.observeStateful(
      OperationKind.InstanceSpotRequest,
      this.requireStateful().requestToMissingInstanceSpot(
        target,
        encodeMultipart(parts),
        timeoutMs,
        sourceSpotId,
        metadata === undefined ? undefined : encodeServiceMetadataFrame(metadata)
      )
    );
  }

  setRoutingId(routingId: unknown): void {
    if (String(routingId) !== this.routingId) {
      throw new Error('MeshNode routing id is immutable after construction.');
    }
  }

  setBind(endpoint: string): void {
    this.requireNotStarted();
    if (endpoint.length === 0) throw new TypeError('MeshNode bind endpoint must be non-empty.');
    this.bindEndpoint = endpoint;
  }

  start(): void {
    if (this.runtime !== undefined) return;
    if (this.closed) throw new Error('MeshNode is closed.');
    if (this.bindEndpoint === undefined) throw new Error('MeshNode bind endpoint is not configured.');
    const descriptor = this.createDescriptor();
    const runtime = new RawServiceMeshRuntime({ descriptor });
    runtime.start();
    this.stateful = new ServiceStatefulRuntime(
      runtime,
      descriptor.nodeRoutingId,
      descriptor.lifecycleGeneration
    );
    this.runtime = runtime;
    this.schedulePoll();
  }

  shutdown(_timeoutMs: number): RequestResultValue {
    this.close();
    return RequestResult.Ok;
  }

  close(): void {
    if (this.closed) return;
    this.closed = true;
    if (this.pollTimer !== undefined) clearTimeout(this.pollTimer);
    this.pollTimer = undefined;
    this.stateful?.close();
    this.stateful = undefined;
    this.runtime?.close();
    this.runtime = undefined;
    this.completions.length = 0;
  }

  addChannelName(name: string): void {
    this.requireNotStarted();
    if (name.length === 0) throw new TypeError('ChannelName must be non-empty.');
    this.channels.set(name, this.channels.get(name) ?? 100);
  }

  setChannelWeight(name: string, weight: number): void {
    if (!this.channels.has(name)) throw new Error(`Channel '${name}' is not registered.`);
    const validated = requirePublicWeight(weight, 'Channel weight');
    if (this.channels.get(name) === validated) return;
    this.channels.set(name, validated);
    this.runtime?.updateLocalWeights({
      channelName: name,
      channelWeight: validated
    });
  }

  setPlacementWeight(weight: number): void {
    const validated = requirePublicWeight(weight, 'Placement weight');
    if (this.placementWeight === validated) return;
    this.placementWeight = validated;
    this.runtime?.updateLocalWeights({ placementWeight: validated });
  }

  connectPeer(options: { readonly endpoint: string; readonly expectedRid?: unknown }): bigint {
    const runtime = this.requireRuntime();
    if (options.expectedRid === undefined) {
      throw new Error('M6A raw manual peer connection requires expectedRid.');
    }
    const nodeRoutingId = String(options.expectedRid);
    runtime.connectPeerByRoutingId(options.endpoint, nodeRoutingId);
    const intent = this.nextPeerIntent++;
    this.peerIntents.set(intent, { endpoint: options.endpoint, nodeRoutingId });
    runtime.announcePeer(nodeRoutingId);
    return intent;
  }

  removePeerConnection(intentId: bigint): void {
    const intent = this.peerIntents.get(intentId);
    if (intent === undefined) return;
    this.peerIntents.delete(intentId);
    this.runtime?.disconnectPeer(intent.endpoint, intent.nodeRoutingId);
  }

  disconnectPeer(peerRid: unknown, _lifecycleGeneration: bigint): void {
    const nodeRoutingId = String(peerRid);
    const found = [...this.peerIntents.entries()]
      .find(([, intent]) => intent.nodeRoutingId === nodeRoutingId);
    if (found === undefined) return;
    this.removePeerConnection(found[0]);
  }

  sendToNode(
    targetRid: unknown,
    parts: MessageLike | readonly MessageLike[]
  ): SubmitResultValue {
    return this.requireRuntime().sendToNode(
      String(targetRid),
      encodeMultipart(parts)
    ) ? SubmitResult.Ok : SubmitResult.NotConnected;
  }

  requestToNode(
    targetRid: unknown,
    parts: MessageLike | readonly MessageLike[],
    options?: { readonly timeoutMs?: number }
  ): MeshOperationId {
    const pending = this.requireRuntime().requestToNode(
      String(targetRid),
      encodeMultipart(parts),
      options?.timeoutMs ?? 30_000
    );
    return this.observeCompletion(pending.id, OperationKind.NodeRequest, pending.promise);
  }

  sendToChannel(
    channelName: string,
    parts: MessageLike | readonly MessageLike[]
  ): SubmitResultValue {
    return this.requireRuntime().sendToChannel(
      channelName,
      encodeMultipart(parts)
    ) ? SubmitResult.Ok : SubmitResult.NotConnected;
  }

  requestToChannel(
    channelName: string,
    parts: MessageLike | readonly MessageLike[],
    options?: { readonly timeoutMs?: number }
  ): MeshOperationId {
    const pending = this.requireRuntime().requestToChannel(
      channelName,
      encodeMultipart(parts),
      options?.timeoutMs ?? 30_000
    );
    if (pending === undefined) {
      return this.enqueueImmediateFailure(OperationKind.ChannelRequest, RequestResult.NotFound);
    }
    return this.observeCompletion(pending.id, OperationKind.ChannelRequest, pending.promise);
  }

  status() {
    const descriptor = this.runtime?.topology.localDescriptor() ?? this.createDescriptor();
    const peers = this.runtime?.topology.peers() ?? [];
    return {
      state: stateCode(descriptor.state),
      routingId: descriptor.nodeRoutingId as RoutingId,
      meshName: descriptor.meshName,
      localEndpoint: descriptor.advertisedEndpoint,
      lifecycleGeneration: descriptor.lifecycleGeneration,
      descriptorRevision: descriptor.descriptorRevision,
      channelCount: descriptor.channels.length,
      configuredPeerCount: this.peerIntents.size,
      admittedPeerCount: peers.length,
      drainingPeerCount: peers.filter(peer => peer.descriptor.state === 'draining').length,
      pendingApplicationMessages: BigInt(this.runtime?.mailbox.pendingMessages('application') ?? 0),
      pendingInfrastructureMessages: BigInt(
        (this.runtime?.mailbox.pendingMessages('infrastructure') ?? 0) + this.completions.length
      ),
      pendingBytes: BigInt(
        (this.runtime?.mailbox.pendingBytes('application') ?? 0)
        + (this.runtime?.mailbox.pendingBytes('infrastructure') ?? 0)
      ),
      lastError: 0,
      lastChangedMs: BigInt(Math.trunc(performance.now()))
    };
  }

  peers(): MeshPeerEntry[] {
    const intents = new Map(
      [...this.peerIntents.entries()].map(([id, intent]) => [intent.nodeRoutingId, id])
    );
    return (this.runtime?.topology.peers() ?? []).map(peer => ({
      connectionIntentId: intents.get(peer.descriptor.nodeRoutingId) ?? 0n,
      source: 1,
      state: stateCode(peer.descriptor.state),
      routingId: peer.descriptor.nodeRoutingId as RoutingId,
      lifecycleGeneration: peer.descriptor.lifecycleGeneration,
      descriptorRevision: peer.descriptor.descriptorRevision,
      endpoint: peer.descriptor.advertisedEndpoint,
      channelCount: peer.descriptor.channels.length,
      lastError: 0,
      lastChangedMs: BigInt(Math.trunc(performance.now()))
    }));
  }

  peerChannels(peerRid: unknown, lifecycleGeneration: bigint) {
    const peer = this.runtime?.topology.peer(String(peerRid));
    if (peer === undefined || peer.descriptor.lifecycleGeneration !== lifecycleGeneration) {
      return { names: [], weights: [] };
    }
    return {
      names: peer.descriptor.channels.map(channel => channel.name),
      weights: peer.descriptor.channels.map(channel => channel.weight)
    };
  }

  createPublisher(): MeshPublisher {
    let publisherClosed = false;
    const publish = (
      channelName: string,
      topic: string,
      parts: MessageLike | readonly MessageLike[]
    ): void => {
      if (publisherClosed) throw new Error('Mesh publisher is closed.');
      this.requireStateful().publishLogicalMulticast(
        channelName,
        topic,
        encodeMultipart(parts)
      );
    };
    return {
      publish,
      publishAsync: async (channelName, topic, parts, _options, signal) => {
        signal?.throwIfAborted();
        publish(channelName, topic, parts);
      },
      close: () => {
        publisherClosed = true;
      }
    };
  }

  setReadyHandler(handler: (readyDomains: number) => number): void {
    this.readyHandler = handler;
    this.notifyReady();
  }

  createReadyBatch(capacity: number): ReadyBatch {
    return new RawReadyBatch(capacity);
  }

  createReceiveBatch(
    messageCapacity: number,
    partCapacity: number,
    byteCapacity: number
  ): ReceiveBatch {
    return new RawReceiveBatch(messageCapacity, partCapacity, byteCapacity);
  }

  drainReady(
    domains: number,
    batch: ReadyBatch
  ): { readonly ok: boolean; readonly hasResidue: boolean; readonly records: readonly ReadyRecord[] } {
    const target = requireRawReadyBatch(batch);
    if ((domains & ReadyDomain.Infrastructure) !== 0) {
      this.drainCompletions(target);
    }
    if ((domains & ReadyDomain.Application) !== 0) {
      this.drainApplication(target);
    }
    const runtime = this.runtime;
    return {
      ok: true,
      hasResidue:
        this.completions.length > 0
        || (runtime?.mailbox.pendingMessages('infrastructure') ?? 0) > 0
        || (runtime?.mailbox.pendingMessages('application') ?? 0) > 0,
      records: target.records
    };
  }

  createSpot(): ServiceSpot {
    return new RawServiceSpot(this, this.requireStateful().createSpot());
  }

  entrySpot(): ServiceSpot {
    return new RawServiceSpot(this, this.requireStateful().entrySpot());
  }

  getOrCreateSpot(routingId: unknown): { readonly spot: ServiceSpot; readonly created: boolean } {
    const stateful = this.requireStateful();
    const rid = String(routingId);
    const existing = stateful.registry.spot(rid);
    const state = existing ?? stateful.createSpot(rid);
    return { spot: new RawServiceSpot(this, state), created: existing === undefined };
  }

  createActor(actorId: string): ZLinkBackendActorRef {
    return this.requireStateful().createActor(actorId).ref;
  }

  registerInstanceIntent(instanceType: string, route: ServiceInstanceRouteFence): void {
    this.requireStateful().registerInstanceIntent(instanceType, route);
  }

  registerAsyncInstanceActivationAuthority(
    authority: ServiceAsyncInstanceActivationAuthority
  ): void {
    this.requireStateful().registerAsyncInstanceActivationAuthority(authority);
  }

  registerInstanceApplicationLifecycle(
    lifecycle: ServiceInstanceApplicationLifecycle
  ): void {
    this.requireStateful().registerInstanceApplicationLifecycle(lifecycle);
  }

  registerUserSpotOperationHandler(handler: ServiceUserSpotOperationHandler): void {
    this.requireStateful().registerUserSpotOperationHandler(handler);
  }

  requestUserSpotCreate(
    targetNodeRid: string,
    request: Omit<ServiceUserSpotCreateRecord, 'kind' | 'correlation' | 'operation'>,
    timeoutMs: number
  ): Promise<ServiceUserSpotOperationResult> {
    return this.requireStateful().requestUserSpotCreate(targetNodeRid, request, timeoutMs);
  }

  requestUserSpotClose(
    targetNodeRid: string,
    request: Omit<ServiceUserSpotCloseRecord, 'kind' | 'correlation' | 'operation'>,
    timeoutMs: number
  ): Promise<ServiceUserSpotOperationResult> {
    return this.requireStateful().requestUserSpotClose(targetNodeRid, request, timeoutMs);
  }

  recoverInstanceActivation(
    envelope: ServiceInstanceActivationRecoveryEnvelope,
    route: ServiceInstanceRouteFence
  ): Promise<void> {
    return this.requireStateful().recoverInstanceActivation(envelope, route);
  }

  recoverPendingInstanceActivation(
    envelope: ServiceInstanceActivationRecoveryEnvelope,
    pending: ServicePendingInstanceActivation
  ): Promise<void> {
    return this.requireStateful().recoverPendingInstanceActivation(envelope, pending);
  }

  completeRecoveredInstanceActivation(
    target: ServiceInstanceActivationTarget,
    route: ServiceInstanceRouteFence
  ): Promise<ServiceInstanceRouteFence> {
    return this.requireStateful().completeRecoveredInstanceActivation(target, route);
  }

  forgetInstanceIntent(
    spotId: string,
    authorityOwnerGeneration: bigint,
    storeVersion?: string
  ): void {
    this.requireStateful().forgetInstanceIntent(
      spotId,
      authorityOwnerGeneration,
      storeVersion
    );
  }

  rememberSpotRoute(route: ServiceSpotRouteFence, storeVersion?: string): void {
    this.requireStateful().rememberSpotRoute(route, storeVersion);
  }

  forgetSpotRoute(
    spot: ServiceSpotRouteFence['spot'],
    authorityOwnerGeneration: bigint,
    storeVersion?: string
  ): void {
    this.requireStateful().forgetSpotRoute(spot, authorityOwnerGeneration, storeVersion);
  }

  requestInstanceSpot(
    route: ServiceInstanceRouteFence,
    parts: MessageLike | readonly MessageLike[],
    timeoutMs = 30_000,
    sourceSpotId?: string
  ): MeshOperationId {
    return this.observeStateful(
      OperationKind.InstanceSpotRequest,
      this.requireStateful().requestToInstanceSpot(
        route,
        encodeMultipart(parts),
        timeoutMs,
        sourceSpotId
      )
    );
  }

  actorLookup(actorId: string) {
    const actor = this.requireStateful().actor(actorId);
    if (actor === undefined) throw new Error(`Actor '${actorId}' was not found.`);
    return {
      actor: actor.ref,
      spotId: actor.spot.spotId,
      spotGeneration: actor.spot.generation,
      membershipEpoch: actor.membershipEpoch
    };
  }

  lookupRemoteActor(targetNodeRid: unknown, actorId: string, timeoutMs = 30_000): MeshOperationId {
    return this.observeStateful(
      OperationKind.ActorLookup,
      this.requireStateful().lookupRemoteActor(String(targetNodeRid), actorId, timeoutMs)
    );
  }

  destroyActor(actor: ZLinkBackendActorRef, timeoutMs = 30_000): MeshOperationId {
    return this.observeStateful(
      OperationKind.ActorDestroy,
      this.requireStateful().destroyActor(actor, timeoutMs)
    );
  }

  joinActorSpot(
    actor: ZLinkBackendActorRef,
    targetNodeRid: unknown,
    targetSpotId: unknown,
    targetSpotGeneration: bigint,
    parts?: MessageLike | readonly MessageLike[],
    timeoutMs = 30_000
  ): MeshOperationId {
    return this.observeStateful(
      OperationKind.ActorJoin,
      this.requireStateful().joinActor(
        actor,
        String(targetNodeRid),
        { spotId: String(targetSpotId), generation: targetSpotGeneration },
        targetSpotGeneration,
        parts === undefined ? undefined : encodeMultipart(parts),
        timeoutMs
      )
    );
  }

  joinActorEntrySpot(
    actor: ZLinkBackendActorRef,
    targetNodeRid: unknown,
    parts?: MessageLike | readonly MessageLike[],
    timeoutMs = 30_000
  ): MeshOperationId {
    const target = String(targetNodeRid);
    return this.observeStateful(
      OperationKind.ActorJoin,
      this.requireStateful().joinActor(
        actor,
        target,
        { spotId: target, generation: 1n },
        1n,
        parts === undefined ? undefined : encodeMultipart(parts),
        timeoutMs
      )
    );
  }

  sendToActor(
    actor: ZLinkBackendActorRef,
    parts: MessageLike | readonly MessageLike[]
  ): SubmitResultValue {
    return this.requireStateful().sendToActor(
      actor,
      this.peerGeneration(String(actor.nodeRid)),
      actor.generation,
      encodeMultipart(parts)
    ) as SubmitResultValue;
  }

  requestToActor(
    actor: ZLinkBackendActorRef,
    parts: MessageLike | readonly MessageLike[],
    options?: { readonly timeoutMs?: number }
  ): MeshOperationId {
    return this.observeStateful(
      OperationKind.ActorRequest,
      this.requireStateful().requestToActor(
        actor,
        this.peerGeneration(String(actor.nodeRid)),
        actor.generation,
        encodeMultipart(parts),
        options?.timeoutMs ?? 30_000
      )
    );
  }

  actorSendToActor(
    source: ZLinkBackendActorRef,
    target: ZLinkBackendActorRef,
    parts: MessageLike | readonly MessageLike[]
  ): SubmitResultValue {
    return this.requireStateful().sendToActor(
      target,
      this.peerGeneration(String(target.nodeRid)),
      target.generation,
      encodeMultipart(parts),
      source
    ) as SubmitResultValue;
  }

  actorRequestToActor(
    source: ZLinkBackendActorRef,
    target: ZLinkBackendActorRef,
    parts: MessageLike | readonly MessageLike[],
    options?: { readonly timeoutMs?: number }
  ): MeshOperationId {
    return this.observeStateful(
      OperationKind.ActorRequest,
      this.requireStateful().requestToActor(
        target,
        this.peerGeneration(String(target.nodeRid)),
        target.generation,
        encodeMultipart(parts),
        options?.timeoutMs ?? 30_000,
        source
      )
    );
  }

  sendActorBoundSession(
    actor: ZLinkBackendActorRef,
    expectedBindingGeneration: bigint,
    parts: MessageLike | readonly MessageLike[]
  ): SubmitResultValue {
    return this.requireStateful().sendBoundSession(
      actor,
      expectedBindingGeneration,
      encodeMultipart(parts)
    ) as SubmitResultValue;
  }

  closeActorBoundSession(
    actor: ZLinkBackendActorRef,
    expectedBindingGeneration: bigint,
    timeoutMs = 30_000
  ): MeshOperationId {
    const binding = this.requireStateful().registry.binding(actor);
    if (binding === undefined) {
      return this.enqueueImmediateFailure(OperationKind.StreamUnbind, RequestResult.NotFound);
    }
    return this.observeStateful(
      OperationKind.StreamUnbind,
      this.requireStateful().unbindSession(
        binding.sessionRid,
        actor,
        expectedBindingGeneration,
        timeoutMs
      )
    );
  }

  leaveActor(
    actor: ZLinkBackendActorRef,
    expectedMembershipEpoch: bigint,
    timeoutMs = 30_000
  ): MeshOperationId {
    return this.observeStateful(
      OperationKind.ActorLeave,
      this.requireStateful().leaveActor(actor, expectedMembershipEpoch, timeoutMs)
    );
  }

  createStreamSessionService(stream: unknown): StreamSessionService {
    return new RawStreamSessionService(
      this.requireStateful(),
      stream as StreamSocket,
      (kind, pending) => this.observeStateful(kind, pending)
    );
  }

  sendFromSpot(
    source: ServiceSpotState,
    targetNodeRid: string,
    targetSpotId: string,
    targetSpotGeneration: bigint,
    parts: MessageLike | readonly MessageLike[]
  ): SubmitResultValue {
    return this.requireStateful().sendToSpot(
      source.ref.spotId,
      targetNodeRid,
      { spotId: targetSpotId, generation: targetSpotGeneration },
      this.peerGeneration(targetNodeRid),
      targetSpotGeneration,
      encodeMultipart(parts)
    ) as SubmitResultValue;
  }

  requestFromSpot(
    source: ServiceSpotState,
    targetNodeRid: string,
    targetSpotId: string,
    targetSpotGeneration: bigint,
    parts: MessageLike | readonly MessageLike[],
    timeoutMs: number
  ): MeshOperationId {
    return this.observeStateful(
      OperationKind.SpotRequest,
      this.requireStateful().requestToSpot(
        source.ref.spotId,
        targetNodeRid,
        { spotId: targetSpotId, generation: targetSpotGeneration },
        this.peerGeneration(targetNodeRid),
        targetSpotGeneration,
        encodeMultipart(parts),
        timeoutMs
      )
    );
  }

  closeSpot(state: ServiceSpotState): boolean {
    return this.requireStateful().registry.closeSpot(state.ref);
  }

  publishFromSpot(
    state: ServiceSpotState,
    channelName: string,
    topic: string,
    parts: MessageLike | readonly MessageLike[]
  ): void {
    this.requireStateful().publishLogicalMulticast(
      channelName,
      topic,
      encodeMultipart(parts),
      state.ref.spotId
    );
  }

  setSpotSubscription(
    state: ServiceSpotState,
    channelName: string,
    topicFilter: string
  ): void {
    this.requireStateful().setSubscription(state, channelName, topicFilter);
  }

  unsetSpotSubscription(
    state: ServiceSpotState,
    channelName: string,
    topicFilter: string
  ): void {
    this.requireStateful().unsetSubscription(state, channelName, topicFilter);
  }

  clearSpotSubscriptions(state: ServiceSpotState): void {
    this.requireStateful().clearSubscriptions(state.ref.spotId);
  }

  private createDescriptor(): ServiceNodeDescriptor {
    const channels: ServiceChannelDescriptor[] = [...this.channels]
      .map(([name, weight]) => ({ name, weight }))
      .sort((left, right) => left.name.localeCompare(right.name));
    return {
      meshName: this.meshName,
      nodeRoutingId: this.routingId,
      lifecycleGeneration: 1n,
      descriptorRevision: 1n,
      advertisedEndpoint: this.bindEndpoint ?? 'inproc://not-started',
      channels,
      state: 'preparing',
      securityIdentity: 'default',
      effectiveMaxMessageBytes: 4 * 1024 * 1024,
      applicationVersion: 0n,
      protocolCapabilities: [
        'framework-service-v11',
        ...this.objectCapabilities
      ],
      objectRole: this.objectRole,
      placementWeight: this.placementWeight,
      activeCapacityLimit: this.activeCapacityLimit,
      pendingCapacityLimit: this.pendingCapacityLimit,
      activeCapacityUsed: 0,
      pendingCapacityUsed: 0
    };
  }

  private schedulePoll(): void {
    if (this.closed || this.pollTimer !== undefined) return;
    this.pollTimer = setTimeout(() => {
      this.pollTimer = undefined;
      try {
        this.poll();
      } finally {
        this.schedulePoll();
      }
    }, 1);
    this.pollTimer.unref();
  }

  private poll(): void {
    const runtime = this.runtime;
    if (runtime === undefined) return;
    runtime.drainMonitorEvents();
    for (let index = 0; index < MAX_DRAIN_RECORDS; index++) {
      const result = runtime.pumpOne();
      if (result === 'noData') break;
      if (result === 'application') this.readyHandler?.(ReadyDomain.Application);
    }
    runtime.announceExpectedPeers();
    runtime.tickLiveness();
    this.notifyReady();
  }

  private notifyReady(): void {
    const runtime = this.runtime;
    let domains = ReadyDomain.None;
    if (this.completions.length > 0 || (runtime?.mailbox.pendingMessages('infrastructure') ?? 0) > 0) {
      domains |= ReadyDomain.Infrastructure;
    }
    if ((runtime?.mailbox.pendingMessages('application') ?? 0) > 0) {
      domains |= ReadyDomain.Application;
    }
    if (domains !== ReadyDomain.None) this.readyHandler?.(domains);
  }

  private observeCompletion(
    low: bigint,
    operationKind: number,
    promise: Promise<RawServiceRequestResult>
  ): MeshOperationId {
    const id = { high: 1n, low };
    void promise.then(
      result => this.enqueueCompletion(id, operationKind, result),
      () => this.enqueueCompletion(id, operationKind, {
        terminalResult: RequestResult.NotConnected,
        failureCode: 0
      })
    );
    return id;
  }

  private observeStateful(
    operationKind: number,
    pending: ServiceStatefulPendingOperation
  ): MeshOperationId {
    const id = { high: 2n, low: pending.id };
    void pending.promise.then(
      result => this.enqueueCompletion(id, operationKind, result),
      () => this.enqueueCompletion(id, operationKind, {
        terminalResult: RequestResult.NotConnected,
        failureCode: 0
      })
    );
    return id;
  }

  private enqueueImmediateFailure(operationKind: number, terminalResult: number): MeshOperationId {
    const low = this.nextPeerIntent++;
    const id = { high: 1n, low };
    this.enqueueCompletion(id, operationKind, { terminalResult, failureCode: 0 });
    return id;
  }

  private enqueueCompletion(
    operationId: MeshOperationId,
    operationKind: number,
    result: RawServiceRequestResult | ServiceStatefulResult
  ): void {
    this.completions.push({ operationId, operationKind, result });
    this.readyHandler?.(ReadyDomain.Infrastructure);
  }

  private drainCompletions(batch: RawReadyBatch): void {
    while (!batch.full && this.completions.length > 0) {
      const completion = this.completions.shift()!;
      batch.push(
        {
          ownerKind: ReadyOwnerKind.Node,
          domain: ReadyDomain.Infrastructure,
          spotId: null,
          actor: null
        },
        new CompletionClaim(completion)
      );
    }
  }

  private drainApplication(batch: RawReadyBatch): void {
    const runtime = this.runtime;
    if (runtime === undefined) return;
    while (!batch.full) {
      const claim = runtime.mailbox.tryClaim(
        'application',
        MAX_DRAIN_RECORDS,
        Number.MAX_SAFE_INTEGER
      );
      if (claim === undefined) break;
      const owner = readyOwner(claim.owner, this.routingId, this.stateful);
      batch.push(
        owner,
        new MailboxClaim(runtime, claim)
      );
    }
  }

  private requireRuntime(): RawServiceMeshRuntime {
    if (this.runtime === undefined) throw new Error('MeshNode is not started.');
    return this.runtime;
  }

  private requireStateful(): ServiceStatefulRuntime {
    if (this.stateful === undefined) throw new Error('Stateful MeshNode is not started.');
    return this.stateful;
  }

  private peerGeneration(nodeRid: string): bigint {
    if (nodeRid === this.routingId) {
      return this.requireRuntime().topology.localDescriptor().lifecycleGeneration;
    }
    return this.requireRuntime().topology.peer(nodeRid)?.descriptor.lifecycleGeneration ?? 1n;
  }

  private requireNotStarted(): void {
    if (this.runtime !== undefined) throw new Error('MeshNode is already started.');
    if (this.closed) throw new Error('MeshNode is closed.');
  }
}

interface PendingCompletion {
  readonly operationId: MeshOperationId;
  readonly operationKind: number;
  readonly result: RawServiceRequestResult | ServiceStatefulResult;
}

interface RawClaim {
  recvBatch(batch: ReceiveBatch): {
    readonly ok: boolean;
    readonly records: ReceiveRecord[];
  };
  release(): void;
}

class RawServiceSpot implements ServiceSpot {
  readonly routingId: RoutingId;
  readonly lifecycleGeneration: bigint;
  private readonly subscriptions = new Set<string>();
  private closed = false;

  constructor(
    private readonly backend: ZLinkNodeRawMeshBackend,
    private readonly state: ServiceSpotState
  ) {
    this.routingId = state.ref.spotId;
    this.lifecycleGeneration = state.ref.generation;
  }

  status() {
    return {
      routingId: this.routingId,
      lifecycleGeneration: this.lifecycleGeneration
    };
  }

  sendToChannel(
    channelName: string,
    parts: MessageLike | readonly MessageLike[],
    options?: { readonly flags?: number }
  ): SubmitResultValue {
    this.requireOpen();
    void options;
    return this.backend.sendToChannel(channelName, parts);
  }

  requestToChannel(
    channelName: string,
    parts: MessageLike | readonly MessageLike[],
    options?: { readonly flags?: number; readonly timeoutMs?: number }
  ): MeshOperationId {
    this.requireOpen();
    return this.backend.requestToChannel(channelName, parts, options);
  }

  sendToSpot(
    targetNodeRid: unknown,
    targetSpotId: unknown,
    targetSpotGeneration: bigint,
    parts: MessageLike | readonly MessageLike[]
  ): SubmitResultValue {
    this.requireOpen();
    return this.backend.sendFromSpot(
      this.state,
      String(targetNodeRid),
      String(targetSpotId),
      targetSpotGeneration,
      parts
    );
  }

  requestToSpot(
    targetNodeRid: unknown,
    targetSpotId: unknown,
    targetSpotGeneration: bigint,
    parts: MessageLike | readonly MessageLike[],
    options?: { readonly timeoutMs?: number }
  ): MeshOperationId {
    this.requireOpen();
    return this.backend.requestFromSpot(
      this.state,
      String(targetNodeRid),
      String(targetSpotId),
      targetSpotGeneration,
      parts,
      options?.timeoutMs ?? 30_000
    );
  }

  publish(
    channelName: string,
    topic: string,
    parts: MessageLike | readonly MessageLike[],
    options?: { readonly flags?: number }
  ): void {
    this.requireOpen();
    void options;
    this.backend.publishFromSpot(this.state, channelName, topic, parts);
  }

  setSubscription(channelName: string, topicFilter: string, kind = 0): void {
    this.requireOpen();
    if (kind !== 0) throw new RangeError('Only the default logical multicast subscription kind is supported.');
    this.backend.setSpotSubscription(this.state, channelName, topicFilter);
    this.subscriptions.add(`${channelName}\0${topicFilter}\0${kind}`);
  }

  unsetSubscription(channelName: string, topicFilter: string, kind = 0): void {
    this.requireOpen();
    if (kind !== 0) throw new RangeError('Only the default logical multicast subscription kind is supported.');
    this.backend.unsetSpotSubscription(this.state, channelName, topicFilter);
    this.subscriptions.delete(`${channelName}\0${topicFilter}\0${kind}`);
  }

  close(): void {
    if (this.closed) return;
    if (this.state.kind !== 'entry' && !this.backend.closeSpot(this.state)) {
      throw new Error(`Spot '${this.routingId}' still has Actor members.`);
    }
    this.backend.clearSpotSubscriptions(this.state);
    this.closed = true;
    this.subscriptions.clear();
  }

  async dispose(): Promise<void> {
    this.close();
  }

  private requireOpen(): void {
    if (this.closed) throw new Error(`Spot '${this.routingId}' is closed.`);
  }
}

class RawStreamSessionService implements StreamSessionService {
  private state = 1;
  private closed = false;

  constructor(
    private readonly stateful: ServiceStatefulRuntime,
    private readonly stream: StreamSocket,
    private readonly observe: (
      operationKind: number,
      pending: ServiceStatefulPendingOperation
    ) => MeshOperationId
  ) {}

  start(): void {
    if (this.closed) throw new Error('STREAM session service is closed.');
    this.state = 2;
  }

  shutdown(_timeoutMs: number): RequestResultValue {
    this.close();
    return RequestResult.Ok;
  }

  close(): void {
    if (this.closed) return;
    this.closed = true;
    this.state = 5;
  }

  status() {
    const bindings = this.allBindings();
    return {
      state: this.state,
      lifecycleGeneration: 1n,
      sessionCount: BigInt(new Set(bindings.map(value => value.sessionRid)).size),
      bindingCount: BigInt(bindings.length),
      pendingMessageCount: 0n,
      pendingByteCount: 0n,
      lastError: 0
    };
  }

  bindActor(
    sessionRid: RoutingId,
    actor: ServiceActorRef,
    timeoutMs = 30_000
  ): MeshOperationId {
    this.requireStarted();
    return this.observe(
      OperationKind.StreamBind,
      this.stateful.bindSession(
        String(sessionRid),
        actor,
        timeoutMs,
        (targetSessionRid, payload) => this.deliver(targetSessionRid, payload.payload)
      )
    );
  }

  unbindActor(
    sessionRid: RoutingId,
    actor: ServiceActorRef,
    expectedBindingGeneration: bigint,
    timeoutMs = 30_000
  ): MeshOperationId {
    this.requireStarted();
    return this.observe(
      OperationKind.StreamUnbind,
      this.stateful.unbindSession(
        String(sessionRid),
        actor,
        expectedBindingGeneration,
        timeoutMs
      )
    );
  }

  bindings(sessionRid: RoutingId) {
    return this.stateful.sessionBindings(String(sessionRid));
  }

  sendToActor(
    sessionRid: RoutingId,
    actor: ServiceActorRef,
    parts: MessageLike | readonly MessageLike[]
  ): SubmitResultValue {
    this.requireStarted();
    return this.stateful.sendSessionToActor(
      String(sessionRid),
      actor,
      encodeMultipart(parts)
    ) as SubmitResultValue;
  }

  private deliver(sessionRid: string, payload: Uint8Array): boolean {
    const operation = this.stream.send(BindingRoutingId.from(sessionRid));
    const parts = decodeMultipartBuffers(payload);
    if (parts.length === 0) return false;
    let submit = operation.message(parts[0]!);
    for (const part of parts.slice(1)) submit = submit.message(part);
    return submit.submit();
  }

  private allBindings() {
    return this.stateful.allSessionBindings();
  }

  private requireStarted(): void {
    if (this.state !== 2 || this.closed) {
      throw new Error('STREAM session service is not started.');
    }
  }
}

class RawReadyBatch implements ReadyBatch {
  readonly records: ReadyRecord[] = [];
  private readonly claims: RawClaim[] = [];

  constructor(private readonly capacity: number) {
    if (!Number.isInteger(capacity) || capacity < 1) throw new RangeError('Ready capacity must be positive.');
  }

  get full(): boolean {
    return this.records.length >= this.capacity;
  }

  push(record: ReadyRecord, claim: RawClaim): void {
    if (this.full) throw new Error('Ready batch is full.');
    this.records.push(record);
    this.claims.push(claim);
  }

  takeClaim(index: number) {
    if (index < 0 || index >= this.claims.length) {
      throw new RangeError('Ready claim index is invalid.');
    }
    return this.claims[index]!;
  }

  reset(): void {
    this.records.length = 0;
    this.claims.length = 0;
  }

  close(): void {
    this.reset();
  }
}

class RawReceiveBatch implements ReceiveBatch {
  constructor(
    readonly messageCapacity: number,
    readonly partCapacity: number,
    readonly byteCapacity: number
  ) {
    if ([messageCapacity, partCapacity, byteCapacity].some(value => !Number.isInteger(value) || value < 1)) {
      throw new RangeError('Receive batch capacities must be positive.');
    }
  }

  reset(): void {}
  close(): void {}
}

class MailboxClaim implements RawClaim {
  private consumed = false;
  private released = false;

  constructor(
    private readonly runtime: RawServiceMeshRuntime,
    private readonly claim: ServiceMailboxClaim
  ) {}

  recvBatch(batch: ReceiveBatch) {
    if (this.consumed) return { ok: false, records: [] };
    this.consumed = true;
    const capacity = batch as RawReceiveBatch;
    const records: ReceiveRecord[] = [];
    let parts = 0;
    let bytes = 0;
    for (const record of this.claim.records) {
      const decoded = decodeMultipartRecord(this.runtime, record);
      const nextParts = decoded.parts.length;
      const nextBytes = decoded.parts.reduce((sum, part) => sum + part.size(), 0);
      if (
        records.length >= capacity.messageCapacity
        || parts + nextParts > capacity.partCapacity
        || bytes + nextBytes > capacity.byteCapacity
      ) {
        for (const part of decoded.parts) part.close();
        break;
      }
      parts += nextParts;
      bytes += nextBytes;
      records.push(decoded);
    }
    return { ok: records.length > 0, records };
  }

  release(): void {
    if (this.released) return;
    this.released = true;
    this.runtime.mailbox.release(this.claim);
  }
}

class CompletionClaim implements RawClaim {
  private consumed = false;

  constructor(private readonly completion: PendingCompletion) {}

  recvBatch(): { readonly ok: boolean; readonly records: ReceiveRecord[] } {
    if (this.consumed) return { ok: false, records: [] };
    this.consumed = true;
    const payload = this.completion.result.payload;
    return {
      ok: true,
      records: [{
        kind: ReceiveKind.Completion,
        domain: ReadyDomain.Infrastructure,
        sourceNodeRid: null,
        sourceSpotId: null,
        sourceBindingGeneration: 0n,
        sourceActor: null,
        operationId: this.completion.operationId,
        operationKind: this.completion.operationKind,
        channelName: null,
        topic: null,
        applicationMetadata: null,
        kindData: 'kindData' in this.completion.result
          ? this.completion.result.kindData ?? null
          : null,
        terminalResult: this.completion.result.terminalResult,
        failureErrno: this.completion.result.failureCode,
        parts: payload === undefined ? [] : decodeMultipart(payload.payload),
        reply: () => SubmitResult.InvalidState,
        replyActorJoin: () => SubmitResult.NotSupported
      }]
    };
  }

  release(): void {}
}

function decodeMultipartRecord(
  runtime: RawServiceMeshRuntime,
  record: ServiceMailboxRecord
): ReceiveRecord {
  const stateful = statefulMailboxData(record);
  if (stateful !== undefined) {
    return decodeStatefulRecord(record, stateful);
  }
  const header = record.parts[0]!;
  const command = header[3]!;
  const payloadFrame = record.parts[1]!;
  const application = decodeApplicationEnvelope(payloadFrame);
  const channelName = record.owner.startsWith('channel:') ? record.owner.slice('channel:'.length) : null;
  const operationId = record.correlation === undefined
    ? { high: 0n, low: 0n }
    : { high: 1n, low: record.correlation };
  const kind = command === 16
    ? ReceiveKind.NodeSend
    : command === 17
      ? ReceiveKind.NodeRequest
      : command === 18
        ? ReceiveKind.ChannelSend
        : ReceiveKind.ChannelRequest;
  const operationKind = kind === ReceiveKind.NodeRequest
    ? OperationKind.NodeRequest
    : kind === ReceiveKind.ChannelRequest
      ? OperationKind.ChannelRequest
      : 0;
  return {
    kind,
    domain: ReadyDomain.Application,
    sourceNodeRid: record.sourceRoutingId as RoutingId | undefined ?? null,
    sourceSpotId: null,
    sourceBindingGeneration: 0n,
    sourceActor: null,
    operationId,
    operationKind,
    channelName,
    topic: null,
    applicationMetadata: null,
    packetName: application.packetName,
    contentType: application.contentType,
    kindData: null,
    terminalResult: 0,
    failureErrno: 0,
    parts: decodeMultipart(application.payload),
    reply(parts) {
      if (record.correlation === undefined) return SubmitResult.InvalidState;
      runtime.reply(record, encodeMultipart(parts));
      return SubmitResult.Ok;
    },
    replyActorJoin: () => SubmitResult.NotSupported
  };
}

function decodeStatefulRecord(
  record: ServiceMailboxRecord,
  stateful: ServiceStatefulMailboxData
): ReceiveRecord {
  const payloadFrame = record.parts.length < 2 ? undefined : record.parts[1];
  const application = payloadFrame === undefined
    ? undefined
    : decodeApplicationEnvelope(payloadFrame);
  const operationId = record.correlation === undefined
    ? { high: 0n, low: 0n }
    : { high: 2n, low: record.correlation };
  return {
    kind: stateful.receiveKind,
    domain: ReadyDomain.Application,
    sourceNodeRid: record.sourceRoutingId as RoutingId | undefined ?? null,
    sourceSpotId: stateful.sourceSpotId as RoutingId | undefined ?? null,
    sourceBindingGeneration: stateful.sourceBindingGeneration ?? 0n,
    sourceActor: stateful.sourceActor ?? null,
    operationId,
    operationKind: stateful.operationKind,
    channelName: stateful.channelName ?? null,
    topic: stateful.topic ?? null,
    applicationMetadata: stateful.applicationMetadata ?? null,
    ...(application === undefined
      ? {}
      : {
          packetName: application.packetName,
          contentType: application.contentType
        }),
    kindData: stateful.kindData ?? null,
    terminalResult: 0,
    failureErrno: 0,
    parts: application === undefined ? [] : decodeMultipart(application.payload),
    ...(stateful.onTerminalCompletion === undefined
      ? {}
      : { onTerminalCompletion: stateful.onTerminalCompletion }),
    reply(parts) {
      if (stateful.reply === undefined) return SubmitResult.InvalidState;
      return stateful.reply(
        RequestResult.Ok,
        0,
        encodeMultipart(parts)
      ) ? SubmitResult.Ok : SubmitResult.InvalidState;
    },
    replyActorJoin(joinResult, parts) {
      if (stateful.reply === undefined || stateful.targetSpot === undefined) {
        return SubmitResult.InvalidState;
      }
      const membershipEpoch = stateful.kindData?.kind === 'actorControl'
        ? stateful.kindData.currentMembershipEpoch
        : undefined;
      if (joinResult === 0 && membershipEpoch === undefined) {
        return SubmitResult.InvalidState;
      }
      const replyParts = Array.isArray(parts) ? parts : [parts];
      return stateful.reply(
        RequestResult.Ok,
        0,
        replyParts.length === 0 ? undefined : encodeMultipart(replyParts),
        {
          kind: 'actorJoin',
          joinResult: joinResult === 0 ? 0 : 1,
          spot: stateful.targetSpot,
          ...(membershipEpoch === undefined ? {} : { membershipEpoch })
        }
      ) ? SubmitResult.Ok : SubmitResult.InvalidState;
    }
  };
}

function readyOwner(
  owner: string,
  nodeRid: string,
  stateful: ServiceStatefulRuntime | undefined
): ReadyRecord {
  if (owner.startsWith('spot:')) {
    return {
      ownerKind: ReadyOwnerKind.Spot,
      domain: ReadyDomain.Application,
      spotId: owner.slice('spot:'.length),
      actor: null
    };
  }
  if (owner.startsWith('actor:')) {
    const separator = owner.indexOf('\0', 'actor:'.length);
    const actorId = separator < 0
      ? owner.slice('actor:'.length)
      : owner.slice('actor:'.length, separator);
    const generation = separator < 0 ? 0n : BigInt(owner.slice(separator + 1));
    const current = stateful?.actor(actorId);
    return {
      ownerKind: ReadyOwnerKind.Actor,
      domain: ReadyDomain.Application,
      spotId: current?.spot.spotId ?? null,
      actor: current?.ref ?? {
        nodeRid,
        actorId,
        generation
      }
    };
  }
  return {
    ownerKind: ReadyOwnerKind.Node,
    domain: ReadyDomain.Application,
    spotId: null,
    actor: null
  };
}

function encodeMultipart(parts: MessageLike | readonly MessageLike[]) {
  const values = Array.isArray(parts) ? parts : [parts];
  if (values.length === 0) throw new TypeError('Multipart payload must contain at least one part.');
  const bytes = values.map(messageBytes);
  const size = 4 + bytes.reduce((sum, part) => sum + 4 + part.byteLength, 0);
  const payload = Buffer.alloc(size);
  payload.writeUInt32BE(bytes.length, 0);
  let offset = 4;
  for (const part of bytes) {
    payload.writeUInt32BE(part.byteLength, offset);
    offset += 4;
    part.copy(payload, offset);
    offset += part.byteLength;
  }
  return {
    packetName: MULTIPART_PACKET_NAME,
    contentType: MULTIPART_CONTENT_TYPE,
    payload
  };
}

function decodeApplicationEnvelope(frame: Uint8Array) {
  const bytes = Buffer.from(frame);
  if (bytes.length < 5 || bytes[0] !== 1) throw new Error('Invalid application envelope.');
  const bodyLength = bytes.readUInt32BE(1);
  if (bodyLength !== bytes.length - 5) throw new Error('Invalid application envelope length.');
  let offset = 5;
  const packetLength = bytes[offset++]!;
  const packetName = bytes.subarray(offset, offset + packetLength).toString();
  offset += packetLength;
  const contentLength = bytes[offset++]!;
  const contentType = bytes.subarray(offset, offset + contentLength).toString();
  offset += contentLength;
  const payloadLength = bytes.readUInt32BE(offset);
  offset += 4;
  if (
    packetName !== MULTIPART_PACKET_NAME
    || contentType !== MULTIPART_CONTENT_TYPE
    || payloadLength !== bytes.length - offset
  ) {
    throw new Error('Unexpected M6A application payload.');
  }
  return { packetName, contentType, payload: bytes.subarray(offset) };
}

function decodeMultipart(payload: Uint8Array): Message[] {
  const buffers = decodeMultipartBuffers(payload);
  return buffers.map(part => Message.from(part));
}

function decodeMultipartBuffers(payload: Uint8Array): Buffer[] {
  const bytes = Buffer.from(payload);
  if (bytes.length < 4) throw new Error('Truncated multipart payload.');
  const count = bytes.readUInt32BE(0);
  const result: Buffer[] = [];
  let offset = 4;
  for (let index = 0; index < count; index++) {
    if (bytes.length - offset < 4) throw new Error('Truncated multipart part length.');
    const length = bytes.readUInt32BE(offset);
    offset += 4;
    if (bytes.length - offset < length) throw new Error('Truncated multipart part.');
    result.push(Buffer.from(bytes.subarray(offset, offset + length)));
    offset += length;
  }
  if (offset !== bytes.length) throw new Error('Multipart payload has trailing bytes.');
  return result;
}

function messageBytes(value: MessageLike): Buffer {
  if (value instanceof Message) return value.toBytes();
  return Buffer.from(value);
}

function requireRawReadyBatch(batch: ReadyBatch): RawReadyBatch {
  if (!(batch instanceof RawReadyBatch)) throw new TypeError('Ready batch belongs to another backend.');
  return batch;
}

function stateCode(state: ServiceNodeDescriptor['state']): number {
  return ['preparing', 'serving', 'retiring', 'draining', 'stopped', 'error'].indexOf(state) + 1;
}

function requirePositivePlacementValue(value: number, name: string): number {
  if (!Number.isSafeInteger(value) || value <= 0 || value > 0x7fff_ffff) {
    throw new RangeError(`${name} must be an integer in 1..2147483647.`);
  }
  return value;
}

function requirePublicWeight(value: number, name: string): number {
  if (!Number.isInteger(value) || value < 0 || value > 10_000) {
    throw new RangeError(`${name} must be an integer in 0..10000.`);
  }
  return value;
}
