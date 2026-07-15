import type { RoutingId } from '../../contracts/Common';
import {
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  zlinkDefaultLocationOptions,
  type ZLinkPeerLocationResolver,
  type ZLinkLocationOptions,
  type ZLinkPeerLocation
} from '../../contracts/Locations';
import { ZLinkLocationKeyCodec } from './key-codec';
import {
  ZLinkAutoConnectPlanner
} from './auto-connect-planner';
import type {
  IZLinkAutoConnectExecutor,
  IZLinkAutoConnectPeerPublisher,
  ZLinkAutoConnectEventSink,
  ZLinkAutoConnectLocal,
  ZLinkAutoConnectTarget
} from './auto-connect-types';

const encodeRoutingIdHex = ZLinkLocationKeyCodec.encodeRoutingIdHex;

export interface ZLinkAutoConnectReconcilerOptions {
  readonly local: ZLinkAutoConnectLocal;
  readonly localRow?: ZLinkPeerLocation;
  readonly runtime: IZLinkAutoConnectPeerPublisher;
  readonly peerResolver: ZLinkPeerLocationResolver;
  readonly executor: IZLinkAutoConnectExecutor;
  readonly reconcilePeers?: boolean;
  readonly events?: ZLinkAutoConnectEventSink;
  readonly options?: ZLinkLocationOptions;
  readonly monotonicNowMs?: () => number;
}

export class ZLinkAutoConnectReconciler {
  private readonly local: ZLinkAutoConnectLocal;
  private readonly localRow?: ZLinkPeerLocation;
  private readonly runtime: IZLinkAutoConnectPeerPublisher;
  private readonly peerResolver: ZLinkPeerLocationResolver;
  private readonly executor: IZLinkAutoConnectExecutor;
  private readonly reconcilePeers: boolean;
  private readonly events?: ZLinkAutoConnectEventSink;
  private readonly options: Required<ZLinkLocationOptions>;
  private readonly monotonicNowMs: () => number;
  private readonly active = new Map<string, ZLinkAutoConnectTarget>();
  private readonly failedEndpoints = new Set<string>();
  private localGeneration = 0n;
  private localPublished = false;
  private storeFailedValue = false;
  private storeFailureStartedAtMs: number | undefined;
  private recoveryDeferUntilMs = 0;
  private lastDesired = new Map<string, ZLinkAutoConnectTarget>();
  private meshMemberRidHexes?: ReadonlySet<string>;

  constructor(options: ZLinkAutoConnectReconcilerOptions) {
    this.local = options.local;
    this.localRow = options.localRow;
    this.runtime = options.runtime;
    this.peerResolver = options.peerResolver;
    this.executor = options.executor;
    this.reconcilePeers = options.reconcilePeers ?? true;
    this.events = options.events;
    this.options = { ...zlinkDefaultLocationOptions, ...options.options };
    this.monotonicNowMs = options.monotonicNowMs ?? (() => performance.now());
    this.executor.onDisconnected?.((endpoint) => this.recordDisconnectedEndpoint(endpoint));
  }

  get storeFailed(): boolean {
    return this.storeFailedValue;
  }

  get activeTargets(): readonly ZLinkAutoConnectTarget[] {
    return [...this.active.values()];
  }

  knowsPeer(nodeRid: RoutingId): boolean | undefined {
    if (this.meshMemberRidHexes === undefined) {
      return undefined;
    }
    return this.meshMemberRidHexes.has(encodeRoutingIdHex(nodeRid));
  }

