const { BingoNotificationInbox } = require('./bingo-notification-inbox');
const {
  PacketNames,
  actorDisplayName,
  authenticateReq,
  bingoNotificationsReq,
  matchBingoReq,
  startBingoGameReq
} = require('../Shared/Contracts/messages');
const { SampleTimings } = require('../Shared/Configuration/sample-names');
import type {
  AuthenticateSessionRes,
  MatchBingoRes,
  RejectedCommandRes,
  StateEnvelope
} from '../Shared/Contracts/messages';

type BingoSessionRouteClient = {
  request(targetNodeRid: string, packetName: string, payload: unknown, timeoutMs: number): Promise<any>;
};

type DeliveredNotification = {
  actorId: string;
  packetName: string;
  payload: any;
};

function isRejectedCommand(result: StateEnvelope | RejectedCommandRes): result is RejectedCommandRes {
  return (result as RejectedCommandRes).rejected === true;
}

class BingoPlayerClient {
  [key: string]: any;
  constructor(actorId: string, sessionClient: BingoSessionRouteClient) {
    this.actorId = actorId;
    this.displayName = actorDisplayName(actorId);
    this.sessionClient = sessionClient;
    this.notifications = new BingoNotificationInbox(actorId);
    this.notificationCursor = 0;
  }

  async authenticate(): Promise<AuthenticateSessionRes> {
    const authenticated: AuthenticateSessionRes = await this.sessionClient.request(
      'session-server',
      PacketNames.authenticateReq,
      authenticateReq(this.actorId),
      SampleTimings.requestTimeout
    );
    this.displayName = authenticated.displayName;
    return {
      actorId: authenticated.actorId,
      displayName: authenticated.displayName
    };
  }

  async match(): Promise<MatchBingoRes> {
    return await this.sessionClient.request(
      'session-server',
      PacketNames.matchBingoReq,
      matchBingoReq(),
      SampleTimings.requestTimeout
    );
  }

  async start(roomId: string): Promise<StateEnvelope> {
    const result: StateEnvelope | RejectedCommandRes = await this.sessionClient.request(
      'session-server',
      PacketNames.startBingoGameReq,
      startBingoGameReq(roomId),
      SampleTimings.requestTimeout
    );
    if (isRejectedCommand(result)) {
      throw new Error(result.reason);
    }
    return result;
  }

  async syncNotifications(): Promise<DeliveredNotification[]> {
    const response = await this.sessionClient.request(
      'session-server',
      PacketNames.bingoNotificationsReq,
      bingoNotificationsReq(this.notificationCursor),
      SampleTimings.requestTimeout
    );
    this.notificationCursor = response.nextSeq;
    this.notifications.apply(response.delivered);
    return response.delivered;
  }
}

export { BingoPlayerClient };
