class CreateGameHttpHandler {
  constructor(playClientFactory) {
    this.playClientFactory = playClientFactory;
  }

  async handle(request) {
    const client = await this.playClientFactory();
    return await client.request('CreateGame', request, 5000);
  }
}

module.exports = { CreateGameHttpHandler };
