/*
 * AWS IoT Device SDK for Embedded C 202012.01
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

/*
 * Demo for showing the use of MQTT APIs to establish an MQTT session,
 * subscribe to a topic, publish to a topic, receive incoming publishes,
 * unsubscribe from a topic and disconnect the MQTT session.
 *
 * The example shown below uses MQTT APIs to send and receive MQTT packets
 * over the TCP connection established using POSIX sockets.
 * The example is single threaded and uses statically allocated memory;
 * it uses QOS0 and therefore does not implement any retransmission
 * mechanism for Publish messages.
 */

/* Standard includes. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* POSIX includes. */
#include <unistd.h>

/* Include Demo Config as the first non-system header. */
#include "plaintext_config.h"
#include "demo_config.h"

/* MQTT API header. */
#include "mqtt_agent_helper.h"

/* Plaintext sockets transport implementation. */
#include "plaintext_posix.h"

/*Include backoff algorithm header for retry logic.*/
#include "backoff_algorithm.h"

/* Clock for timer. */
#include "clock.h"

/**
 * These configuration settings are required to run the plaintext demo.
 * Throw compilation error if the below configs are not defined.
 */
#ifndef BROKER_ENDPOINT
    #error "Please define an MQTT broker endpoint, BROKER_ENDPOINT, in demo_config.h."
#endif
#ifndef CLIENT_IDENTIFIER
    #error "Please define a unique CLIENT_IDENTIFIER in demo_config.h."
#endif

/**
 * @brief The topic to subscribe and publish to in the example.
 *
 * The topic name starts with the client identifier to ensure that each demo
 * interacts with a unique topic name.
 */
#define MQTT_EXAMPLE_TOPIC                  CLIENT_IDENTIFIER "/example/topic"

/**
 * @brief Length of client MQTT topic.
 */
#define MQTT_EXAMPLE_TOPIC_LENGTH           ( ( uint16_t ) ( sizeof( MQTT_EXAMPLE_TOPIC ) - 1 ) )

/**
 * @brief The MQTT message published in this example.
 */
#define MQTT_EXAMPLE_MESSAGE                "Hello World!"

/**
 * @brief The length of the MQTT message published in this example.
 */
#define MQTT_EXAMPLE_MESSAGE_LENGTH         ( ( uint16_t ) ( sizeof( MQTT_EXAMPLE_MESSAGE ) - 1 ) )

/**
 * @brief Delay between MQTT publishes in seconds.
 */
#define DELAY_BETWEEN_PUBLISHES_SECONDS     ( 1U )

/**
 * @brief Number of PUBLISH messages sent per iteration.
 */
#define MQTT_PUBLISH_COUNT_PER_LOOP         ( 5U )

/**
 * @brief Delay in seconds between two iterations of subscribePublishLoop().
 */
#define MQTT_SUBPUB_LOOP_DELAY_SECONDS      ( 5U )

/*-----------------------------------------------------------*/

/**
 * @brief Array to keep subscription topics.
 * Used to re-subscribe to topics that failed initial subscription attempts.
 */
//static MQTTSubscribeInfo_t pGlobalSubscriptionList[ 1 ];

/**
 * @brief Status of latest Subscribe ACK;
 * it is updated every time the callback function processes a Subscribe ACK
 * and accounts for subscription to a single topic.
 */
static MQTTStatus_t globalSubAckStatus = MQTTServerRefused;

/* How many publishes were received. */
static int receivedCount = 0;

/*-----------------------------------------------------------*/

/**
 * @brief The random number generator to use for exponential backoff with
 * jitter retry logic.
 *
 * @return The generated random number.
 */
static uint32_t generateRandomNumber();

/**
 * @brief A function that connects to MQTT broker,
 * subscribes a topic, publishes to the same
 * topic MQTT_PUBLISH_COUNT_PER_LOOP number of times, and verifies if it
 * receives the Publish message back.
 *
 * @param[in] pNetworkContext Pointer to the network context created using Plaintext_Connect.
 *
 * @return EXIT_FAILURE on failure; EXIT_SUCCESS on success.
 */
static int subscribePublishLoop( MQTTAgentContext_t * mqttContextHandle, const char * topic, uint16_t topicLen );

