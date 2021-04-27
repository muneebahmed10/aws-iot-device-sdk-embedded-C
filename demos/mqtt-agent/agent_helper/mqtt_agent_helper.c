/*
 * FreeRTOS V202011.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file mqtt_agent.c
 * @brief Implements an MQTT agent (or daemon task) to enable multithreaded access to
 * coreMQTT.
 *
 * @note Implements an MQTT agent (or daemon task) on top of the coreMQTT MQTT client
 * library.  The agent makes coreMQTT usage thread safe by being the only task (or
 * thread) in the system that is allowed to access the native coreMQTT API - and in
 * so doing, serialises all access to coreMQTT even when multiple tasks are using the
 * same MQTT connection.
 *
 * The agent provides an equivalent API for each coreMQTT API.  Whereas coreMQTT
 * APIs are prefixed "MQTT_", the agent APIs are prefixed "MQTTAgent_".  For example,
 * that agent's MQTTAgent_Publish() API is the thread safe equivalent to coreMQTT's
 * MQTT_Publish() API.
 *
 * See https://www.FreeRTOS.org/mqtt_agent for examples and usage information.
 */

/* Standard includes. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

/* MQTT agent include. */
#include "core_mqtt_serializer.h"
#include "mqtt_agent_helper.h"
#include "mqtt_agent.h"

/*-----------------------------------------------------------*/


static void commandCompleteCallback( void * pContext, MQTTAgentReturnInfo_t * pReturnInfo );

static void subscribeCommandCallback( void * pContext, MQTTAgentReturnInfo_t * pReturnInfo );

static MQTTStatus_t waitForCommand( CommandContext_t * pContext );

static CommandInfo_t * setupCommandInfo( CommandInfo_t * pCommandInfo, CommandContext_t * pContext );

/*-----------------------------------------------------------*/

static void commandCompleteCallback( void * pContext, MQTTAgentReturnInfo_t * pReturnInfo )
{
    CommandContext_t * pCmdContext = ( CommandContext_t * ) pContext;
    pthread_mutex_lock( &( pCmdContext->lock ) );
    pCmdContext->ret = pReturnInfo->returnCode;
    pCmdContext->completed = true;
    pthread_mutex_unlock( &( pCmdContext->lock ) );
    pthread_cond_broadcast( &( pCmdContext->cond ) );
}

static void subscribeCommandCallback( void * pContext, MQTTAgentReturnInfo_t * pReturnInfo )
{
    CommandContext_t * pCmdContext = ( CommandContext_t * ) pContext;
    size_t i;

    if( ( pReturnInfo->returnCode == MQTTSuccess ) && ( pCmdContext->pSubscribeArgs != NULL ) )
    {
        for( i = 0; i < pCmdContext->pSubscribeArgs->numSubscriptions; i++ )
        {
            if( pCmdContext->subscribeCallback != NULL )
            {
                addSubscription( pCmdContext->pSubscriptionList,
                                 pCmdContext->pSubscribeArgs->pSubscribeInfo[ i ].pTopicFilter,
                                 pCmdContext->pSubscribeArgs->pSubscribeInfo[ i ].topicFilterLength,
                                 pCmdContext->subscribeCallback,
                                 pCmdContext->pIncomingPublishCallbackContext );
            }
            else
            {
                removeSubscription( pCmdContext->pSubscriptionList,
                                    pCmdContext->pSubscribeArgs->pSubscribeInfo[ i ].pTopicFilter,
                                    pCmdContext->pSubscribeArgs->pSubscribeInfo[ i ].topicFilterLength );
            }
        }
    }

    pthread_mutex_lock( &( pCmdContext->lock ) );
    pCmdContext->ret = pReturnInfo->returnCode;
    pCmdContext->completed = true;
    pthread_mutex_unlock( &( pCmdContext->lock ) );
    pthread_cond_broadcast( &( pCmdContext->cond ) );
}

/*-----------------------------------------------------------*/

static MQTTStatus_t waitForCommand( CommandContext_t * pContext )
{
    struct timespec now;
    clock_gettime( CLOCK_REALTIME, &now );
    now.tv_sec += MQTT_AGENT_COMMAND_WAIT_TIME_MS / 1000;
    pthread_mutex_lock( &( pContext->lock ) );
    if( !( pContext->completed ) )
    {
        pthread_cond_timedwait( &( pContext->cond ), &( pContext->lock ), &now );
    }
    pthread_mutex_unlock( &( pContext->lock ) );
    if( !( pContext->completed ) )
    {
        LogError( ( "Command did not complete in time" ) );
    }
    return pContext->ret;
}

/*-----------------------------------------------------------*/

static CommandInfo_t * setupCommandInfo( CommandInfo_t * pCommandInfo, CommandContext_t * pContext )
{
    memset( pCommandInfo, 0x00, sizeof( CommandInfo_t ) );
    pCommandInfo->cmdCompleteCallback = commandCompleteCallback;
    pCommandInfo->pCmdCompleteCallbackContext = pContext;
    pCommandInfo->blockTimeMs = 0;
    return pCommandInfo;
}

/*-----------------------------------------------------------*/

