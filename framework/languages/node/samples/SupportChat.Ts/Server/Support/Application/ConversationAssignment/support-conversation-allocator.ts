import { Conversation } from '../../Domain/SupportChat/conversation';
import { AgentAssignmentService } from './agent-assignment-service';

class SupportConversationAllocator {
  private nextId = 1;
  private readonly conversations = new Map<string, Conversation>();

  constructor(private readonly assignments: AgentAssignmentService) {}

  allocate(customerActorId: string, subject: string): Conversation {
    const conversation = new Conversation(`support-${this.nextId++}`, customerActorId, subject);
    const agent = this.assignments.assignNextAgent();
    if (agent !== undefined) {
      conversation.assign(agent);
    }
    this.conversations.set(conversation.snapshot().conversationId, conversation);
    return conversation;
  }

  get(conversationId: string): Conversation {
    const conversation = this.conversations.get(conversationId);
    if (conversation === undefined) {
      throw new Error(`Unknown conversation '${conversationId}'.`);
    }
    return conversation;
  }
}

export { SupportConversationAllocator };
