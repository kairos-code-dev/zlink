import type {
  ActorRef,
  ZLinkRuntimeEventPublisher
} from '../../contracts';
import type {
  DefaultZLinkActorManager,
  ZLinkActorHandoffCoordinator
} from '../actors';
import {
  DefaultZLinkChannelClient,
  DefaultZLinkFanoutClient,
  DefaultZLinkSpotPublisherClient,
  type ZLinkChannelClientTransport,
  type ZLinkDispatchErrorSink,
  type ZLinkSpotPublisherClientTransport
} from '../channels';
import type { ZLinkDispatchErrorReporter } from '../channels';
import type { ZLinkFrameworkRegistration } from '../configuration';
import type { ZLinkLocationLifecycle } from '../locations';
import type { ZLinkBackendSpotNode } from '../backend';
import type { ZLinkSpotRouteResolver } from '../spots/spot-routing-internal';
import type {
  ZLinkDetachedTaskRunner,
  ZLinkSpotManagerOptions,
  ZLinkSpotNodeRuntimeManager,
  ZLinkSpotRoutedTransport
} from '../spots';
import type { ZLinkActorRuntimeOptionsFactory } from './actor-runtime-options-factory';
import type { ZLinkBoundSessionRelay } from './bound-session-relay';
import type { MeshRouterResolver } from './mesh-router-resolver';

export interface ZLinkSpotRuntimeOptionsFactoryOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly channelTransport: ZLinkChannelClientTransport;
  readonly routeTransport: ZLinkSpotRoutedTransport;
  readonly spotPublisherTransport: ZLinkSpotPublisherClientTransport;
  readonly meshRouters: MeshRouterResolver;
  readonly runtimeEventPublisher: ZLinkRuntimeEventPublisher;
  readonly spotNodeRuntime: () => ZLinkSpotNodeRuntimeManager | undefined;
  readonly actorManager: () => DefaultZLinkActorManager | undefined;
  readonly locationLifecycle: () => ZLinkLocationLifecycle | undefined;
  readonly createLocationSpotRouteResolver: () => ZLinkSpotRouteResolver | undefined;
  readonly boundSessionRelay: ZLinkBoundSessionRelay;
  readonly actorHandoff: ZLinkActorHandoffCoordinator;
  readonly dispatchErrorReporter: (errorSink: ZLinkDispatchErrorSink) => ZLinkDispatchErrorReporter;
  readonly runtimeOrPreStartErrorSink: ZLinkDispatchErrorSink;
  readonly detachedTaskRunner: ZLinkDetachedTaskRunner;
}

export class ZLinkSpotRuntimeOptionsFactory {
  constructor(private readonly options: ZLinkSpotRuntimeOptionsFactoryOptions) {}

