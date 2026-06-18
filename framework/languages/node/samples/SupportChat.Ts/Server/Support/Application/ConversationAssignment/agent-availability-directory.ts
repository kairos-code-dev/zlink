type AvailableAgent = {
  actorId: string;
  displayName: string;
};

// AgentAvailabilityDirectory registers, queries and removes agent actor ids that are
// available for assignment. It is a pure application service with no framework dependency.
class AgentAvailabilityDirectory {
  private readonly available: AvailableAgent[] = [];
  private readonly actorIds = new Set<string>();

  setAvailable(actorId: string, displayName: string, isAvailable: boolean): void {
    if (!isAvailable) {
      this.actorIds.delete(actorId);
      return;
    }
    if (!this.actorIds.has(actorId)) {
      this.actorIds.add(actorId);
      this.available.push({ actorId, displayName });
    }
  }

  takeNext(): AvailableAgent | null {
    while (this.available.length > 0) {
      const candidate = this.available.shift() as AvailableAgent;
      if (this.actorIds.delete(candidate.actorId)) {
        return candidate;
      }
    }
    return null;
  }
}

export { AgentAvailabilityDirectory };
export type { AvailableAgent };
