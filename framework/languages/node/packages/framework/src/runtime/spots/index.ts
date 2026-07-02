import type {
  ActorRef,
  RoutingId,
  Type,
  ZLinkMessageSerializer,
  ZLinkActor,
  ZLinkChannelClient,
  ZLinkEntrySpot,
  ZLinkEntrySpotTimerHandlerRegistration,
  ZLinkEntrySpotActorSendHandlerRegistration,
  ZLinkEntrySpotActorRequestHandlerRegistration,
  ZLinkEntrySpotContext,
  ZLinkFanoutClient,
  ZLinkPublishCall,
  ZLinkProviderResolver,
  ZLinkRequestCall,
  ZLinkSendCall,
  ZLinkSpot,
  ZLinkSpotTimerHandlerRegistration,
  ZLinkSpotActorSendHandlerRegistration,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotActorRequestHandlerRegistration,
  ZLinkSpotContext,
  ZLinkSpotCreateResult,
  ZLinkSpotCreateResponse,
  ZLinkSpotHandlerRegistry,
  ZLinkSpotInfo,
  ZLinkSpotManager,
  ZLinkSpotOutbound,
  ZLinkSpotPacketHandler,
  ZLinkSpotPublisherClient,
  ZLinkSpotRemoteAddress,
  ZLinkSpotRemoteAddressResolver,
  ZLinkSpotRequestHandler,
  ZLinkSpotSubscriptionHandler,
  ZLinkSpotTimerHandler,
  ZLinkTimer,
  ZLinkTimerOptions,
  ZLinkTimerTick
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import {
  ZLinkDispatchErrorAction,
  ZLinkDispatchErrorReason,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind,
  ZLinkMessageFlowOutcome,
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkEncodedPayload,
  ZLinkMessage,
  isZLinkMessage,
  ZLinkSpotCreateState,
  ZLinkSpotKind,
  ZLinkSpotEventKind,
  ZLinkRuntimeEventPublisher,
  ZLinkTimerOverrunPolicy
} from '../../contracts';
import {
  Message as BindingMessage,
  Received as BindingReceived,
  RoutingId as BindingRoutingId,
  TopicMessage as BindingTopicMessage,
  type MessageLike
} from '@zlink-systems/zlink';
import {
  ZLinkConfigurationException,
  type ZLinkFrameworkRegistration,
  type ZLinkSpotNodeOptions
} from '../configuration';
import type {
  ZLinkBackendAdapterFactory,
  ZLinkChannelBackendAdapter,
  ZLinkBackendActorRef,
  ZLinkBackendContext,
  ZLinkBackendSpot,
  ZLinkBackendSpotNode,
  ZLinkBackendSpotNodeMode,
  ZLinkBackendActorJoinInfo,
  ZLinkBackendActorJoinRequest,
  ZLinkBackendRecvFlags
} from '../backend/contracts';
import {
  ZLINK_BACKEND_SPOT_NODE_MODE_ALL,
  ZLINK_BACKEND_SPOT_NODE_MODE_PUBSUB,
  ZLINK_BACKEND_SPOT_NODE_MODE_ROUTED,
  ZLINK_BACKEND_SPOT_ROUTE_BRIDGE_ROUTE_ONLY,
  ZLinkBackendSpotDispatchEvent
} from '../backend/contracts';

import { ZLinkDispatchErrorReporter, type ZLinkSpotPublisherClientTransport } from '../channels';
import { flowIfEnabled } from '../diagnostics';
import { encodeChannelPublishEnvelopeParts } from '../channels/channel-envelope';
import {
  decodeChannelEnvelope,
  decodeChannelPayload,
  ZLinkChannelMessageKind,
  encodeChannelErrorReplyParts,
  encodeChannelReplyParts,
  type ZLinkChannelEnvelopeCodecRegistry
} from '../channels/channel-envelope';
import { ZLinkAsyncSubmitter } from '../messaging';
import type { ZLinkWorkerCall } from '../../contracts';
import { DefaultZLinkWorkerCall, deliverOnSerial, ZLinkSpotWorkerRuntime } from '../workers';
import {
  captureZLinkSpotSerialTurn,
  isCurrentZLinkSpotSerialTurn,
  runZLinkSpotSerialTurn,
  ZLinkSpotSerialTurn
} from '../execution';
import {
  ZLinkActorPacketKind,
  ZLinkActorDispatchMailboxSet,
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
  ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET,
  ZLinkSpotActorDispatcher,
  ZLinkSpotActorHandlerRegistryRuntime
} from '../actors';
import type { ZLinkRemoteActorPacketTarget, ZLinkRemoteBoundSessionTarget } from '../actors';
import {
  decodeStreamHeader,
  messageToBytes,
  tryGetStreamFrameHeader,
  ZLinkStreamMessageKind
} from '../streams/protocol';
import {
  decodeFrameworkPayloadMessage,
  encodeFrameworkPayloadMessage,
  wrapFrameworkPayloadMessage
} from '../messaging/payload-codec';

// DontWait recv flag (core RecvFlags.DontWait = 1): non-blocking drain.
const ZLINK_RECV_DONT_WAIT = 1 as ZLinkBackendRecvFlags;
const ZLINK_SPOT_DISPATCH_SUBJECT_SPOT = 1;
const ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER = 3;
const ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED = 1;
const ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT = 2;
const ZLINK_SPOT_ACTOR_LIFECYCLE_DISCONNECTED = 3;

export interface ZLinkSpotManagerOptions {
  readonly spotFactories: readonly Type<ZLinkSpot>[];
  readonly spotTimerHandlers?: readonly ZLinkSpotTimerHandlerRegistration[];
  readonly spotActorSendHandlers?: readonly ZLinkSpotActorSendHandlerRegistration[];
  readonly spotActorRequestHandlers?: readonly ZLinkSpotActorRequestHandlerRegistration[];
  readonly nodeRid?: RoutingId;
  readonly nodeRidProvider?: () => RoutingId | undefined;
  readonly entryNodeRid?: RoutingId;
  readonly entryNodeRidProvider?: () => RoutingId | undefined;
  readonly actorEntryNodeRidProvider?: (actor: ZLinkActor) => RoutingId | undefined;
  readonly entrySpotCallbacks?: {
    onJoinedActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
    onLeaveActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
  };
  readonly actorCountProvider?: (spotRid: RoutingId) => number;
  readonly channelClient?: ZLinkChannelClient;
  readonly fanoutClient?: ZLinkFanoutClient;
  readonly spotPublisherClient?: ZLinkSpotPublisherClient;
  readonly remoteAddressResolver?: ZLinkSpotRemoteAddressResolver;
  readonly routedTransport?: ZLinkSpotRoutedTransport;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  readonly workerRuntime?: ZLinkSpotWorkerRuntime;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  // Backs each user Spot with a core-native Spot object (registered for join
  // routing by rid) so actor-join admission uses the same recv/reply round-trip
  // as the Entry Spot and .NET, for local and remote callers alike.
  readonly createNativeSpot?: (spotRid: RoutingId) => ZLinkBackendSpot | undefined;
  readonly nativeSpotNodeProvider?: () => ZLinkBackendSpotNode | undefined;
  readonly actorResolver?: (actorId: string) => ZLinkActor | undefined;
  readonly routedActorProvider?: (
    actorId: string,
    actorType: string,
    actorRef?: ZLinkBackendActorRef,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    actorCreateRequest?: Message,
    signal?: AbortSignal
  ) => Promise<ZLinkRemoteActorJoinActor>;
  readonly nativeJoinBoundSessionTargetResolver?: (
    info: ZLinkBackendActorJoinInfo
  ) => ZLinkRemoteBoundSessionTarget | undefined;
  readonly routedActorCommitter?: (actor: ZLinkActor, spotRid: RoutingId, spot: ZLinkSpot) => void;
  readonly routedActorLeaveCommitter?: (actor: ZLinkActor) => void;
  readonly actorResponseSender?: (
    actor: ZLinkActor,
    packetName: string,
    requestSeq: bigint,
    response: unknown,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly actorErrorSender?: (
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    actorRef?: ActorRef,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly routedBoundSessionReceiver?: (
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly remoteActorPacketTargetReceiver?: (
    actorId: string,
    target: ZLinkRemoteBoundSessionTarget
  ) => void;
  readonly actorPacketTargetProvider?: (actorId: string) => ZLinkRemoteActorPacketTarget | undefined;
  readonly localActorPacketRouter?: (
    actorId: string,
    parts: readonly Message[],
    returnResponse?: boolean,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget
  ) => Promise<{ readonly handled: boolean; readonly response?: unknown }>;
}

export interface ZLinkSpotNodeRuntimeManagerOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  readonly context: ZLinkBackendContext;
  readonly channelClient?: ZLinkChannelClient;
  readonly fanoutClient?: ZLinkFanoutClient;
  readonly spotPublisherClient?: ZLinkSpotPublisherClient;
  readonly remoteAddressResolver?: ZLinkSpotRemoteAddressResolver;
  readonly routedTransport?: ZLinkSpotRoutedTransport;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly actorResolver?: (actorId: string) => ZLinkActor | undefined;
  readonly routedActorProvider?: ZLinkSpotManagerOptions['routedActorProvider'];
  readonly routedBoundSessionReceiver?: ZLinkSpotManagerOptions['routedBoundSessionReceiver'];
  readonly remoteActorPacketTargetReceiver?: ZLinkSpotManagerOptions['remoteActorPacketTargetReceiver'];
  readonly actorPacketTargetProvider?: ZLinkSpotManagerOptions['actorPacketTargetProvider'];
  readonly actorResponseSender?: ZLinkSpotManagerOptions['actorResponseSender'];
  readonly actorErrorSender?: ZLinkSpotManagerOptions['actorErrorSender'];
  readonly localActorPacketRouter?: ZLinkSpotManagerOptions['localActorPacketRouter'];
  readonly entryActorCommitter?: (actor: ZLinkActor) => void;
  readonly actorDestroyer?: (
    node: ZLinkBackendSpotNode,
    entryNodeRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ) => Promise<void>;
}

const REMOTE_ACTOR_JOIN_PACKET = '__zlink.actor.join_spot.request';
const REMOTE_BOUND_SESSION_BIND_PACKET = 'zlink.framework.actor.bound_session.bind';

interface ZLinkRemoteActorJoinActor {
  readonly actor: ZLinkActor;
  readonly actorRef: ZLinkBackendActorRef;
}

interface ZLinkRemoteActorJoinRequest {
  readonly packetName: typeof REMOTE_ACTOR_JOIN_PACKET;
  readonly actorId: string;
  readonly actorType: string;
  readonly actorNodeRid?: string;
  readonly actorGeneration?: string;
  readonly actorCreateRequest?: string;
  readonly sourceSpotRid?: string;
  readonly routerChannelId?: string;
}

export class ZLinkSpotNodeRuntimeManager {
  private readonly nodes = new Map<string, ZLinkBackendSpotNode>();
  private readonly entryActivations = new Map<string, ZLinkEntrySpotActivation>();
  private readonly entryRouteDispatches: ZLinkSpotActorJoinDispatch[] = [];
  private readonly ownedObjects: ZLinkOwnedBackendObject[] = [];
  private readonly publisherBundles = new Map<string, ZLinkSpotPublisherBundle>();
  private readonly workerRuntime: ZLinkSpotWorkerRuntime;

  constructor(private readonly options: ZLinkSpotNodeRuntimeManagerOptions) {
    this.workerRuntime = new ZLinkSpotWorkerRuntime(options.registration.worker);
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
      connector.configure(node, spotNodeName, spotNode);
      await this.initializeEntrySpot(spotNodeName, node, spotNode);
      this.nodes.set(spotNodeName, node);
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
      (entryActivation) => String(entryActivation.nodeRid) === String(nodeRid)
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

  async dispose(): Promise<void> {
    const nodes = [...this.nodes.values()];
    const ownedObjects = [...this.ownedObjects];
    const entryActivations = [...this.entryActivations.values()];
    this.nodes.clear();
    this.entryActivations.clear();
    this.entryRouteDispatches.length = 0;
    this.ownedObjects.length = 0;
    for (const bundle of this.publisherBundles.values()) {
      bundle.submitter.dispose();
    }
    this.publisherBundles.clear();
    for (const object of ownedObjects.reverse()) {
      await object.dispose();
    }
    for (const activation of entryActivations.reverse()) {
      await activation.dispose();
    }
    for (const node of nodes.reverse()) {
      await node.dispose();
    }
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
      remoteAddressResolver: this.options.remoteAddressResolver,
      routedTransport: this.options.routedTransport,
      timerHandlers: spotNode.entrySpotTimerHandlers,
      actorSendHandlers: spotNode.entrySpotActorSendHandlers,
      actorRequestHandlers: spotNode.entrySpotActorRequestHandlers,
      workerRuntime: this.workerRuntime,
      messageSerializers: this.options.registration.messageSerializers,
      actorResolver: (actorId) => this.options.actorResolver?.(actorId),
      entryActorCommitter: this.options.entryActorCommitter,
      routedBoundSessionReceiver: this.options.routedBoundSessionReceiver,
      remoteActorPacketTargetReceiver: this.options.remoteActorPacketTargetReceiver,
      actorPacketTargetProvider: this.options.actorPacketTargetProvider,
      localActorPacketRouter: this.options.localActorPacketRouter,
      actorResponseSender: this.options.actorResponseSender,
      actorErrorSender: this.options.actorErrorSender,
      dispatchErrors: this.options.dispatchErrors,
      runtimeEventPublisher: this.options.runtimeEventPublisher,
      destroyActor: (nodeRid, actor, signal) => {
        if (this.options.actorDestroyer === undefined) {
          throw new ZLinkConfigurationException('Entry Spot actor destroy runtime is not started.');
        }
        return this.options.actorDestroyer(node, nodeRid, actor, signal);
      }
    });
    try {
      await activation.create();
      await activation.configure();
      await activation.initialize();
    } catch (error) {
      await activation.dispose();
      throw error;
    }
    this.entryActivations.set(spotNodeName, activation);
  }

  private initializeInternalEntryRouteDispatch(node: ZLinkBackendSpotNode): void {
    if (this.options.routedBoundSessionReceiver === undefined) {
      return;
    }
    const dispatch = new ZLinkSpotActorJoinDispatch(
      node.entrySpot(),
      new ZLinkSpotSerialExecutor(),
      (actorId) => this.options.actorResolver?.(actorId),
      () => ({}),
      true,
      undefined,
      undefined,
      undefined,
      undefined,
      this.options.routedBoundSessionReceiver,
      this.options.actorPacketTargetProvider,
      undefined,
      this.options.messageSerializers,
      this.options.providerResolver,
      this.options.dispatchErrors
    );
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

  publishSpot(
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
      event
    ) as readonly Message[];
    return bundle.submitter.submitCommand(
      () => bundle.spot.publish(topic, parts, 0),
      signal
    );
  }

}

interface ZLinkSpotNodeConnectorOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly context: ZLinkBackendContext;
  readonly channelAdapter: ZLinkChannelBackendAdapter;
  readonly ownedObjects: ZLinkOwnedBackendObject[];
  readonly publisherBundles: Map<string, ZLinkSpotPublisherBundle>;
}

class ZLinkSpotNodeConnector {
  constructor(private readonly options: ZLinkSpotNodeConnectorOptions) {}

  configure(node: ZLinkBackendSpotNode, spotNodeName: string, spotNode: ZLinkSpotNodeOptions): void {
    this.applySpotNodeOptions(node, spotNodeName, spotNode);
    this.initializeSpotPublisherClient(node, spotNodeName, spotNode);
  }

  private applySpotNodeOptions(node: ZLinkBackendSpotNode, spotNodeName: string, spotNode: ZLinkSpotNodeOptions): void {
    const routingId = spotNode.router?.routingId ?? spotNode.pubSub?.routingId;
    if (routingId !== undefined) {
      node.setRoutingId(routingId);
    }
    if (spotNode.pubSub?.routingId !== undefined) {
      node.setPublisherRoutingId(spotNode.pubSub.routingId);
      node.setSubscriberRoutingId(spotNode.pubSub.routingId);
    }
    if (spotNode.entrySpot?.routingId !== undefined) {
      node.entrySpot().setRoutingId(spotNode.entrySpot.routingId);
    } else if (spotNode.router?.routingId !== undefined) {
      node.entrySpot().setRoutingId(spotNode.router.routingId);
    }
    if (spotNode.router?.bind !== undefined) {
      node.setRouterBind(spotNode.router.bind);
    }
    for (const endpoint of spotNode.router?.manualConnections ?? []) {
      node.connectPeer(endpoint);
    }
    for (const connection of spotNode.router?.manualPeerConnections ?? []) {
      setImmediate(() => node.connectPeerRid(connection.peerRid, connection.endpoint));
    }
    if (spotNode.pubSub?.bind !== undefined) {
      node.setPubBind(spotNode.pubSub.bind);
    }
    for (const endpoint of spotNode.pubSub?.manualConnections ?? []) {
      node.connectPeer(endpoint);
    }
  }

  private initializeSpotPublisherClient(
    node: ZLinkBackendSpotNode,
    spotNodeName: string,
    spotNode: ZLinkSpotNodeOptions
  ): void {
    if (spotNode.pubSub === undefined) {
      return;
    }
    const publisher = node.createSpot();
    const submitter = new ZLinkAsyncSubmitter((handler) => publisher.onSendReady(handler));
    this.options.ownedObjects.push(publisher);
    this.options.publisherBundles.set(spotNodeName, { node, spot: publisher, submitter });
  }

  private isStreamSessionRelayNode(spotNodeName: string): boolean {
    return this.options.registration.spotNodes.get(spotNodeName)?.router !== undefined;
  }

}

function spotNodeMode(spotNode: ZLinkSpotNodeOptions): ZLinkBackendSpotNodeMode {
  if (spotNode.router !== undefined && spotNode.pubSub !== undefined) {
    return ZLINK_BACKEND_SPOT_NODE_MODE_ALL;
  }
  if (spotNode.router !== undefined) {
    return ZLINK_BACKEND_SPOT_NODE_MODE_ROUTED;
  }
  if (spotNode.pubSub !== undefined) {
    return ZLINK_BACKEND_SPOT_NODE_MODE_PUBSUB;
  }
  return ZLINK_BACKEND_SPOT_NODE_MODE_ALL;
}

interface ZLinkOwnedBackendObject {
  dispose(): Promise<void>;
}

interface ZLinkSpotPublisherBundle {
  readonly node: ZLinkBackendSpotNode;
  readonly spot: ZLinkBackendSpot;
  readonly submitter: ZLinkAsyncSubmitter;
}

interface ZLinkEntrySpotActivationOptions {
  readonly entrySpotType: Type<ZLinkEntrySpot>;
  readonly timerHandlers?: readonly ZLinkEntrySpotTimerHandlerRegistration[];
  readonly actorSendHandlers?: readonly ZLinkEntrySpotActorSendHandlerRegistration[];
  readonly actorRequestHandlers?: readonly ZLinkEntrySpotActorRequestHandlerRegistration[];
  readonly nativeSpot: ZLinkBackendSpot;
  readonly nativeNode: ZLinkBackendSpotNode;
  readonly nodeRid: RoutingId;
  readonly spotNodeName: string;
  readonly channelClient?: ZLinkChannelClient;
  readonly fanoutClient?: ZLinkFanoutClient;
  readonly spotPublisherClient?: ZLinkSpotPublisherClient;
  readonly remoteAddressResolver?: ZLinkSpotRemoteAddressResolver;
  readonly routedTransport?: ZLinkSpotRoutedTransport;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  readonly workerRuntime?: ZLinkSpotWorkerRuntime;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly actorResolver?: (actorId: string) => ZLinkActor | undefined;
  readonly entryActorCommitter?: (actor: ZLinkActor) => void;
  readonly destroyActor?: (
    nodeRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly routedBoundSessionReceiver?: ZLinkSpotManagerOptions['routedBoundSessionReceiver'];
  readonly actorResponseSender?: ZLinkSpotManagerOptions['actorResponseSender'];
  readonly actorErrorSender?: ZLinkSpotManagerOptions['actorErrorSender'];
  readonly remoteActorPacketTargetReceiver?: ZLinkSpotManagerOptions['remoteActorPacketTargetReceiver'];
  readonly actorPacketTargetProvider?: ZLinkSpotManagerOptions['actorPacketTargetProvider'];
  readonly localActorPacketRouter?: ZLinkSpotManagerOptions['localActorPacketRouter'];
}

/**
 * Drives the core actor-join admission round-trip for a single Spot (user Spot
 * or Entry Spot). The CAPI already implements join admission and local/remote
 * routing; the framework only registers the native dispatch handler, runs the
 * Spot's `onActorJoin` on the Spot serial executor, and replies. User and Entry
 * Spots use this same path — they differ only in actor create/destroy ownership.
 */
interface ZLinkActorJoinAdmissionTarget {
  onActorJoin?(actor: ZLinkActor, request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotActorJoinResponse>;
  onJoinedActor?(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
  onLeaveActor?(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
  onDisconnectActor?(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
}

interface ZLinkActorDispatchPart {
  readonly info: {
    readonly actor: ZLinkBackendActorRef;
    readonly sourceNodeRid?: RoutingId;
    readonly sourceSessionRid?: RoutingId;
  };
  readonly message: Message;
  readonly more: boolean;
}

/**
 * Owns SPOT subscription (pub/sub) dispatch: topic-handler registration plus
 * native subscription wiring, the non-blocking subscribe drain loop, and
 * publish-envelope decode + handler invocation with flow tracing. Separated
 * from ZLinkSpotActorJoinDispatch because pub/sub is an independent concern
 * from actor-join admission and route dispatch.
 */
class ZLinkSpotSubscriptionDispatcher {
  private draining = false;
  private readonly handlers = new Map<string, ZLinkSpotHandlerRegistration[]>();

  constructor(
    private readonly nativeSpot: ZLinkBackendSpot,
    private readonly serial: ZLinkSpotSerialExecutor,
    private readonly getTarget: () => ZLinkActorJoinAdmissionTarget,
    private readonly messageSerializers: ReadonlyMap<string, ZLinkMessageSerializer> | undefined,
    private readonly providerResolver: ZLinkProviderResolver | undefined,
    private readonly dispatchErrors: ZLinkDispatchErrorReporter | undefined
  ) {}

  private channelCodecs(): ZLinkChannelEnvelopeCodecRegistry | undefined {
    return this.messageSerializers === undefined
      ? undefined
      : { serializers: this.messageSerializers };
  }

  configure(registrations: readonly ZLinkSpotHandlerRegistration[]): void {
    for (const registration of registrations) {
      if (registration.kind !== 'subscribe' || registration.topic === undefined) {
        continue;
      }
      const existing = this.handlers.get(registration.topic) ?? [];
      existing.push(registration);
      this.handlers.set(registration.topic, existing);
      this.nativeSpot.setSubscription(registration.topic);
    }
  }

  async drain(): Promise<void> {
    if (this.draining) {
      return;
    }
    this.draining = true;
    let message = new BindingTopicMessage();
    try {
      for (;;) {
        if (!this.nativeSpot.subscribe(message, ZLINK_RECV_DONT_WAIT)) {
          message.close();
          return;
        }
        try {
          await this.dispatch(message);
        } finally {
          message.close();
        }
        message = new BindingTopicMessage();
      }
    } finally {
      this.draining = false;
      message.close();
    }
  }

  private async dispatch(message: BindingTopicMessage): Promise<void> {
    const registrations = this.handlers.get(message.topic);
    if (registrations === undefined || registrations.length === 0 || message.parts.length === 0) {
      this.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotSubscription,
        messageKind: ZLinkDispatchMessageKind.Publish,
        reason: message.parts.length === 0
          ? ZLinkDispatchErrorReason.InvalidFrame
          : ZLinkDispatchErrorReason.HandlerMissing,
        action: ZLinkDispatchErrorAction.Drop,
        topic: message.topic,
        sourceRid: message.routingId === null ? undefined : String(message.routingId)
      });
      return;
    }
    const envelope = decodeChannelEnvelope(message.parts);
    if (envelope.header.kind !== ZLinkChannelMessageKind.Publish) {
      this.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotSubscription,
        messageKind: ZLinkDispatchMessageKind.Publish,
        reason: ZLinkDispatchErrorReason.InvalidFrame,
        action: ZLinkDispatchErrorAction.Drop,
        packetName: envelope.packetName,
        channelName: envelope.header.channelName,
        topic: message.topic,
        sourceRid: message.routingId === null ? undefined : String(message.routingId),
        correlationId: envelope.header.correlationId ?? undefined
      });
      return;
    }
    const event = decodeChannelPayload(envelope, this.channelCodecs());
    const spot = this.getTarget() as ZLinkSpot;
    const subSource = message.routingId === null ? undefined : String(message.routingId);
    const subCorr = envelope.header.correlationId ?? undefined;
    flowIfEnabled(this.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Received)?.trace({
      outcome: ZLinkMessageFlowOutcome.Received,
      surface: ZLinkDispatchErrorSurface.SpotSubscription,
      messageKind: ZLinkDispatchMessageKind.Publish,
      packetName: envelope.packetName,
      channelName: envelope.header.channelName,
      topic: message.topic,
      sourceRid: subSource,
      correlationId: subCorr
    });
    await this.serial.execute(async () => {
      for (const registration of registrations) {
        const handler = await createProviderInstance(
          registration.handlerType as Type<ZLinkSpotSubscriptionHandler<ZLinkSpot, unknown>>,
          this.providerResolver
        );
        try {
          await handler.handle(spot, event, {
            channelName: envelope.header.channelName,
            contentType: envelope.header.contentType,
            packetName: envelope.packetName,
            topic: message.topic,
            source: subSource
          });
          flowIfEnabled(this.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Dispatched)?.trace({
            outcome: ZLinkMessageFlowOutcome.Dispatched,
            surface: ZLinkDispatchErrorSurface.SpotSubscription,
            messageKind: ZLinkDispatchMessageKind.Publish,
            packetName: envelope.packetName,
            channelName: envelope.header.channelName,
            topic: message.topic,
            sourceRid: subSource,
            correlationId: subCorr
          });
        } catch (error) {
          this.dispatchErrors?.report({
            surface: ZLinkDispatchErrorSurface.SpotSubscription,
            messageKind: ZLinkDispatchMessageKind.Publish,
            reason: ZLinkDispatchErrorReason.HandlerException,
            action: ZLinkDispatchErrorAction.Drop,
            packetName: envelope.packetName,
            channelName: envelope.header.channelName,
            topic: message.topic,
            sourceRid: message.routingId === null ? undefined : String(message.routingId),
            correlationId: envelope.header.correlationId ?? undefined,
            error
          });
          throw error;
        }
      }
    });
  }
}

class ZLinkSpotActorJoinDispatch {
  private draining = false;
  private routeDraining = false;
  private readonly subscriptions: ZLinkSpotSubscriptionDispatcher;
  private readonly packetHandlers = new Map<string, ZLinkSpotHandlerRegistration[]>();

  constructor(
    private readonly nativeSpot: ZLinkBackendSpot,
    private readonly serial: ZLinkSpotSerialExecutor,
    private readonly resolveActor: (actorId: string) => ZLinkActor | undefined,
    private readonly getTarget: () => ZLinkActorJoinAdmissionTarget,
    private readonly defaultAccept: boolean,
    private readonly routedActorProvider?: ZLinkSpotManagerOptions['routedActorProvider'],
    private readonly nativeJoinBoundSessionTargetResolver?: ZLinkSpotManagerOptions['nativeJoinBoundSessionTargetResolver'],
    private readonly commitRoutedActor?: (actor: ZLinkActor) => Promise<void> | void,
    private readonly actorPacketHandler?: (
      actorId: string,
      parts: readonly Message[],
      returnResponse?: boolean,
      remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
      fallbackActorRef?: ActorRef
    ) => Promise<unknown>,
    private readonly routedBoundSessionReceiver?: ZLinkSpotManagerOptions['routedBoundSessionReceiver'],
    private readonly actorPacketTargetProvider?: ZLinkSpotManagerOptions['actorPacketTargetProvider'],
    private readonly bindRemoteActorSession?: (
      actor: ZLinkBackendActorRef,
      sourceNodeRid: RoutingId,
      sourceSessionRid: RoutingId
    ) => void,
    private readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>,
    private readonly providerResolver?: ZLinkProviderResolver,
    private readonly dispatchErrors?: ZLinkDispatchErrorReporter
  ) {
    this.subscriptions = new ZLinkSpotSubscriptionDispatcher(
      nativeSpot,
      serial,
      getTarget,
      messageSerializers,
      providerResolver,
      dispatchErrors
    );
  }

  private channelCodecs(): ZLinkChannelEnvelopeCodecRegistry | undefined {
    return this.messageSerializers === undefined
      ? undefined
      : { serializers: this.messageSerializers };
  }

  configureSubscriptions(registrations: readonly ZLinkSpotHandlerRegistration[]): void {
    this.subscriptions.configure(registrations);
    for (const registration of registrations) {
      if (registration.kind !== 'packet') {
        continue;
      }
      const packetName = registration.packetName ?? registration.handlerType.name;
      const existing = this.packetHandlers.get(packetName) ?? [];
      existing.push(registration);
      this.packetHandlers.set(packetName, existing);
    }
  }

  attach(): void {
    if (typeof this.nativeSpot.setDispatchHandler !== 'function') {
      return;
    }
    this.nativeSpot.setDispatchHandler((info) => {
      if (info.event === ZLinkBackendSpotDispatchEvent.ActorJoinReadable) {
        void this.drain();
        return;
      }
      if (info.event === ZLinkBackendSpotDispatchEvent.SubscribeReadable) {
        void this.subscriptions.drain().catch((error) => console.error(error));
        return;
      }
      if (info.event === ZLinkBackendSpotDispatchEvent.ChannelReplyReadable) {
        if (info.subjectKind === ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER &&
            info.subjectHandle !== undefined) {
          this.nativeSpot.drainChannelReply(info.subjectHandle);
          return;
        }
        if (info.subjectKind === ZLINK_SPOT_DISPATCH_SUBJECT_SPOT ||
            info.subjectKind === undefined) {
          this.nativeSpot.drainReply();
        }
        return;
      }
      if (info.event === ZLinkBackendSpotDispatchEvent.RoutedReadable) {
        void this.drainRoutes(info.routed ?? undefined, Date.now() + 2000);
        return;
      }
      if (info.event === ZLinkBackendSpotDispatchEvent.ActorReadable) {
        void this.drainActorPackets(info as unknown as {
          recvActorPart(flags?: number): ZLinkActorDispatchPart | null;
        });
        return;
      }
      if (info.event === ZLinkBackendSpotDispatchEvent.ActorLifecycleReadable) {
        void this.drainActorLifecycle();
      }
    });
  }

  private async drainActorLifecycle(): Promise<void> {
    try {
      for (;;) {
        const event = this.nativeSpot.recvActorLifecycle(ZLINK_RECV_DONT_WAIT) as {
          kind: number;
          info: {
            previousActor?: ZLinkBackendActorRef | null;
            currentActor?: ZLinkBackendActorRef | null;
          };
        } | null;
        if (event === null) {
          return;
        }
        const actorRef = event.kind === ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT
          ? event.info.previousActor
          : event.info.currentActor;
        const actorId = actorRef?.actorId;
        const actor = actorId === undefined ? undefined : this.resolveActor(actorId);
        if (actor === undefined) {
          continue;
        }
        await this.serial.execute(() => {
          const target = this.getTarget();
          if (event.kind === ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED) {
            return target.onJoinedActor?.(actor);
          }
          if (event.kind === ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT) {
            return target.onLeaveActor?.(actor);
          }
          if (event.kind === ZLINK_SPOT_ACTOR_LIFECYCLE_DISCONNECTED) {
            return target.onDisconnectActor?.(actor);
          }
          return undefined;
        });
      }
    } catch (error) {
      console.error(error);
    }
  }

  private async drainActorPackets(info: {
    recvActorPart(flags?: number): ZLinkActorDispatchPart | null;
  }): Promise<void> {
    const parts: Message[] = [];
    let actorId: string | undefined;
    let actorRef: ZLinkBackendActorRef | undefined;
    let sourceNodeRid: RoutingId | undefined;
    let sourceSessionRid: RoutingId | undefined;
    try {
      for (;;) {
        const part = info.recvActorPart(ZLINK_RECV_DONT_WAIT);
        if (part === null) {
          return;
        }
        actorId ??= part.info.actor.actorId;
        actorRef ??= part.info.actor;
        sourceNodeRid ??= part.info.sourceNodeRid;
        sourceSessionRid ??= part.info.sourceSessionRid;
        parts.push(part.message);
        if (!part.more) {
          break;
        }
      }
      if (sourceNodeRid !== undefined && sourceSessionRid !== undefined) {
        this.bindRemoteActorSession?.(actorRef, sourceNodeRid, sourceSessionRid);
      }
      if (this.consumeRemoteBoundSessionBind(actorRef, sourceNodeRid, sourceSessionRid, parts)) {
        return;
      }
      if (this.actorPacketHandler === undefined) {
        return;
      }
      await this.actorPacketHandler(
        actorId,
        parts,
        false,
        undefined,
        actorRef as unknown as ActorRef | undefined
      );
    } finally {
      for (const part of parts) {
        part.close();
      }
    }
  }

  private consumeRemoteBoundSessionBind(
    actor: ZLinkBackendActorRef | undefined,
    sourceNodeRid: RoutingId | undefined,
    sourceSessionRid: RoutingId | undefined,
    parts: readonly Message[]
  ): boolean {
    if (parts.length < 1) {
      return false;
    }
    let header: ReturnType<typeof decodeStreamHeader>;
    try {
      header = decodeStreamHeader(messageToBytes(parts[0]));
    } catch {
      return false;
    }
    if (header.name !== REMOTE_BOUND_SESSION_BIND_PACKET) {
      return false;
    }
    if (actor !== undefined && sourceNodeRid !== undefined && sourceSessionRid !== undefined) {
      this.bindRemoteActorSession?.(actor, sourceNodeRid, sourceSessionRid);
    }
    return true;
  }

  private async drain(): Promise<void> {
    if (this.draining) {
      return;
    }
    this.draining = true;
    try {
      for (;;) {
        const request = this.nativeSpot.recvActorJoin(ZLINK_RECV_DONT_WAIT);
        if (request === null) {
          return;
        }
        await this.admit(request);
      }
    } finally {
      this.draining = false;
    }
  }

  private async admit(request: ZLinkBackendActorJoinRequest): Promise<void> {
    const actorId = request.info.targetActor.actorId;
    let accepted = false;
    let reply: Message | undefined;
    let joinedActor: ZLinkActor | undefined;
    const decoded = this.decodeNativeActorJoinRequest(actorId, request.message);
    try {
      let actor = this.resolveActor(actorId);
      if (actor === undefined && decoded !== undefined && this.routedActorProvider !== undefined) {
        const remoteBoundSessionTarget = this.nativeJoinBoundSessionTargetResolver?.(request.info);
        actor = (await this.routedActorProvider(
          actorId,
          decoded.actorType,
          request.info.targetActor,
          remoteBoundSessionTarget,
          decoded.actorCreateRequest
        )).actor;
      }
      if (actor !== undefined) {
        const target = this.getTarget();
        const joinRequest = decoded?.request ?? request.message;
        const joinPayload = wrapFrameworkPayloadMessage(joinRequest, this.messageSerializers);
        const response: ZLinkSpotActorJoinResponse = await this.serial.execute(async () =>
          target.onActorJoin === undefined
            ? { accepted: this.defaultAccept }
            : target.onActorJoin(actor, joinPayload)
        );
        accepted = response.accepted;
        reply = response.reply === undefined
          ? undefined
          : encodeFrameworkPayloadMessage(response.reply, this.messageSerializers);
        if (accepted) {
          this.commitRoutedActor?.(actor);
          joinedActor = actor;
        }
      }
    } catch {
      accepted = false;
      reply = undefined;
    }
    if (joinedActor !== undefined) {
      try {
        await this.serial.execute(() => this.getTarget().onJoinedActor?.(joinedActor));
      } catch (error) {
        this.dispatchErrors?.report({
          surface: ZLinkDispatchErrorSurface.SpotActor,
          messageKind: ZLinkDispatchMessageKind.ActorSend,
          reason: ZLinkDispatchErrorReason.HandlerException,
          action: ZLinkDispatchErrorAction.Drop,
          actorId,
          error
        });
      }
    }
    const operation = this.nativeSpot.replyActorJoin(request, accepted ? 0 : 1);
    if (reply !== undefined) {
      operation.message(reply);
    }
    operation.submit();
    decoded?.request.close();
    decoded?.actorCreateRequest?.close();
  }

  private decodeNativeActorJoinRequest(actorId: string, message: Message): {
    readonly actorType: string;
    readonly actorRef?: ZLinkBackendActorRef;
    readonly actorCreateRequest?: Message;
    readonly request: Message;
  } | undefined {
    try {
      const payload = JSON.parse(message.data().toString()) as {
        readonly packetName?: unknown;
        readonly actorType?: unknown;
        readonly actorNodeRid?: unknown;
        readonly actorNodeRidHex?: unknown;
        readonly actorGeneration?: unknown;
        readonly actorCreateRequest?: unknown;
        readonly request?: unknown;
      };
      if (
        payload.packetName !== REMOTE_ACTOR_JOIN_PACKET ||
        typeof payload.actorType !== 'string' ||
        typeof payload.request !== 'string'
      ) {
        return undefined;
      }
      return {
        actorType: payload.actorType,
        actorRef: decodeRemoteActorRef(payload.actorNodeRid, payload.actorNodeRidHex, actorId, payload.actorGeneration),
        actorCreateRequest: typeof payload.actorCreateRequest === 'string'
          ? BindingMessage.from(Buffer.from(payload.actorCreateRequest, 'base64'))
          : undefined,
        request: BindingMessage.from(Buffer.from(payload.request, 'base64'))
      };
    } catch {
      return undefined;
    }
  }

  private async drainRoutes(
    received: BindingReceived | undefined = undefined,
    retryDeadlineMs = Date.now()
  ): Promise<void> {
    if (this.routeDraining) {
      if (received !== undefined) {
        try {
          await this.admitRouted(received);
        } finally {
          received.close();
        }
      }
      return;
    }
    this.routeDraining = true;
    try {
      if (received !== undefined) {
        try {
          await this.admitRouted(received);
        } finally {
          received.close();
        }
        received = undefined;
      }
      for (;;) {
        received ??= new BindingReceived();
        try {
          if (!this.nativeSpot.recvRoute(received, ZLINK_RECV_DONT_WAIT)) {
            received.close();
            return;
          }
        } catch (error) {
          if (isRouteRecvRetryable(error)) {
            closeReceivedQuietly(received);
            if (Date.now() < retryDeadlineMs) {
              setTimeout(() => void this.drainRoutes(undefined, retryDeadlineMs), 10);
            }
            return;
          }
          received.close();
          throw error;
        }
        try {
          await this.admitRouted(received);
        } finally {
          received.close();
        }
        received = new BindingReceived();
      }
    } finally {
      this.routeDraining = false;
    }
  }

  private async admitRouted(received: BindingReceived): Promise<void> {
    if (received.parts.length < 1) {
      return;
    }
    const boundSessionSend = this.decodeRemoteBoundSessionSend(received.parts);
    if (boundSessionSend !== undefined) {
      await this.routedBoundSessionReceiver?.(
        boundSessionSend.actorId,
        boundSessionSend.message,
        boundSessionSend.packetName,
        boundSessionSend.metadata
      );
      if (isReplyableRequestSeq(received.requestSeq)) {
        if (boundSessionSend.envelope === undefined) {
          received.reply()
            .message(Buffer.from(JSON.stringify({ ok: true })))
            .submit();
        } else {
          appendRouteReplyParts(
            received.reply(),
            encodeChannelReplyParts(boundSessionSend.envelope.header, { ok: true })
          ).submit();
        }
      }
      return;
    }
    const actorPacketRelay = this.decodeRemoteActorPacketRelay(received.parts);
    if (actorPacketRelay !== undefined && this.actorPacketHandler !== undefined) {
      let remoteBoundSessionTarget: ZLinkRemoteBoundSessionTarget | undefined;
      if (
        actorPacketRelay.routerChannelId !== undefined &&
        received.routingId !== null
      ) {
        remoteBoundSessionTarget = {
          routerChannelId: actorPacketRelay.routerChannelId,
          targetNodeRid: decodeWireRoutingId(
            actorPacketRelay.boundSessionTargetNodeRid ?? String(received.routingId),
            undefined
          ),
          spotRid: decodeWireRoutingId(
            actorPacketRelay.boundSessionSpotRid ?? String(received.spotRid ?? received.routingId),
            undefined
          )
        };
      }
      const header = BindingMessage.from(Buffer.from(actorPacketRelay.header, 'base64'));
      const payload = BindingMessage.from(Buffer.from(actorPacketRelay.payload, 'base64'));
      try {
        const response = await this.actorPacketHandler(
          actorPacketRelay.actorId,
          [header, payload],
          true,
          remoteBoundSessionTarget
        );
        const actorPacketTarget = encodeRemoteActorPacketTarget(
          this.actorPacketTargetProvider?.(actorPacketRelay.actorId)
        );
        if (isReplyableRequestSeq(received.requestSeq)) {
          if (actorPacketRelay.envelope === undefined) {
            received.reply()
              .message(Buffer.from(JSON.stringify({ ok: true, response, actorPacketTarget })))
              .submit();
          } else {
            appendRouteReplyParts(
              received.reply(),
              encodeChannelReplyParts(actorPacketRelay.envelope.header, { ok: true, response, actorPacketTarget })
            ).submit();
          }
        }
      } catch (error) {
        if (isReplyableRequestSeq(received.requestSeq)) {
          if (actorPacketRelay.envelope === undefined) {
            received.reply()
              .message(Buffer.from(JSON.stringify({
                ok: false,
                error: error instanceof Error ? error.message : String(error)
              })))
              .submit();
          } else {
            appendRouteReplyParts(
              received.reply(),
              encodeChannelReplyParts(actorPacketRelay.envelope.header, {
                ok: false,
                error: error instanceof Error ? error.message : String(error)
              })
            ).submit();
          }
          return;
        }
        throw error;
      } finally {
        header.close();
        payload.close();
      }
      return;
    }
    if (await this.dispatchRoutedSpotPacket(received)) {
      return;
    }
    if (received.requestSeq === null) {
      return;
    }
    const decoded = this.decodeRemoteActorJoinRequest(received.parts, received);
    if (decoded === undefined) {
      return;
    }
    if (this.routedActorProvider === undefined) {
      await this.admitResolvedRoutedActorJoin(decoded, received);
      return;
    }
    try {
      const { actor, actorRef } = await this.routedActorProvider(
        decoded.actorId,
        decoded.actorType,
        decoded.actorRef,
        decoded.remoteBoundSessionTarget,
        decoded.actorCreateRequest
      );
      const target = this.getTarget();
      const joinPayload = wrapFrameworkPayloadMessage(decoded.request, this.messageSerializers);
      const response: ZLinkSpotActorJoinResponse = await this.serial.execute(async () =>
        target.onActorJoin === undefined
          ? { accepted: this.defaultAccept }
          : target.onActorJoin(actor, joinPayload)
      );
      const reply = response.reply === undefined
        ? undefined
        : encodeFrameworkPayloadMessage(response.reply, this.messageSerializers);
      if (response.accepted) {
        await this.commitRoutedActor?.(actor);
        await this.serial.execute(() => target.onJoinedActor?.(actor));
      }
      const replyPayload = {
        accepted: response.accepted,
        actorNodeRid: String(actorRef.nodeRid),
        actorNodeRidHex: encodeRoutingIdHex(actorRef.nodeRid),
        actorId: actorRef.actorId,
        actorGeneration: actorRef.generation.toString(),
        reply: reply?.data().toString('base64')
      };
      try {
        if (decoded.raw) {
          const operation = received.reply().message(Buffer.from(JSON.stringify(replyPayload)));
          operation.submit();
        } else {
          const envelope = decoded.envelope;
          if (envelope === undefined) {
            throw new ZLinkConfigurationException('Routed actor join channel envelope is missing.');
          }
          appendRouteReplyParts(received.reply(), encodeChannelReplyParts(envelope.header, replyPayload)).submit();
        }
      } finally {
        decoded.request.close();
        decoded.actorCreateRequest?.close();
      }
    } catch (error) {
      try {
        if (decoded.raw) {
          received.reply()
            .message(Buffer.from(JSON.stringify({
              accepted: false,
              actorNodeRid: '',
              actorId: decoded.actorId,
              actorGeneration: '0'
            })))
            .submit();
        } else {
          const envelope = decoded.envelope;
          if (envelope === undefined) {
            throw new ZLinkConfigurationException('Routed actor join channel envelope is missing.');
          }
          appendRouteReplyParts(
            received.reply(),
            encodeChannelErrorReplyParts(envelope.header, error instanceof Error ? error.message : String(error))
          ).submit();
        }
      } finally {
        decoded.request.close();
        decoded.actorCreateRequest?.close();
      }
    }
  }

  private async admitResolvedRoutedActorJoin(
    decoded: {
      readonly envelope?: ReturnType<typeof decodeChannelEnvelope>;
      readonly raw: boolean;
      readonly actorId: string;
      readonly actorType: string;
      readonly actorRef?: ZLinkBackendActorRef;
      readonly actorCreateRequest?: Message;
      readonly request: Message;
    },
    received: BindingReceived
  ): Promise<void> {
    const actor = this.resolveActor(decoded.actorId);
    let response: ZLinkSpotActorJoinResponse = { accepted: false };
    if (actor !== undefined) {
      const target = this.getTarget();
      const joinPayload = wrapFrameworkPayloadMessage(decoded.request, this.messageSerializers);
      response = await this.serial.execute(async () =>
        target.onActorJoin === undefined
          ? { accepted: this.defaultAccept }
          : target.onActorJoin(actor, joinPayload)
      );
    }
    const reply = response.reply === undefined
      ? undefined
      : encodeFrameworkPayloadMessage(response.reply, this.messageSerializers);
    const actorRef = decoded.actorRef;
    const replyPayload = {
      accepted: response.accepted,
      actorNodeRid: String(actorRef?.nodeRid ?? ''),
      actorNodeRidHex: actorRef?.nodeRid === undefined ? undefined : encodeRoutingIdHex(actorRef.nodeRid),
      actorId: decoded.actorId,
      actorGeneration: (actorRef?.generation ?? 0n).toString(),
      reply: reply?.data().toString('base64')
    };
    try {
      if (decoded.raw) {
        received.reply()
          .message(Buffer.from(JSON.stringify(replyPayload)))
          .submit();
      } else {
        const envelope = decoded.envelope;
        if (envelope === undefined) {
          throw new ZLinkConfigurationException('Routed actor join channel envelope is missing.');
        }
        appendRouteReplyParts(received.reply(), encodeChannelReplyParts(envelope.header, replyPayload)).submit();
      }
    } finally {
      decoded.request.close();
    }
    if (response.accepted && actor !== undefined) {
      await this.serial.execute(() => this.getTarget().onJoinedActor?.(actor));
    }
  }

  private decodeRemoteBoundSessionSend(parts: readonly BindingMessage[]): {
    readonly actorId: string;
    readonly message: unknown;
    readonly packetName?: string;
    readonly metadata: ReadonlyMap<string, string>;
    readonly envelope?: ReturnType<typeof decodeChannelEnvelope>;
  } | undefined {
    try {
      if (parts.length === 1) {
        const payload = JSON.parse(parts[0].data().toString()) as {
          readonly packetName?: unknown;
          readonly boundPacketName?: unknown;
          readonly actorId?: unknown;
          readonly message?: unknown;
          readonly metadata?: unknown;
        };
        if (
          payload.packetName !== ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET ||
          typeof payload.actorId !== 'string'
        ) {
          return undefined;
        }
        return {
          actorId: payload.actorId,
          message: payload.message,
          packetName: typeof payload.boundPacketName === 'string' ? payload.boundPacketName : undefined,
          metadata: new Map(Object.entries(
            typeof payload.metadata === 'object' && payload.metadata !== null
              ? payload.metadata as Record<string, string>
              : {}
          ))
        };
      }
      const envelope = decodeChannelEnvelope(parts);
      if (envelope.packetName !== ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET) {
        return undefined;
      }
      const payload = decodeChannelPayload(envelope, this.channelCodecs()) as {
        readonly actorId?: unknown;
        readonly message?: unknown;
        readonly packetName?: unknown;
        readonly boundPacketName?: unknown;
        readonly metadata?: unknown;
      };
      if (typeof payload.actorId !== 'string') {
        return undefined;
      }
      return {
        actorId: payload.actorId,
        message: payload.message,
        packetName: typeof payload.boundPacketName === 'string' ? payload.boundPacketName : undefined,
        metadata: new Map(Object.entries(
          typeof payload.metadata === 'object' && payload.metadata !== null
            ? payload.metadata as Record<string, string>
            : {}
        )),
        envelope
      };
    } catch {
      return undefined;
    }
  }

  private decodeRemoteActorPacketRelay(parts: readonly BindingMessage[]): {
    readonly actorId: string;
    readonly routerChannelId?: string;
    readonly boundSessionTargetNodeRid?: string;
    readonly boundSessionSpotRid?: string;
    readonly header: string;
    readonly payload: string;
    readonly envelope?: ReturnType<typeof decodeChannelEnvelope>;
  } | undefined {
    try {
      if (parts.length >= 2 && parts[0].data().length > 0) {
        const envelope = decodeChannelEnvelope(parts);
        if (envelope.packetName !== ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET) {
          return undefined;
        }
        const payload = decodeChannelPayload(envelope, this.channelCodecs()) as {
          readonly packetName?: unknown;
          readonly actorId?: unknown;
          readonly routerChannelId?: unknown;
          readonly boundSessionTargetNodeRid?: unknown;
          readonly boundSessionSpotRid?: unknown;
          readonly header?: unknown;
          readonly payload?: unknown;
        };
        if (
          payload.packetName !== ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET ||
          typeof payload.actorId !== 'string' ||
          typeof payload.header !== 'string' ||
          typeof payload.payload !== 'string'
        ) {
          return undefined;
        }
        return {
          actorId: payload.actorId,
          routerChannelId: typeof payload.routerChannelId === 'string' ? payload.routerChannelId : undefined,
          boundSessionTargetNodeRid: typeof payload.boundSessionTargetNodeRid === 'string' ? payload.boundSessionTargetNodeRid : undefined,
          boundSessionSpotRid: typeof payload.boundSessionSpotRid === 'string' ? payload.boundSessionSpotRid : undefined,
          header: payload.header,
          payload: payload.payload,
          envelope
        };
      }
      if (parts.length !== 1) {
        return undefined;
      }
      const payload = JSON.parse(parts[0].data().toString()) as {
        readonly packetName?: unknown;
        readonly actorId?: unknown;
        readonly routerChannelId?: unknown;
        readonly boundSessionTargetNodeRid?: unknown;
        readonly boundSessionSpotRid?: unknown;
        readonly header?: unknown;
        readonly payload?: unknown;
      };
      if (
        payload.packetName !== ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET ||
        typeof payload.actorId !== 'string' ||
        typeof payload.header !== 'string' ||
        typeof payload.payload !== 'string'
      ) {
        return undefined;
      }
      return {
        actorId: payload.actorId,
        routerChannelId: typeof payload.routerChannelId === 'string' ? payload.routerChannelId : undefined,
        boundSessionTargetNodeRid: typeof payload.boundSessionTargetNodeRid === 'string' ? payload.boundSessionTargetNodeRid : undefined,
        boundSessionSpotRid: typeof payload.boundSessionSpotRid === 'string' ? payload.boundSessionSpotRid : undefined,
        header: payload.header,
        payload: payload.payload
      };
    } catch {
      return undefined;
    }
  }

  private decodeRemoteActorJoinRequest(parts: readonly BindingMessage[], received: BindingReceived): {
    readonly envelope?: ReturnType<typeof decodeChannelEnvelope>;
    readonly raw: boolean;
    readonly actorId: string;
      readonly actorType: string;
      readonly actorRef?: ZLinkBackendActorRef;
      readonly remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget;
      readonly actorCreateRequest?: Message;
      readonly request: Message;
  } | undefined {
    if (parts.length < 2 || parts[0].data().length === 0) {
      if (parts.length !== 1 || parts[0].data().length === 0) {
        return undefined;
      }
      try {
        const payload = JSON.parse(parts[0].data().toString()) as {
          readonly packetName?: unknown;
          readonly actorId?: unknown;
          readonly actorType?: unknown;
          readonly actorNodeRid?: unknown;
          readonly actorNodeRidHex?: unknown;
          readonly actorGeneration?: unknown;
          readonly actorCreateRequest?: unknown;
          readonly sourceSpotRid?: unknown;
          readonly sourceSpotRidHex?: unknown;
          readonly routerChannelId?: unknown;
          readonly boundSessionRouterChannelId?: unknown;
          readonly boundSessionTargetNodeRid?: unknown;
          readonly boundSessionTargetNodeRidHex?: unknown;
          readonly boundSessionSpotRid?: unknown;
          readonly boundSessionSpotRidHex?: unknown;
          readonly request?: unknown;
        };
        if (
          payload.packetName !== REMOTE_ACTOR_JOIN_PACKET ||
          typeof payload.actorId !== 'string' ||
          typeof payload.actorType !== 'string' ||
          typeof payload.request !== 'string'
        ) {
          return undefined;
        }
        return {
          raw: true,
          actorId: payload.actorId,
          actorType: payload.actorType,
          actorRef: decodeRemoteActorRef(payload.actorNodeRid, payload.actorNodeRidHex, payload.actorId, payload.actorGeneration),
          actorCreateRequest: typeof payload.actorCreateRequest === 'string'
            ? BindingMessage.from(Buffer.from(payload.actorCreateRequest, 'base64'))
            : undefined,
          remoteBoundSessionTarget: decodeRemoteBoundSessionTarget(
            payload.boundSessionRouterChannelId ?? payload.routerChannelId,
            payload.boundSessionTargetNodeRid ?? (
              received.routingId === null
                ? payload.actorNodeRid
                : String(received.routingId)
            ),
            payload.boundSessionTargetNodeRidHex,
            payload.boundSessionSpotRid ?? payload.sourceSpotRid ?? received.spotRid ?? undefined,
            payload.boundSessionSpotRidHex ?? payload.sourceSpotRidHex
          ),
          request: BindingMessage.from(Buffer.from(payload.request, 'base64'))
        };
      } catch {
        return undefined;
      }
    }
    try {
      const envelope = decodeChannelEnvelope(parts);
      if (envelope.packetName !== REMOTE_ACTOR_JOIN_PACKET) {
        return undefined;
      }
      const payload = decodeChannelPayload(envelope, this.channelCodecs());
      if (!isRemoteActorJoinPayload(payload)) {
        return undefined;
      }
      if (
        typeof payload.actorId !== 'string' ||
        typeof payload.actorType !== 'string' ||
        typeof payload.request !== 'string'
      ) {
        return undefined;
      }
      return {
        envelope,
        raw: false,
        actorId: payload.actorId,
        actorType: payload.actorType,
        actorRef: decodeRemoteActorRef(
          (payload as { actorNodeRid?: unknown }).actorNodeRid,
          (payload as { actorNodeRidHex?: unknown }).actorNodeRidHex,
          payload.actorId,
          (payload as { actorGeneration?: unknown }).actorGeneration
        ),
        actorCreateRequest: typeof (payload as unknown as { actorCreateRequest?: unknown }).actorCreateRequest === 'string'
          ? BindingMessage.from(Buffer.from((payload as unknown as { actorCreateRequest: string }).actorCreateRequest, 'base64'))
          : undefined,
        remoteBoundSessionTarget: decodeRemoteBoundSessionTarget(
          (payload as { boundSessionRouterChannelId?: unknown }).boundSessionRouterChannelId
            ?? (payload as { routerChannelId?: unknown }).routerChannelId,
          (payload as { boundSessionTargetNodeRid?: unknown }).boundSessionTargetNodeRid
            ?? (received.routingId === null
              ? (payload as { actorNodeRid?: unknown }).actorNodeRid
              : String(received.routingId)),
          (payload as { boundSessionTargetNodeRidHex?: unknown }).boundSessionTargetNodeRidHex,
          (payload as { boundSessionSpotRid?: unknown }).boundSessionSpotRid
            ?? (payload as { sourceSpotRid?: unknown }).sourceSpotRid
            ?? received.spotRid
            ?? undefined,
          (payload as { boundSessionSpotRidHex?: unknown }).boundSessionSpotRidHex
            ?? (payload as { sourceSpotRidHex?: unknown }).sourceSpotRidHex
        ),
        request: BindingMessage.from(Buffer.from(payload.request, 'base64'))
      };
    } catch {
      try {
        const header = JSON.parse(parts[0].data().toString()) as {
          readonly packetName?: unknown;
          readonly actorId?: unknown;
          readonly actorType?: unknown;
          readonly actorCreateRequest?: unknown;
        };
        if (
          header.packetName !== REMOTE_ACTOR_JOIN_PACKET ||
          typeof header.actorId !== 'string' ||
          typeof header.actorType !== 'string' ||
          parts.length < 2
        ) {
          return undefined;
        }
        return {
          raw: true,
          actorId: header.actorId,
          actorType: header.actorType,
          actorRef: decodeRemoteActorRef(
            (header as { actorNodeRid?: unknown }).actorNodeRid,
            (header as { actorNodeRidHex?: unknown }).actorNodeRidHex,
            header.actorId,
            (header as { actorGeneration?: unknown }).actorGeneration
          ),
          actorCreateRequest: typeof header.actorCreateRequest === 'string'
            ? BindingMessage.from(Buffer.from(header.actorCreateRequest, 'base64'))
            : undefined,
          remoteBoundSessionTarget: decodeRemoteBoundSessionTarget(
            (header as { boundSessionRouterChannelId?: unknown }).boundSessionRouterChannelId
              ?? (header as { routerChannelId?: unknown }).routerChannelId,
            (header as { boundSessionTargetNodeRid?: unknown }).boundSessionTargetNodeRid
              ?? (received.routingId === null
                ? (header as { actorNodeRid?: unknown }).actorNodeRid
                : String(received.routingId)),
            (header as { boundSessionTargetNodeRidHex?: unknown }).boundSessionTargetNodeRidHex,
            (header as { boundSessionSpotRid?: unknown }).boundSessionSpotRid
              ?? (header as { sourceSpotRid?: unknown }).sourceSpotRid
              ?? received.spotRid
              ?? undefined,
            (header as { boundSessionSpotRidHex?: unknown }).boundSessionSpotRidHex
              ?? (header as { sourceSpotRidHex?: unknown }).sourceSpotRidHex
          ),
          request: BindingMessage.from(Buffer.from(parts[1].data()))
        };
      } catch {
        return undefined;
      }
    }
  }

  private async dispatchRoutedSpotPacket(received: BindingReceived): Promise<boolean> {
    let envelope: ReturnType<typeof decodeChannelEnvelope>;
    try {
      envelope = decodeChannelEnvelope(received.parts);
    } catch {
      return false;
    }
    if (
      envelope.header.kind !== ZLinkChannelMessageKind.Request &&
      envelope.header.kind !== ZLinkChannelMessageKind.Command
    ) {
      return false;
    }
    if (
      envelope.packetName === REMOTE_ACTOR_JOIN_PACKET ||
      envelope.packetName === ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET ||
      envelope.packetName === ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET
    ) {
      return false;
    }
    const registrations = this.packetHandlers.get(envelope.packetName ?? '');
    const replyable = isReplyableRequestSeq(received.requestSeq);
    const action = replyable
      ? ZLinkDispatchErrorAction.ReplyError
      : ZLinkDispatchErrorAction.Drop;
    if (registrations === undefined || registrations.length === 0) {
      this.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotRoute,
        messageKind: replyable ? ZLinkDispatchMessageKind.Request : ZLinkDispatchMessageKind.Send,
        reason: ZLinkDispatchErrorReason.HandlerMissing,
        action,
        packetName: envelope.packetName,
        channelName: envelope.header.channelName,
        spotRid: String(this.nativeSpot.routingId),
        sourceRid: received.routingId === null ? undefined : String(received.routingId),
        correlationId: envelope.header.correlationId ?? received.requestSeq?.toString()
      });
      if (replyable) {
        appendRouteReplyParts(
          received.reply(),
          encodeChannelErrorReplyParts(envelope.header, `SPOT route handler not found: ${envelope.packetName}`)
        ).submit();
      }
      return true;
    }
    let payload: unknown;
    try {
      payload = decodeChannelPayload(envelope, this.channelCodecs());
    } catch (error) {
      this.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotRoute,
        messageKind: replyable ? ZLinkDispatchMessageKind.Request : ZLinkDispatchMessageKind.Send,
        reason: ZLinkDispatchErrorReason.PayloadDecodeFailed,
        action,
        packetName: envelope.packetName,
        channelName: envelope.header.channelName,
        spotRid: String(this.nativeSpot.routingId),
        sourceRid: received.routingId === null ? undefined : String(received.routingId),
        correlationId: envelope.header.correlationId ?? received.requestSeq?.toString(),
        error
      });
      if (replyable) {
        appendRouteReplyParts(
          received.reply(),
          encodeChannelErrorReplyParts(envelope.header, error instanceof Error ? error.message : String(error))
        ).submit();
      }
      return true;
    }
    const context = {
      channelName: envelope.header.channelName,
      contentType: envelope.header.contentType,
      packetName: envelope.packetName
    };
    try {
      let response: unknown;
      await this.serial.execute(async () => {
        const spot = this.getTarget() as ZLinkSpot;
        for (const registration of registrations) {
          const handler = await createProviderInstance(
            registration.handlerType as Type<ZLinkSpotPacketHandler<ZLinkSpot, unknown> | ZLinkSpotRequestHandler<ZLinkSpot, unknown, unknown>>,
            this.providerResolver
          );
          response = await handler.handle(spot, payload, context);
        }
      });
      if (replyable) {
        appendRouteReplyParts(
          received.reply(),
          encodeChannelReplyParts(envelope.header, response)
        ).submit();
      }
      return true;
    } catch (error) {
      this.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotRoute,
        messageKind: replyable ? ZLinkDispatchMessageKind.Request : ZLinkDispatchMessageKind.Send,
        reason: ZLinkDispatchErrorReason.HandlerException,
        action,
        packetName: envelope.packetName,
        channelName: envelope.header.channelName,
        spotRid: String(this.nativeSpot.routingId),
        sourceRid: received.routingId === null ? undefined : String(received.routingId),
        correlationId: envelope.header.correlationId ?? received.requestSeq?.toString(),
        error
      });
      if (replyable) {
        appendRouteReplyParts(
          received.reply(),
          encodeChannelErrorReplyParts(envelope.header, error instanceof Error ? error.message : String(error))
        ).submit();
      }
      return true;
    }
  }
}

