import type { RoutingId } from '../Common';
import type {
  ZLinkPeerLocation,
  ZLinkPeerLocationFilter,
  ZLinkSpotAddress,
} from './Models';

export interface IZLinkPeerLocationResolver {
  listLivePeers(filter: ZLinkPeerLocationFilter, signal?: AbortSignal): Promise<readonly ZLinkPeerLocation[]>;
}

export interface IZLinkSpotAddressResolver {
  resolveSpotAddress(
    spotRid: RoutingId,
    signal?: AbortSignal
  ): Promise<ZLinkSpotAddress | undefined>;
}

export interface IZLinkActorAddressResolver {
  resolveActorSpotAddress(
    actorId: string,
    signal?: AbortSignal
  ): Promise<ZLinkSpotAddress | undefined>;
}
