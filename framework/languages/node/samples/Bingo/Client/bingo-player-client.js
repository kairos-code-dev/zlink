const { BingoNotificationInbox } = require('./bingo-notification-inbox');
const { actorDisplayName } = require('../Shared/Contracts/messages');
const { SampleTimings } = require('../Shared/Configuration/sample-names');

class BingoPlayerClient {
  constructor(actorId, apiClient, playClient) {
    this.actorId = actorId;
    this.displayName = actorDisplayName(actorId);
    this.apiClient = apiClient;
    this.playClient = playClient;
    this.notifications = new BingoNotificationInbox(actorId);
  }

  async authenticate() {
    const authenticated = await this.apiClient.request('AuthenticatePlayerReq', {
      accessToken: this.actorId
    }, SampleTimings.requestTimeout);
    if (!authenticated.accepted) {
      throw new Error(authenticated.reason);
    }
    await this.playClient.request('EnsurePlayerActorReq', {
      actorId: authenticated.actorId,
      displayName: authenticated.displayName
    }, SampleTimings.requestTimeout);
    this.displayName = authenticated.displayName;
    return {
      actorId: authenticated.actorId,
      displayName: authenticated.displayName
    };
  }

  async match() {
    return await this.playClient.request('MatchBingoReq', {
      actorId: this.actorId,
      displayName: this.displayName,
      mode: 'four-player'
    }, SampleTimings.requestTimeout);
  }

  async start(roomId) {
    const result = await this.playClient.request('StartBingoGameReq', {
      roomId,
      actorId: this.actorId,
      displayName: this.displayName
    }, SampleTimings.requestTimeout);
    if (result.rejected) {
      throw new Error(result.reason);
    }
    return result;
  }
}

module.exports = { BingoPlayerClient };
