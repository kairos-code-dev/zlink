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
  ZLinkSessionDispatchContext,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import type {
  AuthenticatePlayerRes,
  AuthenticateReq,
  JoinGameReq,
  PlayerInfo,
  TicTacToeActor
} from '../../../../../Shared/Contracts/messages';

type AuthenticatedPlayer = PlayerInfo & { readonly ref: ActorRef; roomId?: string };

type PlayEntrySpotLike = {
  join(actorRef: ActorRef, player: PlayerInfo, roomId: string): Promise<unknown>;
  observeMilestone(actor: PlayerInfo): Promise<unknown>;
};

type PlaySessionDependencies = {
  apiClient: ZLinkChannelClient;
  actorManager: {
    getOrCreate(actorId: string, actorType: string, createRequest: unknown, signal?: AbortSignal): Promise<ActorRef>;
  };
  entrySpot: PlayEntrySpotLike;
  joinGameHandler: {
    handle(
      entrySpot: PlayEntrySpotLike,
      actor: TicTacToeActor,
      context: ZLinkSpotActorRequestContext,
      request: JoinGameReq
    ): Promise<unknown>;
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
    if (header.name === PacketNames.joinGameReq) {
      await this.joinGame(header, payload as JoinGameReq);
      return;
    }
    if (header.name === PacketNames.observeMilestoneReq) {
      await this.observeMilestone(header);
      return;
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
    this.actor = { ...authenticated.player, ref: actorRef };
    this.sessionActor = await this.context.actors.bind(actorRef);
    await this.context.client.reply(authenticateRes(authenticated.player)).submit();
  }

  async joinGame(header: PlaySessionHeader, request: JoinGameReq): Promise<void> {
    void header;
    if (this.actor === null) {
      throw new Error('AuthenticateReq is required before JoinGameReq.');
    }
    console.log(`actor: JoinGameReq received. actor=${this.actor.actorId} roomId=${request.roomId}`);
    const result = await this.dependencies.entrySpot.join(this.actor.ref, this.actor, request.roomId);
    this.actor.roomId = request.roomId;
    await this.context.client.reply(result).submit();
    console.log(`actor: JoinGameReq completed. actor=${this.actor.actorId} roomId=${request.roomId}`);
  }

  async observeMilestone(header: PlaySessionHeader): Promise<void> {
    void header;
    if (this.actor === null) {
      throw new Error('AuthenticateReq is required before ObserveMilestoneReq.');
    }
    console.log(`actor: ObserveMilestoneReq received. actor=${this.actor.actorId}`);
    const result = await this.dependencies.entrySpot.observeMilestone(this.actor);
    await this.context.client.reply(result).submit();
    console.log(`actor: ObserveMilestoneReq completed. actor=${this.actor.actorId}`);
  }

  private async relayToActor(playHeader: PlaySessionHeader, payload: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    if (this.actor === null) {
      throw new Error('AuthenticateReq is required before actor packets.');
    }
    if (this.actor.roomId === undefined) {
      throw new Error('JoinGameReq is required before actor packets.');
    }
    const sessionActor = this.sessionActor ?? this.context.actors.find(this.actor.actorId);
    if (sessionActor === undefined) {
      throw new Error(`Actor '${this.actor.actorId}' is not bound to this stream session.`);
    }
    await sessionActor.relay(payload, signal);
  }
}

function createActorRequestContext(packetName: string): ZLinkSpotActorRequestContext {
  return {
    packetName,
    metadata: { packetName },
    reply: createNoopReplyOptions()
  };
}

function createNoopReplyOptions(): ZLinkSpotActorRequestContext['reply'] {
  return {
    metadata(_key: string, _value: string) {
      return this;
    },
    compress(_enabled?: boolean) {
      return this;
    }
  };
}

function shouldRelayToActor(packetName: string): boolean {
  return packetName === PacketNames.placeMarkReq ||
    packetName === PacketNames.leaveGameReq;
}

export { PlaySession };
