import { Inject } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
import { zlinkActorRefSnapshotToActorRef } from '@zlink-systems/framework';
import { SampleNames, SampleTimings } from '../../Configuration/sample-names';
import {
  PacketNames,
  SupportChatRoles,
  authenticateUser,
  ensureAgentConversation,
  ensureSupportUserActor
} from '../../../Shared/Contracts/messages';
import type {
  AuthenticateReq,
  AuthenticateRes,
  AuthenticateUserRes,
  EnsureAgentConversationRes,
  EnsureSupportUserActorRes,
  SupportRole
} from '../../../Shared/Contracts/messages';
import type {
  ZLinkChannelClient,
  ZLinkMessage,
  ZLinkLocationStore,
  ZLinkSession,
  ZLinkSessionActor,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';

class SupportChatSession implements ZLinkSession {
  private actorId?: string;
  private displayName?: string;
  private role?: SupportRole;
  private readonly conversationActors = new Map<string, string>();

  constructor(
    readonly context: ZLinkSessionContext,
    private readonly channels: ZLinkChannelClient,
    private readonly locations: ZLinkLocationStore
  ) {}

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage): Promise<void> {
    if (dispatch.packetName === PacketNames.authenticateReq) {
      await this.authenticate(payload.decode<AuthenticateReq>(Object as never));
      return;
    }
    const identity = this.requireIdentity(dispatch.packetName);
    if (dispatch.packetName === PacketNames.openConversationReq || dispatch.packetName === PacketNames.setAgentAvailableReq) {
      await identity.relay(payload);
      return;
    }
    if (isConversationPacket(dispatch.packetName)) {
      const conversationId = dispatch.metadata.get(SampleNames.conversationIdMetadataKey);
      if (conversationId === undefined || conversationId.length === 0) {
        if (dispatch.packetName === PacketNames.setTypingReq) return;
        throw new Error(`Conversation metadata is required for '${dispatch.packetName}'.`);
      }
      const actor = await this.conversationActor(conversationId, dispatch.packetName);
      if (actor === undefined) return;
      await actor.relay(payload);
      return;
    }
    throw new Error(`Unsupported SupportChat packet '${dispatch.packetName}'.`);
  }

  async onDisconnected(): Promise<void> {
    await Promise.allSettled(this.context.actors.bound.map((actor) => actor.notifyDisconnected()));
  }

  private async authenticate(request: AuthenticateReq): Promise<void> {
    const authenticated = await this.channels
      .requestToChannel(SampleNames.apiChannel, authenticateUser(request.accessToken))
      .timeout(SampleTimings.requestTimeout)
      .submit<AuthenticateUserRes>();
    if (!authenticated.accepted || authenticated.actorId === undefined || authenticated.displayName === undefined || authenticated.role === undefined) {
      throw new Error(authenticated.reason ?? 'SupportChat authentication failed.');
    }
    const existing = await this.locations.resolveActor({ actorId: authenticated.actorId });
    const actorRef = existing?.actorRef ?? zlinkActorRefSnapshotToActorRef((await this.channels
      .requestToChannel(
        SampleNames.supportChannel,
        ensureSupportUserActor(authenticated.actorId, authenticated.displayName, authenticated.role)
      )
      .timeout(SampleTimings.requestTimeout)
      .submit<EnsureSupportUserActorRes>()).actor);
    await this.context.actors.bindOrGet({
      actorId: actorRef.actorId,
      nodeRid: actorRef.nodeRid,
      generation: actorRef.generation
    });
    this.actorId = authenticated.actorId;
    this.displayName = authenticated.displayName;
    this.role = authenticated.role;
    this.context.client.reply({
      actorId: authenticated.actorId,
      displayName: authenticated.displayName,
      role: authenticated.role
    } satisfies AuthenticateRes).submit();
  }

  private requireIdentity(packetName: string): ZLinkSessionActor {
    if (this.actorId === undefined) {
      throw new Error(`AuthenticateReq is required before '${packetName}'.`);
    }
    const actor = this.context.actors.find(this.actorId);
    if (actor === undefined) throw new Error(`Bound identity actor '${this.actorId}' was not found.`);
    return actor;
  }

  private async conversationActor(conversationId: string, packetName: string): Promise<ZLinkSessionActor | undefined> {
    if (this.role === SupportChatRoles.Customer) {
      return this.requireIdentity(packetName);
    }
    let actorId = this.conversationActors.get(conversationId);
    if (actorId === undefined && packetName === PacketNames.joinConversationReq) {
      actorId = `${this.actorId!}::${conversationId}`;
      const existing = await this.locations.resolveActor({ actorId });
      const actorRef = existing?.actorRef ?? zlinkActorRefSnapshotToActorRef((await this.channels
        .requestToChannel(
          SampleNames.supportChannel,
          ensureAgentConversation(this.actorId!, this.displayName!, conversationId)
        )
        .timeout(SampleTimings.requestTimeout)
        .submit<EnsureAgentConversationRes>()).actor);
      await this.context.actors.bindOrGet({
        actorId: actorRef.actorId,
        nodeRid: actorRef.nodeRid,
        generation: actorRef.generation
      });
      this.conversationActors.set(conversationId, actorId);
    }
    if (actorId === undefined) {
      if (packetName === PacketNames.setTypingReq) return undefined;
      throw new Error(`JoinConversationReq is required before '${packetName}'.`);
    }
    return this.context.actors.find(actorId);
  }
}

class SupportChatSessionFactory implements ZLinkSessionFactory<SupportChatSession> {
  constructor(
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient,
    @Inject('SUPPORTCHAT_LOCATION_STORE') private readonly locations: ZLinkLocationStore
  ) {}

  async create(context: ZLinkSessionContext): Promise<SupportChatSession> {
    return new SupportChatSession(context, this.channels, this.locations);
  }
}

function isConversationPacket(packetName: string): boolean {
  return packetName === PacketNames.joinConversationReq
    || packetName === PacketNames.sendChatMessageReq
    || packetName === PacketNames.setTypingReq
    || packetName === PacketNames.closeConversationReq;
}

export { SupportChatSession, SupportChatSessionFactory };
