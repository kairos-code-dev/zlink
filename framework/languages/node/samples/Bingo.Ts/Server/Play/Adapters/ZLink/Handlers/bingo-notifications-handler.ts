const { Inject } = require('@nestjs/common');
const { zlinkRequestHandler } = require('../../../../../../../../packages/nestjs/dist');
const { BingoNotificationDeliveryLog } = require('../../../notification-delivery-log');
const { PacketNames } = require('../../../../../Shared/Contracts/messages');
import type { ZLinkRequestHandler } from '../../../../../../../packages/framework/dist';
import type { BingoNotificationDeliveryLog as BingoNotificationDeliveryLogType } from '../../../notification-delivery-log';
import type {
  BingoNotificationsReq,
  PlayerIdentity
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('notifications', PacketNames.bingoNotificationsReq)
class BingoNotificationsHandler implements ZLinkRequestHandler<BingoNotificationsReq & PlayerIdentity, unknown> {
  constructor(@Inject(BingoNotificationDeliveryLog) private readonly boundSessions: BingoNotificationDeliveryLogType) {}

  async handle(request: BingoNotificationsReq & PlayerIdentity): Promise<unknown> {
    return await this.boundSessions.waitFor(request.actorId, request.afterSeq);
  }
}

export { BingoNotificationsHandler };
