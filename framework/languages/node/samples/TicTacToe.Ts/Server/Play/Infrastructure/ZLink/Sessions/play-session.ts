import 'reflect-metadata';
import { PacketNames, authenticatePlayerReq, authenticateRes } from '../../../../../Shared/Contracts/messages';
import { SampleNames } from '../../../../Configuration/sample-settings';
import type {
  Message,
  ZLinkActor,
  ZLinkChannelClient,
  ZLinkSession,
  ZLinkSessionActor,
  ZLinkSessionContext,
  ZLinkSpotActorRequestContext,
  ZlinkStreamHeader
} from '@zlink-systems/framework';
import type {
  AuthenticatePlayerRes,
  AuthenticateReq,
  JoinGameReq,
  TicTacToeActor,
  TicTacToeActorClient
} from '../../../../../Shared/Contracts/messages';

type PlaySessionActor = TicTacToeActor & ZLinkActor;

type PlayEntrySpotLike = {
  join(actor: PlaySessionActor, roomId: string): Promise<unknown>;
  observeMilestone(actor: PlaySessionActor): Promise<unknown>;
};

type PlaySessionDependencies = {
  apiClient: ZLinkChannelClient;
  actorManager: {
    getOrCreate(actorId: string, actorType: string, signal?: AbortSignal): Promise<PlaySessionActor>;
  };
  entrySpot: PlayEntrySpotLike;
  joinGameHandler: {
    handle(
      entrySpot: PlayEntrySpotLike,
      actor: PlaySessionActor,
      context: ZLinkSpotActorRequestContext,
      request: JoinGameReq
    ): Promise<unknown>;
  };
};

type PlaySessionHeader = {
  name: string;
};

class PlaySession implements ZLinkSession {
  private actor: PlaySessionActor | null;
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

  async onDispatch(header: unknown, payload: Message, signal?: AbortSignal): Promise<void> {
    const playHeader = requirePlaySessionHeader(header);
    if (shouldRelayToActor(playHeader.name)) {
      await this.relayToActor(header, payload, signal);
      return;
    }
    await this.dispatch(playHeader, JSON.parse(payload.getString()));
  }

  async onDisconnected(context: ZLinkSessionContext): Promise<void> {
    this.actor?.detachClient(context.client as TicTacToeActorClient);
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
    this.actor = await this.dependencies.actorManager.getOrCreate(
      authenticated.player.actorId,
      SampleNames.playerActorType
    );
    this.actor.displayName = authenticated.player.displayName;
    this.actor.level = authenticated.player.level;
    this.actor.wins = authenticated.player.wins;
    this.sessionActor = await this.context.actors.bind(this.actor);
    this.actor.attachClient(this.context.client as TicTacToeActorClient);
    await this.context.client.reply(authenticateRes(authenticated.player)).submit();
  }

  async joinGame(header: PlaySessionHeader, request: JoinGameReq): Promise<void> {
    void header;
    if (this.actor === null) {
      throw new Error('AuthenticateReq is required before JoinGameReq.');
    }
    console.log(`actor: JoinGameReq received. actor=${this.actor.actorId} roomId=${request.roomId}`);
    const result = await this.dependencies.joinGameHandler.handle(
      this.dependencies.entrySpot,
      this.actor,
      createActorRequestContext(PacketNames.joinGameReq),
      request
    );
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

  private async relayToActor(header: unknown, payload: Message, signal?: AbortSignal): Promise<void> {
    const playHeader = requirePlaySessionHeader(header);
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
    await sessionActor.relay(header as ZlinkStreamHeader, payload, signal);
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

function requirePlaySessionHeader(header: unknown): PlaySessionHeader {
  if (
    typeof header !== 'object' ||
    header === null ||
    !('name' in header) ||
    typeof header.name !== 'string'
  ) {
    throw new Error('Play stream packet header is missing a packet name.');
  }
  return header as PlaySessionHeader;
}

function shouldRelayToActor(packetName: string): boolean {
  return packetName === PacketNames.placeMarkReq ||
    packetName === PacketNames.leaveGameReq;
}

export { PlaySession };
