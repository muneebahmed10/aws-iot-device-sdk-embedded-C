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
 * @file mqtt_agent_helper.h
 * @brief Functions for running a coreMQTT client in a dedicated thread.
 */
#ifndef MQTT_AGENT_HELPER_H
#define MQTT_AGENT_HELPER_H

#include <pthread.h>

/* MQTT Agent includes. */
#include "core_mqtt_agent.h"

#include "subscription_manager.h"

#define MQTT_AGENT_COMMAND_WAIT_TIME_MS        ( 10000 )


/*-----------------------------------------------------------*/

struct MQTTAgentCommandContext
{
    MQTTStatus_t ret;
    bool completed;
    IncomingPubCallback_t subscribeCallback;
    void * pIncomingPublishCallbackContext;
    MQTTAgentSubscribeArgs_t * pSubscribeArgs;
    SubscriptionElement_t * pSubscriptionList;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

/*-----------------------------------------------------------*/

/**
 * @brief Add a command to call MQTT_Subscribe() for an MQTT connection.
 *
 * @param[in] pAgentContext Handle to the MQTT connection to use.
 * @param[in] pSubscriptionInfo Struct describing topic to subscribe to.
 * @param[in] incomingPublishCallback Incoming publish callback for the subscriptions.
 * @param[in] incomingPublishCallbackContext Context for the publish callback.
 *
 * @return `MQTTSuccess` if the command was posted to the MQTT agents event queue.
 * Otherwise an enumerated error code.
 */
MQTTStatus_t MQTTAgent_SubscribeBlock( MQTTAgentContext_t * pAgentContext,
                                       MQTTSubscribeInfo_t * pSubscriptionInfo,
                                       IncomingPubCallback_t incomingPublishCallback,
                                       void * incomingPublishCallbackContext );

/**
 * @brief Add a command to call MQTT_Unsubscribe() for an MQTT connection.
 *
 * @param[in] pAgentContext Handle to the MQTT connection to use.
 * @param[in] pSubscriptionList List of topics to unsubscribe from.
 *
 * @return `MQTTSuccess` if the command was posted to the MQTT agents event queue.
 * Otherwise an enumerated error code.
 */
MQTTStatus_t MQTTAgent_UnsubscribeBlock( MQTTAgentContext_t * pAgentContext,
                                         MQTTSubscribeInfo_t * pSubscriptionList );

/**
 * @brief Add a command to call MQTT_Publish() for an MQTT connection.
 *
 * @param[in] pAgentContext Handle for the MQTT context to use.
 * @param[in] pPublishInfo MQTT PUBLISH information.
 *
 * @return `MQTTSuccess` if the command was posted to the MQTT agents event queue.
 * Otherwise an enumerated error code.
 */
MQTTStatus_t MQTTAgent_PublishBlock( MQTTAgentContext_t * pAgentContext,
                                     MQTTPublishInfo_t * pPublishInfo );

/**
 * @brief Send a message to the MQTT agent purely to trigger an iteration of its loop,
 * which will result in a call to MQTT_ProcessLoop().  This function can be used to
 * wake the MQTT agent task when it is known data may be available on the connected
 * socket.
 *
 * @param[in] pAgentContext Handle of the MQTT connection to use.
 *
 * @return `MQTTSuccess` if the command was posted to the MQTT agents event queue.
 * Otherwise an enumerated error code.
 */
MQTTStatus_t MQTTAgent_ProcessLoopBlock( MQTTAgentContext_t * pAgentContext );

/**
 * @brief Add a command to call MQTT_Ping() for an MQTT connection.
 *
 * @param[in] pAgentContext Handle of the MQTT connection to use.
 *
 * @return `MQTTSuccess` if the command was posted to the MQTT agents event queue.
 * Otherwise an enumerated error code.
 */
MQTTStatus_t MQTTAgent_PingBlock( MQTTAgentContext_t * pAgentContext );

/**
 * @brief Add a command to disconnect an MQTT connection.
 *
 * @param[in] pAgentContext Handle of the MQTT connection to use.
 *
 * @return `MQTTSuccess` if the command was posted to the MQTT agents event queue.
 * Otherwise an enumerated error code.
 */
MQTTStatus_t MQTTAgent_DisconnectBlock( MQTTAgentContext_t * pAgentContext );



#endif /* MQTT_AGENT_H */
