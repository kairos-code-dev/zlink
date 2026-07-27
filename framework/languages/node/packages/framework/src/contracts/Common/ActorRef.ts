import type { RoutingId } from './CoreTypes';

export interface ActorRef {
  readonly actorId: string;
  readonly objectGeneration: bigint;
  readonly meshName: string;
  readonly nodeRid: RoutingId;
}
