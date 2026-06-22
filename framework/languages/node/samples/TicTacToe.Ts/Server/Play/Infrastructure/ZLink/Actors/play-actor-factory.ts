import { PlayActor } from './play-actor';
import { actorDisplayName } from '../../../../../Shared/Contracts/messages';
import type {
  ZLinkActor,
  ZLinkActorContext
} from '@zlink-systems/framework';
import type {
  TicTacToeActor
} from '../../../../../Shared/Contracts/messages';

type PlayZLinkActor = TicTacToeActor & ZLinkActor;

class PlayActorFactory {
  create(actorId: string, context: ZLinkActorContext): PlayZLinkActor {
    return new PlayActor(actorId, actorDisplayName(actorId), context);
  }
}

export { PlayActorFactory };
