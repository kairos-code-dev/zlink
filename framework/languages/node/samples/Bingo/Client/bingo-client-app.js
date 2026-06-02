const { createChannelClient } = require('../../shared/channel-runtime');
const { BingoPlayerClient } = require('./bingo-player-client');
const { SampleNames, SampleTimings } = require('../Shared/Configuration/sample-names');

class BingoClientApp {
  async run(options) {
    const apiClient = await createChannelClient({
      channelName: SampleNames.apiChannel,
      peers: [options.apiEndpoint]
    });
    const playClient = await createChannelClient({
      channelName: SampleNames.playChannel,
      peers: [options.playEndpoint]
    });

    try {
      const clients = SampleNames.actorIds.map((actorId) => new BingoPlayerClient(actorId, apiClient, playClient));
      const authentications = [];
      for (const client of clients) {
        authentications.push(await client.authenticate());
      }

      const firstMatch = await clients[0].match();
      const earlyHostStartRejected = await isRejected(() => clients[0].start(firstMatch.roomId));
      const matches = [firstMatch];
      for (const client of clients.slice(1)) {
        matches.push(await client.match());
      }

      const nonHostStartRejected = await isRejected(() => clients[1].start(firstMatch.roomId));
      const started = await clients[0].start(firstMatch.roomId);
      const timer = await playClient.request('RunBingoRoomTimerReq', {
        roomId: firstMatch.roomId
      }, SampleTimings.requestTimeout);
      const delivered = (await playClient.request('BingoDeliveredNotificationsReq', {}, SampleTimings.requestTimeout)).delivered;
      for (const client of clients) {
        client.notifications.apply(delivered);
      }

      const result = {
        authentications,
        matches,
        started,
        ended: timer.state,
        playerJoinedPushCounts: clients.map((client) => client.notifications.playerJoined.length),
        startedPushCounts: clients.map((client) => client.notifications.started.length),
        drawnPushCounts: clients.map((client) => client.notifications.drawn.length),
        endedPushCounts: clients.map((client) => client.notifications.ended.length),
        earlyHostStartRejected,
        nonHostStartRejected
      };
      validate(result);
      return result;
    } finally {
      await playClient.stop();
      await apiClient.stop();
    }
  }
}

async function isRejected(action) {
  try {
    await action();
    return false;
  } catch {
    return true;
  }
}

function validate(result) {
  requireCondition(new Set(result.authentications.map((auth) => auth.actorId)).size === 4, 'Four clients must authenticate as distinct actors.');
  requireCondition(new Set(result.matches.map((match) => match.roomId)).size === 1, 'All match requests must return the same room.');
  requireCondition(result.matches[0].state.hostActorId === result.authentications[0].actorId, 'First joined actor must become host.');
  requireCondition(result.earlyHostStartRejected, 'Host start must be rejected before four players join.');
  requireCondition(result.nonHostStartRejected, 'Non-host start must be rejected.');
  requireCondition(result.started.state.status === 'Running', 'Host start must put room into Running status.');
  requireCondition(result.ended.status === 'Finished', 'Room must finish through timer draws.');
  requireCondition(result.ended.winners.length > 1, 'The deterministic sample must include same-sequence winners.');
  requireCondition(result.ended.players.every((player) => player.card.length === 25), 'Each player card must contain 25 cells.');
  requireCondition(result.ended.players.every((player) => player.marks[12]), 'Center free cell must start marked.');
  requireCondition(result.startedPushCounts.every((count) => count > 0), 'Each client must receive game-start push.');
  requireCondition(result.drawnPushCounts.every((count) => count > 0), 'Each client must receive draw push.');
  requireCondition(result.endedPushCounts.every((count) => count > 0), 'Each client must receive game-ended push.');
}

function requireCondition(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

module.exports = { BingoClientApp };
