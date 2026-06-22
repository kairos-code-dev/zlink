import { Inject } from '@nestjs/common';

const CONVERSATION_STARTER = Symbol('CONVERSATION_STARTER');
const CONVERSATION_EXECUTOR = Symbol('CONVERSATION_EXECUTOR');

interface ConversationStarter {
  start(request: ConversationStartRequest): Promise<void>;
}

interface ConversationExecutor {
  execute<TResult>(
    conversationId: string,
    operation: (conversation: any) => TResult | Promise<TResult>
  ): Promise<TResult>;
}

interface ConversationStartRequest {
  conversationId: string;
  customerActorId: string;
  customerDisplayName: string;
  subject: string;
  createdAtUnixMs: number;
}

// SupportConversationAllocator turns a customer identity and subject into a ConversationId
// by assigning a sample-level id and initializing the conversation owner with
// the create request.
class SupportConversationAllocator {
  private nextConversationId = 1;

  constructor(
    @Inject(CONVERSATION_STARTER) private readonly starter: ConversationStarter,
    @Inject(CONVERSATION_EXECUTOR) private readonly executor: ConversationExecutor
  ) {}

  async allocate(
    customerActorId: string,
    customerDisplayName: string,
    subject: string
  ): Promise<string> {
    const conversationId = `supportchat-conversation-${this.nextConversationId++}`;
    await this.starter.start({
      conversationId,
      customerActorId,
      customerDisplayName,
      subject,
      createdAtUnixMs: Date.now()
    });
    return conversationId;
  }

  async executeInConversation<TResult>(
    conversationId: string,
    operation: (conversation: any) => TResult | Promise<TResult>
  ): Promise<TResult> {
    return await this.executor.execute(conversationId, operation);
  }
}

export { CONVERSATION_EXECUTOR, CONVERSATION_STARTER, SupportConversationAllocator };
export type { ConversationExecutor, ConversationStartRequest, ConversationStarter };