interface ZLinkRouteReplySubmitOperation {
  message(message: MessageLike): ZLinkRouteReplySubmitOperation;
  submit(): void;
}

function appendRouteReplyParts(
  operation: { message(message: MessageLike): ZLinkRouteReplySubmitOperation },
  parts: readonly MessageLike[]
): ZLinkRouteReplySubmitOperation {
  if (parts.length === 0) {
    throw new ZLinkConfigurationException('Spot route reply must contain at least one part.');
  }
  let current = operation.message(parts[0]);
  for (let index = 1; index < parts.length; index++) {
    current = current.message(parts[index]);
  }
  return current;
}

function isReplyableRequestSeq(requestSeq: bigint | null): requestSeq is bigint {
  return requestSeq !== null && requestSeq !== 0n;
}

function isRemoteActorJoinPayload(value: unknown): value is {
  readonly actorId: string;
  readonly actorType: string;
  readonly request: string;
} {
  return (
    typeof value === 'object' &&
    value !== null &&
    typeof (value as { actorId?: unknown }).actorId === 'string' &&
    typeof (value as { actorType?: unknown }).actorType === 'string' &&
    typeof (value as { request?: unknown }).request === 'string'
  );
}

function decodeRemoteActorRef(
  nodeRid: unknown,
  nodeRidHex: unknown,
  actorId: string,
  generation: unknown
): ZLinkBackendActorRef | undefined {
  if (typeof nodeRid !== 'string') {
    return undefined;
  }
  return {
    nodeRid: decodeWireRoutingId(nodeRid, nodeRidHex),
    actorId,
    generation: typeof generation === 'string' ? BigInt(generation) : 0n
  } as ZLinkBackendActorRef;
}

