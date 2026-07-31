import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkMessageSerializer
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkBackendMeshNode, ZLinkMeshCompletionTable } from '../backend';
import type { ZLinkLocationLifecycle } from '../locations';
import { throwIfAborted } from '../abort';
import type {
  ZLinkSpotRouteResolver
} from '../spots/spot-routing-internal';
import type { ZLinkActorJoinCoordinator, ZLinkActorJoinRuntimeResult } from './actor-runtime-contracts';
import type { ZLinkActorRoutedJoinTransport } from './actor-routed-join-transport';
import type { ZLinkStoreLocationResolvers } from '../locations';
import type { ZLinkActorSourceTransfer } from './actor-source-transfer';
import { ZLinkActorRuntimeState, toFrameworkRoutingId } from './actor-runtime-state';
import { lookupNativeActorRef } from './actor-native-lookup';
import { ZLinkPostCommitActorLocation } from './post-commit-actor-location';
import { ZLinkLocalNativeActorJoin } from './actor-local-native-join';
import { ZLinkPostCommitActorBinder } from './post-commit-actor-binder';
import { routingIdsEqual } from '../routing-id';

export interface ZLinkActorNativeJoinCoordinatorOptions {
  readonly node: ZLinkBackendMeshNode | (() => ZLinkBackendMeshNode);
  readonly completionTableProvider: () => ZLinkMeshCompletionTable | undefined;
  readonly spotRouteResolver?: ZLinkSpotRouteResolver;
  readonly routedTransport?: ZLinkActorRoutedJoinTransport;
  readonly locationLifecycle?: ZLinkLocationLifecycle;
  readonly remoteActorBinder?: (actorRef: ActorRef, signal?: AbortSignal, force?: boolean) => Promise<void>;
  readonly postCommitErrorReporter?: (error: unknown) => void;
  readonly sourceTransfer?: ZLinkActorSourceTransfer;
  readonly actorLocationResolver?: () => ZLinkStoreLocationResolvers | undefined;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly shutdownSignal?: AbortSignal;
  readonly actorTransferTimeoutMs?: number;
  readonly entrySpotIdProvider?: (meshName: string | undefined) => string | undefined;
}

/** Selects local native join or remote two-phase transfer for each destination. */
export class ZLinkActorNativeJoinCoordinator implements ZLinkActorJoinCoordinator {
  private readonly localJoin: ZLinkLocalNativeActorJoin;

  constructor(private readonly options: ZLinkActorNativeJoinCoordinatorOptions) {
    const postCommitLocation = options.locationLifecycle === undefined
      ? undefined
      : new ZLinkPostCommitActorLocation({
          lifecycle: options.locationLifecycle,
          reportError: options.postCommitErrorReporter,
          signal: options.shutdownSignal
        });
    this.localJoin = new ZLinkLocalNativeActorJoin({
      postCommitLocation,
      postCommitBinder: options.remoteActorBinder === undefined
        ? undefined
        : new ZLinkPostCommitActorBinder({
            bind: (actorRef, force) => options.remoteActorBinder!(actorRef, undefined, force),
            reportError: options.postCommitErrorReporter,
            signal: options.shutdownSignal
          }),
      completionTableProvider: options.completionTableProvider,
      sourceTransfer: options.sourceTransfer,
      messageSerializers: options.messageSerializers,
      postCommitErrorReporter: options.postCommitErrorReporter,
      entrySpotIdProvider: options.entrySpotIdProvider,
      remoteActivationWaiter: async (actorId, targetNodeRid, timeoutMs, signal) => {
        const deadline = Date.now() + Math.min(timeoutMs ?? 10_000, 10_000);
        for (;;) {
          throwIfAborted(signal);
          const resolver = options.actorLocationResolver?.();
          if (resolver === undefined) {
            return undefined;
          }
          const actorRef = await resolver.resolveActorRef(actorId, signal);
          if (actorRef !== undefined && routingIdsEqual(actorRef.nodeRid, targetNodeRid)) {
            return actorRef;
          }
          if (Date.now() >= deadline) {
            throw new ZLinkFrameworkException(
              ZLinkFrameworkErrorKind.ActorRouteNotFound,
              `Actor '${actorId}' target activation was not published before the join deadline.`
            );
          }
          await new Promise<void>((resolve) => setTimeout(resolve, 10));
        }
      }
    });
  }

  async joinSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    spotId: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined,
    completionOperationId?: import('../../contracts').ZLinkActorJoinOperationId
  ): Promise<ZLinkActorJoinRuntimeResult<Message>> {
    throwIfAborted(signal);
    const node = this.node();
    const actorRef = state.nativeActorRef ?? lookupNativeActorRef(node, actor.context.actorId) ?? node.createActor(actor.context.actorId);
    state.setNativeActorRef(actorRef as never);
    const target = await this.options.spotRouteResolver?.resolve(spotId, signal);
    installResolvedSpotRoute(node, target);
    return await this.localJoin.joinSpot(
      node,
      actor,
      state,
      actorRef,
      spotId,
      target,
      request,
      timeoutMs ?? this.options.actorTransferTimeoutMs,
      signal,
      completionOperationId
    );
  }

  async joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId | undefined,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined,
    _completionOperationId?: import('../../contracts').ZLinkActorJoinOperationId
  ): Promise<ZLinkActorJoinRuntimeResult<Message>> {
    throwIfAborted(signal);
    const node = this.node();
    const actorRef = state.nativeActorRef ?? lookupNativeActorRef(node, actor.context.actorId) ?? node.createActor(actor.context.actorId);
    state.setNativeActorRef(actorRef as never);
    const selectedNodeRid = nodeRid ?? state.entryNodeRid ?? toFrameworkRoutingId(node.status().routingId);
    return await this.localJoin.joinEntrySpot(
      node,
      actor,
      state,
      actorRef,
      selectedNodeRid,
      request,
      timeoutMs ?? this.options.actorTransferTimeoutMs
    );
  }

  private node(): ZLinkBackendMeshNode {
    return typeof this.options.node === 'function'
      ? this.options.node()
      : this.options.node;
  }
}

function installResolvedSpotRoute(
  node: ZLinkBackendMeshNode,
  target: Awaited<ReturnType<ZLinkSpotRouteResolver['resolve']>> | undefined
): void {
  if (
    target?.targetSpotGeneration === undefined
    || target.targetNodeGeneration === undefined
    || target.authorityOwnerGeneration === undefined
    || target.ownerLeaseGeneration === undefined
    || target.authorityStoreVersion === undefined
  ) {
    return;
  }
  node.rememberSpotRoute?.({
    spot: {
      spotId: String(target.spotId),
      generation: target.targetSpotGeneration
    },
    targetNodeRid: String(target.targetNodeRid),
    targetNodeGeneration: target.targetNodeGeneration,
    authorityOwnerGeneration: target.authorityOwnerGeneration,
    ownerLeaseGeneration: target.ownerLeaseGeneration,
    storeVersion: target.authorityStoreVersion
  });
}
