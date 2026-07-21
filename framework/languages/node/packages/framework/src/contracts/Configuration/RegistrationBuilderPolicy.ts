import type {
  Type,
  ZLinkActor,
  ZLinkActorTransferAdapter,
  ZLinkEntrySpot,
  ZLinkSpot
} from '../../contracts';
import { ZLinkConfigurationException } from './ConfigurationException';

export function registerActorTransferAdapter<TActor extends ZLinkActor>(
  adapters: Map<Type, Type>,
  actorType: Type<TActor>,
  adapterType: Type<ZLinkActorTransferAdapter<TActor>>
): void {
  if (adapters.has(actorType)) {
    throw new ZLinkConfigurationException(`Duplicate actor transfer adapter for '${actorType.name}'.`);
  }
  adapters.set(actorType, adapterType);
}

export function validateActorTransferForwardWindow(timeoutMs: number): number {
  if (!Number.isSafeInteger(timeoutMs) || timeoutMs < 0) {
    throw new ZLinkConfigurationException('actor transfer forward window must be a non-negative safe integer.');
  }
  return timeoutMs;
}

export function validateActorTransferTimeout(timeoutMs: number): number {
  if (!Number.isSafeInteger(timeoutMs) || timeoutMs <= 0) {
    throw new ZLinkConfigurationException('actor transfer timeout must be a positive safe integer.');
  }
  return timeoutMs;
}

export function registerEntrySpot(
  options: { entrySpotType?: Type<ZLinkEntrySpot> },
  entrySpotType: Type<ZLinkEntrySpot>
): void {
  if (options.entrySpotType !== undefined) {
    throw new ZLinkConfigurationException('Duplicate Entry Spot registration on SpotNode.');
  }
  options.entrySpotType = entrySpotType;
}

export function registerSpotFactory(
  options: { spotFactories?: Type<ZLinkSpot>[] },
  spotType: Type<ZLinkSpot>
): void {
  options.spotFactories ??= [];
  if (options.spotFactories.includes(spotType)) {
    throw new ZLinkConfigurationException('Duplicate SPOT factory registration on SpotNode.');
  }
  options.spotFactories.push(spotType);
}

export function registerActorFactory(
  options: { actorFactories?: Record<string, Type> },
  actorType: string,
  factoryType: Type
): void {
  const type = actorType.trim();
  if (type.length === 0 || type !== actorType) {
    throw new ZLinkConfigurationException('Actor factory type must not be empty or padded.');
  }
  options.actorFactories ??= {};
  if (Object.hasOwn(options.actorFactories, type)) {
    throw new ZLinkConfigurationException(`Duplicate actor factory '${type}' on SpotNode.`);
  }
  options.actorFactories[type] = factoryType;
}

export interface MutableZLinkRoutingIdAllocationOptions {
  slotCount: number;
  routingIdPrefix: string;
  groupName?: string;
}

export function createRoutingIdAllocation(
  slotCount: number,
  routingIdPrefix: string,
  groupName?: string
): MutableZLinkRoutingIdAllocationOptions {
  if (!Number.isInteger(slotCount) || slotCount < 1) {
    throw new ZLinkConfigurationException('Routing-id allocation slot count must be a positive integer.');
  }
  if (routingIdPrefix.trim().length === 0) {
    throw new ZLinkConfigurationException('Routing-id allocation prefix must not be empty.');
  }
  return { slotCount, routingIdPrefix, groupName };
}

export function setRoutingIdAllocationGroup(
  groupName: string,
  allocation: MutableZLinkRoutingIdAllocationOptions | undefined,
  defaultPrefix: string
): MutableZLinkRoutingIdAllocationOptions {
  if (groupName.trim().length === 0) {
    throw new ZLinkConfigurationException('Routing-id allocation group name must not be empty.');
  }
  return allocation === undefined
    ? { slotCount: 0, routingIdPrefix: defaultPrefix, groupName }
    : { ...allocation, groupName };
}

export function rejectFixedRoutingId(
  routingId: unknown,
  memberName: string
): void {
  if (routingId !== undefined) {
    throw new ZLinkConfigurationException(
      `Routing-id allocation member '${memberName}' cannot combine fixed and allocated routing ids.`
    );
  }
}

export function rejectAllocatedRoutingId(
  allocation: MutableZLinkRoutingIdAllocationOptions | undefined,
  memberName: string
): void {
  if (allocation !== undefined) {
    throw new ZLinkConfigurationException(
      `Routing-id allocation member '${memberName}' cannot combine fixed and allocated routing ids.`
    );
  }
}
