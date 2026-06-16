import 'reflect-metadata';
import { PacketNames, PlaceMarkReq, authenticatePlayerReq, authenticateRes } from '../../../../../Shared/Contracts/messages';
import { SampleNames, SampleTimings } from '../../../../Configuration/sample-settings';
import { TicTacToeGameSpot } from '../Spots/tictactoe-game-spot';
import type {
  ZLinkActor,
  ZLinkChannelClient,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSpotActorRequestContext,
  ZLinkSpotManager
} from '@zlink-systems/framework';
import type {
  AuthenticatePlayerRes,
  AuthenticateReq,
  JoinGameReq,
  PlaceMarkStreamReq,
  TicTacToeActor,
  TicTacToeActorClient
} from '../../../../../Shared/Contracts/messages';

type PlaySessionActor = TicTacToeActor & ZLinkActor;

type PlayEntrySpotLike = {
  join(actor: PlaySessionActor, roomId: string): Promise<unknown>;
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
  spotManager: ZLinkSpotManager;
  placeMarkHandler: {
    handle(
      spot: TicTacToeGameSpot,
      actor: PlaySessionActor,
      context: ZLinkSpotActorRequestContext,
      request: PlaceMarkReq
    ): Promise<unknown>;
  };
};

type PlaySessionHeader = {
  name: string;
};

class PlaySession implements ZLinkSession {
  private actor: PlaySessionActor | null;

  constructor(
    private readonly dependencies: PlaySessionDependencies,
    readonly context: ZLinkSessionContext
  ) {
    this.dependencies = dependencies;
    this.context = context;
    this.actor = null;
  }

  async onDispatch(header: unknown, payload: { getString(): string }): Promise<void> {
    await this.dispatch(requirePlaySessionHeader(header), JSON.parse(payload.getString()));
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
    if (header.name === PacketNames.placeMarkReq) {
      await this.placeMark(header, payload as PlaceMarkStreamReq);
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
      authenticated.actorId,
      SampleNames.playerActorType
    );
    this.actor.displayName = authenticated.displayName;
    this.actor.attachClient(this.context.client as TicTacToeActorClient);
    await this.context.client.reply(authenticateRes(authenticated.actorId, authenticated.displayName)).submit();
  }

  async joinGame(header: PlaySessionHeader, request: JoinGameReq): Promise<void> {
    void header;
    if (this.actor === null) {
      throw new Error('AuthenticateReq is required before JoinGameReq.');
    }
    const result = await this.dependencies.joinGameHandler.handle(
      this.dependencies.entrySpot,
      this.actor,
      createActorRequestContext(PacketNames.joinGameReq),
      request
    );
    await this.context.client.reply(result).submit();
  }

  async placeMark(header: PlaySessionHeader, request: PlaceMarkStreamReq): Promise<void> {
    void header;
    if (this.actor === null) {
      throw new Error('AuthenticateReq is required before PlaceMarkReq.');
    }
    const roomId = this.actor.roomId;
    if (roomId === undefined) {
      throw new Error('JoinGameReq is required before PlaceMarkReq.');
    }
    const result = await this.dependencies.spotManager.executeOnSpot(
      TicTacToeGameSpot,
      roomId,
      (spot) => this.dependencies.placeMarkHandler.handle(
        spot,
        this.actor as PlaySessionActor,
        createActorRequestContext(PacketNames.placeMarkReq),
        new PlaceMarkReq(request.cell)
      )
    );
    await this.context.client.reply(result).submit();
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

export { PlaySession };
