class StartBingoGameHandler {
  [key: string]: any;
  async handle(room, actor, request) {
    return await room.start(actor, request);
  }
}

export { StartBingoGameHandler };
