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
#include "mqtt_agent.h"

#define MQTT_AGENT_COMMAND_WAIT_TIME_MS        ( 10000 )


/*-----------------------------------------------------------*/

struct CommandContext
{
    MQTTStatus_t ret;
    bool completed;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

/*-----------------------------------------------------------*/

/**
 * @brief Add a command to call MQTT_Subscribe() for an MQTT connection.
 *
 * @param[in] mqttContextHandle Handle to the MQTT connection to use.
 * @param[in] pSubscriptionInfo Struct describing topic to subscribe to.
 * @param[in] incomingPublishCallback Incoming publish callback for the subscriptions.
 * @param[in] incomingPublishCallbackContext Context for the publish callback.
 *
 * @return `MQTTSuccess` if the command was posted to the MQTT agents event queue.
 * Otherwise an enumerated error code.
 */
MQTTStatus_t MQTTAgent_SubscribeBlock( MQTTContextHandle_t mqttContextHandle,
                                       MQTTSubscribeInfo_t * pSubscriptionInfo,
                                       IncomingPublishCallback_t incomingPublishCallback,
                                       void * incomingPublishCallbackContext );

/**
 * @brief Add a command to call MQTT_Unsubscribe() for an MQTT connection.
 *
 * @param[in] mqttContextHandle Handle to the MQTT connection to use.
 * @param[in] pSubscriptionList List of topics to unsubscribe from.
 *
 * @return `MQTTSuccess` if the command was posted to the MQTT agents event queue.
 * Otherwise an enumerated error code.
 */
MQTTStatus_t MQTTAgent_UnsubscribeBlock( MQTTContextHandle_t mqttContextHandle,
                                         MQTTSubscribeInfo_t * pSubscriptionList );

/**
 * @brief Add a command to call MQTT_Publish() for an MQTT connection.
 *
 * @param[in] mqttContextHandle Handle for the MQTT context to use.
 * @param[in] pPublishInfo MQTT PUBLISH information.
 *
 * @return `MQTTSuccess` if the command was posted to the MQTT agents event queue.
 * Otherwise an enumerated error code.
 */
MQTTStatus_t MQTTAgent_PublishBlock( MQTTContextHandle_t mqttContextHandle,
                                     MQTTPublishInfo_t * pPublishInfo );

/**
 * @brief Send a message to the MQTT agent purely to trigger an iteration of its loop,
 * which will result in a call to MQTT_ProcessLoop().  This function can be used to
 * wake the MQTT agent task when it is known data may be available on the connected
 * socket.
 *
 * @param[in] mqttContextHandle Handle of the MQTT connection to use.
 * @param[in] blockTimeMS The maximum amount of time in milliseconds to wait for the
 * command to be posted to the MQTT agent should the MQTT agent's event queue be
 * full.  Tasks wait in the Blocked state so don't use any CPU time.
 *
 * @return `MQTTSuccess` if the command was posted to the MQTT agents event queue.
 * Otherwise an enumerated error code.
 */
MQTTStatus_t MQTTAgent_ProcessLoop( MQTTContextHandle_t mqttContextHandle,
                                    uint32_t blockTimeMS );

/**
 * @brief Add a command to call MQTT_Ping() for an MQTT connection.
 *
 * @param[in] mqttContextHandle Handle of the MQTT connection to use.
 *
 * @return `MQTTSuccess` if the command was posted to the MQTT agents event queue.
 * Otherwise an enumerated error code.
 */
MQTTStatus_t MQTTAgent_PingBlock( MQTTContextHandle_t mqttContextHandle );

/**
 * @brief Add a command to disconnect an MQTT connection.
 *
 * @param[in] mqttContextHandle Handle of the MQTT connection to use.
 *
 * @return `MQTTSuccess` if the command was posted to the MQTT agents event queue.
 * Otherwise an enumerated error code.
 */
MQTTStatus_t MQTTAgent_DisconnectBlock( MQTTContextHandle_t mqttContextHandle );



#endif /* MQTT_AGENT_H */
