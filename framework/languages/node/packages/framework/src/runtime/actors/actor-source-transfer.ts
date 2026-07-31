import type { ActorRef, ZLinkActor, ZLinkMessage } from '../../contracts';
import type { ZLinkSpotRouteTarget } from '../spots/spot-routing-internal';
import type { ZLinkActorRuntimeState } from './actor-runtime-state';
import type { ZLinkActorHandoffPacket, ZLinkActorHandoffResult } from './actor-handoff';

export interface ZLinkPreparedActorSource {
  readonly adapterKey?: string;
  readonly state: ZLinkMessage;
  readonly stateReference?: string;
  readonly stateChecksumCrc32c?: number;
  readonly handoffBacklog: readonly ZLinkActorHandoffPacket[];
  readonly sourceLeaveCompletion?: Promise<void>;
  reserveTarget(target: ZLinkSpotRouteTarget, signal?: AbortSignal): Promise<void>;
  commitAuthority(
    target: ZLinkSpotRouteTarget,
    targetActorRef: ActorRef,
    signal?: AbortSignal
  ): Promise<void>;
  commit(
    target: ZLinkSpotRouteTarget,
    targetActorRef: ActorRef,
    results: readonly ZLinkActorHandoffResult[],
    releaseLocation?: boolean
  ): void;
  rollback(): Promise<void>;
}

export interface ZLinkActorSourceTransfer {
  prepareSource(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    signal?: AbortSignal,
    lifecycleAuthority?: 'framework' | 'core'
  ): Promise<ZLinkPreparedActorSource>;
}
