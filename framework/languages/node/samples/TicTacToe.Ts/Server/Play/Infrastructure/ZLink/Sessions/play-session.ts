import 'reflect-metadata';
import { PacketNames, authenticatePlayerReq, authenticateRes } from '../../../../../Shared/Contracts/messages';
import { SampleNames } from '../../../../Configuration/sample-settings';
import type {
  ActorRef,
  ZLinkChannelClient,
  ZLinkMessage,
  ZLinkSession,
  ZLinkSessionActor,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext
} from '@zlink-systems/framework';
import type {
  AuthenticatePlayerRes,
  AuthenticateReq,
  PlayerInfo
} from '../../../../../Shared/Contracts/messages';

type AuthenticatedPlayer = PlayerInfo & {
  readonly ref: ActorRef;
  roomId?: string;
  push(payload: unknown): void;
};

type PlaySessionDependencies = {
  apiClient: ZLinkChannelClient;
  actorManager: {
    getOrCreate(actorId: string, actorType: string, createRequest: unknown, signal?: AbortSignal): Promise<ActorRef>;
  };
};

type PlaySessionHeader = {
  name: string;
};

class PlaySession implements ZLinkSession {
  private actor: AuthenticatedPlayer | null;
  private sessionActor: ZLinkSessionActor | null;

  constructor(
    private readonly dependencies: PlaySessionDependencies,
    readonly context: ZLinkSessionContext
  ) {
    this.dependencies = dependencies;
    this.context = context;
    this.actor = null;
    this.sessionActor = null;
  }

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    const playHeader = { name: dispatch.packetName };
    if (playHeader.name === PacketNames.joinGameReq) {
      await this.relayToActor(playHeader, payload, signal);
      if (this.actor !== null) {
        this.actor.roomId = 'joined';
      }
      return;
    }
    if (shouldRelayToActor(playHeader.name)) {
      await this.relayToActor(playHeader, payload, signal);
      return;
    }
    await this.dispatch(playHeader, payload.decode());
  }

  async onDisconnected(context: ZLinkSessionContext): Promise<void> {
    void context;
    await this.sessionActor?.notifyDisconnected();
  }

  async dispatch(header: PlaySessionHeader, payload: unknown): Promise<void> {
    if (header.name === PacketNames.authenticateReq) {
      await this.authenticate(header, payload as AuthenticateReq);
      return;
    }
    if (this.actor === null) {
      throw new Error('AuthenticateReq is required before play packets.');
    }
    throw new Error(`Unsupported play stream packet '${header.name}'.`);
  }

  async authenticate(header: PlaySessionHeader, request: AuthenticateReq): Promise<void> {
    void header;
    const authenticated = await this.dependencies.apiClient
      .requestToChannel(SampleNames.apiChannel, authenticatePlayerReq(request.accessToken))
      .submit<AuthenticatePlayerRes>();
    const actorRef = await this.dependencies.actorManager.getOrCreate(
      authenticated.player.actorId,
      SampleNames.playerActorType,
      authenticated.player
    );
    let pushSeq = 0;
    const sessionClient = this.context.client;
    this.actor = {
      ...authenticated.player,
      ref: actorRef,
      push(payload: unknown): void {
        pushSeq += 1;
        void sessionClient
          .send(payload)
          .metadata('seq', String(pushSeq))
          .submit();
      }
    };
    this.sessionActor = await this.context.actors.bindOrGet(actorRef);
    this.context.client.reply(authenticateRes(authenticated.player)).submit();
  }

  private async relayToActor(playHeader: PlaySessionHeader, payload: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    if (this.actor === null) {
      throw new Error('AuthenticateReq is required before actor packets.');
    }
    if (this.actor.roomId === undefined && requiresRoomMembership(playHeader.name)) {
      throw new Error('JoinGameReq is required before actor packets.');
    }
    const sessionActor = this.sessionActor ?? this.context.actors.find(this.actor.actorId);
    if (sessionActor === undefined) {
      throw new Error(`Actor '${this.actor.actorId}' is not bound to this stream session.`);
    }
    await sessionActor.relay(payload, signal);
  }
}

function shouldRelayToActor(packetName: string): boolean {
  return packetName === PacketNames.observeMilestoneReq ||
    requiresRoomMembership(packetName);
}

function requiresRoomMembership(packetName: string): boolean {
  return packetName === PacketNames.placeMarkReq ||
    packetName === PacketNames.leaveGameReq;
}

export { PlaySession };