function decodeRemoteBoundSessionTarget(
  routerChannelId: unknown,
  targetNodeRid: unknown,
  targetNodeRidHex: unknown,
  spotRid: unknown,
  spotRidHex: unknown
): ZLinkRemoteBoundSessionTarget | undefined {
  if (
    typeof routerChannelId !== 'string' ||
    typeof targetNodeRid !== 'string' ||
    spotRid === undefined ||
    spotRid === null
  ) {
    return undefined;
  }
  return {
    routerChannelId,
    targetNodeRid: decodeWireRoutingId(targetNodeRid, targetNodeRidHex),
    spotRid: decodeWireRoutingId(String(spotRid), spotRidHex)
  };
}

function encodeRoutingIdHex(routingId: RoutingId): string | undefined {
  const toHex = (routingId as unknown as { toHex?: () => string }).toHex;
  return typeof toHex === 'function' ? toHex.call(routingId) : undefined;
}

function encodeRemoteActorPacketTarget(target: ZLinkRemoteActorPacketTarget | undefined): {
  readonly routerChannelId: string;
  readonly targetNodeRid: string;
  readonly targetNodeRidHex?: string;
  readonly spotRid: string;
  readonly spotRidHex?: string;
  readonly spotKind: ZLinkSpotKind;
} | undefined {
  if (target === undefined) {
    return undefined;
  }
  return {
    routerChannelId: target.routerChannelId,
    targetNodeRid: String(target.targetNodeRid),
    targetNodeRidHex: encodeRoutingIdHex(target.targetNodeRid),
    spotRid: String(target.spotRid),
    spotRidHex: encodeRoutingIdHex(target.spotRid),
    spotKind: target.spotKind ?? ZLinkSpotKind.User
  };
}

