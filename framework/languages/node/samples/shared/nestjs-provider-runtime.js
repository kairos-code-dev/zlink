require('reflect-metadata');

const { Module } = require('@nestjs/common');
const { NestFactory } = require('@nestjs/core');
const nestjs = require('../../packages/nestjs/dist');

async function createZLinkNestRuntime(options, providers = []) {
  class SampleZLinkModule {}

  Module({
    imports: [nestjs.ZLinkModule.forRoot(options)],
    providers,
    exports: providers.map(providerToken)
  })(SampleZLinkModule);

  const app = await NestFactory.createApplicationContext(SampleZLinkModule, { logger: false, abortOnError: false });
  return {
    app,
    get: (token) => app.get(token, { strict: false }),
    close: () => app.close()
  };
}

function providerToken(provider) {
  return typeof provider === 'function' ? provider : provider.provide;
}

module.exports = { createZLinkNestRuntime, nestjs };
