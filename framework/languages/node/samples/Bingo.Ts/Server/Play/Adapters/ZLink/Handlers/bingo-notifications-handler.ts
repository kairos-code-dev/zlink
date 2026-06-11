const { Inject } = require('@nestjs/common');
const { BingoNotificationDeliveryLog } = require('../../../notification-delivery-log');
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
    return await this.boundSessions.waitFor(request.actorId, request.afterSeq);
  }
}

Inject(BingoNotificationDeliveryLog)(BingoNotificationsHandler, undefined, 0);

export { BingoNotificationsHandler };
