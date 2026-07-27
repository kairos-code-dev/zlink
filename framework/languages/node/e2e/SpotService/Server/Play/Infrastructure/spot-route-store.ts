interface SpotRoute {
  readonly spotId: string;
  readonly ownerNodeRid: string;
}

export class InMemorySpotRouteStore {
  private static readonly routes = new Map<string, SpotRoute>();

  static recordUserSpot(spotId: string, ownerNodeRid: string): void {
    this.routes.set(spotId, { spotId, ownerNodeRid });
  }
}