  create(actorRuntimeOptions: ZLinkActorRuntimeOptionsFactory): Partial<ZLinkSpotManagerOptions> {
    return {
      nodeRid: undefined,
      nodeRidProvider: () => this.primaryNode()?.routingId,
      entryNodeRid: undefined,
      entryNodeRidProvider: () => this.primaryNode()?.routingId,
      actorEntryNodeRidProvider: (actor) => actorRuntimeOptions.actorEntryNodeRid(actor),
      entrySpotCallbacks: {
        onJoinedActor: (actor, signal) =>
          this.options.spotNodeRuntime()?.notifyPrimaryEntrySpotActorJoined(actor, signal) ?? Promise.resolve(),
        onLeaveActor: (actor, signal) =>
          this.options.spotNodeRuntime()?.notifyPrimaryEntrySpotActorLeft(actor, signal) ?? Promise.resolve()
      },
      channelClient: new DefaultZLinkChannelClient(this.options.registration, this.options.channelTransport),
      fanoutClient: new DefaultZLinkFanoutClient(this.options.registration, this.options.channelTransport),
      spotPublisherClient: new DefaultZLinkSpotPublisherClient(
        this.options.registration,
        this.options.spotPublisherTransport
      ),
      routedTransport: this.options.routeTransport,
      spotRouterChannelIdForMesh: this.options.meshRouters.spotRouterChannelIdByMesh(),
      messageSerializers: this.options.registration.messageSerializers,
      runtimeEventPublisher: this.options.runtimeEventPublisher,
      detachedTaskRunner: this.options.detachedTaskRunner,
      locationLifecycle: this.options.locationLifecycle(),
      locationMeshName: this.options.meshRouters.primarySpotMeshName(),
      spotRouteResolver: this.options.createLocationSpotRouteResolver(),
      createNativeSpot: (spotRid) => this.primaryNode()?.getOrCreateSpot(spotRid).spot,
      nativeSpotNodeProvider: () => this.primaryNode(),
      actorResolver: (actorId) => {
        const state = this.options.actorManager()?.getState(actorId);
        return state?.isMoving === true ? undefined : state?.actor;
      },
      routedActorProvider: (
        actorId,
        actorType,
        actorRef,
        remoteBoundSessionTarget,
        actorCreateRequest,
        signal
      ) => actorRuntimeOptions.getOrCreateRoutedActor(
        actorId,
        actorType,
        actorRef as ActorRef | undefined,
        remoteBoundSessionTarget,
        actorCreateRequest,
        signal
      ),
      routedActorTransferProvider: (
        actorId,
        actorType,
        adapterKey,
        transferState,
        remoteBoundSessionTarget,
        signal
      ) => actorRuntimeOptions.materializeRoutedActor(
        actorId,
        actorType,
        adapterKey,
        transferState,
        remoteBoundSessionTarget,
        signal
      ),
      routedActorFinalizer: (actor, spotRid) => actorRuntimeOptions.finalizeRoutedActor(
        actor,
        spotRid,
        this.options.meshRouters.primarySpotMeshName() ?? ''
      ),
      routedActorCommitter: (actor, spotRid, spot) =>
        actorRuntimeOptions.commitRoutedActor(actor, spotRid, spot),
      routedActorLeaveCommitter: (actor) =>
        actorRuntimeOptions.clearRoutedActor(
          actor,
          (actorId) => this.options.boundSessionRelay.clearRemoteActorPacketTarget(actorId)
        ),
      routedActorRollback: (actor) => actorRuntimeOptions.rollbackRoutedActor(actor),
      routedBoundSessionReceiver: (actorId, message, packetName, metadata, actorRef) =>
        this.options.boundSessionRelay.receiveRoutedBoundSession(actorId, message, packetName, metadata, actorRef),
      routedBoundSessionResponseReceiver: (actorId, packetName, requestSeq, message, replyOptions, actorPacketTarget) =>
        this.options.boundSessionRelay.receiveRoutedBoundSessionResponse(
          actorId,
          packetName,
          requestSeq,
          message,
          replyOptions,
          actorPacketTarget
        ),
      routedBoundSessionErrorReceiver: (actorId, packetName, requestSeq, error, metadata, actorPacketTarget) =>
        this.options.boundSessionRelay.receiveRoutedBoundSessionError(
          actorId,
          packetName,
          requestSeq,
          error,
          metadata,
          actorPacketTarget
        ),
      routedBoundSessionOwnershipReceiver: (payload) =>
        this.options.boundSessionRelay.receiveRemoteBoundSessionOwnership(payload),
      remoteActorPacketTargetReceiver: (actorId, target) => {
        const state = this.options.actorManager()?.getState(actorId);
        state?.setRemoteBoundSessionTarget(target);
      },
      remoteBoundSessionTargetResolver: (sourceNodeRid) =>
        this.options.meshRouters.remoteBoundSessionTargetForSource(sourceNodeRid),
      actorPacketTargetProvider: (actorId) => this.options.boundSessionRelay.actorPacketTargetForState(actorId),
      actorPacketHandoff: (actorId, parts, returnResponse, remoteBoundSessionTarget, fallbackActorRef) =>
        this.options.actorHandoff.capture(
          actorId,
          parts,
          returnResponse,
          remoteBoundSessionTarget,
          fallbackActorRef
        ),
      actorResponseSender: (actor, packetName, requestSeq, response, replyOptions, signal) =>
        this.options.boundSessionRelay.sendActorResponse(actor, packetName, requestSeq, response, replyOptions, signal),
      actorErrorSender: (actorId, packetName, requestSeq, error, metadata, actorRef, signal) =>
        this.options.boundSessionRelay.sendActorError(actorId, packetName, requestSeq, error, metadata, actorRef, signal),
      dispatchErrors: this.options.dispatchErrorReporter(this.options.runtimeOrPreStartErrorSink)
    };
  }

  private primaryNode(): ZLinkBackendSpotNode | undefined {
    return this.options.spotNodeRuntime()?.primaryNode;
  }
}
