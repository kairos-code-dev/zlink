class MatchBingoHandler {
  constructor(playClient) {
    this.playClient = playClient;
  }

  async handle() {
    return await this.playClient.request('play-server', 'RunBingoRoom', {
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
