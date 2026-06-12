require('reflect-metadata');

const {
  PacketNames,
  authenticateReq,
  authenticateRes,
  placeMarkReq
} = require('../../../../../Shared/Contracts/messages');
const { SampleNames, SampleTimings } = require('../../../../Configuration/sample-settings');
import type {
  ZLinkChannelClient,
  ZLinkSession,
  ZLinkSessionContext
} from '../../../../../../../packages/framework/dist';
import type {
  AuthenticatePlayerRes,
  AuthenticateReq,
  JoinGameReq,
  PlaceMarkStreamReq,
  TicTacToeActor,
  TicTacToeActorClient
} from '../../../../../Shared/Contracts/messages';

type PlaySessionDependencies = {
  apiClient: ZLinkChannelClient;
  actorManager: {
    getOrCreate(actorId: string, actorType: string, signal?: AbortSignal): Promise<TicTacToeActor>;
  };
  entrySpot: {
    join(actor: TicTacToeActor, roomId: string): Promise<unknown>;
  };
  placeMarkHandler: {
    handle(request: ReturnType<typeof placeMarkReq>): Promise<unknown>;
  };
};

type PlaySessionHeader = {
  name: string;
};

class PlaySession implements ZLinkSession {
  private actor: TicTacToeActor | null;

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
      .requestToChannel(SampleNames.apiChannel, authenticateReq(request.accessToken))
      .packetName(PacketNames.authenticatePlayerReq)
      .timeout(SampleTimings.requestTimeout)
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
    const result = await this.dependencies.entrySpot.join(this.actor, request.roomId);
    await this.context.client.reply(result).submit();
  }

  async placeMark(header: PlaySessionHeader, request: PlaceMarkStreamReq): Promise<void> {
    void header;
    if (this.actor === null) {
      throw new Error('AuthenticateReq is required before PlaceMarkReq.');
    }
    const result = await this.dependencies.placeMarkHandler.handle(placeMarkReq(this.actor, request.cell));
    await this.context.client.reply(result).submit();
  }
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
