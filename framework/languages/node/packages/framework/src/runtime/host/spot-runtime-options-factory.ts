import type {
  Type,
  ZLinkRuntimeEventPublisher,
  ZLinkSpot,
  ZLinkSpotDrainPolicy
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
import type { ZLinkActorTransferRuntime } from './actor-transfer-runtime';
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
  readonly metrics: import('../diagnostics').ZLinkRuntimeMetrics;
}

export class ZLinkSpotRuntimeOptionsFactory {
  constructor(private readonly options: ZLinkSpotRuntimeOptionsFactoryOptions) {}

  create(actorTransferRuntime: ZLinkActorTransferRuntime): Partial<ZLinkSpotManagerOptions> {
    return {
      nodeRid: undefined,
      nodeRidProvider: () => this.primaryNode()?.routingId,
      entryNodeRid: undefined,
      entryNodeRidProvider: () => this.primaryNode()?.routingId,
      entrySpotCallbacks: {
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
      actorTransferRuntime,
      boundSessionRuntime: this.options.boundSessionRelay.boundSessions,
      actorHandoffRuntime: this.options.actorHandoff,
      spotDrainPolicy: this.spotDrainPolicyResolver(),
      metrics: this.options.metrics,
      dispatchErrors: this.options.dispatchErrorReporter(this.options.runtimeOrPreStartErrorSink)
    };
  }

  private primaryNode(): ZLinkBackendSpotNode | undefined {
    return this.options.spotNodeRuntime()?.primaryNode;
  }

  private spotDrainPolicyResolver(): (spotType: Type<ZLinkSpot>) => ZLinkSpotDrainPolicy {
    const policies = new Map<Type<ZLinkSpot>, ZLinkSpotDrainPolicy[]>();
    for (const node of this.options.registration.spotNodes.values()) {
      for (const spotType of node.spotFactories ?? []) {
        const registered = policies.get(spotType) ?? [];
        registered.push(node.drainPolicy ?? 'DrainNatural');
        policies.set(spotType, registered);
      }
    }
    return (spotType) => {
      const registered = policies.get(spotType);
      if (registered === undefined || registered.includes('DrainNatural')) return 'DrainNatural';
      return 'ReleaseAndRecreate';
    };
  }
}
