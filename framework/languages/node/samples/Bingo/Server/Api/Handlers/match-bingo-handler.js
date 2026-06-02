class MatchBingoHandler {
  constructor(playClient) {
    this.playClient = playClient;
  }

  async handle(request) {
    return await this.playClient
      .requestToChannel('bingo.play', {
        mode: request.mode ?? 'sample',
        players: [
          { actorId: 'p1', numbers: [7] },
          { actorId: 'p2', numbers: [9] },
          { actorId: 'p3', numbers: [7] },
          { actorId: 'p4', numbers: [11] }
        ],
        draws: [7, 9, 11]
      })
      .packetName('AllocateBingoRoom')
      .timeout(10000)
      .submit();
  }
}

module.exports = { MatchBingoHandler };
