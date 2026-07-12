import type { RoutingId, SpotRef } from '../Common';
import type {
  ZLinkPeerLocation,
  ZLinkPeerLocationFilter,
} from './Models';

export interface ZLinkPeerLocationResolver {
  listLivePeers(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]>;
}

export interface ZLinkSpotRefResolver {
  resolveSpotRef(
    spotRid: RoutingId,
    signal?: AbortSignal
  ): Promise<SpotRef | undefined>;
}

export interface IZLinkActorAddressResolver {
  resolveActorSpotRef(
    actorId: string,
    signal?: AbortSignal
  ): Promise<SpotRef | undefined>;
}
