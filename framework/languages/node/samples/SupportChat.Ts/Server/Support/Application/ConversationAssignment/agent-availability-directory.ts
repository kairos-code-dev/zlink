class AgentAvailabilityDirectory {
  private readonly available = new Set<string>();

  setAvailable(agentActorId: string, isAvailable: boolean): boolean {
    if (isAvailable) {
      this.available.add(agentActorId);
    } else {
      this.available.delete(agentActorId);
    }
    return this.available.has(agentActorId);
  }

  firstAvailable(): string | undefined {
    return [...this.available][0];
  }
}

export { AgentAvailabilityDirectory };
