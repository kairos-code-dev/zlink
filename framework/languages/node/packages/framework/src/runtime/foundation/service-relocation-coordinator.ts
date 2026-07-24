import type {
  ZLinkAuthorityKey,
  ZLinkAuthoritySnapshot,
  ZLinkLocationOwnerToken,
  ZLinkRelocationCapacityFence
} from '../../contracts/Locations';
import {
  ServiceDurableRelocationRuntime,
  type ServiceRelocationEnvelope
} from './service-relocation-runtime';

export interface ServiceRelocationStaging {
  readonly id: string;
}

export interface ServiceRelocationRestoreOwner<TStaging extends ServiceRelocationStaging> {
  prepare(
    envelope: ServiceRelocationEnvelope,
    signal?: AbortSignal
  ): Promise<TStaging>;
  commit(
    staging: TStaging,
    authority: ZLinkAuthoritySnapshot,
    signal?: AbortSignal
  ): Promise<void>;
  abort(staging: TStaging): Promise<void> | void;
}

export interface ServiceRelocationRouteReplacement<TStaging extends ServiceRelocationStaging> {
  replace(
    staging: TStaging,
    authority: ZLinkAuthoritySnapshot,
    signal?: AbortSignal
  ): Promise<void>;
}

export interface ServiceCommittedRelocation<TStaging extends ServiceRelocationStaging> {
  readonly staging: TStaging;
  readonly authority: ZLinkAuthoritySnapshot;
}

export class ServiceRelocationPostCommitError extends Error {
  constructor(
    readonly authority: ZLinkAuthoritySnapshot,
    readonly cause: unknown
  ) {
    super('Relocation owner committed, but target publication or route replacement failed.');
    this.name = 'ServiceRelocationPostCommitError';
  }
}

/**
 * Materializes a relocation into a hidden target owner, commits authority once,
 * then publishes the owner and replaces routes. A failure after the authority
 * CAS is never converted into a source rollback.
 */
export class ServiceRelocationCoordinator<TStaging extends ServiceRelocationStaging> {
  constructor(
    private readonly durable: ServiceDurableRelocationRuntime,
    private readonly owner: ServiceRelocationRestoreOwner<TStaging>,
    private readonly routes: ServiceRelocationRouteReplacement<TStaging>
  ) {}

  async restoreAndCommit(
    key: ZLinkAuthorityKey,
    published: ZLinkAuthoritySnapshot,
    targetOwner: ZLinkLocationOwnerToken,
    relocationCapacityFence?: ZLinkRelocationCapacityFence,
    signal?: AbortSignal
  ): Promise<ServiceCommittedRelocation<TStaging>> {
    const envelope = await this.durable.restore(published, signal);
    const staging = await this.owner.prepare(envelope, signal);
    let committed: ZLinkAuthoritySnapshot | undefined;
    try {
      committed = await this.durable.commitOwner(
        key,
        published,
        targetOwner,
        relocationCapacityFence,
        signal
      );
      await this.owner.commit(staging, committed, signal);
      await this.routes.replace(staging, committed, signal);
      return { staging, authority: committed };
    } catch (error) {
      if (committed !== undefined) {
        throw new ServiceRelocationPostCommitError(committed, error);
      }
      await this.owner.abort(staging);
      throw error;
    }
  }

  async complete(
    key: ZLinkAuthorityKey,
    committed: ServiceCommittedRelocation<TStaging>,
    signal?: AbortSignal
  ): Promise<ZLinkAuthoritySnapshot> {
    return await this.durable.release(key, committed.authority, signal);
  }
}
