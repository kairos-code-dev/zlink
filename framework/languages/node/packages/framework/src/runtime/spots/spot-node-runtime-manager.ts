import type { ZLinkLocationOptionOverrides } from '../../contracts/Locations/Options';
import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkMessage,
  ZLinkMessageSerializer,
  ZLinkProviderResolver,
  ZLinkPublishResult,
  ZLinkRuntimeEventPublisher,
  ZLinkSpotPublisherClient
} from '../../contracts';
import {
  ZLinkLocationWriteIntent,
  ZLinkSpotKind,
  ZLinkSubmitStatus
} from '../../contracts';
import {
  ZLinkRuntimeDispatchErrorAction as ZLinkDispatchErrorAction,
  ZLinkRuntimeDispatchErrorReason as ZLinkDispatchErrorReason,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind
} from '../../contracts/Dispatch/ZLinkDispatchOptions';
import {
  SubmitError,
  SubmitResult,
  type MeshPublishDetail,
  ReceiveKind,
  type ReadyRecord,
  type ReceiveRecord
} from '@zlink-systems/zlink';
import type { Message } from '../../contracts/Common/Message';
import {
  ZLinkConfigurationException,
  type ZLinkFrameworkRegistration,
  type ZLinkSpotNodeOptions
} from '../configuration';
import type {
  ZLinkBackendAdapterFactory,
  ZLinkBackendContext,
  ZLinkBackendMeshNode,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import { ZLinkMeshDispatchPump } from '../backend/mesh-dispatch-pump';
import { ZLinkMeshCompletionTable } from '../backend/mesh-completion-table';
import type { ZLinkDispatchErrorReporter } from '../channels';
import {
  decodeChannelEnvelope,
  decodeChannelPayload,
  encodeChannelErrorReplyParts,
  encodeChannelPublishEnvelopeParts,
  encodeChannelReplyParts
} from '../channels/channel-envelope';
import {
  ZLinkAutoConnectLoop,
  ZLinkAutoConnectReconciler,
  ZLinkLocationRuntime,
  type ZLinkLocationEventSink,
  type ZLinkLocationRuntimeStores
} from '../locations';
import type { ZLinkRemoteBoundSessionTarget } from '../actors';
import type { ZLinkDetachedTaskRunner } from './spot-actor-join-dispatch';
import { ZLinkEntrySpotActivation } from './spot-entry-activation';
import {
  createSpotNodeLocationAutoConnectContext,
  spotNodeAutoConnectCapability,
  type ZLinkSpotNodeLocationAutoConnectContext
} from './spot-node-autoconnect';
import type { ZLinkSpotRoutedTransport } from './spot-outbound';
import type { ZLinkSpotRouteResolver } from './spot-routing-internal';
import { routingIdsEqual, toBindingRoutingId } from '../routing-id';
import type {
  ZLinkEntryActorRuntime,
  ZLinkSpotActorTransferRuntime,
  ZLinkSpotActorHandoffRuntime,
  ZLinkSpotBoundSessionRuntime
} from './spot-runtime-ports';

const ZLINK_SEND_DONT_WAIT = 1;

export interface ZLinkSpotNodeRuntimeManagerOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly primaryMeshName?: string;
  readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  readonly context: ZLinkBackendContext;
  readonly channelClient?: ZLinkChannelClient;
  readonly fanoutClient?: ZLinkFanoutClient;
  readonly spotPublisherClient?: ZLinkSpotPublisherClient;
  readonly spotRouteResolver?: ZLinkSpotRouteResolver;
  readonly routedTransport?: ZLinkSpotRoutedTransport;
  readonly spotRouterChannelIdForMesh?: (meshName: string) => string;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly entryActorRuntime?: ZLinkEntryActorRuntime;
  readonly actorTransferRuntime?: ZLinkSpotActorTransferRuntime;
  readonly boundSessionRuntime?: ZLinkSpotBoundSessionRuntime;
  readonly actorHandoffRuntime?: ZLinkSpotActorHandoffRuntime;
  readonly detachedTaskRunner?: ZLinkDetachedTaskRunner;
  readonly meshRecordDispatcher?: (
    meshName: string,
    owner: ReadyRecord,
    record: ReceiveRecord
  ) => void | Promise<void>;
}