MQTTStatus_t MQTTAgent_SubscribeBlock( MQTTAgentContext_t * pAgentContext,
                                       MQTTSubscribeInfo_t * pSubscriptionInfo,
                                       IncomingPubCallback_t incomingPublishCallback,
                                       void * incomingPublishCallbackContext )
{
    CommandContext_t context = { .lock = PTHREAD_MUTEX_INITIALIZER,
                                 .cond = PTHREAD_COND_INITIALIZER,
                                 .completed = false,
                                 .ret = MQTTRecvFailed,
                                 .subscribeCallback = incomingPublishCallback,
                                 .pIncomingPublishCallbackContext = incomingPublishCallbackContext,
                                 .pSubscribeArgs = NULL,
                                 .pSubscriptionList = NULL };
    CommandInfo_t commandInfo;
    MQTTAgentSubscribeArgs_t subscribeArgs;
    subscribeArgs.pSubscribeInfo = pSubscriptionInfo;
    subscribeArgs.numSubscriptions = 1U;
    setupCommandInfo( &commandInfo, &context );
    commandInfo.cmdCompleteCallback = subscribeCommandCallback;
    context.pSubscribeArgs = &subscribeArgs;
    if( pAgentContext != NULL )
    {
        context.pSubscriptionList = pAgentContext->pIncomingCallbackContext;
    }
    else
    {
        return MQTTBadParameter;
    }
    MQTTAgent_Subscribe( pAgentContext,
                         &subscribeArgs,
                         &commandInfo );
    return waitForCommand( &context );
}

/*-----------------------------------------------------------*/

MQTTStatus_t MQTTAgent_UnsubscribeBlock( MQTTAgentContext_t * pAgentContext,
                                         MQTTSubscribeInfo_t * pSubscriptionList )
{
    CommandContext_t context = { .lock = PTHREAD_MUTEX_INITIALIZER,
                                 .cond = PTHREAD_COND_INITIALIZER,
                                 .completed = false,
                                 .ret = MQTTRecvFailed,
                                 .subscribeCallback = NULL,
                                 .pIncomingPublishCallbackContext = NULL,
                                 .pSubscribeArgs = NULL,
                                 .pSubscriptionList = NULL };
    CommandInfo_t commandInfo;
    MQTTAgentSubscribeArgs_t subscribeArgs;
    subscribeArgs.pSubscribeInfo = pSubscriptionList;
    subscribeArgs.numSubscriptions = 1U;
    setupCommandInfo( &commandInfo, &context );
    commandInfo.cmdCompleteCallback = subscribeCommandCallback;
    context.pSubscribeArgs = &subscribeArgs;
    if( pAgentContext != NULL )
    {
        context.pSubscriptionList = pAgentContext->pIncomingCallbackContext;
    }
    else
    {
        return MQTTBadParameter;
    }
    MQTTAgent_Unsubscribe( pAgentContext,
                           &subscribeArgs,
                           &commandInfo );
    return waitForCommand( &context );
}

/*-----------------------------------------------------------*/

MQTTStatus_t MQTTAgent_PublishBlock( MQTTAgentContext_t * pAgentContext,
                                     MQTTPublishInfo_t * pPublishInfo )
{
    CommandContext_t context = { .lock = PTHREAD_MUTEX_INITIALIZER,
                                 .cond = PTHREAD_COND_INITIALIZER,
                                 .completed = false,
                                 .ret = MQTTRecvFailed,
                                 .subscribeCallback = NULL,
                                 .pIncomingPublishCallbackContext = NULL,
                                 .pSubscribeArgs = NULL,
                                 .pSubscriptionList = NULL };
    CommandInfo_t commandInfo;
    MQTTAgent_Publish( pAgentContext,
                       pPublishInfo,
                       setupCommandInfo( &commandInfo, &context ) );
    return waitForCommand( &context );
}

/*-----------------------------------------------------------*/

MQTTStatus_t MQTTAgent_ProcessLoopBlock( MQTTAgentContext_t * pAgentContext )
{
    CommandContext_t context = { .lock = PTHREAD_MUTEX_INITIALIZER,
                                 .cond = PTHREAD_COND_INITIALIZER,
                                 .completed = false,
                                 .ret = MQTTRecvFailed,
                                 .subscribeCallback = NULL,
                                 .pIncomingPublishCallbackContext = NULL,
                                 .pSubscribeArgs = NULL,
                                 .pSubscriptionList = NULL };
    CommandInfo_t commandInfo;
    MQTTAgent_ProcessLoop( pAgentContext, setupCommandInfo( &commandInfo, &context ) );
    return waitForCommand( &context );
}

/*-----------------------------------------------------------*/

MQTTStatus_t MQTTAgent_PingBlock( MQTTAgentContext_t * pAgentContext )
{
    CommandContext_t context = { .lock = PTHREAD_MUTEX_INITIALIZER,
                                 .cond = PTHREAD_COND_INITIALIZER,
                                 .completed = false,
                                 .ret = MQTTRecvFailed,
                                 .subscribeCallback = NULL,
                                 .pIncomingPublishCallbackContext = NULL,
                                 .pSubscribeArgs = NULL,
                                 .pSubscriptionList = NULL };
    CommandInfo_t commandInfo;
    MQTTAgent_Ping( pAgentContext, setupCommandInfo( &commandInfo, &context ) );
    return waitForCommand( &context );
}

/*-----------------------------------------------------------*/

MQTTStatus_t MQTTAgent_DisconnectBlock( MQTTAgentContext_t * pAgentContext )
{
    CommandContext_t context = { .lock = PTHREAD_MUTEX_INITIALIZER,
                                 .cond = PTHREAD_COND_INITIALIZER,
                                 .completed = false,
                                 .ret = MQTTRecvFailed,
                                 .subscribeCallback = NULL,
                                 .pIncomingPublishCallbackContext = NULL,
                                 .pSubscribeArgs = NULL,
                                 .pSubscriptionList = NULL };
    CommandInfo_t commandInfo;
    MQTTAgent_Disconnect( pAgentContext, setupCommandInfo( &commandInfo, &context ) );
    return waitForCommand( &context );
}

/*-----------------------------------------------------------*/
