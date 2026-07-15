import { ZLINK_ALLOCATED_ROUTING_ID_PROVIDER } from '@zlink-systems/nestjs';
import type { ZLinkAllocatedRoutingIdProvider } from '@zlink-systems/framework';
import type { ZLinkAllocatedRoutingId } from '@zlink-systems/framework';

type ApplicationContext = {
  get<T>(token: unknown, options?: { strict?: boolean }): T;
};

async function reportRoutingAllocation(
  app: ApplicationContext,
  role: string,
  groupName: string,
  members: readonly string[]
): Promise<ZLinkAllocatedRoutingId> {
  const provider = app.get<ZLinkAllocatedRoutingIdProvider>(
    ZLINK_ALLOCATED_ROUTING_ID_PROVIDER,
    { strict: false }
  );
  const allocation = await provider.waitForReadyAllocation(groupName);
  const memberAllocations = members.map((member) => {
    const routingId = allocation.memberRoutingIds.get(member);
    if (routingId === undefined) {
      throw new Error(`Allocation group '${groupName}' did not allocate member '${member}'.`);
    }
    return [member, routingId] as const;
  });
  if (new Set(memberAllocations.map(([, routingId]) => routingId)).size !== 1) {
    throw new Error(`Allocation group '${groupName}' did not assign one shared routing id.`);
  }
  console.log(
    `zoneworld routing allocation ready role=${role} group=${groupName} slot=${allocation.slot} `
      + `members=${memberAllocations.map(([member, rid]) => `${member}=${rid}`).join(',')}`
  );
  return allocation;
}

export { reportRoutingAllocation };
