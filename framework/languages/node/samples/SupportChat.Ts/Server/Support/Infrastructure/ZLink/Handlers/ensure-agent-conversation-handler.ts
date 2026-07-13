import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { joinConversation, PacketNames, SupportChatRoles } from '../../../../../Shared/Contracts/messages';
import { SupportActorDirectory } from '../Actors/support-actor-directory';
import { zlinkActorRefSnapshotFrom } from '@zlink-systems/framework';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { EnsureAgentConversationReq, EnsureAgentConversationRes } from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('support', PacketNames.ensureAgentConversationReq)
class EnsureAgentConversationHandler implements ZLinkRequestHandler<EnsureAgentConversationReq, EnsureAgentConversationRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    private readonly actors: SupportActorDirectory
  ) {}

  async handle(request: EnsureAgentConversationReq): Promise<EnsureAgentConversationRes> {
    const actorId = `${request.rosterActorId}::${request.conversationId}`;
    const actorRef = await this.actorManager.getOrCreate(actorId, 'support.user', {
      actorId,
      displayName: request.displayName,
      role: SupportChatRoles.Agent,
      participantId: request.rosterActorId
    });
    const actor = this.actors.require(actorId);
    const joined = await actor.context.joinSpot(
      request.conversationId,
      joinConversation(request.rosterActorId, SupportChatRoles.Agent, request.displayName)
    ).submit<{ state: EnsureAgentConversationRes['state'] }>();
    if (joined.status !== 'accepted') {
      throw new Error(`Conversation '${request.conversationId}' rejected agent actor.`);
    }
    return { actor: zlinkActorRefSnapshotFrom(actorRef), state: joined.reply.state };
  }
}

export { EnsureAgentConversationHandler };
