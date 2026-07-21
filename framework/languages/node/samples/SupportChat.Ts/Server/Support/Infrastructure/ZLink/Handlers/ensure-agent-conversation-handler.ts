import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_CLIENT, ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { PacketNames, SupportChatRoles } from '../../../../../Shared/Contracts/messages';
import { JoinSupportConversation } from '../Actors/support-user-actor';
import { zlinkActorRefSnapshotFrom } from '@zlink-systems/framework';
import type { ZLinkActorClient, ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { EnsureAgentConversationReq, EnsureAgentConversationRes } from '../../../../../Shared/Contracts/messages';
import { SampleNames } from '../../../../Configuration/sample-names';

@zlinkRequestHandler('support', PacketNames.ensureAgentConversationReq)
class EnsureAgentConversationHandler implements ZLinkRequestHandler<EnsureAgentConversationReq, EnsureAgentConversationRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient
  ) {}

  async handle(request: EnsureAgentConversationReq): Promise<EnsureAgentConversationRes> {
    const actorId = `${request.rosterActorId}::${request.conversationId}`;
    const actorRef = await this.actorManager.getOrCreate(SampleNames.conversationSpotMesh, actorId, 'support.user', {
      actorId,
      displayName: request.displayName,
      role: SupportChatRoles.Agent,
      participantId: request.rosterActorId
    });
    const joined = await this.actors.requestToActor(
      SampleNames.conversationSpotMesh,
      actorRef,
      new JoinSupportConversation(
        request.conversationId,
        request.rosterActorId,
        SupportChatRoles.Agent,
        request.displayName
      )
    ).submit<{ state: EnsureAgentConversationRes['state'] }>();
    return { actor: zlinkActorRefSnapshotFrom(actorRef), state: joined.state };
  }
}

export { EnsureAgentConversationHandler };
