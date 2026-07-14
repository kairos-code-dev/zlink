import type {
  RoutingId,
  ActorRef,
  Type,
  ZLinkActor,
  ZLinkActorFactory,
  ZLinkBoundSession,
  ZLinkMessage,
  ZLinkMessageSerializer,
  ZLinkProviderResolver
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkBackendSpotNode } from '../backend/contracts';
import type { ZLinkLocationLifecycle } from '../locations';
import type { ZLinkActorRuntimeState } from './actor-runtime-state';
import type { ZLinkActorTransferRegistry } from './actor-transfer-registry';

export interface ZLinkActorJoinRuntimeResult<TReply> {
  readonly accepted: boolean;
  readonly actor?: import('../../contracts').ActorRef;
  readonly reply?: TReply;
}

export interface ZLinkActorManagerOptions {
  readonly actorFactories: ReadonlyMap<string, Type | ZLinkActorFactory>;
  readonly joinCoordinator?: ZLinkActorJoinCoordinator;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly nativeActorNode?: ZLinkBackendSpotNode;
  readonly nativeActorNodeProvider?: () => ZLinkBackendSpotNode | undefined;
  readonly actorCreatedNodeRidProvider?: () => RoutingId | undefined;
  readonly actorRefResolver?: ZLinkActorRefResolver;
  readonly actorCreatedNotifier?: (
    nodeRid: RoutingId,
    actor: ZLinkActor,
    createRequest: ZLinkMessage,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly actorDestroyedCleanup?: (actorId: string) => void;
  readonly locationLifecycle?: ZLinkLocationLifecycle;
  readonly boundSessionFactory?: ZLinkActorBoundSessionFactory;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly actorTransferRegistry?: ZLinkActorTransferRegistry;
  readonly shutdownSignal?: AbortSignal;
  readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
}

export interface ZLinkActorRefResolver {
  resolveActorRef(actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined>;
}

export type ZLinkActorBoundSessionFactory = (actorId: string) => ZLinkBoundSession;

export interface ZLinkActorJoinCoordinator {
  joinSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    spotRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinRuntimeResult<Message>>;
  joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinRuntimeResult<Message>>;
}