export class ZLinkSpotNodeRuntimeManager {
  private readonly meshNodes = new Map<string, ZLinkBackendMeshNode>();
  private readonly meshPumps = new Map<string, ZLinkMeshDispatchPump>();
  private readonly meshCompletions = new Map<string, ZLinkMeshCompletionTable>();
  private readonly entryActivations = new Map<string, ZLinkEntrySpotActivation>();
  private readonly entryActivationStarts = new Map<string, Promise<void>>();
  private readonly publishers = new Map<
    string,
    ReturnType<ZLinkBackendMeshNode['createPublisher']>
  >();
  private readonly activePublishes = new Set<string>();
  private readonly autoConnectLoops: ZLinkAutoConnectLoop[] = [];
  private locationAutoConnect?: ZLinkSpotNodeLocationAutoConnectContext;

  constructor(private readonly options: ZLinkSpotNodeRuntimeManagerOptions) {}

  configureLocationAutoConnect(
    runtime: ZLinkLocationRuntime,
    stores: ZLinkLocationRuntimeStores,
    options: ZLinkLocationOptionOverrides,
    events?: ZLinkLocationEventSink
  ): void {
    this.locationAutoConnect = createSpotNodeLocationAutoConnectContext(runtime, stores, options, events);
  }

  async startLocationAutoConnect(signal?: AbortSignal): Promise<void> {
    const location = this.locationAutoConnect;
    if (location === undefined || this.autoConnectLoops.length > 0) {
      return;
    }
    try {
      for (const [spotNodeName, spotNode] of this.options.registration.spotNodes.entries()) {
        const node = this.meshNodes.get(spotNodeName);
        if (node === undefined) {
          continue;
        }
        const capability = spotNodeAutoConnectCapability(spotNodeName, spotNode, node);
        if (capability === undefined) continue;
        const status = node.status();
        await location.runtime.writeSpot({
          meshName: spotNodeName,
          spotRid: String(status.routingId),
          spotGeneration: status.lifecycleGeneration,
          spotType: spotNode.entrySpotType?.name ?? '',
          ownerNodeRid: String(status.routingId),
          ownerNodeGeneration: status.lifecycleGeneration,
          spotKind: ZLinkSpotKind.Entry,
          ownerId: '',
          updatedAt: new Date(0)
        }, ZLinkLocationWriteIntent.NewClaim, signal);
        const reconciler = new ZLinkAutoConnectReconciler({
          local: capability.local,
          localRow: capability.localRow,
          runtime: location.runtime,
          peerResolver: location.resolver,
          executor: capability.executor,
          events: location.events,
          options: location.options
        });
        const loop = new ZLinkAutoConnectLoop({
          reconciler,
          local: capability.local,
          options: location.options,
          changeStampStore: location.changeStampStore,
          watchStore: location.watchStore,
          leaseTracker: location.leaseTracker
        });
        await loop.start(signal);
        this.autoConnectLoops.push(loop);
      }
    } catch (error) {
      await Promise.allSettled(this.autoConnectLoops.map((loop) => loop.stop(signal)));
      this.autoConnectLoops.length = 0;
      throw error;
    }
  }

  async start(): Promise<void> {
    if (this.options.registration.spotNodes.size === 0) {
      return;
    }
    const meshAdapter = this.options.backendAdapterFactory.createMeshAdapter();
    for (const [spotNodeName, spotNode] of this.options.registration.spotNodes.entries()) {
      const routingId = spotNode.router?.routingId ?? spotNode.pubSub?.routingId;
      const bind = spotNode.router?.bind;
      if (routingId === undefined || bind === undefined) {
        throw new ZLinkConfigurationException(
          `MeshNode '${spotNodeName}' requires a routing id and ROUTER bind endpoint.`
        );
      }
      const node = meshAdapter.createMeshNode(this.options.context, {
        meshName: spotNodeName,
        routingId
      });
      let pump: ZLinkMeshDispatchPump | undefined;
      const completions = new ZLinkMeshCompletionTable();
      try {
        node.setBind(bind);
        for (const [channelName, channel] of Object.entries(spotNode.meshChannels ?? {})) {
          node.addChannelName(channelName);
          if (channel.weight !== undefined) {
            node.setChannelWeight(channelName, channel.weight);
          }
        }
        node.start();
        this.publishers.set(spotNodeName, node.createPublisher());
        for (const endpoint of spotNode.router?.manualConnections ?? []) {
          node.connectPeer({ endpoint });
        }
        for (const peer of spotNode.router?.manualPeerConnections ?? []) {
          node.connectPeer({
            endpoint: peer.endpoint,
            expectedRid: toBackendRoutingId(peer.peerRid)
          });
        }
        pump = new ZLinkMeshDispatchPump(node, {
          dispatch: (owner, record) => this.dispatchMeshRecord(spotNodeName, owner, record),
          reportError: (error) => this.options.dispatchErrors?.report({
            surface: ZLinkDispatchErrorSurface.RouteMeshChannel,
            messageKind: ZLinkDispatchMessageKind.Send,
            reason: ZLinkDispatchErrorReason.HandlerException,
            action: ZLinkDispatchErrorAction.Drop,
            channelName: spotNodeName,
            error
          })
        });
        pump.start();
        this.meshNodes.set(spotNodeName, node);
        this.meshPumps.set(spotNodeName, pump);
        this.meshCompletions.set(spotNodeName, completions);
      } catch (error) {
        this.entryActivations.delete(spotNodeName);
        this.publishers.get(spotNodeName)?.close();
        this.publishers.delete(spotNodeName);
        this.meshCompletions.delete(spotNodeName);
        this.meshPumps.delete(spotNodeName);
        this.meshNodes.delete(spotNodeName);
        completions.dispose(error);
        await pump?.dispose();
        node.close();
        throw error;
      }
    }
  }

