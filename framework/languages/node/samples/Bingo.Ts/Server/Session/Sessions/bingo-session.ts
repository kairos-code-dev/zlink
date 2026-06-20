import { Inject } from '@nestjs/common';
import { ZlinkStreamCodec } from '@zlink-systems/stream-connector';
import { SessionAuthenticator } from './Handlers/authenticate-session-handler';
import { PacketNames } from '../../../Shared/Contracts/messages';
import { decodeBingoPayload } from '../../../Shared/Contracts/protobuf-codec';
import type {
  Message,
  ZLinkSession,
  ZLinkSessionActor,
  ZLinkSessionContext,
  ZLinkSessionFactory,
  ZlinkStreamHeader
} from '@zlink-systems/framework';
import type { AuthenticateReq } from '../../../Shared/Contracts/messages';

type BingoSessionHeader = {
  name: string;
};

class BingoSession implements ZLinkSession {
  private actor: ZLinkSessionActor | null = null;
  actorId: string | null = null;
  displayName: string | null = null;

  constructor(
    private readonly authenticator: SessionAuthenticator,
    readonly context: ZLinkSessionContext
  ) {}

  async onDispatch(header: unknown, payload: Message, signal?: AbortSignal): Promise<void> {
    const bingoHeader = requireBingoSessionHeader(header);
    console.log(`session-dispatch packet=${bingoHeader.name}`);
    if (bingoHeader.name === PacketNames.authenticateReq) {
      const authContext = {
        actors: this.context.actors,
        actorId: this.actorId,
        displayName: this.displayName
      };
      const response = await this.authenticator.handle(
        decodeBingoPayload<AuthenticateReq>({ codec: ZlinkStreamCodec.Protobuf, payload: payload.toBytes() }),
        authContext
      );
      this.actorId = authContext.actorId;
      this.displayName = authContext.displayName;
      this.actor = this.context.actors.find(response.actorId) ?? null;
      console.log(`session-auth reply actor=${response.actorId}`);
      await this.context.client.reply(response).submit(signal);
      console.log(`session-authenticated actor=${response.actorId}`);
      return;
    }
    if (this.actor === null) {
      throw new Error(`Client must authenticate before relaying packet '${bingoHeader.name}'.`);
    }
    await this.actor.relay(header as ZlinkStreamHeader, payload, signal);
  }

  async onDisconnected(): Promise<void> {
    this.actor = null;
    this.actorId = null;
    this.displayName = null;
  }
}

class BingoSessionFactory implements ZLinkSessionFactory<BingoSession> {
  constructor(@Inject(SessionAuthenticator) private readonly authenticator: SessionAuthenticator) {}

  create(context: ZLinkSessionContext): BingoSession {
    return new BingoSession(this.authenticator, context);
  }
}

function requireBingoSessionHeader(header: unknown): BingoSessionHeader {
  if (
    typeof header !== 'object' ||
    header === null ||
    !('name' in header) ||
    typeof (header as { name?: unknown }).name !== 'string'
  ) {
    throw new Error('Bingo stream header is missing packet name.');
  }
  return header as BingoSessionHeader;
}

export { BingoSession, BingoSessionFactory };
