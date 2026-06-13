import type {
  ZLinkActor,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext
} from '../../../../../../../packages/framework/dist';
import type {
  JoinGameRes,
  TicTacToeActor
} from '../../../../../Shared/Contracts/messages';

class PlayEntrySpot implements ZLinkEntrySpot {
  readonly context!: ZLinkEntrySpotContext;

  async join(actor: TicTacToeActor, roomId: string): Promise<JoinGameRes> {
    const joined = await toZLinkActor(actor).context.joinSpot(roomId).submit();
    const reply = joined.reply === undefined ? {} : JSON.parse(joined.reply.getString('utf8')) as Partial<JoinGameRes & { error: string }>;
    joined.reply?.close();
    if (joined.resultCode !== 0) {
      throw new Error(reply.error ?? `Room '${roomId}' rejected actor '${actor.actorId}'.`);
    }
    return reply as JoinGameRes;
  }

  async onDisconnectActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    void signal;
    const player = toTicTacToeActor(actor);
    player.markDisconnected();
  }

  async onCreateActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    void actor;
    void signal;
  }

  async onJoinActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    if (isDestroyMarked(actor)) {
      await this.context.destroyActor(actor, signal);
    }
  }

  async onLeaveActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    void actor;
    void signal;
  }

}

function toZLinkActor(actor: TicTacToeActor): ZLinkActor {
  const candidate = actor as Partial<ZLinkActor>;
  if (candidate.context === undefined) {
    throw new Error(`Actor '${actor.actorId}' is not attached to a ZLink actor context.`);
  }
  return candidate as ZLinkActor;
}

function toTicTacToeActor(actor: ZLinkActor): TicTacToeActor {
  const candidate = actor as ZLinkActor & Partial<TicTacToeActor>;
  if (typeof candidate.markDisconnected !== 'function') {
    throw new Error(`Actor '${actor.actorId}' is not a tic-tac-toe player actor.`);
  }
  return candidate as TicTacToeActor;
}

function isDestroyMarked(actor: ZLinkActor): boolean {
  return (actor as Partial<TicTacToeActor>).destroyAfterEntrySpotJoin === true;
}

export { PlayEntrySpot };
