const { Inject } = require('@nestjs/common');
const { SampleBoundSessionRuntime } = require('../../../bound-session-runtime');
import type {
  BingoNotificationsReq,
  PlayerIdentity
} from '../../../../../Shared/Contracts/messages';

class BingoNotificationsHandler {
  private readonly boundSessions;

  constructor(boundSessions: any) {
    this.boundSessions = boundSessions;
  }
  async handle(request: BingoNotificationsReq & PlayerIdentity): Promise<unknown> {
    return await this.boundSessions.deliveredFor(request.actorId, request.afterSeq);
  }
}

Inject(SampleBoundSessionRuntime)(BingoNotificationsHandler, undefined, 0);

export { BingoNotificationsHandler };
