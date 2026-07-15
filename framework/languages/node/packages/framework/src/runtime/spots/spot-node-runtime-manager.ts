import type {
  RoutingId,
  ZLinkActor,
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkLocationOptions,
  ZLinkMessage,
  ZLinkMessageSerializer,
  ZLinkProviderResolver,
  ZLinkRuntimeEventPublisher,
  ZLinkSpotPublisherClient
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import {
  ZLinkConfigurationException,
  type ZLinkFrameworkRegistration,
  type ZLinkSpotNodeOptions
} from '../configuration';
import type {
  ZLinkBackendAdapterFactory,
  ZLinkBackendContext,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import type { ZLinkDispatchErrorReporter } from '../channels';
import { encodeChannelPublishEnvelopeParts } from '../channels/channel-envelope';
import {
  ZLinkAutoConnectLoop,
  ZLinkAutoConnectReconciler,
  ZLinkLocationRuntime,
  type ZLinkLocationEventSink,
  type ZLinkLocationRuntimeStores
} from '../locations';
import type { ZLinkRemoteBoundSessionTarget } from '../actors';
import { ZLinkWorkerRuntime } from '../workers';
import { ZLinkSpotActorJoinDispatch } from './spot-actor-join-dispatch';
import type { ZLinkDetachedTaskRunner } from './spot-actor-join-dispatch';
import { ZLinkEntrySpotActivation } from './spot-entry-activation';
import {
  createSpotNodeLocationAutoConnectContext,
  spotNodeAutoConnectCapability,
  type ZLinkSpotNodeLocationAutoConnectContext
} from './spot-node-autoconnect';
import {
  ZLinkSpotNodeConnector,
  spotNodeMode,
  type ZLinkOwnedBackendObject,
  type ZLinkSpotPublisherBundle
} from './spot-node-connector';
import type { ZLinkSpotRoutedTransport } from './spot-outbound';
import type { ZLinkSpotRouteResolver } from './spot-routing-internal';
import { ZLinkSpotSerialExecutor } from './spot-serial-executor';
import { routingIdsEqual } from '../routing-id';
import type {
  ZLinkEntryActorRuntime,
  ZLinkSpotActorTransferRuntime,
  ZLinkSpotActorHandoffRuntime,
  ZLinkSpotBoundSessionRuntime
} from './spot-runtime-ports';

export interface ZLinkSpotNodeRuntimeManagerOptions {
  readonly registration: ZLinkFrameworkRegistration;
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
}

export class ZLinkSpotNodeRuntimeManager {
  private readonly nodes = new Map<string, ZLinkBackendSpotNode>();
  private readonly entryActivations = new Map<string, ZLinkEntrySpotActivation>();
  private readonly entryRouteDispatches: ZLinkSpotActorJoinDispatch[] = [];
  private readonly ownedObjects: ZLinkOwnedBackendObject[] = [];
  private readonly publisherBundles = new Map<string, ZLinkSpotPublisherBundle>();
  private readonly autoConnectLoops: ZLinkAutoConnectLoop[] = [];
  private readonly workerRuntime: ZLinkWorkerRuntime;
  private locationAutoConnect?: ZLinkSpotNodeLocationAutoConnectContext;

  constructor(private readonly options: ZLinkSpotNodeRuntimeManagerOptions) {
    this.workerRuntime = new ZLinkWorkerRuntime(options.registration.worker);
  }

  configureLocationAutoConnect(
    runtime: ZLinkLocationRuntime,
    stores: ZLinkLocationRuntimeStores,
    options: ZLinkLocationOptions,
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
        const node = this.nodes.get(spotNodeName);
        const capability = node === undefined ? undefined : spotNodeAutoConnectCapability(spotNodeName, spotNode, node);
        if (capability === undefined) {
          continue;
        }
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
    const spotAdapter = this.options.backendAdapterFactory.createSpotAdapter();
    const channelAdapter = this.options.backendAdapterFactory.createChannelAdapter();
    const connector = new ZLinkSpotNodeConnector({
      registration: this.options.registration,
      context: this.options.context,
      channelAdapter,
      ownedObjects: this.ownedObjects,
      publisherBundles: this.publisherBundles
    });
    for (const [spotNodeName, spotNode] of this.options.registration.spotNodes.entries()) {
      const node = spotAdapter.createSpotNode(this.options.context, spotNodeMode(spotNode));
      try {
        connector.configure(node, spotNodeName, spotNode);
        await this.initializeEntrySpot(spotNodeName, node, spotNode);
        this.nodes.set(spotNodeName, node);
      } catch (error) {
        try {
          await node.dispose();
        } catch (disposeError) {
          throw new AggregateError(
            [error, disposeError],
            `SPOT node '${spotNodeName}' startup cleanup failed.`
          );
        }
        throw error;
      }
    }
  }

  get nodesByName(): ReadonlyMap<string, ZLinkBackendSpotNode> {
    return this.nodes;
  }

  get primaryNode(): ZLinkBackendSpotNode | undefined {
    return this.nodes.values().next().value;
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
    signal?: AbortSignal
  ): Promise<void> {
    const activation = this.primaryEntryActivation();
    return activation?.notifyLeaveActor(actor, signal) ?? Promise.resolve();
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
    const nodes = [...this.nodes.values()];
    const ownedObjects = [...this.ownedObjects];
    const entryActivations = [...this.entryActivations.values()];
    this.nodes.clear();
    this.entryActivations.clear();
    this.entryRouteDispatches.length = 0;
    this.autoConnectLoops.length = 0;
    this.ownedObjects.length = 0;
    const publisherSubmitters = [...this.publisherBundles.values()].map((bundle) => bundle.submitter);
    this.publisherBundles.clear();
    const errors: unknown[] = [];
    const settle = async (operations: readonly Promise<unknown>[]) => {
      const results = await Promise.allSettled(operations);
      errors.push(...results
        .filter((result): result is PromiseRejectedResult => result.status === 'rejected')
        .map((result) => result.reason));
    };
    await settle(publisherSubmitters.map(async (submitter) => submitter.dispose()));
    await settle(autoConnectLoops.map((loop) => loop.stop(signal)));
    await settle(ownedObjects.reverse().map((object) => object.dispose()));
    await settle(entryActivations.reverse().map((activation) => activation.dispose()));
    await settle(nodes.reverse().map((node) => node.dispose()));
    if (errors.length === 1) throw errors[0];
    if (errors.length > 1) throw new AggregateError(errors, 'SPOT node runtime cleanup failed.');
  }

  private async initializeEntrySpot(
    spotNodeName: string,
    node: ZLinkBackendSpotNode,
    spotNode: ZLinkSpotNodeOptions
  ): Promise<void> {
    if (spotNode.entrySpotType === undefined) {
      this.initializeInternalEntryRouteDispatch(node);
      return;
    }
    const nativeSpot = node.entrySpot();
    const activation = new ZLinkEntrySpotActivation({
      entrySpotType: spotNode.entrySpotType,
      nativeSpot,
      nativeNode: node,
      nodeRid: node.routingId,
      spotNodeName,
      providerResolver: this.options.providerResolver,
      channelClient: this.options.channelClient,
      fanoutClient: this.options.fanoutClient,
      spotPublisherClient: this.options.spotPublisherClient,
      routedTransport: this.options.routedTransport,
      spotRouterChannelIdForMesh: this.options.spotRouterChannelIdForMesh,
      timerHandlers: spotNode.entrySpotTimerHandlers,
      packetHandlers: spotNode.entrySpotPacketHandlers,
      subscriptionHandlers: spotNode.entrySpotSubscriptionHandlers,
      actorSendHandlers: spotNode.entrySpotActorSendHandlers,
      actorRequestHandlers: spotNode.entrySpotActorRequestHandlers,
      workerRuntime: this.workerRuntime,
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
    } catch (error) {
      try {
        await activation.dispose();
      } catch (disposeError) {
        throw new AggregateError(
          [error, disposeError],
          `Entry Spot '${spotNodeName}' startup cleanup failed.`
        );
      }
      throw error;
    }
    this.entryActivations.set(spotNodeName, activation);
  }

  private initializeInternalEntryRouteDispatch(node: ZLinkBackendSpotNode): void {
    if (this.options.boundSessionRuntime === undefined) {
      return;
    }
    const dispatch = new ZLinkSpotActorJoinDispatch({
      nativeSpot: node.entrySpot(),
      serial: new ZLinkSpotSerialExecutor(),
      actors: {
        resolveActor: (actorId) => this.options.entryActorRuntime?.resolveActor(actorId),
        getTarget: () => ({}),
        defaultAccept: true,
        transfer: this.options.actorTransferRuntime === undefined ? { kind: 'disabled' } : {
          kind: 'enabled',
          runtime: this.options.actorTransferRuntime
        }
      },
      boundSessionRuntime: this.options.boundSessionRuntime,
      messageSerializers: this.options.messageSerializers,
      providerResolver: this.options.providerResolver,
      dispatchErrors: this.options.dispatchErrors,
      detachedTaskRunner: this.options.detachedTaskRunner
    });
    dispatch.attach();
    this.entryRouteDispatches.push(dispatch);
  }

  private primaryEntryActivation(): ZLinkEntrySpotActivation | undefined {
    return this.entryActivations.values().next().value;
  }

  dispatchEntryActorPacket(
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget
  ): Promise<unknown> {
    const activation = this.primaryEntryActivation();
    if (activation === undefined) {
      throw new ZLinkConfigurationException('Entry Spot actor packet dispatch requires an Entry Spot.');
    }
    return activation.dispatchActorPacket(actorId, parts, returnResponse, remoteBoundSessionTarget);
  }

  publish(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: Message,
    signal?: AbortSignal
  ): Promise<void> {
    const bundle = this.publisherBundles.get(channelName);
    if (bundle === undefined) {
      throw new ZLinkConfigurationException(`SPOT publisher channel '${channelName}' is not started.`);
    }
    const parts = encodeChannelPublishEnvelopeParts(
      channelName,
      topic,
      packetName,
      event,
      undefined,
      this.options.dispatchErrors?.flow.flowCreationEnabled() ?? true
    ) as readonly Message[];
    return bundle.submitter.submitCommand(
      () => bundle.spot.publish(topic, parts, 0),
      signal
    );
  }
}