  async tick(signal?: AbortSignal): Promise<void> {
    try {
      await this.publishLocal(signal);
    } catch {
      this.recordStoreFailure();
      return;
    }

    if (!this.reconcilePeers) {
      this.storeFailedValue = false;
      this.storeFailureStartedAtMs = undefined;
      return;
    }

    let rows: readonly ZLinkPeerLocation[];
    try {
      rows = await this.peerResolver.listLivePeers({
        autoConnectType: this.local.autoConnectType,
        meshName: this.local.meshName
      }, signal);
    } catch {
      this.recordStoreFailure();
      return;
    }

    if (this.storeFailedValue) {
      this.storeFailedValue = false;
      this.storeFailureStartedAtMs = undefined;
      this.recoveryDeferUntilMs = this.monotonicNowMs() + this.options.heartbeatIntervalMs;
    }

    this.meshMemberRidHexes = new Set(rows
      .map((row) => row.nodeRid)
      .filter((nodeRid): nodeRid is RoutingId => nodeRid !== undefined)
      .map((nodeRid) => encodeRoutingIdHex(nodeRid)));

    const desired = new Map(ZLinkAutoConnectPlanner.computeDesired(this.local, rows));
    const desiredEndpoints = new Set([...desired.values()].map((target) => target.endpoint));
    for (const endpoint of this.failedEndpoints) {
      if (!desiredEndpoints.has(endpoint)) {
        this.failedEndpoints.delete(endpoint);
      }
    }
    const existingTargets = ZLinkAutoConnectPlanner.computeDesired(this.local, rows, true);
    for (const [key, target] of existingTargets) {
      if (this.active.has(key)) desired.set(key, target);
    }
    this.lastDesired = new Map(desired);
    const connectedEndpoints: string[] = [];
    const disconnectedEndpoints: string[] = [];
    for (const [key, target] of desired) {
      const current = this.active.get(key);
      if (current === undefined) {
        const connected = this.executor.connect(target);
        if (connected) {
          connectedEndpoints.push(target.endpoint);
          this.active.set(key, target);
          this.failedEndpoints.delete(target.endpoint);
        }
        continue;
      }

      if (current.endpoint !== target.endpoint || current.ownerId !== target.ownerId) {
        this.executor.disconnect(current);
        const connected = this.executor.connect(target);
        disconnectedEndpoints.push(current.endpoint);
        this.active.delete(key);
        if (connected) {
          connectedEndpoints.push(target.endpoint);
          this.active.set(key, target);
          this.failedEndpoints.delete(target.endpoint);
        }
      }
    }

    if (this.monotonicNowMs() < this.recoveryDeferUntilMs) {
      this.publishDesiredSetChange(connectedEndpoints, disconnectedEndpoints);
      return;
    }

    for (const [key, target] of [...this.active]) {
      if (!desired.has(key)) {
        this.executor.disconnect(target);
        disconnectedEndpoints.push(target.endpoint);
        this.active.delete(key);
      }
    }

    this.publishDesiredSetChange(connectedEndpoints, disconnectedEndpoints);
  }

  async shutdown(signal?: AbortSignal): Promise<void> {
    if (this.localPublished && this.localRow !== undefined) {
      await this.runtime.removePeer({
        autoConnectType: this.localRow.autoConnectType,
        meshName: this.localRow.meshName,
        role: this.localRow.role,
        nodeRid: this.localRow.nodeRid,
        endpoint: this.localRow.endpoint
      }, this.localGeneration, signal);
      this.localPublished = false;
    }

    for (const target of this.active.values()) {
      this.executor.disconnect(target);
    }
    this.active.clear();
  }

  private async publishLocal(signal?: AbortSignal): Promise<void> {
    if (this.localRow === undefined || this.localPublished) {
      return;
    }

    const claimed = await this.runtime.writePeer(this.localRow, ZLinkLocationWriteIntent.NewClaim, signal);
    if (claimed.status === ZLinkLocationWriteStatus.Stored) {
      this.localGeneration = claimed.generation;
      this.localPublished = true;
      return;
    }

    if (claimed.status === ZLinkLocationWriteStatus.RejectedConflict && this.localGeneration > 0n) {
      const renewed = await this.runtime.writePeer({
        ...this.localRow,
        generation: this.localGeneration
      }, ZLinkLocationWriteIntent.Renew, signal);
      this.localPublished = renewed.status === ZLinkLocationWriteStatus.Stored;
    }
  }

  private recordStoreFailure(): void {
    const nowMs = this.monotonicNowMs();
    this.storeFailureStartedAtMs ??= nowMs;
    this.storeFailedValue = true;
    this.localPublished = false;
    if (
      this.options.storeFailureGraceMs <= 0 ||
      nowMs - this.storeFailureStartedAtMs > this.options.storeFailureGraceMs
    ) {
      return;
    }
    for (const [key, target] of this.lastDesired) {
      if (!this.active.has(key) && !this.failedEndpoints.has(target.endpoint) && this.executor.connect(target)) {
        this.active.set(key, target);
      }
    }
  }

  private recordDisconnectedEndpoint(endpoint: string): void {
    let removed = false;
    for (const [key, target] of this.active) {
      if (target.endpoint === endpoint) {
        this.active.delete(key);
        removed = true;
      }
    }
    if (removed) {
      this.failedEndpoints.add(endpoint);
    }
  }

  private publishDesiredSetChange(
    connectedEndpoints: readonly string[],
    disconnectedEndpoints: readonly string[]
  ): void {
    if (connectedEndpoints.length === 0 && disconnectedEndpoints.length === 0) {
      return;
    }
    this.events?.desiredSetChanged({
      autoConnectType: this.local.autoConnectType,
      meshName: this.local.meshName,
      connectedEndpoints,
      disconnectedEndpoints
    });
  }
}