function decodeWireRoutingId(text: string, hex: unknown): RoutingId {
  return typeof hex === 'string'
    ? BindingRoutingId.fromHex(hex) as unknown as RoutingId
    : BindingRoutingId.from(text) as unknown as RoutingId;
}

function isRouteRecvRetryable(error: unknown): boolean {
  return typeof error === 'object' &&
    error !== null &&
    ([201, 202, 204].includes(Number((error as { result?: unknown }).result)) ||
      /Device or resource busy|Resource temporarily unavailable|Bad address/i.test(String((error as { message?: unknown }).message ?? '')));
}

function closeReceivedQuietly(received: BindingReceived): void {
  try {
    received.close();
  } catch {
  }
}

export class ZLinkEntrySpotActivation {
  private readonly serial = new ZLinkSpotSerialExecutor();
  private readonly actorPacketMailboxes = new ZLinkActorDispatchMailboxSet();
  private readonly timers = new ZLinkSpotTimerRegistry();
  private readonly actorHandlers = new ZLinkSpotActorHandlerRegistryRuntime();
  private readonly handlers = new DefaultZLinkSpotHandlerRegistry(this.actorHandlers);
  private readonly outbound: DefaultZLinkSpotOutbound;
  private readonly workerRuntime: ZLinkSpotWorkerRuntime;
  private initialized = false;

