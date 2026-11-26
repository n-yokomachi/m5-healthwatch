import { CfnOutput, Stack, type StackProps } from 'aws-cdk-lib';
import * as cognito from 'aws-cdk-lib/aws-cognito';
import * as dynamodb from 'aws-cdk-lib/aws-dynamodb';
import { Construct } from 'constructs';

/** The Cognito user pool that signs the agent's callers in, and the agent's table. */
export class InfraStack extends Stack {
  constructor(scope: Construct, id: string, props?: StackProps) {
    super(scope, id, props);

    const pool = new cognito.UserPool(this, 'UserPool', {
      userPoolName: 'HealthWatch',
      selfSignUpEnabled: false,
    });
    const client = pool.addClient('Client', {
      generateSecret: false,
      authFlows: { userPassword: true },
    });

    // The name is fixed because agentcore/agentcore.json and the execution role policy refer to it.
    const table = new dynamodb.Table(this, 'Table', {
      tableName: 'HealthWatch',
      partitionKey: { name: 'pk', type: dynamodb.AttributeType.STRING },
      sortKey: { name: 'sk', type: dynamodb.AttributeType.STRING },
      billingMode: dynamodb.BillingMode.PAY_PER_REQUEST,
    });

    new CfnOutput(this, 'UserPoolId', { value: pool.userPoolId });
    new CfnOutput(this, 'UserPoolClientId', { value: client.userPoolClientId });
    new CfnOutput(this, 'TableName', { value: table.tableName });
  }
}
