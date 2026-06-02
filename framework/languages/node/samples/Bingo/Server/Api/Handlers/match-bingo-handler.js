class MatchBingoHandler {
  constructor(playClientFactory) {
    this.playClientFactory = playClientFactory;
  }

  async handle() {
    const playClient = await this.playClientFactory();
    return await playClient.request('play-server', 'RunBingoRoom', {
      players: [
        { actorId: 'p1', numbers: [7] },
        { actorId: 'p2', numbers: [9] },
        { actorId: 'p3', numbers: [7] },
        { actorId: 'p4', numbers: [11] }
      ],
      draws: [7, 9, 11]
    });
  }
}

module.exports = { MatchBingoHandler };
