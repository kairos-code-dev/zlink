class StartBingoGameHandler {
  async handle(room, actor, request) {
    return await room.start(actor, request);
  }
}

module.exports = { StartBingoGameHandler };
