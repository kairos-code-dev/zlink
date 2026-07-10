import type {
  ActorRef,
  ZLinkActor,
  ZLinkProviderResolver,
  ZLinkRuntimeEventPublisher
} from '../../contracts';
import {
  DefaultZLinkChannelClient,
  DefaultZLinkFanoutClient,
  DefaultZLinkSpotPublisherClient,
  type ZLinkChannelClientTransport,
  type ZLinkSpotPublisherClientTransport
} from '../channels';
import type { ZLinkDispatchErrorReporter } from '../channels';
import type { ZLinkFrameworkRegistration } from '../configuration';
import type { ZLinkBackendActorRef, ZLinkBackendAdapterFactory, ZLinkBackendContext } from '../backend';
import type { DefaultZLinkActorManager, ZLinkRemoteBoundSessionTarget } from '../actors';
import type { Message } from '../../contracts/Common/Message';
import type {
  DefaultZLinkSpotManager,
  ZLinkSpotNodeRuntimeManager,
  ZLinkSpotNodeRuntimeManagerOptions,
  ZLinkDetachedTaskRunner,
  ZLinkSpotRoutedTransport
} from '../spots';
import type { MeshRouterResolver } from './mesh-router-resolver';
import type { ZLinkBoundSessionRelay } from './bound-session-relay';
import type { ZLinkStreamBindingRuntime } from '../streams';
import type { ZLinkActorHandoffCoordinator } from '../actors';

export interface ZLinkSpotNodeRuntimeOptionsFactoryOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  readonly context: ZLinkBackendContext;
  readonly channelTransport: ZLinkChannelClientTransport;
  readonly routeTransport: ZLinkSpotRoutedTransport;
  readonly spotPublisherTransport: ZLinkSpotPublisherClientTransport;
  readonly meshRouters: MeshRouterResolver;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors: ZLinkDispatchErrorReporter;
  readonly runtimeEventPublisher: ZLinkRuntimeEventPublisher;
  readonly actorManager: () => DefaultZLinkActorManager | undefined;
  readonly spotManager: () => DefaultZLinkSpotManager | undefined;
  readonly spotNodeRuntime: () => ZLinkSpotNodeRuntimeManager | undefined;
  readonly streamBindingRuntime: ZLinkStreamBindingRuntime;
  readonly boundSessionRelay: ZLinkBoundSessionRelay;
  readonly actorHandoff: ZLinkActorHandoffCoordinator;
  readonly detachedTaskRunner: ZLinkDetachedTaskRunner;
}

export class ZLinkSpotNodeRuntimeOptionsFactory {
  constructor(private readonly options: ZLinkSpotNodeRuntimeOptionsFactoryOptions) {}

  create(): ZLinkSpotNodeRuntimeManagerOptions {
    return {
      registration: this.options.registration,
      backendAdapterFactory: this.options.backendAdapterFactory,
      context: this.options.context,
      channelClient: new DefaultZLinkChannelClient(this.options.registration, this.options.channelTransport),
      fanoutClient: new DefaultZLinkFanoutClient(this.options.registration, this.options.channelTransport),
      spotPublisherClient: new DefaultZLinkSpotPublisherClient(
        this.options.registration,
        this.options.spotPublisherTransport
      ),
      routedTransport: this.options.routeTransport,
      spotRouterChannelIdForMesh: this.options.meshRouters.spotRouterChannelIdByMesh(),
      providerResolver: this.options.providerResolver,
      dispatchErrors: this.options.dispatchErrors,
      runtimeEventPublisher: this.options.runtimeEventPublisher,
      detachedTaskRunner: this.options.detachedTaskRunner,
      messageSerializers: this.options.registration.messageSerializers,
      actorResolver: (actorId) => this.options.actorManager()?.getState(actorId)?.actor,
      entryActorCommitter: (actor) => this.commitEntryActor(actor),
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
      localActorPacketRouter: (actorId, parts, returnResponse, remoteBoundSessionTarget) =>
        this.dispatchLocalActorPacket(actorId, parts, returnResponse, remoteBoundSessionTarget),
      actorResponseSender: (actor, packetName, requestSeq, response, replyOptions, signal) =>
        this.options.boundSessionRelay.sendActorResponse(actor, packetName, requestSeq, response, replyOptions, signal),
      actorErrorSender: (actorId, packetName, requestSeq, error, metadata, actorRef, signal) =>
        this.options.boundSessionRelay.sendActorError(actorId, packetName, requestSeq, error, metadata, actorRef, signal),
      actorDestroyer: (node, entryNodeRid, actor, signal) =>
        this.requireActorManager().destroyActor(node, entryNodeRid, actor, signal)
    };
  }

  private async commitEntryActor(actor: ZLinkActor): Promise<void> {
    const state = this.options.actorManager()?.getState(actor.actorId);
    const entryNode = this.options.spotNodeRuntime()?.primaryNode;
    if (state === undefined || entryNode === undefined) {
      return;
    }
    const generation = state.nativeActorRef?.generation ?? 0n;
    state.clearJoinedSpot();
    this.options.boundSessionRelay.clearRemoteActorPacketTarget(actor.actorId);
    const actorRef = {
      nodeRid: entryNode.routingId,
      actorId: actor.actorId,
      generation
    } as ZLinkBackendActorRef;
    state.setNativeActorRef(actorRef);
    await this.options.streamBindingRuntime.refreshActor(actorRef as ActorRef);
  }

  private async dispatchLocalActorPacket(
    actorId: string,
    parts: readonly Message[],
    returnResponse?: boolean,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget
  ): Promise<{ readonly handled: boolean; readonly response?: unknown }> {
    const actorManager = this.options.actorManager();
    const spotManager = this.options.spotManager();
    const spotRid = actorManager?.getState(actorId)?.spotRid;
    if (spotRid === undefined || spotManager === undefined) {
      return { handled: false };
    }
    return {
      handled: true,
      response: await spotManager.dispatchRoutedActorPacket(
        spotRid,
        actorId,
        parts,
        returnResponse,
        remoteBoundSessionTarget
      )
    };
  }

  private requireActorManager(): DefaultZLinkActorManager {
    const manager = this.options.actorManager();
    if (manager === undefined) {
      throw new Error('Entry Spot actor destroy requires ZLINK_ACTOR_MANAGER.');
    }
    return manager;
  }
}
