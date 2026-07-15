import { ZLINK_ALLOCATED_ROUTING_ID_PROVIDER } from '@zlink-systems/nestjs';
import type { ZLinkAllocatedRoutingIdProvider } from '@zlink-systems/framework';

type ApplicationContext = {
  get<T>(token: unknown, options?: { strict?: boolean }): T;
};

async function reportBingoRoutingId(
  app: ApplicationContext,
  role: string,
  groupName: string,
  members: readonly string[]
): Promise<void> {
  const provider = app.get<ZLinkAllocatedRoutingIdProvider>(
    ZLINK_ALLOCATED_ROUTING_ID_PROVIDER,
    { strict: false }
  );
  const allocation = await provider.waitForReadyAllocation(groupName);
  const allocatedMembers = members.map((member) => {
    const routingId = allocation.memberRoutingIds.get(member);
    if (routingId === undefined) {
      throw new Error(`Bingo allocation group '${groupName}' did not allocate member '${member}'.`);
    }
    return [member, routingId] as const;
  });
  if (new Set(allocatedMembers.map(([, routingId]) => routingId)).size !== 1) {
    throw new Error(`Bingo allocation group '${groupName}' did not assign one shared routing id.`);
  }
  console.log(
    `bingo routing allocation ready role=${role} group=${groupName} slot=${allocation.slot} `
      + `members=${allocatedMembers.map(([member, routingId]) => `${member}=${routingId}`).join(',')}`
  );
}

export { reportBingoRoutingId };
