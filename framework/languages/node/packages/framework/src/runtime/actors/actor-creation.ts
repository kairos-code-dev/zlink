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
import type { ZLinkActorManagerOptions } from './index';

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
          this.options.actorDestroyedCleanup?.(actorId);
          state.clearAfterDestroy();
        },
        () => this.createActorAfterClaim(actorId, actorType, state, createRequest, true, signal)
      );
      if (activation.activated !== undefined) {
        state.markLocationOwned();
        return activation.activated;
      }
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorCreateFailed,
        activation.existingLocation === undefined
          ? `Actor '${actorId}' location claim was rejected and no live location row was found.`
          : `Actor '${actorId}' is already active on node '${activation.existingLocation.nodeRid}' (location claim conflict).`
      );
    }

    return await this.createActorAfterClaim(actorId, actorType, state, createRequest, claimLocation, signal);
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
      this.options.messageSerializers
    ));
    const actor = await factory.create(actorId, context, signal);
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
            toFrameworkActorRef(actorRef)
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
              { nodeRid, actorId, generation: 0n }
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
      : toFrameworkRoutingId(nativeActorNode.routingId);
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
