export class InMemoryActorSpotStore {
  private static readonly spotByActor = new Map<string, string>();

  static record(actorId: string, spotRid: string): void {
    this.spotByActor.set(actorId, spotRid);
  }

  static find(actorId: string): string | undefined {
    return this.spotByActor.get(actorId);
  }
}
