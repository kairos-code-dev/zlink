import {
  zlinkRuntimeDefaultLocationOptions,
  type ZLinkLocationOptionOverrides
} from '../../contracts/Locations/Options';
import {
  type ZLinkOwnerLeaseStore,
  type ZLinkOwnerLease
} from '../../contracts/Locations';

export interface ZLinkOwnerLeaseTrackerOptions {
  readonly store: ZLinkOwnerLeaseStore;
  readonly options?: ZLinkLocationOptionOverrides;
  readonly monotonicNowMs?: () => number;
}

interface OwnerLeaseTrackerSnapshot {
  readonly leases: ReadonlyMap<string, ZLinkOwnerLease>;
  readonly storeNow: Date;
  readonly fetchedAtMs: number;
}

export class ZLinkOwnerLeaseTracker {
  private readonly store: ZLinkOwnerLeaseStore;
  private readonly options: Required<ZLinkLocationOptionOverrides>;
  private readonly monotonicNowMs: () => number;
  private snapshot?: OwnerLeaseTrackerSnapshot;
  private refresh?: Promise<OwnerLeaseTrackerSnapshot>;
  private liveOwnerFingerprint?: string;
  private liveOwnerVersion = 0;

  constructor(options: ZLinkOwnerLeaseTrackerOptions) {
    this.store = options.store;
    this.options = { ...zlinkRuntimeDefaultLocationOptions, ...options.options };
    this.monotonicNowMs = options.monotonicNowMs ?? (() => performance.now());
  }

  async isOwnerLive(ownerId: string, signal?: AbortSignal): Promise<boolean> {
    let snapshot = await this.getSnapshot(signal);
    let lease = snapshot.leases.get(ownerId);
    if (lease === undefined || this.remainingLeaseMs(lease, snapshot) <= 0) {
      snapshot = await this.refreshAfterOwnerMissOrExpiry(snapshot, signal);
      lease = snapshot.leases.get(ownerId);
      if (lease === undefined) {
        return false;
      }
    }
    return this.remainingLeaseMs(lease, snapshot) > 0;
  }

  async getLiveOwnerSetVersion(signal?: AbortSignal): Promise<number> {
    const snapshot = await this.getSnapshot(signal);
    const live = [...snapshot.leases.values()]
      .filter((lease) => this.remainingLeaseMs(lease, snapshot) > 0)
      .map((lease) => lease.ownerId)
      .sort()
      .join('\n');
    if (live !== this.liveOwnerFingerprint) {
      this.liveOwnerFingerprint = live;
      this.liveOwnerVersion++;
    }
    return this.liveOwnerVersion;
  }

  private async getSnapshot(signal?: AbortSignal): Promise<OwnerLeaseTrackerSnapshot> {
    const current = this.snapshot;
    if (current !== undefined && this.monotonicNowMs() - current.fetchedAtMs < this.options.pollingIntervalMs) {
      return current;
    }

    if (this.refresh !== undefined) {
      return this.refresh;
    }

    this.refresh = this.refreshSnapshot(signal);
    try {
      return await this.refresh;
    } finally {
      this.refresh = undefined;
    }
  }

  private async refreshAfterOwnerMissOrExpiry(
    observed: OwnerLeaseTrackerSnapshot,
    signal?: AbortSignal
  ): Promise<OwnerLeaseTrackerSnapshot> {
    const current = this.snapshot;
    if (current !== undefined && current !== observed) {
      return current;
    }
    if (this.refresh !== undefined) {
      return this.refresh;
    }

    this.refresh = this.refreshSnapshot(signal);
    try {
      return await this.refresh;
    } finally {
      this.refresh = undefined;
    }
  }

  private async refreshSnapshot(signal?: AbortSignal): Promise<OwnerLeaseTrackerSnapshot> {
    const listed = await this.store.listOwnerLeases(signal);
    const snapshot = {
      leases: new Map(listed.leases.map((lease) => [lease.ownerId, lease])),
      storeNow: listed.storeNow,
      fetchedAtMs: this.monotonicNowMs()
    };
    this.snapshot = snapshot;
    return snapshot;
  }

  private remainingLeaseMs(lease: ZLinkOwnerLease, snapshot: OwnerLeaseTrackerSnapshot): number {
    const elapsedMs = this.monotonicNowMs() - snapshot.fetchedAtMs;
    return lease.leaseExpiresAt.getTime() - snapshot.storeNow.getTime() - elapsedMs;
  }
}

export class ZLinkLiveRowFilter {
  constructor(private readonly leaseTracker: ZLinkOwnerLeaseTracker) {}

  async filter<TRow>(
    rows: readonly TRow[],
    ownerIdOf: (row: TRow) => string,
    signal?: AbortSignal,
    include?: (row: TRow) => boolean
  ): Promise<TRow[]> {
    const live: TRow[] = [];
    for (const row of rows) {
      if ((include === undefined || include(row)) && await this.isLive(row, ownerIdOf, signal)) {
        live.push(row);
      }
    }
    return live;
  }

  async resolve<TRow>(
    row: TRow | undefined,
    ownerIdOf: (row: TRow) => string,
    signal?: AbortSignal,
    include?: (row: TRow) => boolean
  ): Promise<TRow | undefined> {
    if (row === undefined || (include !== undefined && !include(row))) {
      return undefined;
    }
    return await this.isLive(row, ownerIdOf, signal) ? row : undefined;
  }

  private async isLive<TRow>(
    row: TRow,
    ownerIdOf: (row: TRow) => string,
    signal?: AbortSignal
  ): Promise<boolean> {
    return await this.leaseTracker.isOwnerLive(ownerIdOf(row), signal);
  }
}
