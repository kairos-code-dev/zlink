const { Inject } = require('@nestjs/common');
const { BingoNotificationDeliveryLog } = require('../../../notification-delivery-log');
import type { ZLinkRequestHandler } from '../../../../../../../packages/framework/dist';
import type { BingoNotificationDeliveryLog as BingoNotificationDeliveryLogType } from '../../../notification-delivery-log';
import type {
  BingoNotificationsReq,
  PlayerIdentity
} from '../../../../../Shared/Contracts/messages';

class BingoNotificationsHandler implements ZLinkRequestHandler<BingoNotificationsReq & PlayerIdentity, unknown> {
  constructor(private readonly boundSessions: BingoNotificationDeliveryLogType) {}
  async handle(request: BingoNotificationsReq & PlayerIdentity): Promise<unknown> {
    return await this.boundSessions.waitFor(request.actorId, request.afterSeq);
  }
}

Inject(BingoNotificationDeliveryLog)(BingoNotificationsHandler, undefined, 0);

export { BingoNotificationsHandler };