/**
 * @brief The function to handle the incoming publishes.
 *
 * @param[in] pPublishInfo Pointer to publish info of the incoming publish.
 * @param[in] packetIdentifier Packet identifier of the incoming publish.
 */
static void handleIncomingPublish( void * pTopic, MQTTPublishInfo_t * pPublishInfo );

/**
 * @brief Sends an MQTT SUBSCRIBE to subscribe to #MQTT_EXAMPLE_TOPIC
 * defined at the top of the file.
 *
 * @param[in] pMqttContext MQTT context pointer.
 *
 * @return EXIT_SUCCESS if SUBSCRIBE was successfully sent;
 * EXIT_FAILURE otherwise.
 */
static int subscribeToTopic( MQTTAgentContext_t * pMqttContext, const char * topic, uint16_t topicLen );

/**
 * @brief Sends an MQTT UNSUBSCRIBE to unsubscribe from
 * #MQTT_EXAMPLE_TOPIC defined at the top of the file.
 *
 * @param[in] pMqttContext MQTT context pointer.
 *
 * @return EXIT_SUCCESS if UNSUBSCRIBE was successfully sent;
 * EXIT_FAILURE otherwise.
 */
static int unsubscribeFromTopic( MQTTAgentContext_t * pMqttContext, const char * topic, uint16_t topicLen );

/**
 * @brief Sends an MQTT PUBLISH to #MQTT_EXAMPLE_TOPIC defined at
 * the top of the file.
 *
 * @param[in] pMqttContext MQTT context pointer.
 *
 * @return EXIT_SUCCESS if PUBLISH was successfully sent;
 * EXIT_FAILURE otherwise.
 */
static int publishToTopic( MQTTAgentContext_t * pMqttContext, const char * topic, uint16_t topicLen );

/**
 * @brief Function to handle resubscription of topics on Subscribe
 * ACK failure. Uses an exponential backoff strategy with jitter.
 *
 * @param[in] pMqttContext MQTT context pointer.
 */
static int handleResubscribe( MQTTAgentContext_t * pMqttContext, const char * topic, uint16_t topicLen );

/*-----------------------------------------------------------*/

static uint32_t generateRandomNumber()
{
    return( rand() );
}

/*-----------------------------------------------------------*/

static void handleIncomingPublish( void * pTopic, MQTTPublishInfo_t * pPublishInfo )
{
    assert( pPublishInfo != NULL );
    receivedCount++;
    const char * topic = ( const char * ) pTopic;
    assert( topic );

    /* Process incoming Publish. */
    LogInfo( ( "Incoming QOS : %d.", pPublishInfo->qos ) );

    /* Verify the received publish is for the topic we have subscribed to. */
    if( ( pPublishInfo->topicNameLength >= MQTT_EXAMPLE_TOPIC_LENGTH ) &&
        ( 0 == strncmp( topic,
                        pPublishInfo->pTopicName,
                        pPublishInfo->topicNameLength ) ) )
    {
        LogInfo( ( "Incoming Publish Topic Name: %.*s matches subscribed topic.\n"
                   "Incoming Publish Message : %.*s.\n\n",
                   pPublishInfo->topicNameLength,
                   pPublishInfo->pTopicName,
                   ( int ) pPublishInfo->payloadLength,
                   ( const char * ) pPublishInfo->pPayload ) );
    }
    else
    {
        LogInfo( ( "Incoming Publish Topic Name: %.*s does not match subscribed topic %s.",
                   pPublishInfo->topicNameLength,
                   pPublishInfo->pTopicName,
                   topic) );
    }
}

