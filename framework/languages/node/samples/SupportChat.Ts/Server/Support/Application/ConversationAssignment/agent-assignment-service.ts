import { Inject } from '@nestjs/common';
import { AgentAvailabilityDirectory } from './agent-availability-directory';
import type { AgentAvailabilityDirectory as AgentAvailabilityDirectoryType, AvailableAgent } from './agent-availability-directory';

// AgentAssignmentService selects a waiting agent for a conversation and returns the
// assignment result. The actual conversation join is performed by the infrastructure layer.
class AgentAssignmentService {
  constructor(private readonly availability: AgentAvailabilityDirectoryType) {}

  assignNextAgent(): AvailableAgent | null {
    return this.availability.takeNext();
  }
}

Inject(AgentAvailabilityDirectory)(AgentAssignmentService, undefined, 0);

export { AgentAssignmentService };