  entrySpot: ZLinkEntrySpot;
  readonly context: ZLinkEntrySpotContext;

  constructor(private readonly options: ZLinkEntrySpotActivationOptions) {
    // Entry Spot lifecycle, timers and detached continuations use this serial
    // executor. Actor packets use the target actor mailbox.
    this.outbound = new DefaultZLinkSpotOutbound(
      this.serial,
      options.channelClient,
      options.fanoutClient,
      options.spotPublisherClient,
      options.remoteAddressResolver,
      options.routedTransport
    );
    this.workerRuntime = options.workerRuntime ?? new ZLinkSpotWorkerRuntime();
    this.context = this.createContext();
    this.entrySpot = undefined as unknown as ZLinkEntrySpot;
    for (const handler of options.actorSendHandlers ?? []) {
      if (handler.entrySpotType === options.entrySpotType) {
        this.handlers.addActorPacketRegistration(
          ZLinkActorPacketKind.Send,
          handler.handlerType,
          handler.actorType,
          handler.packetName
        );
      }
    }
    for (const handler of options.actorRequestHandlers ?? []) {
      if (handler.entrySpotType === options.entrySpotType) {
        this.handlers.addActorPacketRegistration(
          ZLinkActorPacketKind.Request,
          handler.handlerType,
          handler.actorType,
          handler.packetName
        );
      }
    }
  }

  /**
   * The Entry Spot serial dispatch line for Entry Spot-owned callbacks such as
   * lifecycle, timers, request continuations and worker completions. Actor
   * packets use the target actor mailbox instead.
   */
  get serialExecutor(): ZLinkSpotSerialExecutor {
    return this.serial;
  }

  get nodeRid(): RoutingId {
    return this.options.nodeRid;
  }

  async create(): Promise<void> {
    const entrySpot = await createProviderInstance(this.options.entrySpotType, this.options.providerResolver, this.context);
    this.entrySpot = entrySpot;
    Object.defineProperty(this.entrySpot, 'context', {
      configurable: true,
      enumerable: false,
      value: this.context
    });
    for (const handler of this.options.timerHandlers ?? []) {
      if (handler.entrySpotType === this.options.entrySpotType) {
        await this.timers.add(
          handler.name,
          handler.periodMs,
          handler.options,
          handler.handlerType as Type<ZLinkSpotTimerHandler<ZLinkEntrySpot>>,
          this.serial,
          this.entrySpot,
          this.options.providerResolver,
          undefined,
          createTimerDiagnostics(
            this.options.spotNodeName,
            this.options.nodeRid,
            true,
            handler.name,
            handler.handlerType,
            this.options.runtimeEventPublisher
          )
        );
      }
    }
  }

  async configure(): Promise<void> {
    await this.entrySpot.configure?.();
  }

  async initialize(): Promise<void> {
    await this.serial.execute(() => this.entrySpot.onInitialize?.());
    this.attachActorJoinDispatch();
    this.initialized = true;
  }

  async dispose(): Promise<void> {
    if (this.initialized) {
      await this.serial.execute(() => this.entrySpot.onClosing?.());
    }
    await this.timers.dispose();
    if (typeof this.options.nativeSpot.dispose === 'function') {
      await this.options.nativeSpot.dispose();
    }
  }

  notifyCreateActor(actor: ZLinkActor, createRequest: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    return this.serial.execute(() => this.entrySpot.onCreateActor?.(actor, createRequest, signal));
  }

  notifyJoinActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    return this.serial.execute(() => this.entrySpot.onJoinedActor?.(actor, signal));
  }

  /**
   * Entry Spot join admission uses the shared core round-trip. Re-entry defaults
   * to accept when the Entry Spot does not declare `onActorJoin` (actor returning
   * home), so `defaultAccept` is true.
   */
  private attachActorJoinDispatch(): void {
    const dispatch = new ZLinkSpotActorJoinDispatch(
      this.options.nativeSpot,
      this.serial,
      (actorId) => this.options.actorResolver?.(actorId),
      () => this.entrySpot,
      true,
      undefined,
      undefined,
      this.options.entryActorCommitter,
      (actorId, parts, returnResponse, remoteBoundSessionTarget) =>
        this.dispatchActorPacket(actorId, parts, returnResponse, remoteBoundSessionTarget),
      this.options.routedBoundSessionReceiver,
      this.options.actorPacketTargetProvider,
      (actor, sourceNodeRid, sourceSessionRid) =>
        String(sourceNodeRid) === String(this.options.nativeNode.routingId)
          ? undefined
          : this.options.nativeNode.bindRemoteActorSession(actor, sourceNodeRid, sourceSessionRid),
      this.options.messageSerializers,
      this.options.providerResolver,
      this.options.dispatchErrors
    );
    dispatch.configureSubscriptions(this.handlers.snapshot());
    dispatch.attach();
  }

  notifyLeaveActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    return this.serial.execute(() => this.entrySpot.onLeaveActor?.(actor, signal));
  }

  notifyDisconnectActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    return this.serial.execute(() => this.entrySpot.onDisconnectActor?.(actor, signal));
  }

  async dispatchActorPacket(
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    return this.actorPacketMailboxes.submit(actorId, () =>
      this.dispatchActorPacketInsideMailbox(
        actorId,
        parts,
        returnResponse,
        remoteBoundSessionTarget,
        fallbackActorRef
      ));
  }

  private async dispatchActorPacketInsideMailbox(
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    if (parts.length < 2) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotActor,
        messageKind: ZLinkDispatchMessageKind.ActorSend,
        reason: ZLinkDispatchErrorReason.InvalidFrame,
        action: ZLinkDispatchErrorAction.Drop,
        spotRid: String(this.options.nativeSpot.routingId),
        actorId
      });
      return undefined;
    }
    let header: ReturnType<typeof decodeStreamHeader>;
    try {
      header = decodeStreamHeader(messageToBytes(parts[0]));
    } catch (error) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotActor,
        messageKind: ZLinkDispatchMessageKind.ActorSend,
        reason: ZLinkDispatchErrorReason.InvalidFrame,
        action: ZLinkDispatchErrorAction.Drop,
        spotRid: String(this.options.nativeSpot.routingId),
        actorId,
        error
      });
      throw error;
    }
    const messageKind = header.kind === ZLinkStreamMessageKind.Request
      ? ZLinkDispatchMessageKind.ActorRequest
      : ZLinkDispatchMessageKind.ActorSend;
    const action = messageKind === ZLinkDispatchMessageKind.ActorRequest
      ? ZLinkDispatchErrorAction.ReplyError
      : ZLinkDispatchErrorAction.Drop;
    flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Received)?.trace({
      outcome: ZLinkMessageFlowOutcome.Received,
      surface: ZLinkDispatchErrorSurface.SpotActor,
      messageKind,
      packetName: header.name,
      spotRid: String(this.options.nativeSpot.routingId),
      actorId,
      correlationId: header.correlationId ?? header.requestSeq?.toString()
    });
    if (remoteBoundSessionTarget !== undefined) {
      this.options.remoteActorPacketTargetReceiver?.(actorId, remoteBoundSessionTarget);
    }
    const routed = await this.options.localActorPacketRouter?.(
      actorId,
      parts,
      returnResponse,
      remoteBoundSessionTarget
    );
    if (routed?.handled === true) {
      return routed.response;
    }
    const actor = this.options.actorResolver?.(actorId);
    if (actor === undefined) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotActor,
        messageKind,
        reason: ZLinkDispatchErrorReason.HandlerMissing,
        action,
        packetName: header.name,
        spotRid: String(this.options.nativeSpot.routingId),
        actorId,
        correlationId: header.correlationId ?? header.requestSeq?.toString()
      });
      if (messageKind === ZLinkDispatchMessageKind.ActorRequest) {
        if (header.requestSeq !== undefined && !returnResponse && this.options.actorErrorSender !== undefined) {
          await this.options.actorErrorSender(
            actorId,
            header.name,
            header.requestSeq,
            new ZLinkFrameworkException(
              ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound,
              `SPOT actor is not registered locally: ${actorId}`
            ),
            header.metadata,
            fallbackActorRef
          );
          return undefined;
        }
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound,
          `SPOT actor is not registered locally: ${actorId}`
        );
      }
      return undefined;
    }
    if (header.name === ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET) {
      await this.notifyDisconnectActor(actor);
      return undefined;
    }
    let payload: unknown;
    try {
      payload = decodeFrameworkPayloadMessage(parts[1], this.options.messageSerializers);
    } catch (error) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotActor,
        messageKind,
        reason: ZLinkDispatchErrorReason.PayloadDecodeFailed,
        action,
        packetName: header.name,
        spotRid: String(this.options.nativeSpot.routingId),
        actorId,
        correlationId: header.correlationId ?? header.requestSeq?.toString(),
        error
      });
      throw error;
    }
    const dispatcher = new ZLinkSpotActorDispatcher({
      registry: this.actorHandlers,
      spot: this.entrySpot as unknown as ZLinkSpot,
      providerResolver: this.options.providerResolver,
      messageSerializers: this.options.messageSerializers
    });
    try {
      if (header.kind === ZLinkStreamMessageKind.Send) {
        await dispatcher.dispatchSend(actor, header.name, payload, {
          metadata: Object.fromEntries(header.metadata)
        });
        flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Dispatched)?.trace({
          outcome: ZLinkMessageFlowOutcome.Dispatched,
          surface: ZLinkDispatchErrorSurface.SpotActor,
          messageKind: ZLinkDispatchMessageKind.ActorSend,
          packetName: header.name,
          spotRid: String(this.options.nativeSpot.routingId),
          actorId,
          correlationId: header.correlationId ?? header.requestSeq?.toString()
        });
        return undefined;
      }
      if (header.kind !== ZLinkStreamMessageKind.Request || header.requestSeq === undefined) {
        this.options.dispatchErrors?.report({
          surface: ZLinkDispatchErrorSurface.SpotActor,
          messageKind: ZLinkDispatchMessageKind.ActorRequest,
          reason: ZLinkDispatchErrorReason.InvalidFrame,
          action: ZLinkDispatchErrorAction.Drop,
          packetName: header.name,
          spotRid: String(this.options.nativeSpot.routingId),
          actorId
        });
        return undefined;
      }
      const requestSeq = header.requestSeq;
      if (returnResponse || this.options.actorResponseSender === undefined) {
        const response = await dispatcher.dispatchRequest(actor, header.name, payload, {
          metadata: Object.fromEntries(header.metadata)
        });
        flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Replied)?.trace({
          outcome: ZLinkMessageFlowOutcome.Replied,
          surface: ZLinkDispatchErrorSurface.SpotActor,
          messageKind: ZLinkDispatchMessageKind.ActorRequest,
          packetName: header.name,
          spotRid: String(this.options.nativeSpot.routingId),
          actorId,
          correlationId: header.correlationId ?? requestSeq.toString()
        });
        return response;
      }
      await dispatcher.dispatchRequestThen(actor, header.name, payload, {
        metadata: Object.fromEntries(header.metadata)
      }, async (response) => {
        flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Replied)?.trace({
          outcome: ZLinkMessageFlowOutcome.Replied,
          surface: ZLinkDispatchErrorSurface.SpotActor,
          messageKind: ZLinkDispatchMessageKind.ActorRequest,
          packetName: header.name,
          spotRid: String(this.options.nativeSpot.routingId),
          actorId,
          correlationId: header.correlationId ?? requestSeq.toString()
        });
        await this.options.actorResponseSender?.(
          actor,
          header.name,
          requestSeq,
          response,
          new Map(),
          undefined
        );
      });
      return undefined;
    } catch (error) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotActor,
        messageKind,
        reason: error instanceof ZLinkFrameworkException
          && error.kind === ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound
          ? ZLinkDispatchErrorReason.HandlerMissing
          : ZLinkDispatchErrorReason.HandlerException,
        action,
        packetName: header.name,
        spotRid: String(this.options.nativeSpot.routingId),
        actorId,
        correlationId: header.correlationId ?? header.requestSeq?.toString(),
        error
      });
      if (
        messageKind === ZLinkDispatchMessageKind.ActorRequest
        && header.requestSeq !== undefined
        && !returnResponse
        && this.options.actorErrorSender !== undefined
      ) {
        await this.options.actorErrorSender(
          actorId,
          header.name,
          header.requestSeq,
          error,
          header.metadata,
          fallbackActorRef
        );
        return undefined;
      }
      throw error;
    }
  }

  private createContext(): ZLinkEntrySpotContext {
    const activation = this;
    return {
      spotRid: this.options.nativeSpot.routingId,
      nodeRid: this.options.nodeRid,
      routingId: this.options.nativeSpot.routingId,
      handlers: this.handlers,
      outbound: this.outbound,
      destroyActor(actor: ZLinkActor, signal?: AbortSignal) {
        if (activation.options.destroyActor === undefined) {
          throw new ZLinkConfigurationException('Entry Spot actor destroy runtime is not started.');
        }
        return activation.options.destroyActor(
          activation.options.nodeRid,
          actor,
          signal
        );
      },
      addTimer<THandler extends ZLinkSpotTimerHandler<ZLinkEntrySpot>>(
        name: string,
        periodMs: number,
        handlerType: Type<THandler>,
        options?: ZLinkTimerOptions,
        signal?: AbortSignal
      ) {
        return activation.timers.add(
          name,
          periodMs,
          options,
          handlerType,
          activation.serial,
          activation.entrySpot,
          activation.options.providerResolver,
          signal,
          createTimerDiagnostics(
            activation.options.spotNodeName,
            activation.options.nodeRid,
            true,
            name,
            handlerType,
            activation.options.runtimeEventPublisher
          )
        );
      },
      runWorker<T>(work: (signal: AbortSignal) => T | Promise<T>): ZLinkWorkerCall<T> {
        return new DefaultZLinkWorkerCall(activation.workerRuntime, activation.serial, work);
      }
    };
  }

}

export class ZLinkRuntimeSpotPublisherTransport implements ZLinkSpotPublisherClientTransport {
  constructor(private readonly manager: () => ZLinkSpotNodeRuntimeManager | undefined) {}

  publishSpot(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: Message,
    signal?: AbortSignal
  ): Promise<void> {
    const manager = this.manager();
    if (manager === undefined) {
      throw new ZLinkConfigurationException('SPOT publisher runtime is not started.');
    }
    return manager.publishSpot(channelName, topic, packetName, event, signal);
  }
}

