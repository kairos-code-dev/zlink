interface SpotRoute {
  readonly spotRid: string;
  readonly ownerNodeRid: string;
}

export class InMemorySpotRouteStore {
  private static readonly routes = new Map<string, SpotRoute>();

  static recordUserSpot(spotRid: string, ownerNodeRid: string): void {
    this.routes.set(spotRid, { spotRid, ownerNodeRid });
  }
}
