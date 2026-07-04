import { AgentAvailabilityDirectory } from './agent-availability-directory';

class AgentAssignmentService {
  constructor(private readonly agents: AgentAvailabilityDirectory) {}

  assignNextAgent(): string | undefined {
    return this.agents.firstAvailable();
  }
}

export { AgentAssignmentService };