interface SpotActivation {
  readonly spotRid: RoutingId;
  readonly spotType: Type<ZLinkSpot>;
  readonly spot: ZLinkSpot;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly timers: ZLinkSpotTimerRegistry;
  readonly actors: Map<string, ZLinkActor>;
  readonly actorHandlers: ZLinkSpotActorHandlerRegistryRuntime;
  readonly handlers: DefaultZLinkSpotHandlerRegistry;
  readonly actorCount: () => number;
  readonly nativeSpot?: ZLinkBackendSpot;
}

interface PendingSpotActivation {
  readonly spotType: Type<ZLinkSpot>;
  readonly ready: Promise<ZLinkSpotCreateResult>;
}

function spotActivationKey(spotRid: RoutingId): string {
  return String(spotRid);
}

export class DefaultZLinkSpotManager implements ZLinkSpotManager {
  private nextId = 1;
  private readonly factories: ReadonlySet<Type<ZLinkSpot>>;
  private readonly activations = new Map<string, SpotActivation>();
  private readonly pending = new Map<string, PendingSpotActivation>();
  private readonly workerRuntime: ZLinkSpotWorkerRuntime;

  constructor(private readonly options: ZLinkSpotManagerOptions) {
    this.factories = new Set(options.spotFactories);
    this.workerRuntime = options.workerRuntime ?? new ZLinkSpotWorkerRuntime();
  }

  async create<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    request?: unknown,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult> {
    const spotRid = this.allocateSpotRid();
    const ownedRequest = request === undefined
      ? BindingMessage.from(Buffer.alloc(0))
      : encodeFrameworkPayloadMessage(request, this.options.messageSerializers);
    try {
      return await this.createActivation(spotType, spotRid, ownedRequest, signal);
    } finally {
      if (ownsFrameworkPayloadMessage(request)) {
        ownedRequest.close();
      }
    }
  }

  async getOrCreate<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    request?: unknown,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult> {
    const key = spotActivationKey(spotRid);
    const existing = this.activations.get(key);
    if (existing !== undefined) {
      if (existing.spotType !== spotType) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.SpotTypeMismatch,
          `Spot '${spotRid}' already exists with a different spot type.`
        );
      }
      return { spotRid, state: ZLinkSpotCreateState.Existing };
    }

    const pending = this.pending.get(key);
    if (pending !== undefined) {
      if (pending.spotType !== spotType) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.SpotTypeMismatch,
          `Spot '${spotRid}' is being created with a different spot type.`
        );
      }
      const result = await pending.ready;
      return result.state === ZLinkSpotCreateState.Created
        ? { spotRid, state: ZLinkSpotCreateState.Existing }
        : { spotRid, state: result.state, reply: result.reply };
    }

    const ownedRequest = request === undefined
      ? BindingMessage.from(Buffer.alloc(0))
      : encodeFrameworkPayloadMessage(request, this.options.messageSerializers);
    const ready = Promise.resolve().then(() => this.createActivation(spotType, spotRid, ownedRequest, signal));
    this.pending.set(key, { spotType, ready });
    try {
      return await ready;
    } finally {
      this.pending.delete(key);
      if (ownsFrameworkPayloadMessage(request)) {
        ownedRequest.close();
      }
    }
  }

  async find(spotRid: RoutingId): Promise<ZLinkSpotInfo | null> {
    return this.activations.has(spotActivationKey(spotRid)) ? { spotRid } : null;
  }

  async list(): Promise<readonly ZLinkSpotInfo[]> {
    return [...this.activations.values()]
      .map((activation) => String(activation.spotRid))
      .sort((left, right) => left.localeCompare(right))
      .map((spotRid) => ({ spotRid }));
  }

  async close(spotRid: RoutingId, signal?: AbortSignal): Promise<boolean> {
    const key = spotActivationKey(spotRid);
    const activation = this.activations.get(key);
    if (activation === undefined) {
      return false;
    }
    if (activation.actorCount() > 0) {
      return false;
    }
    this.activations.delete(key);
    if (activation.serial.isCurrentTurn) {
      void activation.serial.post(() => this.closeActivationInsideSerial(activation, signal));
      return true;
    }
    await this.closeActivation(activation, signal);
    return true;
  }

  async executeOnSpot<TSpot extends ZLinkSpot, TResult>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    operation: (spot: TSpot) => Promise<TResult> | TResult,
    signal?: AbortSignal
  ): Promise<TResult> {
    throwIfAborted(signal);
    const activation = this.activations.get(spotActivationKey(spotRid));
    if (activation === undefined) {
      throw new ZLinkConfigurationException(`Spot '${spotRid}' is not active.`);
    }
    if (activation.spotType !== spotType) {
      throw new ZLinkConfigurationException(`Spot '${spotRid}' has a different spot type.`);
    }
    return activation.serial.execute(() => operation(activation.spot as TSpot));
  }

  hasActiveSpot(spotRid: RoutingId): boolean {
    return this.activations.has(spotActivationKey(spotRid));
  }

  async admitActorJoin(
    spotRid: RoutingId,
    actor: ZLinkActor,
    request: Message,
    commit: (spot: ZLinkSpot) => Promise<void> | void,
    signal?: AbortSignal
  ): Promise<ZLinkSpotActorJoinResponse> {
    throwIfAborted(signal);
    const activation = this.activations.get(spotActivationKey(spotRid));
    if (activation === undefined) {
      throw new ZLinkConfigurationException(`Spot '${spotRid}' is not active.`);
    }
    const dispatcher = this.createActorDispatcher(activation);
    const response = await dispatcher.admitActorJoin(actor, request, async () => {
      await commit(activation.spot);
      activation.actors.set(actor.actorId, actor);
      const entryLeave = this.options.entrySpotCallbacks?.onLeaveActor(actor, signal);
      if (entryLeave !== undefined) {
        void entryLeave.catch((error) => {
          this.options.dispatchErrors?.report({
            surface: ZLinkDispatchErrorSurface.SpotActor,
            messageKind: ZLinkDispatchMessageKind.ActorSend,
            reason: ZLinkDispatchErrorReason.HandlerException,
            action: ZLinkDispatchErrorAction.Drop,
            spotRid: String(spotRid),
            actorId: actor.actorId,
            error
          });
        });
      }
    });
    return {
      accepted: response.accepted,
      reply: response.reply === undefined
        ? undefined
        : encodeFrameworkPayloadMessage(response.reply, this.options.messageSerializers)
    };
  }

  async leaveActor(
    spotRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const activation = this.activations.get(spotActivationKey(spotRid));
    if (activation === undefined) {
      throw new ZLinkConfigurationException(`Spot '${spotRid}' is not active.`);
    }
    await activation.serial.execute(async () => {
      await activation.spot.onLeaveActor?.(actor, signal);
      activation.actors.delete(actor.actorId);
      this.options.routedActorLeaveCommitter?.(actor);
    });
    const localEntryNodeRid =
      this.options.entryNodeRidProvider?.() ??
      this.options.entryNodeRid ??
      this.options.nodeRid;
    const entryNodeRid = this.options.actorEntryNodeRidProvider?.(actor) ??
      localEntryNodeRid;
    if (entryNodeRid === undefined) {
      throw new ZLinkConfigurationException('Spot actor leave requires an Entry Spot node routing id.');
    }
    const actorRef = (actor.context as unknown as { actorRef?: ActorRef }).actorRef;
    if (
      localEntryNodeRid !== undefined &&
      actorRef?.nodeRid !== undefined &&
      String(actorRef.nodeRid) !== String(localEntryNodeRid) &&
      String(entryNodeRid) !== String(localEntryNodeRid)
    ) {
      return;
    }
    const request = BindingMessage.from(Buffer.alloc(0));
    try {
      await actor.context.joinEntrySpot(
        entryNodeRid,
        ZLinkMessage.fromEncoded(ZLinkEncodedPayload.from(request.data()))
      ).submit(signal);
    } finally {
      request.close();
    }
  }

  async notifyJoinedSpotActorDisconnected(
    spotRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<boolean> {
    throwIfAborted(signal);
    const activation = this.activations.get(spotActivationKey(spotRid));
    if (activation === undefined) {
      return false;
    }
    const joinedActor = activation.actors.get(actor.actorId) ?? actor;
    await activation.serial.execute(() => activation.spot.onDisconnectActor?.(joinedActor, signal));
    return true;
  }

  dispatchRoutedActorPacket(
    spotRid: RoutingId,
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget
  ): Promise<unknown> {
    const activation = this.activations.get(spotActivationKey(spotRid));
    if (activation === undefined) {
      throw new ZLinkConfigurationException(`Spot '${spotRid}' is not active.`);
    }
    return this.dispatchActorPacket(activation, actorId, parts, returnResponse, remoteBoundSessionTarget);
  }

  async dispatchRoutedSpotSend(
    spotRid: RoutingId,
    packetName: string | undefined,
    message: unknown,
    context: { readonly channelName: string; readonly contentType?: string }
  ): Promise<void> {
    await this.dispatchRoutedSpotPacket(spotRid, packetName, message, context, false);
  }

  async dispatchRoutedSpotRequest<TReply>(
    spotRid: RoutingId,
    packetName: string | undefined,
    request: unknown,
    context: { readonly channelName: string; readonly contentType?: string }
  ): Promise<TReply> {
    return await this.dispatchRoutedSpotPacket(spotRid, packetName, request, context, true) as TReply;
  }

  private async dispatchRoutedSpotPacket(
    spotRid: RoutingId,
    packetName: string | undefined,
    payload: unknown,
    context: { readonly channelName: string; readonly contentType?: string },
    returnResponse: boolean
  ): Promise<unknown> {
    const activation = this.activations.get(spotActivationKey(spotRid));
    if (activation === undefined) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotRoute,
        messageKind: returnResponse ? ZLinkDispatchMessageKind.Request : ZLinkDispatchMessageKind.Send,
        reason: ZLinkDispatchErrorReason.HandlerMissing,
        action: returnResponse ? ZLinkDispatchErrorAction.ReplyError : ZLinkDispatchErrorAction.Drop,
        packetName,
        channelName: context.channelName,
        spotRid: String(spotRid)
      });
      if (!returnResponse) {
        return undefined;
      }
      throw new ZLinkConfigurationException(`Spot '${spotRid}' is not active.`);
    }
    const registrations = activation.handlers.snapshot().filter((registration) => {
      if (registration.kind !== 'packet') {
        return false;
      }
      return (registration.packetName ?? registration.handlerType.name) === (packetName ?? '');
    });
    if (registrations.length === 0) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotRoute,
        messageKind: returnResponse ? ZLinkDispatchMessageKind.Request : ZLinkDispatchMessageKind.Send,
        reason: ZLinkDispatchErrorReason.HandlerMissing,
        action: returnResponse ? ZLinkDispatchErrorAction.ReplyError : ZLinkDispatchErrorAction.Drop,
        packetName,
        channelName: context.channelName,
        spotRid: String(spotRid)
      });
      if (!returnResponse) {
        return undefined;
      }
      throw new ZLinkConfigurationException(`SPOT route handler not found: ${packetName}`);
    }
    let response: unknown;
    try {
      await activation.serial.execute(async () => {
        for (const registration of registrations) {
          const handler = await createProviderInstance(
            registration.handlerType as Type<ZLinkSpotPacketHandler<ZLinkSpot, unknown> | ZLinkSpotRequestHandler<ZLinkSpot, unknown, unknown>>,
            this.options.providerResolver
          );
          response = await handler.handle(activation.spot, payload, {
            channelName: context.channelName,
            contentType: context.contentType,
            packetName
          });
        }
      });
    } catch (error) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotRoute,
        messageKind: returnResponse ? ZLinkDispatchMessageKind.Request : ZLinkDispatchMessageKind.Send,
        reason: ZLinkDispatchErrorReason.HandlerException,
        action: returnResponse ? ZLinkDispatchErrorAction.ReplyError : ZLinkDispatchErrorAction.Drop,
        packetName,
        channelName: context.channelName,
        spotRid: String(spotRid),
        error
      });
      throw error;
    }
    return returnResponse ? response : undefined;
  }

  private async createActivation<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    request: Message,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult> {
    this.requireRegisteredFactory(spotType);
    const serial = new ZLinkSpotSerialExecutor();
    const actorHandlers = new ZLinkSpotActorHandlerRegistryRuntime();
    const handlers = new DefaultZLinkSpotHandlerRegistry(actorHandlers);
    for (const handler of this.options.spotActorSendHandlers ?? []) {
      if (handler.spotType === spotType) {
        handlers.addActorPacketRegistration(
          ZLinkActorPacketKind.Send,
          handler.handlerType,
          handler.actorType,
          handler.packetName
        );
      }
    }
    for (const handler of this.options.spotActorRequestHandlers ?? []) {
      if (handler.spotType === spotType) {
        handlers.addActorPacketRegistration(
          ZLinkActorPacketKind.Request,
          handler.handlerType,
          handler.actorType,
          handler.packetName
        );
      }
    }
    const timers = new ZLinkSpotTimerRegistry();
    const outbound = new DefaultZLinkSpotOutbound(
      serial,
      this.options.channelClient,
      this.options.fanoutClient,
      this.options.spotPublisherClient,
      this.options.remoteAddressResolver,
      this.options.routedTransport
    );
    let spot: ZLinkSpot | undefined;
    const context = this.createSpotContext(spotRid, handlers, outbound, timers, serial, () => spot);
    spot = await createFreshProviderInstance(spotType, this.options.providerResolver, context);
    Object.defineProperty(spot, 'context', {
      configurable: true,
      enumerable: false,
      value: context
    });

    const actors = new Map<string, ZLinkActor>();
    // getOrCreateSpot registers the native Spot under this rid so core routes
    // actor-join admission requests to it (createSpot alone does not register).
    const nativeSpot = this.options.createNativeSpot?.(spotRid);
    const activation: SpotActivation = {
      spotRid,
      spotType,
      spot,
      serial,
      timers,
      actors,
      actorHandlers,
      handlers,
      actorCount: () => actors.size + (this.options.actorCountProvider?.(spotRid) ?? 0),
      nativeSpot
    };
    let nativeDispatch: ZLinkSpotActorJoinDispatch | undefined;
    if (nativeSpot !== undefined) {
      // user Spot join admission uses the same core round-trip as the Entry Spot
      // (and .NET). A user Spot must decide admission, so re-entry default rejects.
      nativeDispatch = new ZLinkSpotActorJoinDispatch(
        nativeSpot,
        serial,
        (actorId) => this.options.actorResolver?.(actorId),
        () => activation.spot,
        false,
        this.options.routedActorProvider,
        this.options.nativeJoinBoundSessionTargetResolver,
        (actor) => {
          this.options.routedActorCommitter?.(actor, spotRid, activation.spot);
          activation.actors.set(actor.actorId, actor);
        },
        (actorId, parts, returnResponse, remoteBoundSessionTarget) =>
          this.dispatchActorPacket(activation, actorId, parts, returnResponse, remoteBoundSessionTarget),
        this.options.routedBoundSessionReceiver,
        this.options.actorPacketTargetProvider,
        (actor, sourceNodeRid, sourceSessionRid) => {
          const node = this.options.nativeSpotNodeProvider?.();
          if (node === undefined || String(sourceNodeRid) === String(node.routingId)) {
            return;
          }
          node.bindRemoteActorSession(actor, sourceNodeRid, sourceSessionRid);
        },
        this.options.messageSerializers,
        this.options.providerResolver,
        this.options.dispatchErrors
      );
      nativeDispatch.attach();
    }

    try {
      await spot.configure?.();
      nativeDispatch?.configureSubscriptions(handlers.snapshot());
      for (const handler of this.options.spotTimerHandlers ?? []) {
        if (handler.spotType === spotType) {
          await timers.add(
            handler.name,
            handler.periodMs,
            handler.options,
            handler.handlerType as Type<ZLinkSpotTimerHandler<ZLinkSpot>>,
            serial,
            spot,
            this.options.providerResolver,
            signal,
            createTimerDiagnostics(
              String(spotRid),
              spotRid,
              false,
              handler.name,
              handler.handlerType,
              this.options.runtimeEventPublisher
            )
          );
        }
      }
      let createResponse: ZLinkSpotCreateResponse | undefined;
      await serial.execute(async () => {
        createResponse = await spot.onCreate?.(
          wrapFrameworkPayloadMessage(request, this.options.messageSerializers),
          signal
        );
        if (createResponse?.accepted === false) {
          return;
        }
        await spot.onInitialize?.(signal);
      });
      if (createResponse?.accepted === false) {
        await activation.timers.dispose();
        return {
          spotRid,
          state: ZLinkSpotCreateState.Rejected,
          reply: this.decodeCreateReply(createResponse.reply)
        };
      }
      this.activations.set(spotActivationKey(spotRid), activation);
      return {
        spotRid,
        state: ZLinkSpotCreateState.Created,
        reply: this.decodeCreateReply(createResponse?.reply)
      };
    } catch (error) {
      await this.closeActivation(activation, signal);
      throw error;
    }
  }

  private decodeCreateReply(reply: unknown): unknown {
    if (reply === undefined) {
      return undefined;
    }
    const message = encodeFrameworkPayloadMessage(reply, this.options.messageSerializers);
    try {
      return decodeFrameworkPayloadMessage(message, this.options.messageSerializers);
    } finally {
      if (ownsFrameworkPayloadMessage(reply)) {
        message.close();
      }
    }
  }

  private createSpotContext(
    spotRid: RoutingId,
    handlers: ZLinkSpotHandlerRegistry,
    outbound: ZLinkSpotOutbound,
    timers: ZLinkSpotTimerRegistry,
    serial: ZLinkSpotSerialExecutor,
    getSpot: () => ZLinkSpot | undefined
  ): ZLinkSpotContext {
    const options = this.options;
    return {
      spotRid,
      get nodeRid() {
        return options.nodeRidProvider?.() ?? options.nodeRid ?? '';
      },
      routingId: spotRid,
      handlers,
      outbound,
      leaveActor: (actor: ZLinkActor, signal?: AbortSignal) => this.leaveActor(spotRid, actor, signal),
      close: (signal?: AbortSignal) => this.close(spotRid, signal),
      addTimer: <THandler extends ZLinkSpotTimerHandler<ZLinkSpot>>(
        name: string,
        periodMs: number,
        handlerType: Type<THandler>,
        options?: ZLinkTimerOptions,
        signal?: AbortSignal
      ) => {
        const spot = getSpot();
        if (spot === undefined) {
          throw new ZLinkConfigurationException('Spot timer cannot be registered before spot activation.');
        }
        return timers.add(
          name,
          periodMs,
          options,
          handlerType,
          serial,
          spot,
          this.options.providerResolver,
          signal,
          createTimerDiagnostics(String(spotRid), spotRid, false, name, handlerType, this.options.runtimeEventPublisher)
        );
      },
      runWorker: <T>(work: (signal: AbortSignal) => T | Promise<T>): ZLinkWorkerCall<T> =>
        new DefaultZLinkWorkerCall(this.workerRuntime, serial, work)
    };
  }

  private async closeActivation(activation: SpotActivation, signal?: AbortSignal): Promise<void> {
    await activation.serial.execute(() => this.closeActivationInsideSerial(activation, signal));
  }

  private async closeActivationInsideSerial(activation: SpotActivation, signal?: AbortSignal): Promise<void> {
    try {
      await activation.spot.onClosing?.(signal);
    } finally {
      await activation.timers.dispose();
      if (activation.nativeSpot !== undefined && typeof activation.nativeSpot.dispose === 'function') {
        await activation.nativeSpot.dispose();
      }
    }
  }

  private requireRegisteredFactory(spotType: Type<ZLinkSpot>): void {
    if (!this.factories.has(spotType)) {
      throw new ZLinkConfigurationException('Spot type is not registered as a spot factory.');
    }
  }

  private createActorDispatcher(activation: SpotActivation): ZLinkSpotActorDispatcher {
    return new ZLinkSpotActorDispatcher({
      registry: activation.actorHandlers,
      spot: activation.spot,
      providerResolver: this.options.providerResolver,
      serial: activation.serial,
      messageSerializers: this.options.messageSerializers
    });
  }

  private async dispatchActorPacket(
    activation: SpotActivation,
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    if (parts.length < 2) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotActor,
        messageKind: ZLinkDispatchMessageKind.ActorSend,
        reason: ZLinkDispatchErrorReason.InvalidFrame,
        action: ZLinkDispatchErrorAction.Drop,
        spotRid: String(activation.spotRid),
        actorId
      });
      return undefined;
    }
    let header: ReturnType<typeof decodeStreamHeader>;
    try {
      header = decodeStreamHeader(messageToBytes(parts[0]));
    } catch (error) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotActor,
        messageKind: ZLinkDispatchMessageKind.ActorSend,
        reason: ZLinkDispatchErrorReason.PayloadDecodeFailed,
        action: ZLinkDispatchErrorAction.Drop,
        spotRid: String(activation.spotRid),
        actorId,
        error
      });
      throw error;
    }
    const messageKind = header.kind === ZLinkStreamMessageKind.Request
      ? ZLinkDispatchMessageKind.ActorRequest
      : ZLinkDispatchMessageKind.ActorSend;
    const action = messageKind === ZLinkDispatchMessageKind.ActorRequest
      ? ZLinkDispatchErrorAction.ReplyError
      : ZLinkDispatchErrorAction.Drop;
    flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Received)?.trace({
      outcome: ZLinkMessageFlowOutcome.Received,
      surface: ZLinkDispatchErrorSurface.SpotActor,
      messageKind,
      packetName: header.name,
      spotRid: String(activation.spotRid),
      actorId,
      correlationId: header.correlationId ?? header.requestSeq?.toString()
    });
    const actor = activation.actors.get(actorId) ?? this.options.actorResolver?.(actorId);
    if (actor === undefined) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotActor,
        messageKind,
        reason: ZLinkDispatchErrorReason.HandlerMissing,
        action,
        packetName: header.name,
        spotRid: String(activation.spotRid),
        actorId,
        correlationId: header.correlationId ?? header.requestSeq?.toString()
      });
      if (messageKind === ZLinkDispatchMessageKind.ActorRequest) {
        if (header.requestSeq !== undefined && !returnResponse && this.options.actorErrorSender !== undefined) {
          await this.options.actorErrorSender(
            actorId,
            header.name,
            header.requestSeq,
            new ZLinkFrameworkException(
              ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound,
              `SPOT actor is not registered locally: ${actorId}`
            ),
            header.metadata,
            fallbackActorRef
          );
          return undefined;
        }
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound,
          `SPOT actor is not registered locally: ${actorId}`
        );
      }
      return undefined;
    }
    if (remoteBoundSessionTarget !== undefined) {
      this.options.remoteActorPacketTargetReceiver?.(actorId, remoteBoundSessionTarget);
    }
    if (header.name === ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET) {
      await activation.serial.execute(() => activation.spot.onDisconnectActor?.(actor));
      return undefined;
    }
    let payload: unknown;
    try {
      payload = decodeFrameworkPayloadMessage(parts[1], this.options.messageSerializers);
    } catch (error) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotActor,
        messageKind,
        reason: ZLinkDispatchErrorReason.PayloadDecodeFailed,
        action,
        packetName: header.name,
        spotRid: String(activation.spotRid),
        actorId,
        correlationId: header.correlationId ?? header.requestSeq?.toString(),
        error
      });
      throw error;
    }
    const dispatcher = this.createActorDispatcher(activation);
    try {
      if (header.kind === ZLinkStreamMessageKind.Send) {
        await dispatcher.dispatchSend(actor, header.name, payload, {
          metadata: Object.fromEntries(header.metadata)
        });
        flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Dispatched)?.trace({
          outcome: ZLinkMessageFlowOutcome.Dispatched,
          surface: ZLinkDispatchErrorSurface.SpotActor,
          messageKind: ZLinkDispatchMessageKind.ActorSend,
          packetName: header.name,
          spotRid: String(activation.spotRid),
          actorId,
          correlationId: header.correlationId ?? header.requestSeq?.toString()
        });
        return undefined;
      }
      if (header.kind !== ZLinkStreamMessageKind.Request || header.requestSeq === undefined) {
        this.options.dispatchErrors?.report({
          surface: ZLinkDispatchErrorSurface.SpotActor,
          messageKind: ZLinkDispatchMessageKind.ActorRequest,
          reason: ZLinkDispatchErrorReason.InvalidFrame,
          action: ZLinkDispatchErrorAction.Drop,
          packetName: header.name,
          spotRid: String(activation.spotRid),
          actorId
        });
        return undefined;
      }
      const requestSeq = header.requestSeq;
      if (returnResponse || this.options.actorResponseSender === undefined) {
        const response = await dispatcher.dispatchRequest(actor, header.name, payload, {
          metadata: Object.fromEntries(header.metadata)
        });
        flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Replied)?.trace({
          outcome: ZLinkMessageFlowOutcome.Replied,
          surface: ZLinkDispatchErrorSurface.SpotActor,
          messageKind: ZLinkDispatchMessageKind.ActorRequest,
          packetName: header.name,
          spotRid: String(activation.spotRid),
          actorId,
          correlationId: header.correlationId ?? requestSeq.toString()
        });
        return response;
      }
      await dispatcher.dispatchRequestThen(actor, header.name, payload, {
        metadata: Object.fromEntries(header.metadata)
      }, async (response) => {
        flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Replied)?.trace({
          outcome: ZLinkMessageFlowOutcome.Replied,
          surface: ZLinkDispatchErrorSurface.SpotActor,
          messageKind: ZLinkDispatchMessageKind.ActorRequest,
          packetName: header.name,
          spotRid: String(activation.spotRid),
          actorId,
          correlationId: header.correlationId ?? requestSeq.toString()
        });
        await this.options.actorResponseSender?.(
          actor,
          header.name,
          requestSeq,
          response,
          new Map(),
          undefined
        );
      });
      return undefined;
    } catch (error) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.SpotActor,
        messageKind,
        reason: error instanceof ZLinkFrameworkException
          && error.kind === ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound
          ? ZLinkDispatchErrorReason.HandlerMissing
          : ZLinkDispatchErrorReason.HandlerException,
        action,
        packetName: header.name,
        spotRid: String(activation.spotRid),
        actorId,
        correlationId: header.correlationId ?? header.requestSeq?.toString(),
        error
      });
      if (
        messageKind === ZLinkDispatchMessageKind.ActorRequest
        && header.requestSeq !== undefined
        && !returnResponse
        && this.options.actorErrorSender !== undefined
      ) {
        await this.options.actorErrorSender(
          actorId,
          header.name,
          header.requestSeq,
          error,
          header.metadata,
          fallbackActorRef
        );
        return undefined;
      }
      throw error;
    }
  }

  private allocateSpotRid(): RoutingId {
    let spotRid: RoutingId;
    do {
      spotRid = `spot-${this.nextId}`;
      this.nextId += 1;
    } while (this.activations.has(spotActivationKey(spotRid)));
    return spotRid;
  }
}

