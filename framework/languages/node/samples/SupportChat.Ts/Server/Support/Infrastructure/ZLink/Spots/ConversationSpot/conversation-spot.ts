import { Injectable, Scope } from '@nestjs/common';
import { SampleTimings } from '../../../../../Configuration/sample-names';
import {
  SupportChatRoles
} from '../../../../../../Shared/Contracts/messages';
import { Conversation } from '../../../../Domain/SupportChat/conversation';
import { ConversationIdleTimerHandler } from './Handlers/conversation-idle-timer-handler';
import { SupportNotificationPublisher } from './Notifications/support-notification-publisher';
import { AgentAssignmentService } from '../../../../Application/ConversationAssignment/agent-assignment-service';
import { SupportActorDirectory } from '../../Actors/support-actor-directory';
import type {
  ConversationState,
} from '../../../../../../Shared/Contracts/messages';
import type { ConversationCreateRequest } from './conversation-create-request';
import type {
  ZLinkMessage,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotContext,
  ZLinkSpotCreateResponse,
  ZLinkTimer
} from '@zlink-systems/framework';
import type { SupportUserActor } from '../../Actors/support-user-actor';

@Injectable({ scope: Scope.TRANSIENT })
class ConversationSpot implements ZLinkSpot<SupportUserActor> {
  readonly context!: ZLinkSpotContext<SupportUserActor, ConversationSpot>;
  private conversation?: Conversation;
  private readonly actors = new Map<string, SupportUserActor>();
  private timer?: ZLinkTimer;

  constructor(
    private readonly assignments: AgentAssignmentService,
    private readonly directory: SupportActorDirectory,
    private readonly notifications: SupportNotificationPublisher
  ) {}

  async onCreate(request: ZLinkMessage): Promise<ZLinkSpotCreateResponse> {
    const value = request.decode<ConversationCreateRequest>(Object as never);
    this.conversation = new Conversation(
      value.conversationId,
      value.customerActorId,
      value.customerDisplayName,
      value.subject
    );
    return { accepted: true };
  }

  async onInitialize(): Promise<void> {
    this.timer = await this.context.addTimer('conversation-idle', 50, ConversationIdleTimerHandler);
  }

  async onClosing(): Promise<void> {
    await this.timer?.cancel();
  }

  async onActorJoin(_actorId: string, _request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    return { accepted: true, reply: { state: this.snapshot() } };
  }

  async onJoinedActor(actor: SupportUserActor): Promise<void> {
    this.actors.set(actor.actorId, actor);
    if (actor.role === SupportChatRoles.Agent) {
      const joined = this.requireConversation().join(actor.participantId, SupportChatRoles.Agent, actor.displayName);
      const customer = this.findParticipant(joined.state.customerActorId);
      this.notifications.publish(joined.event, customer === undefined ? [] : [customer]);
      return;
    }
    const agentActorId = this.assignments.assignNextAgent();
    if (agentActorId === undefined) return;
    const roster = this.directory.get(agentActorId);
    if (roster === undefined) {
      throw new Error(`Assigned roster actor '${agentActorId}' was not found.`);
    }
    const assigned = this.assignAgent(agentActorId, roster.displayName);
    this.notifications.publish(assigned.event, [roster]);
  }

  async onLeaveActor(actor: SupportUserActor): Promise<void> {
    this.actors.delete(actor.actorId);
  }

  async onDisconnectActor(_actor: SupportUserActor): Promise<void> {}

  snapshot(): ConversationState {
    return this.requireConversation().snapshot();
  }

  assignAgent(agentActorId: string, displayName: string): { state: ConversationState; event: import('../../../../Domain/SupportChat/conversation-events').ConversationEvent } {
    return this.requireConversation().assign(agentActorId, displayName);
  }

  join(actor: SupportUserActor): ConversationState {
    this.requireConversationId(actor);
    return this.requireConversation().join(actor.participantId, actor.role, actor.displayName).state;
  }

  sendChat(actor: SupportUserActor, text: string): { message: import('../../../../../../Shared/Contracts/messages').ChatMessage; state: ConversationState } {
    this.requireConversationId(actor);
    const result = this.requireConversation().appendMessage(actor.participantId, text);
    this.notifications.publish(result.event, this.otherActors(actor));
    return result;
  }

  setTyping(actor: SupportUserActor, isTyping: boolean): void {
    const event = this.requireConversation().changeTyping(actor.participantId, isTyping);
    if (event !== undefined) this.notifications.publish(event, this.otherActors(actor));
  }

  async close(actor: SupportUserActor): Promise<ConversationState> {
    this.requireConversationId(actor);
    if (!this.requireConversation().canParticipate(actor.participantId)) {
      throw new Error('Only a conversation participant can close the conversation.');
    }
    const closed = this.requireConversation().close();
    this.notifications.publish(closed.event, this.otherActors(actor));
    await this.closeSpot();
    return closed.state;
  }

  async onTimer(now = Date.now()): Promise<string | undefined> {
    const conversation = this.requireConversation();
    if (conversation.shouldBecomeIdle(now, SampleTimings.idleTimeout)) {
      const idle = conversation.markIdle(now + SampleTimings.closeGraceTimeout);
      if (idle.event !== undefined) this.notifications.publish(idle.event, this.actors.values());
      return undefined;
    }
    if (conversation.shouldCloseAfterIdle(now)) {
      const closed = conversation.close();
      this.notifications.publish(closed.event, this.actors.values());
      await this.closeSpot();
      return closed.state.agentActorId;
    }
    return undefined;
  }

  private metadataActorCanParticipate(actor: SupportUserActor): boolean {
    return this.requireConversation().canParticipate(actor.participantId);
  }

  private requireConversationId(actor: SupportUserActor): void {
    if (!this.metadataActorCanParticipate(actor)) {
      throw new Error(`Actor '${actor.participantId}' is not a conversation participant.`);
    }
  }

  private otherActors(source: SupportUserActor): SupportUserActor[] {
    return [...this.actors.values()].filter((actor) => actor.actorId !== source.actorId);
  }

  private findParticipant(participantId: string): SupportUserActor | undefined {
    return [...this.actors.values()].find((actor) => actor.participantId === participantId);
  }

  private async closeSpot(): Promise<void> {
    for (const actor of [...this.actors.values()]) {
      await this.context.leaveActor(actor);
    }
    await this.context.close();
  }

  private requireConversation(): Conversation {
    if (this.conversation === undefined) {
      throw new Error('Conversation Spot has not been created.');
    }
    return this.conversation;
  }
}

export { ConversationSpot };
