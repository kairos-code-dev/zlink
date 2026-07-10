import type { Type, ZLinkActor, ZLinkActorTransferAdapter } from '../../contracts';
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