export interface ZLinkSpotHandlerRegistration {
  readonly kind:
    | 'handler'
    | 'packet'
    | 'subscribe'
    | 'actorSend'
    | 'actorRequest'
    | 'spotHandler';
  readonly handlerType: Type;
  readonly packetName?: string;
  readonly topic?: string;
  readonly actorType?: Type<ZLinkActor>;
}

export class DefaultZLinkSpotHandlerRegistry<TActor extends ZLinkActor = ZLinkActor> implements ZLinkSpotHandlerRegistry<TActor> {
  private readonly entries: ZLinkSpotHandlerRegistration[] = [];

  constructor(private readonly actorHandlers?: ZLinkSpotActorHandlerRegistryRuntime) {}

  addHandler(handlerType: Type): this {
    this.entries.push({ kind: 'handler', handlerType });
    return this;
  }

  addPacket(handlerType: Type, packetName?: string): this {
    this.entries.push({ kind: 'packet', handlerType, packetName });
    return this;
  }

  packet(packetName: string, handlerType: Type): this {
    return this.addPacket(handlerType, packetName);
  }

  addActorPacketRegistration(
    kind: ZLinkActorPacketKind,
    handlerType: Type,
    actorType: Type<TActor>,
    packetName: string
  ): this {
    this.entries.push({
      kind: kind === ZLinkActorPacketKind.Send ? 'actorSend' : 'actorRequest',
      handlerType,
      actorType,
      packetName
    });
    this.actorHandlers?.addPacket({
      kind,
      packetName,
      actorType,
      handlerType
    });
    return this;
  }

  actorSend(packetName: string, handlerType: Type, actorType: Type<TActor> = Object as unknown as Type<TActor>): this {
    return this.addActorPacketRegistration(
      ZLinkActorPacketKind.Send,
      handlerType,
      actorType,
      packetName
    );
  }

  actorRequest(packetName: string, handlerType: Type, actorType: Type<TActor> = Object as unknown as Type<TActor>): this {
    return this.addActorPacketRegistration(
      ZLinkActorPacketKind.Request,
      handlerType,
      actorType,
      packetName
    );
  }

  addSubscribe(handlerType: Type, topic: string): this {
    if (topic.trim().length === 0) {
      throw new ZLinkConfigurationException('SPOT subscribe topic must not be empty.');
    }
    this.entries.push({ kind: 'subscribe', handlerType, topic });
    return this;
  }

  subscribe(topic: string, handlerType: Type): this {
    return this.addSubscribe(handlerType, topic);
  }

  addSpotHandler(handlerType: Type): this {
    this.entries.push({ kind: 'spotHandler', handlerType });
    return this;
  }

  snapshot(): readonly ZLinkSpotHandlerRegistration[] {
    return [...this.entries];
  }
}

type ZLinkTimerOwnerSpot = ZLinkSpot | ZLinkEntrySpot;
type ZLinkTimerFailureReporter = (
  tick: ZLinkTimerTick,
  cause: unknown,
  event?: ZLinkSpotEventKind.TimerHandlerFailed | ZLinkSpotEventKind.TimerStoppedAfterUnhandledException
) => Promise<void> | void;

export class ZLinkSpotTimerRegistry {
  private readonly timers = new Set<ZLinkTimer>();

  async add<TSpot extends ZLinkTimerOwnerSpot, THandler extends ZLinkSpotTimerHandler<TSpot>>(
    name: string,
    periodMs: number,
    options: ZLinkTimerOptions | undefined,
    handlerType: Type<THandler>,
    serial: ZLinkSpotSerialExecutor,
    spot: TSpot,
    providerResolver?: ZLinkProviderResolver,
    signal?: AbortSignal,
    reportFailure?: ZLinkTimerFailureReporter
  ): Promise<ZLinkTimer> {
    validateTimer(name, periodMs, options);
    throwIfAborted(signal);
    const handler = await createProviderInstance(handlerType, providerResolver);
    const timer = new ZLinkManagedTimer(
      name,
      periodMs,
      normalizeTimerOptions(options),
      async (tick) => {
        await serial.execute(() => handler.handle(spot, tick));
      },
      reportFailure
    );
    this.timers.add(timer);
    return timer;
  }

  async dispose(): Promise<void> {
    const timers = [...this.timers];
    this.timers.clear();
    for (const timer of timers) {
      await timer.dispose();
    }
  }
}

export class ZLinkManagedTimer implements ZLinkTimer {
  private disposed = false;
  private readonly startedAtMs = Date.now();
  private deliveryIndex = 0n;
  private lastScheduledIndex = 0n;
  private timeout: NodeJS.Timeout | undefined;
  private running: Promise<void> = Promise.resolve();

  constructor(
    private readonly name: string,
    private readonly periodMs: number,
    private readonly options: Required<ZLinkTimerOptions>,
    private readonly onTick: (tick: ZLinkTimerTick) => Promise<void>,
    private readonly onFailure?: ZLinkTimerFailureReporter
  ) {
    this.scheduleNext();
  }

  get isDisposed(): boolean {
    return this.disposed;
  }

  async cancel(_signal?: AbortSignal): Promise<void> {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    if (this.timeout !== undefined) {
      clearTimeout(this.timeout);
      this.timeout = undefined;
    }
    await this.running;
  }

  dispose(): Promise<void> {
    return this.cancel();
  }

  private scheduleNext(): void {
    if (this.disposed) {
      return;
    }

    const delayMs = this.options.overrunPolicy === ZLinkTimerOverrunPolicy.DelayNextTick
      ? this.periodMs
      : Math.max(0, Number(this.lastScheduledIndex + 1n) * this.periodMs - this.elapsedMs());
    this.timeout = setTimeout(() => {
      this.timeout = undefined;
      this.running = this.fire().catch(() => undefined);
    }, delayMs);
  }

