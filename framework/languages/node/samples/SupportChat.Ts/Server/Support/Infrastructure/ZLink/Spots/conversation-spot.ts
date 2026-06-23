import { Injectable } from '@nestjs/common';
import { Conversation } from '../../../Domain/SupportChat/conversation';
import { SupportNotificationPublisher } from '../Notifications/support-notification-publisher';
import { SupportChatRoles, joinConversationRes } from '../../../../../Shared/Contracts/messages';
import type {
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotContext
} from '@zlink-systems/framework';
import type { SupportUserActor as SupportUserActorType } from '../Actors/support-user-actor';
import type { ConversationCreateRequest } from './conversation-create-request';
import type {
  CloseConversationReq,
  CloseConversationRes,
  ConversationState,
  SendChatMessageReq,
  SendChatMessageRes,
  SetTypingReq,
  SetTypingRes
} from '../../../../../Shared/Contracts/messages';

type SupportNotificationPublisherLike = Pick<
  SupportNotificationPublisher,
  'publish' | 'publishJoinedAgentToCustomer'
>;

// ConversationSpot owns the ZLink Spot lifecycle, actor join callbacks, the idle timer and the
// connection to the notification publisher. Domain judgement (message sequence, typing, idle and
// close transitions) lives in Conversation; this adapter only translates callbacks and publishes
// the events Conversation returns.
@Injectable()
class ConversationSpot implements ZLinkSpot<SupportUserActorType> {
  private static notifications: SupportNotificationPublisherLike | null = null;
  readonly context!: ZLinkSpotContext<SupportUserActorType, ConversationSpot>;
  private conversationId = 'supportchat-conversation';
  private conversation: Conversation | null = null;
  private readonly actors = new Map<string, SupportUserActorType>();

  static useNotifications(notifications: SupportNotificationPublisherLike): void {
    ConversationSpot.notifications = notifications;
  }

  async onInitialize(): Promise<void> {
    this.conversationId = this.context.spotRid;
  }

  // The conversation create request is delivered by SupportConversationAllocator immediately
  // after the Spot is created, before any actor joins.
  initializeConversation(create: ConversationCreateRequest): void {
    this.conversationId = this.context.spotRid;
    this.conversation = new Conversation(
      this.conversationId,
      create.subject,
      create.customerActorId,
      create.customerDisplayName,
      create.createdAtUnixMs
    );
  }

  async onActorJoin(actor: SupportUserActorType): Promise<ZLinkSpotActorJoinResponse> {
    try {
      const conversation = this.requireConversation();
      if (actor.role === SupportChatRoles.agent) {
        const state = await this.joinAgent(actor);
        return { accepted: true, reply: joinConversationRes(state) };
      }

      actor.joinConversation(conversation.conversationId);
      this.actors.set(actor.actorId, actor);
      const snapshot = conversation.snapshot();
      // The customer joins after the agent was already assigned, so it never observed the
      // agent's ParticipantJoinedNotify. Deliver a catch-up notify so the customer sees the
      // already-present agent and the Active status.
      if (snapshot.agentActorId !== null) {
        await this.requireNotifications().publishJoinedAgentToCustomer(actor.actorId, snapshot);
      }
      return { accepted: true, reply: joinConversationRes(snapshot) };
    } catch (error) {
      return {
        accepted: false,
        reply: { error: error instanceof Error ? error.message : String(error) }
      };
    }
  }

  async onJoinedActor(actor: SupportUserActorType): Promise<void> {
    void actor;
  }

  async onLeaveActor(actor: SupportUserActorType): Promise<void> {
    this.actors.delete(actor.actorId);
  }

  async onDisconnectActor(actor: SupportUserActorType): Promise<void> {
    actor.markDisconnected();
  }

  private async joinAgent(agent: SupportUserActorType): Promise<ConversationState> {
    const conversation = this.requireConversation();
    const change = conversation.joinAgent(agent.actorId, agent.displayName, Date.now());
    agent.joinConversation(conversation.conversationId);
    this.actors.set(agent.actorId, agent);
    await this.requireNotifications().publish(change.events, this.participantActorIds());
    return change.state;
  }

  async sendMessage(actor: SupportUserActorType, request: SendChatMessageReq): Promise<SendChatMessageRes> {
    this.ensureConversationId(request.conversationId);
    const change = this.requireConversation().sendMessage(actor.actorId, request.text, Date.now());
    await this.requireNotifications().publish(change.events, this.participantActorIds());
    const appended = change.events.find((event) => event.message !== undefined);
    if (appended?.message === undefined) {
      throw new Error('Message event was not created.');
    }
    return { message: appended.message, state: change.state };
  }

  async setTyping(actor: SupportUserActorType, request: SetTypingReq): Promise<SetTypingRes> {
    this.ensureConversationId(request.conversationId);
    const change = this.requireConversation().setTyping(actor.actorId, request.isTyping);
    await this.requireNotifications().publish(change.events, this.participantActorIds());
    return { state: change.state };
  }

  async close(actor: SupportUserActorType, request: CloseConversationReq): Promise<CloseConversationRes> {
    this.ensureConversationId(request.conversationId);
    const change = this.requireConversation().close(actor.actorId, request.reason);
    await this.requireNotifications().publish(change.events, this.participantActorIds());
    return { state: change.state };
  }

  // The idle timer handler only forwards the time signal. Conversation decides whether the
  // conversation becomes WaitingForClose or Closed.
  async checkIdle(): Promise<void> {
    const conversation = this.conversation;
    if (conversation === null) {
      return;
    }
    const now = Date.now();
    const idleChange = conversation.markIdle(now);
    if (idleChange.events.length > 0) {
      await this.requireNotifications().publish(idleChange.events, this.participantActorIds());
    }
  }

  snapshot(): ConversationState {
    return this.requireConversation().snapshot();
  }

  private participantActorIds(): string[] {
    return [...this.actors.keys()];
  }

  private ensureConversationId(conversationId: string): void {
    const conversation = this.requireConversation();
    if (conversationId !== conversation.conversationId) {
      throw new Error(`Request targets another conversation. conversation=${conversationId}`);
    }
  }

  private requireConversation(): Conversation {
    if (this.conversation === null) {
      throw new Error('Conversation has not been created.');
    }
    return this.conversation;
  }

  private requireNotifications(): SupportNotificationPublisherLike {
    if (ConversationSpot.notifications === null) {
      throw new Error('ConversationSpot notifications were not configured.');
    }
    return ConversationSpot.notifications;
  }
}

export { ConversationSpot };
export type { ConversationState };
