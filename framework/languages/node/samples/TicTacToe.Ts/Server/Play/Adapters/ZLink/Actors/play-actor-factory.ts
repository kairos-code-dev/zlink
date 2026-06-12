const { PlayActor } = require('./play-actor');
const { actorDisplayName } = require('../../../../../Shared/Contracts/messages');
import type { ZLinkActorContext } from '../../../../../../../packages/framework/dist';
import type {
  TicTacToeActor
} from '../../../../../Shared/Contracts/messages';

class PlayActorFactory {
  create(actorId: string, context: ZLinkActorContext): TicTacToeActor {
    return new PlayActor(actorId, actorDisplayName(actorId), context);
  }
}

export { PlayActorFactory };