  get meshNodesByName(): ReadonlyMap<string, ZLinkBackendMeshNode> {
    return this.meshNodes;
  }

  meshNode(meshName: string): ZLinkBackendMeshNode | undefined {
    return this.meshNodes.get(meshName);
  }

  meshCompletionTable(meshName: string): ZLinkMeshCompletionTable | undefined {
    return this.meshCompletions.get(meshName);
  }

  get primaryMeshNode(): ZLinkBackendMeshNode | undefined {
    return this.options.primaryMeshName === undefined
      ? this.meshNodes.values().next().value
      : this.meshNodes.get(this.options.primaryMeshName);
  }

  get primaryMeshCompletions(): ZLinkMeshCompletionTable | undefined {
    const meshName = this.options.primaryMeshName ?? this.meshNodes.keys().next().value;
    return meshName === undefined ? undefined : this.meshCompletions.get(meshName);
  }

  notifyEntrySpotActorCreated(
    nodeRid: RoutingId,
    actor: ZLinkActor,
    createRequest: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<void> {
    const activation = [...this.entryActivations.values()].find(
      (entryActivation) => routingIdsEqual(entryActivation.nodeRid, nodeRid)
    );
    return activation?.notifyCreateActor(actor, createRequest, signal) ?? Promise.resolve();
  }

  notifyPrimaryEntrySpotActorJoined(
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    const activation = this.primaryEntryActivation();
    return activation?.notifyJoinActor(actor, signal) ?? Promise.resolve();
  }

  notifyPrimaryEntrySpotActorLeft(
    actor: ZLinkActor,
    signal?: AbortSignal,
    actorRef?: ActorRef,
    membershipEpoch?: bigint
  ): Promise<void> {
    const activation = this.primaryEntryActivation();
    return activation?.notifyLeaveActor(actor, signal, actorRef, membershipEpoch) ?? Promise.resolve();
  }

  notifyPrimaryEntrySpotActorDisconnected(
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    const activation = this.primaryEntryActivation();
    return activation?.notifyDisconnectActor(actor, signal) ?? Promise.resolve();
  }

  async dispose(signal?: AbortSignal): Promise<void> {
    const autoConnectLoops = [...this.autoConnectLoops];
    const entryActivations = [...this.entryActivations.values()];
    const meshPumps = [...this.meshPumps.values()];
    const meshNodes = [...this.meshNodes.values()];
    const meshCompletions = [...this.meshCompletions.values()];
    this.entryActivations.clear();
    this.autoConnectLoops.length = 0;
    this.meshPumps.clear();
    this.meshNodes.clear();
    this.meshCompletions.clear();
    const publishers = [...this.publishers.values()];
    this.publishers.clear();
    const errors: unknown[] = [];
    const settle = async (operations: readonly Promise<unknown>[]) => {
      const results = await Promise.allSettled(operations);
      errors.push(...results
        .filter((result): result is PromiseRejectedResult => result.status === 'rejected')
        .map((result) => result.reason));
    };
    await settle(publishers.map(async (publisher) => publisher.close()));
    await settle(autoConnectLoops.map((loop) => loop.prepareTransportShutdown()));
    await settle(entryActivations.reverse().map((activation) => activation.dispose()));
    await settle(meshPumps.reverse().map((pump) => pump.dispose()));
    for (const completions of meshCompletions) {
      completions.dispose();
    }
    await settle(meshNodes.reverse().map(async (node) => {
      node.shutdown(1000);
      node.close();
    }));
    await settle(autoConnectLoops.map((loop) => loop.finishTransportShutdown(signal)));
    if (errors.length === 1) throw errors[0];
    if (errors.length > 1) throw new AggregateError(errors, 'SPOT node runtime cleanup failed.');
  }

  private async dispatchMeshRecord(
    meshName: string,
    owner: ReadyRecord,
    record: ReceiveRecord
  ): Promise<void> {
    if (record.kind === ReceiveKind.Completion) {
      const completions = this.meshCompletions.get(meshName);
      if (completions === undefined) {
        throw new ZLinkConfigurationException(
          `MeshNode '${meshName}' received a completion before its completion table was registered.`
        );
      }
      completions.complete(record);
      return;
    }
    const node = this.meshNodes.get(meshName);
    const targetsEntrySpot = owner.spotRid === null
      || (node !== undefined && routingIdsEqual(
        owner.spotRid as unknown as RoutingId,
        String(node.status().routingId)
      ));
    if (targetsEntrySpot && (
      record.kind === ReceiveKind.ActorSend
      || record.kind === ReceiveKind.ActorRequest
      || record.kind === ReceiveKind.SpotSend
      || record.kind === ReceiveKind.SpotRequest
      || record.kind === ReceiveKind.SpotMulticast
    )) {
      await this.ensureEntryActivation(meshName);
    }
    const dispatcher = this.options.meshRecordDispatcher;
    if (dispatcher === undefined) {
      throw new ZLinkConfigurationException(
        `MeshNode '${meshName}' received a record before its framework dispatcher was registered.`
      );
    }
    await dispatcher(meshName, owner, record);
  }

  async dispatchEntrySpotRecord(
    meshName: string,
    owner: ReadyRecord,
    record: ReceiveRecord
  ): Promise<boolean> {
    if (record.kind !== ReceiveKind.SpotSend
      && record.kind !== ReceiveKind.SpotRequest
      && record.kind !== ReceiveKind.SpotMulticast) {
      return false;
    }
    await this.ensureEntryActivation(meshName);
    const activation = this.entryActivations.get(meshName);
    if (
      activation === undefined
      || owner.spotRid === null
      || !routingIdsEqual(activation.spotRid, owner.spotRid as unknown as RoutingId)
    ) {
      return false;
    }
    if (record.kind === ReceiveKind.SpotMulticast) {
      if (record.topic === null || record.topic.length === 0) {
        throw new ZLinkConfigurationException('MeshNode Entry Spot multicast record is missing its topic.');
      }
      await activation.dispatchSubscriptionRecord(
        record.topic,
        record.parts,
        record.sourceNodeRid as unknown as RoutingId | null
      );
      return true;
    }
    const envelope = decodeChannelEnvelope(record.parts);
    const codecs = { serializers: this.options.registration.messageSerializers };
    const payload = decodeChannelPayload(envelope, codecs);
    const context = {
      channelName: envelope.header.channelName,
      contentType: envelope.header.contentType
    };
    if (record.kind === ReceiveKind.SpotSend) {
      await activation.dispatchPacket(envelope.packetName, payload, context, false);
      return true;
    }
    try {
      const response = await activation.dispatchPacket(envelope.packetName, payload, context, true);
      requireEntrySpotReply(record.reply(encodeChannelReplyParts(envelope.header, response, codecs)));
    } catch (error) {
      requireEntrySpotReply(record.reply(encodeChannelErrorReplyParts(envelope.header, error)));
    }
    return true;
  }

  private async ensureEntryActivation(spotNodeName: string): Promise<void> {
    if (this.entryActivations.has(spotNodeName)) {
      return;
    }
    const pending = this.entryActivationStarts.get(spotNodeName);
    if (pending !== undefined) {
      await pending;
      return;
    }
    const spotNode = this.options.registration.spotNodes.get(spotNodeName);
    const node = this.meshNodes.get(spotNodeName);
    if (spotNode === undefined || node === undefined || spotNode.entrySpotType === undefined) {
      return;
    }
    const start = this.initializeEntrySpot(spotNodeName, node, spotNode);
    this.entryActivationStarts.set(spotNodeName, start);
    try {
      await start;
    } finally {
      if (this.entryActivationStarts.get(spotNodeName) === start) {
        this.entryActivationStarts.delete(spotNodeName);
      }
    }
  }

  private async initializeEntrySpot(
    spotNodeName: string,
    node: ZLinkBackendMeshNode,
    spotNode: ZLinkSpotNodeOptions
  ): Promise<void> {
    if (spotNode.entrySpotType === undefined) {
      return;
    }
    const activation = new ZLinkEntrySpotActivation({
      entrySpotType: spotNode.entrySpotType,
      nativeSpot: node.entrySpot() as never,
      nativeNode: node as unknown as ZLinkBackendSpotNode,
      nodeRid: String(node.status().routingId),
      spotNodeName,
      providerResolver: this.options.providerResolver,
      channelClient: this.options.channelClient,
      fanoutClient: this.options.fanoutClient,
      spotPublisherClient: this.options.spotPublisherClient,
      routedTransport: this.options.routedTransport,
      spotRouterChannelIdForMesh: this.options.spotRouterChannelIdForMesh,
      channelMeshNameForChannel: (channelName) => resolveChannelMeshName(
        this.options.registration,
        channelName
      ),
      timerHandlers: spotNode.entrySpotTimerHandlers,
      packetHandlers: spotNode.entrySpotPacketHandlers,
      subscriptionHandlers: spotNode.entrySpotSubscriptionHandlers,
      actorSendHandlers: spotNode.entrySpotActorSendHandlers,
      actorRequestHandlers: spotNode.entrySpotActorRequestHandlers,
      messageSerializers: this.options.registration.messageSerializers,
      entryActorRuntime: this.options.entryActorRuntime,
      actorTransferRuntime: this.options.actorTransferRuntime,
      boundSessionRuntime: this.options.boundSessionRuntime,
      actorHandoffRuntime: this.options.actorHandoffRuntime,
      detachedTaskRunner: this.options.detachedTaskRunner,
      dispatchErrors: this.options.dispatchErrors,
      runtimeEventPublisher: this.options.runtimeEventPublisher,
      metrics: this.options.metrics
    });
    try {
      await activation.create();
      await activation.configure();
      await activation.initialize();
      this.entryActivations.set(spotNodeName, activation);
    } catch (error) {
      try {
        await activation.dispose();
      } catch (disposeError) {
        throw new AggregateError(
          [error, disposeError],
          `Entry Spot '${spotNodeName}' materialization cleanup failed.`
        );
      }
      throw error;
    }
  }

  private primaryEntryActivation(): ZLinkEntrySpotActivation | undefined {
    return this.entryActivations.values().next().value;
  }

  async dispatchEntryActorPacket(
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    let activation = this.primaryEntryActivation();
    if (activation === undefined) {
      const candidates = this.options.primaryMeshName === undefined
        ? this.options.registration.spotNodes
        : new Map([[this.options.primaryMeshName, this.options.registration.spotNodes.get(
          this.options.primaryMeshName
        )]]);
      for (const [spotNodeName, spotNode] of candidates) {
        if (spotNode === undefined) {
          continue;
        }
        if (spotNode.entrySpotType === undefined) {
          continue;
        }
        await this.ensureEntryActivation(spotNodeName);
        activation = this.entryActivations.get(spotNodeName);
        if (activation !== undefined) {
          break;
        }
      }
    }
    if (activation === undefined) {
      throw new ZLinkConfigurationException('Entry Spot actor packet dispatch requires an Entry Spot.');
    }
    return await activation.dispatchActorPacket(
      actorId,
      parts,
      returnResponse,
      remoteBoundSessionTarget,
      fallbackActorRef
    );
  }

  async publish(
    meshName: string,
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: Message,
    signal?: AbortSignal,
    metadata: ReadonlyMap<string, string> = new Map()
  ): Promise<ZLinkPublishResult> {
    if (signal?.aborted === true) {
      throw signal.reason;
    }
    const publisher = this.publishers.get(meshName);
    if (publisher === undefined) {
      throw new ZLinkConfigurationException(`RouteMesh '${meshName}' publisher is not started.`);
    }
    if (this.activePublishes.has(meshName)) {
      return emptyPublishResult(ZLinkSubmitStatus.Backpressured);
    }

    // The slot limits direct handoff capacity but does not commit the publish.
    // The binding arbitrates abort against the native worker's Core-call start.
    this.activePublishes.add(meshName);
    try {
      const parts = encodeChannelPublishEnvelopeParts(
        channelName,
        topic,
        packetName,
        event,
        undefined,
        this.options.dispatchErrors?.flow.flowCreationEnabled() ?? true,
        metadata
      );
      return mapPublishResult(await publisher.publishAsync(
        channelName,
        topic,
        parts,
        undefined,
        signal
      ));
    } finally {
      this.activePublishes.delete(meshName);
    }
  }

  tryPublish(
    meshName: string,
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: Message,
    metadata: ReadonlyMap<string, string> = new Map()
  ): ZLinkPublishResult {
    return this.publishWithFlags(meshName, channelName, topic, packetName, event, ZLINK_SEND_DONT_WAIT, metadata);
  }

  private publishWithFlags(
    meshName: string,
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: Message,
    flags: number,
    metadata: ReadonlyMap<string, string>
  ): ZLinkPublishResult {
    const publisher = this.publishers.get(meshName);
    if (publisher === undefined) {
      throw new ZLinkConfigurationException(`RouteMesh '${meshName}' publisher is not started.`);
    }
    const parts = encodeChannelPublishEnvelopeParts(
      channelName,
      topic,
      packetName,
      event,
      undefined,
      this.options.dispatchErrors?.flow.flowCreationEnabled() ?? true,
      metadata
    );
    try {
      return mapPublishDetail(publisher.publish(channelName, topic, parts, { flags }));
    } catch (error) {
      if (error instanceof SubmitError) {
        return emptyPublishResult(mapPublishSubmitStatus(error.result));
      }
      throw error;
    }
  }
}

function resolveChannelMeshName(
  registration: ZLinkFrameworkRegistration,
  channelName: string
): string | undefined {
  const matches = [...registration.spotNodes.entries()]
    .filter(([, node]) => Object.prototype.hasOwnProperty.call(node.meshChannels ?? {}, channelName))
    .map(([meshName]) => meshName);
  return matches.length === 1 ? matches[0] : undefined;
}

function mapPublishResult(outcome: {
  readonly result: number;
  readonly detail: MeshPublishDetail;
}): ZLinkPublishResult {
  return {
    status: mapPublishSubmitStatus(outcome.result),
    detail: {
      snapshotRemoteNodeCount: BigInt(outcome.detail.snapshotRemoteTargetCount),
      admittedRemoteNodeCount: BigInt(outcome.detail.admittedRemoteTargetCount),
      droppedRemoteNodeCount: BigInt(outcome.detail.droppedRemoteTargetCount),
      unreachableRemoteNodeCount: BigInt(outcome.detail.unreachableRemoteTargetCount),
      snapshotLocalSpotCount: BigInt(outcome.detail.snapshotLocalSpotCount),
      admittedLocalSpotCount: BigInt(outcome.detail.admittedLocalSpotCount),
      droppedLocalSpotCount: BigInt(outcome.detail.droppedLocalSpotCount)
    }
  };
}

function mapPublishDetail(detail: MeshPublishDetail): ZLinkPublishResult {
  return mapPublishResult({ result: SubmitResult.Ok, detail });
}

function mapPublishSubmitStatus(result: number): ZLinkSubmitStatus {
  switch (result) {
    case SubmitResult.Ok:
      return ZLinkSubmitStatus.Submitted;
    case SubmitResult.Backpressured:
    case SubmitResult.NotAdmitted:
      return ZLinkSubmitStatus.Backpressured;
    case SubmitResult.NotFound:
      return ZLinkSubmitStatus.TargetNotFound;
    case SubmitResult.NotConnected:
      return ZLinkSubmitStatus.RouteNotConnected;
    case SubmitResult.Terminated:
    case SubmitResult.InvalidHandle:
      return ZLinkSubmitStatus.Shutdown;
    default:
      throw new ZLinkConfigurationException(`Logical Multicast failed with submit result '${result}'.`);
  }
}

function emptyPublishResult(status: ZLinkSubmitStatus): ZLinkPublishResult {
  return {
    status,
    detail: {
      snapshotRemoteNodeCount: 0n,
      admittedRemoteNodeCount: 0n,
      droppedRemoteNodeCount: 0n,
      unreachableRemoteNodeCount: 0n,
      snapshotLocalSpotCount: 0n,
      admittedLocalSpotCount: 0n,
      droppedLocalSpotCount: 0n
    }
  };
}

function requireEntrySpotReply(result: number): void {
  if (result !== SubmitResult.Ok) {
    throw new ZLinkConfigurationException(
      `MeshNode Entry Spot reply was not accepted (submit result ${result}).`
    );
  }
}

function toBackendRoutingId(routingId: RoutingId) {
  return toBindingRoutingId(routingId);
}
