import type { ZLinkRuntimeEvent } from './Contracts';

export type ZLinkSpotDrainPolicy = 'DrainNatural' | 'ReleaseAndRecreate';
export type ZLinkDrainForceReason =
  | 'DeadlineExceeded'
  | 'DrainingStatePublishFailed'
  | 'OwnerCleanupFailed'
  | 'TeardownFailed';
export type ZLinkDrainResult =
  | { readonly kind: 'drained' }
  | { readonly kind: 'force-stopped'; readonly reason: ZLinkDrainForceReason };
export type ZLinkDrainState = 'Serving' | 'Draining' | 'Drained' | 'ForceStopping';

export interface ZLinkDrainEvent extends ZLinkRuntimeEvent {
  readonly state: ZLinkDrainState;
  readonly result?: ZLinkDrainResult;
}

export interface ZLinkDrainControl {
  drain(deadlineMs?: number, signal?: AbortSignal): Promise<ZLinkDrainResult>;
  awaitDrained(signal?: AbortSignal): Promise<ZLinkDrainResult>;
  isReady(): boolean;
}
