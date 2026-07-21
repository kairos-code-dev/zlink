import {
  zlinkRuntimeDefaultLocationOptions,
  type ZLinkLocationOptionOverrides
} from '../../contracts/Locations/Options';
import {
  ZLinkLocationKind,
  type ZLinkLocationChangeStampStore,
  type ZLinkLocationWatchStore,
  type ZLinkLocationChangeStampScope
} from '../../contracts/Locations';
import type { ZLinkOwnerLeaseTracker } from './lease-tracker';
import type { ZLinkAutoConnectLocal } from './auto-connect-types';
import type { ZLinkAutoConnectReconciler } from './auto-connect-reconciler';

export interface ZLinkAutoConnectLoopOptions {
  readonly reconciler: ZLinkAutoConnectReconciler;
  readonly local: ZLinkAutoConnectLocal;
  readonly options?: ZLinkLocationOptionOverrides;
  readonly changeStampStore?: ZLinkLocationChangeStampStore;
  readonly watchStore?: ZLinkLocationWatchStore;
  readonly leaseTracker?: ZLinkOwnerLeaseTracker;
  readonly setTimer?: (callback: () => void, delayMs: number) => unknown;
  readonly clearTimer?: (handle: unknown) => void;
}

export class ZLinkAutoConnectLoop {
  private readonly reconciler: ZLinkAutoConnectReconciler;
  private readonly options: Required<ZLinkLocationOptionOverrides>;
  private readonly changeStampScope: ZLinkLocationChangeStampScope;
  private readonly changeStampStore?: ZLinkLocationChangeStampStore;
  private readonly watchStore?: ZLinkLocationWatchStore;
  private readonly leaseTracker?: ZLinkOwnerLeaseTracker;
  private readonly setTimer: (callback: () => void, delayMs: number) => unknown;
  private readonly clearTimer: (handle: unknown) => void;
  private controller?: AbortController;
  private timer?: unknown;
  private watchTask?: Promise<void>;
  private lastStamp?: bigint;
  private lastLiveOwnerSetVersion?: number;
  private lastTickFailed = false;

  constructor(options: ZLinkAutoConnectLoopOptions) {
    this.reconciler = options.reconciler;
    this.options = { ...zlinkRuntimeDefaultLocationOptions, ...options.options };
    this.changeStampScope = { kind: ZLinkLocationKind.Peer, meshName: options.local.meshName };
    this.changeStampStore = options.changeStampStore;
    this.watchStore = options.watchStore;
    this.leaseTracker = options.leaseTracker;
    this.setTimer = options.setTimer ?? ((callback, delayMs) => setTimeout(callback, delayMs));
    this.clearTimer = options.clearTimer ?? ((handle) => clearTimeout(handle as NodeJS.Timeout));
  }

  async start(signal?: AbortSignal): Promise<void> {
    if (this.controller !== undefined) {
      return;
    }
    await this.tick(signal);
    this.controller = new AbortController();
    this.scheduleNext();
    if (this.watchStore !== undefined) {
      this.watchTask = this.watch(this.controller.signal);
    }
  }

  async stop(signal?: AbortSignal): Promise<void> {
    await this.prepareTransportShutdown();
    await this.finishTransportShutdown(signal);
  }

  async prepareTransportShutdown(): Promise<void> {
    const controller = this.controller;
    this.controller = undefined;
    if (controller !== undefined) {
      controller.abort();
    }
    if (this.timer !== undefined) {
      this.clearTimer(this.timer);
      this.timer = undefined;
    }
    await this.watchTask?.catch(() => undefined);
    this.watchTask = undefined;
    this.reconciler.disconnectPeers();
  }

  async finishTransportShutdown(signal?: AbortSignal): Promise<void> {
    await this.reconciler.unpublishLocal(signal);
  }

  async tick(signal?: AbortSignal): Promise<void> {
    if (this.changeStampStore !== undefined && !this.lastTickFailed) {
      try {
        const stamp = await this.changeStampStore.getChangeStamp(this.changeStampScope, signal);
        const liveOwners = this.leaseTracker === undefined
          ? 0
          : await this.leaseTracker.getLiveOwnerSetVersion(signal);
        if (this.lastStamp === stamp && this.lastLiveOwnerSetVersion === liveOwners) {
          return;
        }
        const tickFailed = await this.runReconcile(signal);
        if (!tickFailed) {
          this.lastStamp = stamp;
          this.lastLiveOwnerSetVersion = liveOwners;
        }
        return;
      } catch {
        // A failed stamp read degrades to a full reconcile tick.
      }
    }

    await this.runReconcile(signal);
    this.lastStamp = undefined;
    this.lastLiveOwnerSetVersion = undefined;
  }

  private async runReconcile(signal?: AbortSignal): Promise<boolean> {
    await this.reconciler.tick(signal);
    this.lastTickFailed = this.reconciler.storeFailed;
    return this.lastTickFailed;
  }

  private scheduleNext(): void {
    if (this.controller === undefined || this.controller.signal.aborted) {
      return;
    }
    this.timer = this.setTimer(() => {
      this.timer = undefined;
      void this.tick(this.controller?.signal)
        .catch(() => undefined)
        .finally(() => this.scheduleNext());
    }, this.options.pollingIntervalMs);
  }

  private async watch(signal: AbortSignal): Promise<void> {
    while (!signal.aborted && this.watchStore !== undefined) {
      try {
        for await (const _ of this.watchStore.watch({ kind: ZLinkLocationKind.Peer, meshName: this.changeStampScope.meshName }, signal)) {
          await this.tick(signal);
        }
      } catch (error) {
        if (isAbortError(error)) {
          return;
        }
        await sleep(this.options.pollingIntervalMs, signal);
      }
    }
  }
}

function isAbortError(error: unknown): boolean {
  return error instanceof Error && error.name === 'AbortError';
}

function sleep(delayMs: number, signal: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    if (signal.aborted) {
      reject(new DOMException('The operation was aborted.', 'AbortError'));
      return;
    }
    const timer = setTimeout(resolve, delayMs);
    signal.addEventListener('abort', () => {
      clearTimeout(timer);
      reject(new DOMException('The operation was aborted.', 'AbortError'));
    }, { once: true });
  });
}
