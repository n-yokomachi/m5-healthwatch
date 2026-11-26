#!/usr/bin/env node
import { App } from 'aws-cdk-lib';
import { InfraStack } from '../lib/infra-stack';

const app = new App();
new InfraStack(app, 'HealthWatchInfra', {
  env: { account: process.env.CDK_DEFAULT_ACCOUNT, region: 'ap-northeast-1' },
});