  private async fire(): Promise<void> {
    if (this.disposed) {
      return;
    }

    const scheduledIndex = this.selectScheduledIndex();
    const skippedTicks = scheduledIndex - this.lastScheduledIndex - 1n;
    const startedElapsedMs = this.elapsedMs();
    const scheduledElapsedMs = Number(scheduledIndex) * this.periodMs;
    this.deliveryIndex += 1n;
    const tick: ZLinkTimerTick = {
      name: this.name,
      deliveryIndex: this.deliveryIndex,
      scheduledIndex,
      periodMs: this.periodMs,
      scheduledAt: new Date(this.startedAtMs + scheduledElapsedMs),
      startedAt: new Date(this.startedAtMs + startedElapsedMs),
      scheduledElapsedMs,
      startedElapsedMs,
      delayMs: startedElapsedMs - scheduledElapsedMs,
      skippedTicks
    };

    let shouldContinue = true;
    try {
      await this.onTick(tick);
    } catch (cause) {
      await this.onFailure?.(tick, cause);
      shouldContinue = !this.options.stopOnUnhandledException;
      if (!shouldContinue) {
        await this.onFailure?.(tick, cause, ZLinkSpotEventKind.TimerStoppedAfterUnhandledException);
      }
    }

    this.lastScheduledIndex = scheduledIndex;
    if (!shouldContinue) {
      this.disposed = true;
      return;
    }
    this.scheduleNext();
  }

  private selectScheduledIndex(): bigint {
    if (this.options.overrunPolicy === ZLinkTimerOverrunPolicy.DelayNextTick) {
      return this.lastScheduledIndex + 1n;
    }

    const dueScheduledIndex = BigInt(Math.max(1, Math.floor(this.elapsedMs() / this.periodMs)));
    if (this.options.overrunPolicy === ZLinkTimerOverrunPolicy.SkipLateTicks) {
      return dueScheduledIndex;
    }

    const availableTicks = dueScheduledIndex - this.lastScheduledIndex;
    const maxCatchUpTicks = BigInt(this.options.maxCatchUpTicks);
    if (availableTicks > maxCatchUpTicks) {
      return dueScheduledIndex - maxCatchUpTicks + 1n;
    }

    return this.lastScheduledIndex + 1n;
  }

  private elapsedMs(): number {
    return Date.now() - this.startedAtMs;
  }
}

export class DefaultZLinkSpotOutbound implements ZLinkSpotOutbound {
  constructor(
    private readonly serial: ZLinkSpotSerialExecutor,
    private readonly channelClient?: ZLinkChannelClient,
    private readonly fanoutClient?: ZLinkFanoutClient,
    private readonly spotPublisherClient?: ZLinkSpotPublisherClient,
    private readonly remoteAddressResolver?: ZLinkSpotRemoteAddressResolver,
    private readonly routedTransport?: ZLinkSpotRoutedTransport
  ) {}

  sendToSpot(spotRid: RoutingId, message: unknown): ZLinkSendCall {
    return wrapRoutedSpotSendCall(this.serial, this.requireRemoteAddressResolver(), this.requireRoutedTransport(), spotRid, message);
  }

  requestToSpot(spotRid: RoutingId, request: unknown): ZLinkRequestCall {
    return wrapRoutedSpotRequestCall(
      this.serial,
      this.requireRemoteAddressResolver(),
      this.requireRoutedTransport(),
      spotRid,
      request
    );
  }

  publish(topic: string, event: unknown): ZLinkPublishCall {
    if (this.spotPublisherClient !== undefined) {
      return wrapPublishCall(this.serial, this.spotPublisherClient.publishSpot('', topic, event));
    }
    return wrapPublishCall(this.serial, this.requireFanoutClient().publish(topic, event));
  }

  sendToChannel(channelName: string, message: unknown): ZLinkSendCall {
    return wrapSendCall(this.serial, this.requireChannelClient().sendToChannel(channelName, message));
  }

  requestToChannel(channelName: string, request: unknown): ZLinkRequestCall {
    return wrapRequestCall(this.serial, this.requireChannelClient().requestToChannel(channelName, request));
  }

  private requireChannelClient(): ZLinkChannelClient {
    if (this.channelClient === undefined) {
      throw new ZLinkConfigurationException('Spot channel outbound runtime is not started.');
    }
    return this.channelClient;
  }

  private requireFanoutClient(): ZLinkFanoutClient {
    if (this.fanoutClient === undefined) {
      throw new ZLinkConfigurationException('Spot publisher runtime is not started.');
    }
    return this.fanoutClient;
  }

  private requireRemoteAddressResolver(): ZLinkSpotRemoteAddressResolver {
    if (this.remoteAddressResolver === undefined) {
      throw new ZLinkConfigurationException(
        'IZLinkSpotOutbound remote address lookup requires a spot remote address resolver.'
      );
    }
    return this.remoteAddressResolver;
  }

  private requireRoutedTransport(): ZLinkSpotRoutedTransport {
    if (this.routedTransport === undefined) {
      throw new ZLinkConfigurationException('Spot routed outbound runtime is not started.');
    }
    return this.routedTransport;
  }
}

export interface ZLinkSpotRoutedTransport {
  sendToSpot(
    remoteAddress: ZLinkSpotRemoteAddress,
    message: unknown,
    options: ZLinkSpotRoutedSendOptions
  ): Promise<void>;
  requestToSpot<TReply = unknown>(
    remoteAddress: ZLinkSpotRemoteAddress,
    request: unknown,
    options: ZLinkSpotRoutedRequestOptions
  ): Promise<TReply>;
}

export interface ZLinkSpotRoutedSendOptions {
  readonly packetName?: string;
  readonly signal?: AbortSignal;
}

export interface ZLinkSpotRoutedRequestOptions extends ZLinkSpotRoutedSendOptions {
  readonly timeoutMs?: number;
}

export class ZLinkSpotSerialExecutor {
  private tail: Promise<unknown> = Promise.resolve();
  private depth = 0;
  private turnSequence = 0;
  activeTurnId = 0;

  get isExecuting(): boolean {
    return this.depth > 0;
  }

  get isCurrentTurn(): boolean {
    return this.depth > 0 && isCurrentZLinkSpotSerialTurn(this);
  }

  get currentTurn(): ZLinkSpotSerialTurn | undefined {
    return captureZLinkSpotSerialTurn(this);
  }

  /**
   * Runs `operation` in serial order, one turn at a time. A call made from
   * within the currently active turn of this executor is re-entrant and runs
   * as part of that turn instead of queueing (which would deadlock a turn
   * that awaits the nested result).
   */
  execute<T>(operation: () => Promise<T> | T): Promise<T> {
    if (this.isCurrentTurn) {
      return Promise.resolve().then(operation);
    }
    return this.post(operation);
  }

  /**
   * Always enqueues `operation` as its own serial turn, even when called
   * from within the currently active turn. Detached completion callbacks use
   * this so they never run inline inside another callback's turn.
   */
  post<T>(operation: () => Promise<T> | T): Promise<T> {
    const result = new Promise<T>((resolve, reject) => {
      const gate = this.tail.then(
        () => this.runTurn(operation, resolve, reject),
        () => this.runTurn(operation, resolve, reject)
      );
      this.tail = gate.catch(() => undefined);
    });
    return result;
  }

  yieldPromise<T>(pending: Promise<T>): Promise<T> {
    const turn = this.currentTurn;
    if (turn === undefined) {
      throw new Error('yield requires a framework Spot handler turn.');
    }
    return turn.yieldPromise(pending);
  }

  private runTurn<T>(
    operation: () => Promise<T> | T,
    resolve: (value: T) => void,
    reject: (reason: unknown) => void
  ): Promise<void> {
    const turn = new ZLinkSpotSerialTurn((resumeTurn, resume) => this.postResume(resumeTurn, resume));
    const wrapped = async () => {
      this.depth += 1;
      this.turnSequence += 1;
      const turnId = this.turnSequence;
      this.activeTurnId = turnId;
      try {
        return await runZLinkSpotSerialTurn(this, turnId, turn, operation);
      } finally {
        this.depth -= 1;
        this.activeTurnId = 0;
      }
    };
    const owner = wrapped();
    turn.bindOwner(owner);
    owner.then(resolve, reject);
    return Promise.race([
      owner.then(() => undefined, () => undefined),
      turn.suspended
    ]);
  }

  private postResume(turn: ZLinkSpotSerialTurn, resume: () => void): boolean {
    void this.post(async () => {
      turn.resetSuspension();
      resume();
      await turn.resumeOwnerUntilNextYield();
    });
    return true;
  }
}

function wrapSendCall(serial: ZLinkSpotSerialExecutor, inner: ZLinkSendCall): ZLinkSendCall {
  return {
    packetName(packetName: string) {
      inner.packetName(packetName);
      return this;
    },
    submit(signal?: AbortSignal) {
      void serial.execute(() => inner.submit(signal)).catch(() => undefined);
    }
  };
}

function wrapPublishCall(serial: ZLinkSpotSerialExecutor, inner: ZLinkPublishCall): ZLinkPublishCall {
  return {
    packetName(packetName: string) {
      inner.packetName(packetName);
      return this;
    },
    submit(signal?: AbortSignal) {
      void serial.execute(() => inner.submit(signal)).catch(() => undefined);
    }
  };
}

function wrapRequestCall(serial: ZLinkSpotSerialExecutor, inner: ZLinkRequestCall): ZLinkRequestCall {
  const yieldTurn = serial.currentTurn;
  return {
    packetName(packetName: string) {
      inner.packetName(packetName);
      return this;
    },
    timeout(timeoutMs: number) {
      inner.timeout(timeoutMs);
      return this;
    },
    submit<TReply>(signal?: AbortSignal) {
      const insideCurrentTurn = serial.isCurrentTurn;
      const pending = startRequestOnSerial(serial, () => ({ pending: inner.submit<TReply>(signal) }));
      return insideCurrentTurn ? pending : deliverOnSerial(serial, pending);
    },
    yield<TReply>(signal?: AbortSignal) {
      if (yieldTurn === undefined) {
        return Promise.reject(new ZLinkConfigurationException(
          'yield requires a framework Spot handler turn captured when the call object was created.'
        ));
      }
      const pending = startRequestOnSerial(serial, () => ({ pending: inner.submit<TReply>(signal) }));
      return yieldTurn.yieldPromise(pending);
    }
  };
}

/**
 * Starts an outbound request in Spot serial order without gating the serial
 * line on the request round trip. When already running inside the Spot line
 * (a handler turn), the request starts immediately as part of that turn;
 * otherwise the start is enqueued as its own serial turn.
 */
function startRequestOnSerial<TReply>(
  serial: ZLinkSpotSerialExecutor,
  begin: () => Promise<{ pending: Promise<TReply> }> | { pending: Promise<TReply> }
): Promise<TReply> {
  return serial.execute(begin).then((startedRequest) => startedRequest.pending);
}

async function createProviderInstance<T>(
  type: Type<T>,
  resolver: ZLinkProviderResolver | undefined,
  fallbackArg?: unknown
): Promise<T> {
  const existing = resolver?.get?.(type);
  if (existing !== undefined) {
    return existing;
  }
  const created = await resolver?.create?.(type);
  if (created !== undefined) {
    return created;
  }
  if (fallbackArg !== undefined) {
    return new (type as new (arg: unknown) => T)(fallbackArg);
  }
  return new (type as new () => T)();
}

async function createFreshProviderInstance<T>(
  type: Type<T>,
  resolver: ZLinkProviderResolver | undefined,
  fallbackArg?: unknown
): Promise<T> {
  const created = await resolver?.create?.(type);
  if (created !== undefined) {
    return created;
  }
  if (fallbackArg !== undefined) {
    return new (type as new (arg: unknown) => T)(fallbackArg);
  }
  return new (type as new () => T)();
}

function wrapRoutedSpotSendCall(
  serial: ZLinkSpotSerialExecutor,
  resolver: ZLinkSpotRemoteAddressResolver,
  transport: ZLinkSpotRoutedTransport,
  spotRid: RoutingId,
  message: unknown
): ZLinkSendCall {
  let selectedPacketName: string | undefined;
  return {
    packetName(packetName: string) {
      selectedPacketName = packetName;
      return this;
    },
    submit(signal?: AbortSignal) {
      void serial.execute(async () => {
        const remoteAddress = await resolver.resolve(spotRid, signal);
        await transport.sendToSpot(remoteAddress, message, { packetName: selectedPacketName, signal });
      }).catch(() => undefined);
    }
  };
}

function wrapRoutedSpotRequestCall(
  serial: ZLinkSpotSerialExecutor,
  resolver: ZLinkSpotRemoteAddressResolver,
  transport: ZLinkSpotRoutedTransport,
  spotRid: RoutingId,
  request: unknown
): ZLinkRequestCall {
  let selectedPacketName: string | undefined;
  let selectedTimeoutMs: number | undefined;
  const yieldTurn = serial.currentTurn;
  return {
    packetName(packetName: string) {
      selectedPacketName = packetName;
      return this;
    },
    timeout(timeoutMs: number) {
      selectedTimeoutMs = timeoutMs;
      return this;
    },
    submit<TReply>(signal?: AbortSignal) {
      const insideCurrentTurn = serial.isCurrentTurn;
      const pending = startRequestOnSerial<TReply>(serial, async () => {
        const remoteAddress = await resolver.resolve(spotRid, signal);
        return {
          pending: transport.requestToSpot<TReply>(remoteAddress, request, {
            packetName: selectedPacketName,
            timeoutMs: selectedTimeoutMs,
            signal
          })
        };
      });
      return insideCurrentTurn ? pending : deliverOnSerial(serial, pending);
    },
    yield<TReply>(signal?: AbortSignal) {
      if (yieldTurn === undefined) {
        return Promise.reject(new ZLinkConfigurationException(
          'yield requires a framework Spot handler turn captured when the call object was created.'
        ));
      }
      const pending = startRequestOnSerial<TReply>(serial, async () => {
        const remoteAddress = await resolver.resolve(spotRid, signal);
        return {
          pending: transport.requestToSpot<TReply>(remoteAddress, request, {
            packetName: selectedPacketName,
            timeoutMs: selectedTimeoutMs,
            signal
          })
        };
      });
      return yieldTurn.yieldPromise(pending);
    }
  };
}

function validateTimer(name: string, periodMs: number, options: ZLinkTimerOptions | undefined): void {
  if (name.trim().length === 0) {
    throw new ZLinkConfigurationException('SPOT timer name must not be empty.');
  }
  if (!Number.isFinite(periodMs) || periodMs <= 0) {
    throw new ZLinkConfigurationException('SPOT timer period must be greater than zero.');
  }
  if (
    options?.overrunPolicy !== undefined
    && !Object.values(ZLinkTimerOverrunPolicy).includes(options.overrunPolicy)
  ) {
    throw new ZLinkConfigurationException('SPOT timer overrun policy is not supported.');
  }
  if (
    options?.overrunPolicy === ZLinkTimerOverrunPolicy.CatchUpBounded
    && (options.maxCatchUpTicks === undefined || options.maxCatchUpTicks <= 0)
  ) {
    throw new ZLinkConfigurationException('SPOT timer MaxCatchUpTicks must be greater than zero.');
  }
}

function createTimerDiagnostics(
  sourceName: string,
  spotRid: RoutingId,
  isEntrySpot: boolean,
  timerName: string,
  handlerType: Type,
  publisher: ZLinkRuntimeEventPublisher | undefined
): ZLinkTimerFailureReporter | undefined {
  if (publisher === undefined) {
    return undefined;
  }
  return async (tick, cause, event = ZLinkSpotEventKind.TimerHandlerFailed) => {
    try {
      await publisher.publish({
        sourceName,
        timestamp: new Date(),
        event,
        timerDiagnostic: {
          spotRid,
          isEntrySpot,
          timerName,
          handlerType: handlerType.name,
          deliveryIndex: tick.deliveryIndex,
          scheduledIndex: tick.scheduledIndex,
          exceptionType: exceptionType(cause),
          exceptionMessage: exceptionMessage(cause)
        }
      });
    } catch {
    }
  };
}

function exceptionType(cause: unknown): string {
  return cause instanceof Error ? cause.name : typeof cause;
}

function exceptionMessage(cause: unknown): string {
  return cause instanceof Error ? cause.message : String(cause);
}

function normalizeTimerOptions(options: ZLinkTimerOptions | undefined): Required<ZLinkTimerOptions> {
  return {
    overrunPolicy: options?.overrunPolicy ?? ZLinkTimerOverrunPolicy.SkipLateTicks,
    maxCatchUpTicks: options?.maxCatchUpTicks ?? 1,
    stopOnUnhandledException: options?.stopOnUnhandledException ?? false
  };
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted === true) {
    throw new Error('The operation was aborted.');
  }
}

function isMessage(value: unknown): value is Message {
  return typeof value === 'object'
    && value !== null
    && typeof (value as { data?: unknown }).data === 'function';
}

function ownsFrameworkPayloadMessage(value: unknown): boolean {
  return value === undefined || !(isMessage(value) || (isZLinkMessage(value) && value.isEncoded()));
}
