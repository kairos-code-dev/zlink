const { BingoNotificationInbox } = require('./bingo-notification-inbox');
const { actorDisplayName } = require('../Shared/Contracts/messages');
const { SampleTimings } = require('../Shared/Configuration/sample-names');

class BingoPlayerClient {
  [key: string]: any;
  constructor(actorId, sessionClient) {
    this.actorId = actorId;
    this.displayName = actorDisplayName(actorId);
    this.sessionClient = sessionClient;
    this.notifications = new BingoNotificationInbox(actorId);
    this.notificationCursor = 0;
  }

  async authenticate() {
    const authenticated = await this.sessionClient.request('session-server', 'AuthenticateReq', {
      accessToken: this.actorId
    }, SampleTimings.requestTimeout);
    this.displayName = authenticated.displayName;
    return {
      actorId: authenticated.actorId,
      displayName: authenticated.displayName
    };
  }

  async match() {
    return await this.sessionClient.request('session-server', 'MatchBingoReq', {
      mode: 'four-player'
    }, SampleTimings.requestTimeout);
  }

  async start(roomId) {
    const result = await this.sessionClient.request('session-server', 'StartBingoGameReq', {
      roomId
    }, SampleTimings.requestTimeout);
    if (result.rejected) {
      throw new Error(result.reason);
    }
    return result;
  }

  async syncNotifications() {
    const response = await this.sessionClient.request('session-server', 'BingoNotificationsReq', {
      afterSeq: this.notificationCursor
    }, SampleTimings.requestTimeout);
    this.notificationCursor = response.nextSeq;
    this.notifications.apply(response.delivered);
    return response.delivered;
  }
}

export { BingoPlayerClient };
