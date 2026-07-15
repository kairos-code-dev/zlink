import type { RoutingId } from '../Common';
import type { ZLinkLocationOwnerToken } from './Writes';

export interface ZLinkRoutingIdSlotAllocationMember {
  readonly channelName: string;
  readonly routingIdPrefix: string;
}

export interface ZLinkRoutingIdSlotAcquireRequest {
  readonly groupName: string;
  readonly members: readonly ZLinkRoutingIdSlotAllocationMember[];
  readonly slotCount: number;
  readonly ownerId: string;
  readonly leaseTtlMs: number;
}

export interface ZLinkRoutingIdSlotAllocation {
  readonly slot: number;
  readonly owner: ZLinkLocationOwnerToken;
  readonly leaseExpiresAt: Date;
  readonly storeNow: Date;
}

export type ZLinkRoutingIdSlotAcquireResult =
  | { readonly kind: 'acquired'; readonly allocation: ZLinkRoutingIdSlotAllocation }
  | { readonly kind: 'groupExhausted' }
  | {
      readonly kind: 'groupConfigurationMismatch';
      readonly expectedMembers: readonly ZLinkRoutingIdSlotAllocationMember[];
      readonly expectedSlotCount: number;
      readonly actualMembers: readonly ZLinkRoutingIdSlotAllocationMember[];
      readonly actualSlotCount: number;
    }
  | { readonly kind: 'identityModeConflict' };

export type ZLinkRoutingIdSlotReleaseResult = 'released' | 'ignoredStale';

export interface ZLinkRoutingIdSlotAllocationSnapshot {
  readonly groupName: string;
  readonly members: readonly ZLinkRoutingIdSlotAllocationMember[];
  readonly slotCount: number;
  readonly allocations: readonly ZLinkRoutingIdSlotAllocation[];
  readonly storeNow: Date;
}

export interface ZLinkRoutingIdSlotAllocationStore {
  acquireRoutingIdSlot(
    request: ZLinkRoutingIdSlotAcquireRequest,
    signal?: AbortSignal
  ): Promise<ZLinkRoutingIdSlotAcquireResult>;
  releaseRoutingIdSlot(
    groupName: string,
    slot: number,
    owner: ZLinkLocationOwnerToken,
    signal?: AbortSignal
  ): Promise<ZLinkRoutingIdSlotReleaseResult>;
  listRoutingIdSlots(
    groupName: string,
    signal?: AbortSignal
  ): Promise<ZLinkRoutingIdSlotAllocationSnapshot>;
}

export interface ZLinkAllocatedRoutingId {
  readonly groupName: string;
  readonly slot: number;
  readonly memberRoutingIds: ReadonlyMap<string, RoutingId>;
}

export interface ZLinkAllocatedRoutingIdProvider {
  waitForReadyAllocation(groupName: string, signal?: AbortSignal): Promise<ZLinkAllocatedRoutingId>;
}

export const ZLINK_ALLOCATED_ROUTING_ID_PROVIDER = Symbol.for(
  '@zlink-systems/framework:allocated-routing-id-provider'
);
