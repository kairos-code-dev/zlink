import { Inject } from '@nestjs/common';
import { SessionAuthenticator } from './Handlers/authenticate-session-handler';
import { PacketNames } from '../../../Shared/Contracts/messages';
import type {
  ZLinkMessage,
  ZLinkSession,
  ZLinkSessionActor,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';
import type { AuthenticateReq } from '../../../Shared/Contracts/messages';

class BingoSession implements ZLinkSession {
  private actor: ZLinkSessionActor | null = null;
  actorId: string | null = null;
  displayName: string | null = null;

  constructor(
    private readonly authenticator: SessionAuthenticator,
    readonly context: ZLinkSessionContext
  ) {}

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    console.log(`session-dispatch packet=${dispatch.packetName}`);
    if (dispatch.packetName === PacketNames.authenticateReq) {
      const authContext = {
        actors: this.context.actors,
        actorId: this.actorId,
        displayName: this.displayName
      };
      const response = await this.authenticator.handle(
        payload.decode<AuthenticateReq>(Object as never),
        authContext
      );
      this.actorId = authContext.actorId;
      this.displayName = authContext.displayName;
      this.actor = this.context.actors.find(response.actorId) ?? null;
      console.log(`session-auth reply actor=${response.actorId}`);
      this.context.client.reply(response).submit();
      console.log(`session-authenticated actor=${response.actorId}`);
      return;
    }
    if (this.actor === null) {
      throw new Error(`Client must authenticate before relaying packet '${dispatch.packetName}'.`);
    }
    await this.actor.relay(payload, signal);
  }

  async onDisconnected(): Promise<void> {
    console.error(`bingo-lifecycle session-disconnect actor=${this.actorId ?? '-'} destroy=false`);
    this.actor = null;
    this.actorId = null;
    this.displayName = null;
  }
}

class BingoSessionFactory implements ZLinkSessionFactory<BingoSession> {
  constructor(@Inject(SessionAuthenticator) private readonly authenticator: SessionAuthenticator) {}

  async create(context: ZLinkSessionContext): Promise<BingoSession> {
    return new BingoSession(this.authenticator, context);
  }
}

export { BingoSession, BingoSessionFactory };
