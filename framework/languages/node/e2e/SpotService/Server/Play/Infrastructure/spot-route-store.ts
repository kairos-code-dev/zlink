import { Injectable } from '@nestjs/common';
import { ZLinkSpotKind } from '@zlink-systems/framework';
import type {
  RoutingId,
  ZLinkSpotRemoteAddress
} from '@zlink-systems/framework';
import { SpotServiceNames } from '../../../Shared/messages';

interface SpotRoute {
  readonly spotRid: string;
  readonly ownerNodeRid: string;
}

@Injectable()
export class InMemorySpotRouteStore {
  private static readonly routes = new Map<string, SpotRoute>();

  static recordUserSpot(spotRid: string, ownerNodeRid: string): void {
    this.routes.set(spotRid, { spotRid, ownerNodeRid });
  }

  async resolve(spotRid: RoutingId): Promise<ZLinkSpotRemoteAddress> {
    const route = InMemorySpotRouteStore.routes.get(String(spotRid));
    if (route === undefined) {
      throw new Error(`Spot route not found. spotRid=${String(spotRid)}`);
    }
    const routerChannelId = route.ownerNodeRid === 'play-b'
      ? SpotServiceNames.externalSpotChannelB
      : SpotServiceNames.externalSpotChannel;
    return {
      routerChannelId,
      targetNodeRid: route.ownerNodeRid,
      spotRid: route.spotRid,
      spotKind: ZLinkSpotKind.User
    };
  }
}
