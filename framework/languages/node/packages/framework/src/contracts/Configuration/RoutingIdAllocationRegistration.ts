import type { ZLinkFrameworkRegistration, ZLinkRoutingIdAllocationOptions } from './RegistrationTypes';

export interface ZLinkRoutingIdAllocationMemberRegistration {
  readonly groupName: string;
  readonly memberName: string;
  readonly routingIdPrefix: string;
  readonly slotCount: number;
  readonly fixedRoutingId?: string;
  readonly explicitEntrySpotRoutingId: boolean;
  readonly hasBindableRole: boolean;
  apply(routingId: string): void;
}

export function collectRoutingIdAllocationMembers(
  registration: ZLinkFrameworkRegistration
): readonly ZLinkRoutingIdAllocationMemberRegistration[] {
  const members: ZLinkRoutingIdAllocationMemberRegistration[] = [];
  for (const [name, channel] of registration.channels) {
    if (channel.routingIdAllocation === undefined) continue;
    members.push(member(
      name,
      channel.routingIdAllocation,
      channel.routingId ?? channel.server?.routingId,
      false,
      channel.server !== undefined || channel.client !== undefined
        || channel.publisher !== undefined || channel.subscriber !== undefined,
      (routingId) => {
        const mutable = channel as MutableChannelIdentity;
        mutable.routingId = routingId;
        if (mutable.server !== undefined) mutable.server.routingId = routingId;
      }
    ));
  }
  for (const [name, route] of registration.routeChannelOptions) {
    if (route.routingIdAllocation === undefined) continue;
    members.push(member(
      name,
      route.routingIdAllocation,
      route.routingId,
      false,
      route.bind !== undefined || (route.manualConnections?.length ?? 0) > 0,
      (routingId) => { (route as MutableRouteIdentity).routingId = routingId; }
    ));
  }
  for (const [name, spot] of registration.spotNodes) {
    if (spot.routingIdAllocation === undefined) continue;
    members.push(member(
      name,
      spot.routingIdAllocation,
      spot.routingId ?? spot.router?.routingId ?? spot.pubSub?.routingId,
      spot.entrySpot?.routingId !== undefined,
      spot.router !== undefined || spot.pubSub !== undefined,
      (routingId) => {
        const mutable = spot as MutableSpotIdentity;
        mutable.routingId = routingId;
        if (mutable.router !== undefined) mutable.router.routingId = routingId;
        if (mutable.pubSub !== undefined) mutable.pubSub.routingId = routingId;
        mutable.entrySpot = { ...(mutable.entrySpot ?? {}), routingId };
      }
    ));
  }
  return members;
}

function member(
  memberName: string,
  allocation: ZLinkRoutingIdAllocationOptions,
  fixedRoutingId: string | undefined,
  explicitEntrySpotRoutingId: boolean,
  hasBindableRole: boolean,
  apply: (routingId: string) => void
): ZLinkRoutingIdAllocationMemberRegistration {
  return {
    groupName: allocation.groupName ?? memberName,
    memberName,
    routingIdPrefix: allocation.routingIdPrefix,
    slotCount: allocation.slotCount,
    fixedRoutingId,
    explicitEntrySpotRoutingId,
    hasBindableRole,
    apply
  };
}

interface MutableChannelIdentity {
  routingId?: string;
  server?: { routingId?: string };
}

interface MutableRouteIdentity {
  routingId?: string;
}

interface MutableSpotIdentity {
  routingId?: string;
  router?: { routingId?: string };
  pubSub?: { routingId?: string };
  entrySpot?: { routingId?: string };
}
