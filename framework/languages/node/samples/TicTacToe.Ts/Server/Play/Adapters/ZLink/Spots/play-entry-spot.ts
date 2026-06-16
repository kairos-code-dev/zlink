import { PlayActorJoinGameHandler } from './Handlers/play-actor-join-game-handler';
import { PacketNames } from '../../../../../Shared/Contracts/messages';
import type {
  ZLinkActor,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext
} from '@zlink-systems/framework';
import type {
  JoinGameRes,
  TicTacToeActor
} from '../../../../../Shared/Contracts/messages';

type PlayEntrySpotActor = TicTacToeActor & ZLinkActor;

class PlayEntrySpot implements ZLinkEntrySpot<PlayEntrySpotActor> {
  readonly context!: ZLinkEntrySpotContext<PlayEntrySpotActor>;

  configure(): void {
    this.context.handlers.actorRequest(PacketNames.joinGameReq, PlayActorJoinGameHandler);
  }

  async join(actor: PlayEntrySpotActor, roomId: string): Promise<JoinGameRes> {
    const joined = await actor.context.joinSpot(roomId).submit();
    const reply = joined.reply === undefined ? {} : JSON.parse(joined.reply.getString('utf8')) as Partial<JoinGameRes & { error: string }>;
    joined.reply?.close();
    if (joined.resultCode !== 0) {
      throw new Error(reply.error ?? `Room '${roomId}' rejected actor '${actor.actorId}'.`);
    }
    return reply as JoinGameRes;
  }

  async onDisconnectActor(actor: PlayEntrySpotActor, signal?: AbortSignal): Promise<void> {
    void signal;
    actor.markDisconnected();
  }

  async onCreateActor(actor: PlayEntrySpotActor, signal?: AbortSignal): Promise<void> {
    void actor;
    void signal;
  }

  async onJoinActor(actor: PlayEntrySpotActor, signal?: AbortSignal): Promise<void> {
    if (actor.destroyAfterEntrySpotJoin) {
      await this.context.destroyActor(actor, signal);
    }
  }

  async onLeaveActor(actor: PlayEntrySpotActor, signal?: AbortSignal): Promise<void> {
    void actor;
    void signal;
  }

}

export { PlayEntrySpot };
