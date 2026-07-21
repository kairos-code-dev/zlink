import type {
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkActorFactory,
  ZLinkProviderResolver
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkMessage
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { ZLinkConfigurationException } from '../configuration';
import { DefaultZLinkActorContext } from './actor-context';
import {
  ZLinkActorRuntimeState,
  toFrameworkActorRef,
  toFrameworkRoutingId
} from './actor-runtime-state';
import type { ZLinkActorManagerOptions } from './actor-runtime-contracts';

export interface ZLinkActorCreateRequest {
  readonly nativeRequest: Message | undefined;
  readonly callbackRequest: ZLinkMessage;
}

export class ZLinkActorCreationCoordinator {
  constructor(private readonly options: ZLinkActorManagerOptions) {}

  async createActor(
    actorId: string,
    actorType: string,
    state: ZLinkActorRuntimeState,
    createRequest: ZLinkActorCreateRequest,
    claimLocation: boolean,
    signal?: AbortSignal
  ): Promise<ZLinkActor> {
    const lifecycle = this.options.locationLifecycle;
    if (lifecycle !== undefined && claimLocation) {
      const nodeRid = this.resolveLocationNodeRid();
      const activation = await lifecycle.executeActorClaimThenActivate(
        actorType,
        actorId,
        nodeRid,
        async () => {
          // A target takeover is the commit point of an in-flight remote move.
          // Keep the source runtime state until the commit reply installs the
          // remote ActorRef used to finish the caller's current request.
          if (state.isMoving || state.remoteActorPacketTarget !== undefined) {
            return;
          }
          this.options.actorDestroyedCleanup?.(actorId);
          state.clearAfterDestroy();
        },
        () => this.createActorAfterClaim(actorId, actorType, state, createRequest, true, signal)
      );
      if (activation.activated !== undefined) {
        if (activation.generation !== undefined) state.setLocationGeneration(activation.generation);
        state.markLocationOwned();
        return activation.activated;
      }
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorCreateFailed,
        activation.existingLocation === undefined
          ? `Actor '${actorId}' location claim was rejected and no live location row was found.`
          : `Actor '${actorId}' is already active on node '${activation.existingLocation.ownerNodeRid}' (location claim conflict).`
      );
    }

    return await this.createActorAfterClaim(actorId, actorType, state, createRequest, claimLocation, signal);
  }

  async materializeTransferredActor(
    actorId: string,
    actorType: string,
    state: ZLinkActorRuntimeState,
    restore: (() => Promise<ZLinkActor>) | undefined
  ): Promise<ZLinkActor> {
    const context = state.ensureContext(() => new DefaultZLinkActorContext(
      state,
      this.options.joinCoordinator,
      this.options.boundSessionFactory,
      this.options.messageSerializers,
      this.options.actorMeshNameProvider,
      this.options.actorLeaveSpot
    ));
    const actor = restore === undefined
      ? await (await this.createFactory(actorType)).create(actorId, context)
      : await restore();
    attachTransferredActorContext(actor, context);
    state.bindActor(actor, context);
    const nativeActorNode = this.options.nativeActorNode ?? this.options.nativeActorNodeProvider?.();
    if (nativeActorNode !== undefined) {
      state.ensureNativeActorRef(nativeActorNode);
    }
    return actor;
  }

  private async createActorAfterClaim(
    actorId: string,
    actorType: string,
    state: ZLinkActorRuntimeState,
    createRequest: ZLinkActorCreateRequest,
    updateLocation: boolean,
    signal?: AbortSignal
  ): Promise<ZLinkActor> {
    const factory = await this.createFactory(actorType);
    const context = state.ensureContext(() => new DefaultZLinkActorContext(
      state,
      this.options.joinCoordinator,
      this.options.boundSessionFactory,
      this.options.messageSerializers,
      this.options.actorMeshNameProvider,
      this.options.actorLeaveSpot
    ));
    const actor = await factory.create(actorId, context);
    try {
      state.bindActor(actor, context);
      const nativeActorNode = this.options.nativeActorNode ?? this.options.nativeActorNodeProvider?.();
      if (nativeActorNode !== undefined) {
        const actorRef = state.ensureNativeActorRef(nativeActorNode, createRequest.nativeRequest);
        await this.options.actorCreatedNotifier?.(
          toFrameworkRoutingId(actorRef.nodeRid),
          actor,
          createRequest.callbackRequest,
          signal
        );
        if (updateLocation) {
          await this.options.locationLifecycle?.setActorRef(
            actorType,
            actorId,
            toFrameworkActorRef(actorRef),
            nativeActorNode.status().lifecycleGeneration
          );
        }
      } else {
        const nodeRid = this.options.actorCreatedNodeRidProvider?.();
        if (nodeRid !== undefined) {
          await this.options.actorCreatedNotifier?.(nodeRid, actor, createRequest.callbackRequest, signal);
          if (updateLocation) {
            await this.options.locationLifecycle?.setActorRef(
              actorType,
              actorId,
              { nodeRid, actorId, generation: 0n },
              0n
            );
          }
        }
      }
    } catch (error) {
      state.clearAfterDestroy();
      throw error;
    }
    return actor;
  }

  private resolveLocationNodeRid(): RoutingId {
    const nativeActorNode = this.options.nativeActorNode ?? this.options.nativeActorNodeProvider?.();
    const nodeRid = nativeActorNode === undefined
      ? this.options.actorCreatedNodeRidProvider?.()
      : toFrameworkRoutingId(nativeActorNode.status().routingId as never);
    if (nodeRid === undefined) {
      throw new ZLinkConfigurationException('Location actor claim requires a node routing id.');
    }
    return nodeRid;
  }

  private async createFactory(actorType: string): Promise<ZLinkActorFactory> {
    const factoryOrType = this.options.actorFactories.get(actorType);
    if (factoryOrType === undefined) {
      throw new ZLinkConfigurationException(`Actor factory '${actorType}' is not registered.`);
    }
    if (typeof factoryOrType === 'function') {
      const type = factoryOrType as Type<ZLinkActorFactory>;
      return await createProviderInstance(type, this.options.providerResolver);
    }
    return factoryOrType;
  }
}

function attachTransferredActorContext(actor: ZLinkActor, context: ZLinkActor['context']): void {
  const current = (actor as { context?: ZLinkActor['context'] }).context;
  if (current === context) {
    return;
  }
  Object.defineProperty(actor, 'context', {
    configurable: false,
    enumerable: true,
    writable: false,
    value: context
  });
}

async function createProviderInstance<T>(
  type: Type<T>,
  resolver: ZLinkProviderResolver | undefined
): Promise<T> {
  const existing = resolver?.get?.(type);
  if (existing !== undefined) {
    return existing;
  }
  const created = await resolver?.create?.(type);
  if (created !== undefined) {
    return created;
  }
  return new (type as new () => T)();
}