/*-----------------------------------------------------------*/
#define CONNECTION_RETRY_MAX_ATTEMPTS            ( 5U )
#define CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS    ( 5000U )
#define CONNECTION_RETRY_BACKOFF_BASE_MS         ( 500U )
static int handleResubscribe( MQTTAgentContext_t * pMqttContext, const char * topic, uint16_t topicLen )
{
    int returnStatus = EXIT_SUCCESS;
    MQTTStatus_t mqttStatus = MQTTSuccess;
    BackoffAlgorithmStatus_t backoffAlgStatus = BackoffAlgorithmSuccess;
    BackoffAlgorithmContext_t retryParams;
    uint16_t nextRetryBackOff = 0U;
    MQTTSubscribeInfo_t pGlobalSubscriptionList[ 1 ];

    ( void ) memset( ( void * ) pGlobalSubscriptionList, 0x00, sizeof( pGlobalSubscriptionList ) );

    /* This example subscribes to only one topic and uses QOS0. */
    pGlobalSubscriptionList[ 0 ].qos = MQTTQoS0;
    pGlobalSubscriptionList[ 0 ].pTopicFilter = topic;
    pGlobalSubscriptionList[ 0 ].topicFilterLength = topicLen;

    /* Initialize retry attempts and interval. */
    BackoffAlgorithm_InitializeParams( &retryParams,
                                       CONNECTION_RETRY_BACKOFF_BASE_MS,
                                       CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS,
                                       CONNECTION_RETRY_MAX_ATTEMPTS );

    do
    {
        /* Send SUBSCRIBE packet.
         * Note: reusing the value specified in globalSubscribePacketIdentifier is acceptable here
         * because this function is entered only after the receipt of a SUBACK, at which point
         * its associated packet id is free to use. */
        mqttStatus = MQTTAgent_SubscribeBlock( pMqttContext,
                                               pGlobalSubscriptionList,
                                               handleIncomingPublish,
                                               ( void * ) topic );

        if( ( mqttStatus != MQTTSuccess ) && ( mqttStatus != MQTTServerRefused ) )
        {
            LogError( ( "Failed to send SUBSCRIBE packet to broker with error = %s.",
                        MQTT_Status_strerror( mqttStatus ) ) );
            returnStatus = EXIT_FAILURE;
            break;
        }

        LogInfo( ( "SUBSCRIBE sent for topic %.*s to broker.\n\n",
                   topicLen,
                   topic ) );

        /* Check if recent subscription request has been rejected. globalSubAckStatus is updated
         * in eventCallback to reflect the status of the SUBACK sent by the broker. It represents
         * either the QoS level granted by the server upon subscription, or acknowledgement of
         * server rejection of the subscription request. */
        if( globalSubAckStatus == MQTTServerRefused )
        {
            /* Generate a random number and get back-off value (in milliseconds) for the next re-subscribe attempt. */
            backoffAlgStatus = BackoffAlgorithm_GetNextBackoff( &retryParams, generateRandomNumber(), &nextRetryBackOff );

            if( backoffAlgStatus == BackoffAlgorithmRetriesExhausted )
            {
                LogError( ( "Subscription to topic failed, all attempts exhausted." ) );
                returnStatus = EXIT_FAILURE;
            }
            else if( backoffAlgStatus == BackoffAlgorithmSuccess )
            {
                LogWarn( ( "Server rejected subscription request. Retrying "
                           "connection after %hu ms backoff.",
                           ( unsigned short ) nextRetryBackOff ) );
                Clock_SleepMs( nextRetryBackOff );
            }
        }
    } while( ( globalSubAckStatus == MQTTServerRefused ) && ( backoffAlgStatus == BackoffAlgorithmSuccess ) );

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int subscribeToTopic( MQTTAgentContext_t * pMqttContext, const char * topic, uint16_t topicLen )
{
    int returnStatus = EXIT_SUCCESS;
    MQTTStatus_t mqttStatus;
    MQTTSubscribeInfo_t pGlobalSubscriptionList[ 1 ];

    /* Start with everything at 0. */
    ( void ) memset( ( void * ) pGlobalSubscriptionList, 0x00, sizeof( pGlobalSubscriptionList ) );

    /* This example subscribes to only one topic and uses QOS0. */
    pGlobalSubscriptionList[ 0 ].qos = MQTTQoS0;
    pGlobalSubscriptionList[ 0 ].pTopicFilter = topic;
    pGlobalSubscriptionList[ 0 ].topicFilterLength = topicLen;

    /* Send SUBSCRIBE packet. */
    mqttStatus = MQTTAgent_SubscribeBlock( pMqttContext,
                                           pGlobalSubscriptionList,
                                           handleIncomingPublish,
                                           ( void * ) topic );
    globalSubAckStatus = mqttStatus;

    if( ( mqttStatus != MQTTSuccess ) && ( mqttStatus != MQTTServerRefused ) )
    {
        LogError( ( "Failed to send SUBSCRIBE packet to broker with error = %s.",
                    MQTT_Status_strerror( mqttStatus ) ) );
        returnStatus = EXIT_FAILURE;
    }
    else
    {
        LogInfo( ( "SUBSCRIBE sent for topic %.*s to broker.\n\n",
                   topicLen,
                   topic ) );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int unsubscribeFromTopic( MQTTAgentContext_t * pMqttContext, const char * topic, uint16_t topicLen )
{
    int returnStatus = EXIT_SUCCESS;
    MQTTStatus_t mqttStatus;
    MQTTSubscribeInfo_t pGlobalSubscriptionList[ 1 ];

    /* Start with everything at 0. */
    ( void ) memset( ( void * ) pGlobalSubscriptionList, 0x00, sizeof( pGlobalSubscriptionList ) );

    /* This example subscribes to and unsubscribes from only one topic
     * and uses QOS0. */
    pGlobalSubscriptionList[ 0 ].qos = MQTTQoS0;
    pGlobalSubscriptionList[ 0 ].pTopicFilter = topic;
    pGlobalSubscriptionList[ 0 ].topicFilterLength = topicLen;

    /* Send UNSUBSCRIBE packet. */
    mqttStatus = MQTTAgent_UnsubscribeBlock( pMqttContext,
                                             pGlobalSubscriptionList );

    if( mqttStatus != MQTTSuccess )
    {
        LogError( ( "Failed to send UNSUBSCRIBE packet to broker with error = %s.",
                    MQTT_Status_strerror( mqttStatus ) ) );
        returnStatus = EXIT_FAILURE;
    }
    else
    {
        LogInfo( ( "UNSUBSCRIBE sent for topic %.*s to broker.\n\n",
                   topicLen,
                   topic ) );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int publishToTopic( MQTTAgentContext_t * pMqttContext, const char * topic, uint16_t topicLen )
{
    int returnStatus = EXIT_SUCCESS;
    MQTTStatus_t mqttStatus = MQTTSuccess;
    MQTTPublishInfo_t publishInfo;

    /* Some fields not used by this demo so start with everything at 0. */
    ( void ) memset( ( void * ) &publishInfo, 0x00, sizeof( publishInfo ) );

    /* This example publishes to only one topic and uses QOS0. */
    publishInfo.qos = MQTTQoS0;
    publishInfo.pTopicName = topic;
    publishInfo.topicNameLength = topicLen;
    publishInfo.pPayload = MQTT_EXAMPLE_MESSAGE;
    publishInfo.payloadLength = MQTT_EXAMPLE_MESSAGE_LENGTH;

    /* Send PUBLISH packet. Packet Id is not used for a QoS0 publish.
     * Hence 0 is passed as packet id. */
    mqttStatus = MQTTAgent_PublishBlock( pMqttContext, &publishInfo );

    if( mqttStatus != MQTTSuccess )
    {
        LogError( ( "Failed to send PUBLISH packet to broker with error = %s.",
                    MQTT_Status_strerror( mqttStatus ) ) );
        returnStatus = EXIT_FAILURE;
    }
    else
    {
        LogInfo( ( "PUBLISH send for topic %.*s to broker.",
                   topicLen,
                   topic ) );
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int subscribePublishLoop( MQTTAgentContext_t * mqttContextHandle, const char * topic, uint16_t topicLen )
{
    int returnStatus = EXIT_SUCCESS;
    uint32_t publishCount = 0;
    const uint32_t maxPublishCount = MQTT_PUBLISH_COUNT_PER_LOOP;

    if( returnStatus == EXIT_SUCCESS )
    {
        /* The client is now connected to the broker. Subscribe to the topic
         * as specified in MQTT_EXAMPLE_TOPIC at the top of this file by sending a
         * subscribe packet. This client will then publish to the same topic it
         * subscribed to, so it will expect all the messages it sends to the broker
         * to be sent back to it from the broker. This demo uses QOS0 in Subscribe,
         * therefore, the Publish messages received from the broker will have QOS0. */
        LogInfo( ( "Subscribing to the MQTT topic %.*s.",
                   topicLen,
                   topic ) );
        returnStatus = subscribeToTopic( mqttContextHandle, topic, topicLen );
    }

    /* Check if recent subscription request has been rejected. globalSubAckStatus is updated
     * in eventCallback to reflect the status of the SUBACK sent by the broker. */
    if( ( returnStatus == EXIT_SUCCESS ) && ( globalSubAckStatus == MQTTServerRefused ) )
    {
        /* If server rejected the subscription request, attempt to resubscribe to topic.
         * Attempts are made according to the exponential backoff retry strategy
         * implemented in retryUtils. */
        LogInfo( ( "Server rejected initial subscription request. Attempting to re-subscribe to topic %.*s.",
                   topicLen,
                   topic ) );
        returnStatus = handleResubscribe( mqttContextHandle, topic, topicLen );
    }

    if( returnStatus == EXIT_SUCCESS )
    {
        /* Publish messages with QOS0, receive incoming messages and
         * send keep alive messages. */
        for( publishCount = 0; publishCount < maxPublishCount; publishCount++ )
        {
            LogInfo( ( "Sending Publish to the MQTT topic %.*s.",
                       MQTT_EXAMPLE_TOPIC_LENGTH,
                       MQTT_EXAMPLE_TOPIC ) );
            returnStatus = publishToTopic( mqttContextHandle, topic, topicLen );

            LogInfo( ( "Delay before continuing to next iteration.\n\n" ) );

            /* Leave connection idle for some time. */
            sleep( DELAY_BETWEEN_PUBLISHES_SECONDS );
        }
    }

    while( receivedCount < maxPublishCount )
    {
        LogInfo( ( "Received %d publishes, expected %d", receivedCount, maxPublishCount ) );
        sleep( 1 );
    }

    if( returnStatus == EXIT_SUCCESS )
    {
        /* Unsubscribe from the topic. */
        LogInfo( ( "Unsubscribing from the MQTT topic %.*s.",
                   MQTT_EXAMPLE_TOPIC_LENGTH,
                   MQTT_EXAMPLE_TOPIC ) );
        returnStatus = unsubscribeFromTopic( mqttContextHandle, topic, topicLen );
    }

    /* Reset global SUBACK status variable after completion of subscription request cycle. */
    globalSubAckStatus = MQTTServerRefused;

    return returnStatus;
}

/*-----------------------------------------------------------*/

struct threadContext {
    MQTTAgentContext_t * handle;
    int num;
};

/**
 * @brief Entry point of demo.
 *
 * The example shown below uses MQTT APIs to send and receive MQTT packets
 * over the TCP connection established using POSIX sockets.
 * The example is single threaded and uses statically allocated memory;
 * it uses QOS0 and therefore does not implement any retransmission
 * mechanism for Publish messages. This example runs forever, if connection to
 * the broker goes down, the code tries to reconnect to the broker with exponential
 * backoff mechanism.
 *
 */
void * plaintext_demo( void * args )
{
    int returnStatus = EXIT_SUCCESS, i = 0;
    struct timespec tp;
    struct threadContext * ctx = ( struct threadContext * ) args;
    //MQTTContextHandle_t contextHandle = ( MQTTContextHandle_t ) args;
    char buf[ 50 ];
    uint16_t topicLen;
    snprintf( buf, 50, "%s%d", MQTT_EXAMPLE_TOPIC, ctx->num );
    topicLen = strlen( buf );

    /* Seed pseudo random number generator used in the demo for
     * backoff period calculation when retrying failed network operations
     * with broker. */

    /* Get current time to seed pseudo random number generator. */
    ( void ) clock_gettime( CLOCK_REALTIME, &tp );
    /* Seed pseudo random number generator with nanoseconds. */
    srand( tp.tv_nsec );

    for( i = 0; i < 1; i++ )
    {
        if( 1 )
        {
            /* If TCP connection is successful, execute Subscribe/Publish loop. */
            returnStatus = subscribePublishLoop( ctx->handle, buf, topicLen );
        }

        if( returnStatus == EXIT_SUCCESS )
        {
            /* Log message indicating an iteration completed successfully. */
            LogInfo( ( "Demo completed successfully." ) );
        }

        LogInfo( ( "Short delay before starting the next iteration....\n" ) );
        //sleep( MQTT_SUBPUB_LOOP_DELAY_SECONDS );
    }

    return NULL;
}

/*-----------------------------------------------------------*/

