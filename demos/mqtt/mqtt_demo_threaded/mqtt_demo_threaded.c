/*
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

/* POSIX includes. */
#include <unistd.h>
#include <pthread.h>

/* Include Demo Config as the first non-system header. */
#include "demo_config.h"

/* MQTT API header. */
#include "mqtt.h"

/* Plaintext sockets transport implementation. */
#include "plaintext_posix.h"

/* Reconnect parameters. */
#include "transport_reconnect.h"

/* Clock for timer. */
#include "clock.h"

#include "demo_queue.h"

/**
 * These configuration settings are required to run the plaintext demo.
 * Throw compilation error if the below configs are not defined.
 */
#ifndef CLIENT_IDENTIFIER
    #error "Please define a unique CLIENT_IDENTIFIER."
#endif

/**
 * Provide default values for undefined configuration settings.
 */
#ifndef BROKER_PORT
    #define BROKER_PORT    ( 1883 )
#endif

#ifndef NETWORK_BUFFER_SIZE
    #define NETWORK_BUFFER_SIZE    ( 1024U )
#endif

/**
 * @brief Length of client identifier.
 */
#define CLIENT_IDENTIFIER_LENGTH            ( ( uint16_t ) ( sizeof( CLIENT_IDENTIFIER ) - 1 ) )

/**
 * @brief Length of MQTT server host name.
 */
#define BROKER_ENDPOINT_LENGTH              ( ( uint16_t ) ( sizeof( BROKER_ENDPOINT ) - 1 ) )

/**
 * @brief Timeout for receiving CONNACK packet in milliseconds.
 */
#define CONNACK_RECV_TIMEOUT_MS             ( 1000U )

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
 * @brief Timeout for MQTT_ProcessLoop function in milliseconds.
 */
#define MQTT_PROCESS_LOOP_TIMEOUT_MS        ( 900U )

/**
 * @brief The maximum time interval in seconds which is allowed to elapse
 *  between two Control Packets.
 *
 *  It is the responsibility of the Client to ensure that the interval between
 *  Control Packets being sent does not exceed the this Keep Alive value. In the
 *  absence of sending any other Control Packets, the Client MUST send a
 *  PINGREQ Packet.
 */
#define MQTT_KEEP_ALIVE_INTERVAL_SECONDS    ( 60U )

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

/**
 * @brief Transport timeout in milliseconds for transport send and receive.
 */
#define TRANSPORT_SEND_RECV_TIMEOUT_MS      ( 20 )

/*-----------------------------------------------------------*/

/**
 * @brief The network buffer must remain valid for the lifetime of the MQTT context.
 */
static uint8_t buffer[ NETWORK_BUFFER_SIZE ];

/*-----------------------------------------------------------*/

/**
 * @brief Connect to MQTT broker with reconnection retries.
 *
 * If connection fails, retry is attempted after a timeout.
 * Timeout value will exponentially increased till maximum
 * timeout value is reached or the number of attempts are exhausted.
 *
 * @param[out] pNetworkContext The output parameter to return the created network context.
 *
 * @return EXIT_FAILURE on failure; EXIT_SUCCESS on successful connection.
 */
static int connectToServerWithBackoffRetries( NetworkContext_t * pNetworkContext );

/**
 * @brief Sends an MQTT CONNECT packet over the already connected TCP socket.
 *
 * @param[in] pMqttContext MQTT context pointer.
 * @param[in] pNetworkContext Pointer to the network context created using Plaintext_Connect.
 *
 * @return EXIT_SUCCESS if an MQTT session is established;
 * EXIT_FAILURE otherwise.
 */
static int establishMqttSession( MQTTContext_t * pMqttContext,
                                 NetworkContext_t * pNetworkContext );

static void subscriptionManager( MQTTContext_t * pMqttContext,
                                 MQTTPacketInfo_t * pPacketInfo,
                                 uint16_t packetIdentifier,
                                 MQTTPublishInfo_t * pPublishInfo );

/*-----------------------------------------------------------*/

static int connectToServerWithBackoffRetries( NetworkContext_t * pNetworkContext )
{
    int returnStatus = EXIT_SUCCESS;
    bool retriesArePending = true;
    SocketStatus_t socketStatus = SOCKETS_SUCCESS;
    TransportReconnectParams_t reconnectParams;
    ServerInfo_t serverInfo;

    /* Initialize information to connect to the MQTT broker. */
    serverInfo.pHostName = BROKER_ENDPOINT;
    serverInfo.hostNameLength = BROKER_ENDPOINT_LENGTH;
    serverInfo.port = BROKER_PORT;

    /* Initialize reconnect attempts and interval */
    Transport_ReconnectParamsReset( &reconnectParams );

    /* Attempt to connect to MQTT broker. If connection fails, retry after
     * a timeout. Timeout value will exponentially increase till maximum
     * attempts are reached.
     */
    do
    {
        /* Establish a TCP connection with the MQTT broker. This example connects
         * to the MQTT broker as specified in BROKER_ENDPOINT and BROKER_PORT
         * at the demo config header. */
        LogInfo( ( "Creating a TCP connection to %.*s:%d.",
                   BROKER_ENDPOINT_LENGTH,
                   BROKER_ENDPOINT,
                   BROKER_PORT ) );
        socketStatus = Plaintext_Connect( pNetworkContext,
                                          &serverInfo,
                                          TRANSPORT_SEND_RECV_TIMEOUT_MS,
                                          TRANSPORT_SEND_RECV_TIMEOUT_MS );

        if( socketStatus != SOCKETS_SUCCESS )
        {
            LogWarn( ( "Connection to the broker failed. Retrying connection with backoff and jitter." ) );
            retriesArePending = Transport_ReconnectBackoffAndSleep( &reconnectParams );
        }

        if( retriesArePending == false )
        {
            LogError( ( "Connection to the broker failed, all attempts exhausted." ) );
            returnStatus = EXIT_FAILURE;
        }
    } while( ( socketStatus != SOCKETS_SUCCESS ) && ( retriesArePending == true ) );

    return returnStatus;
}

/*-----------------------------------------------------------*/

static int establishMqttSession( MQTTContext_t * pMqttContext,
                                 NetworkContext_t * pNetworkContext )
{
    int returnStatus = EXIT_SUCCESS;
    MQTTStatus_t mqttStatus;
    MQTTConnectInfo_t connectInfo;
    bool sessionPresent;
    MQTTFixedBuffer_t networkBuffer;
    MQTTApplicationCallbacks_t callbacks;
    TransportInterface_t transport;

    assert( pMqttContext != NULL );
    assert( pNetworkContext != NULL );

    /* Fill in TransportInterface send and receive function pointers.
     * For this demo, TCP sockets are used to send and receive data
     * from network. Network context is socket file descriptor.*/
    transport.pNetworkContext = pNetworkContext;
    transport.send = Plaintext_Send;
    transport.recv = Plaintext_Recv;

    /* Fill the values for network buffer. */
    networkBuffer.pBuffer = buffer;
    networkBuffer.size = NETWORK_BUFFER_SIZE;

    /* Application callbacks for receiving incoming publishes and incoming acks
     * from MQTT library. */
    callbacks.appCallback = subscriptionManager;

    /* Application callback for getting the time for MQTT library. This time
     * function will be used to calculate intervals in MQTT library.*/
    callbacks.getTime = Clock_GetTimeMs;

    /* Initialize MQTT library. */
    mqttStatus = MQTT_Init( pMqttContext, &transport, &callbacks, &networkBuffer );

    if( mqttStatus != MQTTSuccess )
    {
        returnStatus = EXIT_FAILURE;
        LogError( ( "MQTT init failed with status %u.", mqttStatus ) );
    }
    else
    {
        /* Establish MQTT session by sending a CONNECT packet. */

        /* Start with a clean session i.e. direct the MQTT broker to discard any
         * previous session data. Also, establishing a connection with clean session
         * will ensure that the broker does not store any data when this client
         * gets disconnected. */
        connectInfo.cleanSession = true;

        /* The client identifier is used to uniquely identify this MQTT client to
         * the MQTT broker. In a production device the identifier can be something
         * unique, such as a device serial number. */
        connectInfo.pClientIdentifier = CLIENT_IDENTIFIER;
        connectInfo.clientIdentifierLength = CLIENT_IDENTIFIER_LENGTH;

        /* The maximum time interval in seconds which is allowed to elapse
         * between two Control Packets.
         * It is the responsibility of the Client to ensure that the interval between
         * Control Packets being sent does not exceed the this Keep Alive value. In the
         * absence of sending any other Control Packets, the Client MUST send a
         * PINGREQ Packet. */
        connectInfo.keepAliveSeconds = MQTT_KEEP_ALIVE_INTERVAL_SECONDS;

        /* Username and password for authentication. Not used in this demo. */
        connectInfo.pUserName = NULL;
        connectInfo.userNameLength = 0U;
        connectInfo.pPassword = NULL;
        connectInfo.passwordLength = 0U;

        /* Send MQTT CONNECT packet to broker. */
        mqttStatus = MQTT_Connect( pMqttContext, &connectInfo, NULL, CONNACK_RECV_TIMEOUT_MS, &sessionPresent );

        if( mqttStatus != MQTTSuccess )
        {
            returnStatus = EXIT_FAILURE;
            LogError( ( "Connection with MQTT broker failed with status %u.", mqttStatus ) );
        }
        else
        {
            LogInfo( ( "MQTT connection successfully established with broker.\n\n" ) );
        }
    }

    return returnStatus;
}

/*-----------------------------------------------------------*/

typedef enum operationType {
    PROCESSLOOP,
    PUBLISH,
    SUBSCRIBE,
    UNSUBSCRIBE,
    PING,
    DISCONNECT,
    CONNECT
} CommandType_t;

typedef struct CommandContext {
    /* Synchronization for boolean, not return status. */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool complete;
    MQTTStatus_t returnStatus;
    DeQueue_t * pResponseQueue;
} CommandContext_t;

typedef void (* CommandCallback_t )( CommandContext_t * );

typedef struct Command {
    CommandType_t commandType;
    MQTTPublishInfo_t publishInfo;
    MQTTSubscribeInfo_t subscribeInfo;
    size_t subscriptionCount;
    char pTopicName[ 100 ];
    uint8_t pPublishPayload[ 100 ];
    CommandContext_t * pContext;
    CommandCallback_t callback;
} Command_t;

typedef struct ackInfo {
    uint16_t packetId;
    //TODO a single subscribe can be for multiple topics, so to avoid dynamic allocation
    //this should be moved to the command context.
    char pTopicFilter[ 100 ];
    uint16_t topicFilterLength;
    CommandContext_t * pCommandContext;
    CommandCallback_t callback;
} AckInfo_t;

typedef struct subscriptionElement {
    char pTopicFilter[ 100 ];
    uint16_t topicFilterLength;
    DeQueue_t * responseQueue;
} SubscriptionElement_t;

#define PENDING_ACKS_MAX_SIZE       20
#define SUBSCRIPTIONS_MAX_COUNT     10
#define PUBLISH_COUNT               32
#define PUBLISH_DELAY_MS            50

static MQTTContext_t globalMqttContext;
static AckInfo_t pendingAcks[ PENDING_ACKS_MAX_SIZE ];
static SubscriptionElement_t subscriptions[ 10 ];
static DeQueue_t globalDequeue;

static DeQueue_t commandQueue;
static DeQueue_t responseQueue1;
static DeQueue_t responseQueue2;

static void initializeCommandContext( CommandContext_t * pContext )
{
    pthread_mutex_init( &( pContext->lock ), NULL );
    pthread_cond_init( &( pContext->cond ), NULL );
    pContext->complete = false;
    pContext->pResponseQueue = NULL;
    pContext->returnStatus = MQTTSuccess;
}

static void destroyCommandContext( CommandContext_t * pContext )
{
    pthread_mutex_destroy( &( pContext->lock ) );
    pthread_cond_destroy( &( pContext->cond ) );
}

static void addPendingAck( uint16_t packetId,
                           const char * pTopicFilter,
                           size_t topicFilterLength,
                           CommandContext_t * pContext,
                           CommandCallback_t callback )
{
    int32_t i = 0;
    for( i = 0; i < PENDING_ACKS_MAX_SIZE; i++ )
    {
        if( pendingAcks[ i ].packetId == MQTT_PACKET_ID_INVALID )
        {
            pendingAcks[ i ].packetId = packetId;
            pendingAcks[ i ].pCommandContext = pContext;
            pendingAcks[ i ].callback = callback;
            memcpy( pendingAcks[ i ].pTopicFilter, pTopicFilter, topicFilterLength );
            pendingAcks[ i ].topicFilterLength = topicFilterLength;
            break;
        }
    }
}

static AckInfo_t popAck( uint16_t packetId )
{
    int32_t i = 0;
    AckInfo_t ret = { 0 };
    for( i = 0; i < PENDING_ACKS_MAX_SIZE; i++ )
    {
        if( pendingAcks[ i ].packetId == packetId )
        {
            ret = pendingAcks[ i ];
            pendingAcks[ i ].packetId = MQTT_PACKET_ID_INVALID;
            pendingAcks[ i ].topicFilterLength = 0;
            pendingAcks[ i ].pCommandContext = NULL;
            pendingAcks[ i ].callback = NULL;
            break;
        }
    }
    return ret;
}

static void addSubscription( const char * pTopicFilter, size_t topicFilterLength, DeQueue_t * pQueue )
{
    int32_t i = 0;
    for( i = 0; i < SUBSCRIPTIONS_MAX_COUNT; i++ )
    {
        if( subscriptions[ i ].topicFilterLength == 0 )
        {
            subscriptions[ i ].topicFilterLength = topicFilterLength;
            subscriptions[ i ].responseQueue = pQueue;
            memcpy( subscriptions[ i ].pTopicFilter, pTopicFilter, topicFilterLength );
            break;
        }
    }
}

static void removeSubscription( const char * pTopicFilter, size_t topicFilterLength, DeQueue_t * pQueue )
{
    ( void ) pQueue;
    int32_t i = 0;
    for( i = 0; i< SUBSCRIPTIONS_MAX_COUNT; i++ )
    {
        if( subscriptions[ i ].topicFilterLength == topicFilterLength )
        {
            if( ( strncmp( subscriptions[ i ].pTopicFilter, pTopicFilter, topicFilterLength ) == 0 ) && true )
            {
                subscriptions[ i ].topicFilterLength = 0;
                subscriptions[ i ].responseQueue = NULL;
                break;
            }
        }
    }
}

static Command_t * createCommand( CommandType_t commandType,
                                  MQTTPublishInfo_t * pPublishInfo,
                                  MQTTSubscribeInfo_t * pSubscriptionInfo,
                                  size_t subscriptionCount,
                                  CommandContext_t * context,
                                  CommandCallback_t callback )
{
    Command_t * pCommand = ( Command_t * ) malloc( sizeof( Command_t ) );
    pCommand->commandType = commandType;
    pCommand->subscriptionCount = subscriptionCount;
    pCommand->pContext = context;
    pCommand->callback = callback;

    /* Copy publish info. */
    if( pPublishInfo != NULL )
    {
        pCommand->publishInfo = *pPublishInfo;
        pCommand->publishInfo.pTopicName = pCommand->pTopicName;
        pCommand->publishInfo.pPayload = pCommand->pPublishPayload;
        memcpy( pCommand->pTopicName, pPublishInfo->pTopicName, pPublishInfo->topicNameLength );
        memcpy( pCommand->pPublishPayload, pPublishInfo->pPayload, pPublishInfo->payloadLength );
    }

    /* Copy subscription info. */
    if( pSubscriptionInfo != NULL )
    {
        pCommand->subscribeInfo = *pSubscriptionInfo;
        pCommand->subscribeInfo.pTopicFilter = pCommand->pTopicName;
        memcpy( pCommand->pTopicName, pSubscriptionInfo->pTopicFilter, pSubscriptionInfo->topicFilterLength );
        pCommand->subscribeInfo.pTopicFilter = pCommand->pTopicName;
        //memcpy( &( pCommand->subscribeInfo ), pSubscriptionInfo, sizeof( MQTTSubscribeInfo_t ) * subscriptionCount );
    }
    return pCommand;
}

static void addCommandToQueue( Command_t * pCommand )
{
    DeQueueElement_t * pQueueElement = DeQueueElement_Create( pCommand, sizeof( Command_t ), free );
    DeQueue_PushBack( &commandQueue, pQueueElement );
}

static void destroyPublishInfo( void * pPublish )
{
    MQTTPublishInfo_t * pPublishInfo = ( MQTTPublishInfo_t * ) pPublish;
    free( ( void * ) pPublishInfo->pTopicName );
    free( ( void * ) pPublishInfo->pPayload );
    free( pPublishInfo );
}

static void copyPublishToQueue( MQTTPublishInfo_t * pPublishInfo, DeQueue_t * pResponseQueue )
{
    MQTTPublishInfo_t * pCopiedPublish = ( MQTTPublishInfo_t * ) malloc( sizeof( MQTTPublishInfo_t ) );
    char * pTopicName = ( char * ) malloc( ( pPublishInfo->topicNameLength + 1 ) * sizeof( char ) );
    char * pPayload = ( char * ) malloc( ( pPublishInfo->payloadLength + 1 ) * sizeof( char ) );
    memcpy( pCopiedPublish, pPublishInfo, sizeof( MQTTPublishInfo_t ) );
    memcpy( pTopicName, pPublishInfo->pTopicName, pPublishInfo->topicNameLength );
    memcpy( pPayload, pPublishInfo->pPayload, pPublishInfo->payloadLength );
    pCopiedPublish->pTopicName = pTopicName;
    pCopiedPublish->pPayload = pPayload;
    DeQueueElement_t * pNewElement = DeQueueElement_Create( pCopiedPublish, sizeof( MQTTPublishInfo_t ), destroyPublishInfo );
    DeQueue_PushBack( pResponseQueue, pNewElement );
}

static MQTTStatus_t processCommand( Command_t * pCommand )
{
    MQTTStatus_t status = MQTTSuccess;
    uint16_t packetId = MQTT_PACKET_ID_INVALID;
    size_t topicFilterLength = 0;
    const char * pTopicFilter = NULL;
    bool addAckToList = false;

    switch( pCommand->commandType )
    {
        case PROCESSLOOP:
            LogInfo( ( "Running Process Loop." ) );
            status = MQTT_ProcessLoop( &globalMqttContext, MQTT_PROCESS_LOOP_TIMEOUT_MS );
            break;
        case PUBLISH:
            if( pCommand->publishInfo.qos != MQTTQoS0 )
            {
                packetId = MQTT_GetPacketId( &globalMqttContext );
            }
            LogInfo( ( "Publishing message to %.*s.", ( int ) pCommand->publishInfo.topicNameLength, pCommand->publishInfo.pTopicName ) );
            status = MQTT_Publish( &globalMqttContext, &( pCommand->publishInfo ), packetId );
            pCommand->pContext->returnStatus = status;

            /* Add to pending ack list, or call callback if QoS 0. */
            addAckToList = ( pCommand->publishInfo.qos != MQTTQoS0 ) && ( status == MQTTSuccess );
            break;
            
        case SUBSCRIBE:
        case UNSUBSCRIBE:
            assert( pCommand->subscribeInfo.pTopicFilter != NULL );
            packetId = MQTT_GetPacketId( &globalMqttContext );
            if( pCommand->commandType == SUBSCRIBE )
            {
                LogInfo( ( "Subscribing to %.*s", pCommand->subscribeInfo.topicFilterLength, pCommand->subscribeInfo.pTopicFilter ) );
                status = MQTT_Subscribe( &globalMqttContext, &( pCommand->subscribeInfo ), pCommand->subscriptionCount, packetId );
            }
            else
            {
                LogInfo( ( "Unsubscribing from %.*s", pCommand->subscribeInfo.topicFilterLength, pCommand->subscribeInfo.pTopicFilter ) );
                status = MQTT_Unsubscribe( &globalMqttContext, &( pCommand->subscribeInfo ), pCommand->subscriptionCount, packetId );
            }
            pCommand->pContext->returnStatus = status;
            addAckToList = ( status == MQTTSuccess );
            topicFilterLength = pCommand->subscribeInfo.topicFilterLength;
            pTopicFilter = pCommand->subscribeInfo.pTopicFilter;
            break;
            
        case PING:
            status = MQTT_Ping( &globalMqttContext );
            pCommand->pContext->returnStatus = status;
            break;

        case DISCONNECT:
            status = MQTT_Disconnect( &globalMqttContext );
            //pCommand->pContext->returnStatus = status;
            break;
        case CONNECT:
            /* TODO: Reconnect. I just used this as a generic command while testing to make sure the command loop works. */
            LogInfo( (" Processed Connect Command") );
        default:
            break;
    }

    if( addAckToList )
    {
        addPendingAck( packetId, pTopicFilter, topicFilterLength, pCommand->pContext, pCommand->callback );
    }
    else
    {
        if( pCommand->callback != NULL )
        {
            pCommand->callback( pCommand->pContext );
        }
    }
    
    return status;
}

static bool matchEndWildcards( const char * pTopicFilter,
                                uint16_t topicNameLength,
                                uint16_t topicFilterLength,
                                uint16_t nameIndex,
                                uint16_t filterIndex,
                                bool * pMatch )
{
    bool status = false, endChar = false;

    /* Determine if the last character is reached for both topic name and topic
     * filter for the '#' wildcard. */
    endChar = ( nameIndex == ( topicNameLength - 1U ) ) && ( filterIndex == ( topicFilterLength - 3U ) );

    if( endChar == true )
    {
        /* Determine if the topic filter ends with the '#' wildcard. */
        status = ( pTopicFilter[ filterIndex + 2U ] == '#' );
    }

    if( status == false )
    {
        /* Determine if the last character is reached for both topic name and topic
         * filter for the '+' wildcard. */
        endChar = ( nameIndex == ( topicNameLength - 1U ) ) && ( filterIndex == ( topicFilterLength - 2U ) );

        if( endChar == true )
        {
            /* Filter "sport/+" also matches the "sport/" but not "sport". */
            status = ( pTopicFilter[ filterIndex + 1U ] == '+' );
        }
    }

    *pMatch = status;

    return status;
}

/*-----------------------------------------------------------*/

static bool matchWildcards( const char * pTopicFilter,
                             const char * pTopicName,
                             uint16_t topicNameLength,
                             uint16_t filterIndex,
                             uint16_t * pNameIndex,
                             bool * pMatch )
{
    bool status = false;

    /* Check for wildcards. */
    if( pTopicFilter[ filterIndex ] == '+' )
    {
        /* Move topic name index to the end of the current level.
         * This is identified by '/'. */
        while( ( *pNameIndex < topicNameLength ) && ( pTopicName[ *pNameIndex ] != '/' ) )
        {
            ( *pNameIndex )++;
        }

        ( *pNameIndex )--;
    }
    else if( pTopicFilter[ filterIndex ] == '#' )
    {
        /* Subsequent characters don't need to be checked for the
         * multi-level wildcard. */
        *pMatch = true;
        status = true;
    }
    else
    {
        /* Any character mismatch other than '+' or '#' means the topic
         * name does not match the topic filter. */
        *pMatch = false;
        status = true;
    }

    return status;
}

/*-----------------------------------------------------------*/

static bool topicFilterMatch( const char * pTopicName,
                               uint16_t topicNameLength,
                               const char * pTopicFilter,
                               uint16_t topicFilterLength )
{
    bool status = false, matchFound = false;
    uint16_t nameIndex = 0, filterIndex = 0;

    while( ( nameIndex < topicNameLength ) && ( filterIndex < topicFilterLength ) )
    {
        /* Check if the character in the topic name matches the corresponding
         * character in the topic filter string. */
        if( pTopicName[ nameIndex ] == pTopicFilter[ filterIndex ] )
        {
            /* Handle special corner cases regarding wildcards at the end of
             * topic filters, as documented by the MQTT protocol spec. */
            matchFound = matchEndWildcards( pTopicFilter,
                                             topicNameLength,
                                             topicFilterLength,
                                             nameIndex,
                                             filterIndex,
                                             &status );
        }
        else
        {
            /* Check for matching wildcards. */
            matchFound = matchWildcards( pTopicFilter,
                                          pTopicName,
                                          topicNameLength,
                                          filterIndex,
                                          &nameIndex,
                                          &status );
        }

        if( matchFound == true )
        {
            break;
        }

        /* Increment indexes. */
        nameIndex++;
        filterIndex++;
    }

    if( status == false )
    {
        /* If the end of both strings has been reached, they match. */
        status = ( ( nameIndex == topicNameLength ) && ( filterIndex == topicFilterLength ) );
    }

    return status;
}

static void subscriptionManager( MQTTContext_t * pMqttContext,
                                 MQTTPacketInfo_t * pPacketInfo,
                                 uint16_t packetIdentifier,
                                 MQTTPublishInfo_t * pPublishInfo )
{
    assert( pMqttContext != NULL );
    assert( pPacketInfo != NULL );
    AckInfo_t ackInfo;
    MQTTStatus_t status = MQTTSuccess;
    bool isMatched = false;

    /* Handle incoming publish. The lower 4 bits of the publish packet
     * type is used for the dup, QoS, and retain flags. Hence masking
     * out the lower bits to check if the packet is publish. */
    if( ( pPacketInfo->type & 0xF0U ) == MQTT_PACKET_TYPE_PUBLISH )
    {
        assert( pPublishInfo != NULL );
        /* Handle incoming publish. */
        //handleIncomingPublish( pPublishInfo, packetIdentifier );
        for( int i = 0; i < SUBSCRIPTIONS_MAX_COUNT; i++ )
        {
            if( subscriptions[ i ].topicFilterLength > 0 )
            {
                isMatched = topicFilterMatch( pPublishInfo->pTopicName, pPublishInfo->topicNameLength, subscriptions[ i ].pTopicFilter, subscriptions[ i ].topicFilterLength );
                if( isMatched )
                {
                    LogInfo( ( "Adding publish to response queue for %.*s", subscriptions[ i ].topicFilterLength, subscriptions[ i ].pTopicFilter ) );
                    copyPublishToQueue( pPublishInfo, subscriptions[ i ].responseQueue );
                }
                // if( strncmp( subscriptions[ i ].pTopicFilter, pPublishInfo->pTopicName, pPublishInfo->topicNameLength ) == 0 )
                // {
                //     LogInfo( ( "Adding publish to response queue for %.*s", pPublishInfo->topicNameLength, pPublishInfo->pTopicName ) );
                //     copyPublishToQueue( pPublishInfo, subscriptions[ i ].responseQueue );
                // }
            }
        }
    }
    else
    {
        /* Handle other packets. */
        switch( pPacketInfo->type )
        {
            case MQTT_PACKET_TYPE_PUBACK:
            case MQTT_PACKET_TYPE_PUBCOMP:
                ackInfo = popAck( packetIdentifier );
                if( ackInfo.packetId == packetIdentifier )
                {
                    ackInfo.pCommandContext->returnStatus = status;
                    if( ackInfo.callback != NULL )
                    {
                        ackInfo.callback( ackInfo.pCommandContext );
                    }
                }
                break;

            case MQTT_PACKET_TYPE_SUBACK:
                ackInfo = popAck( packetIdentifier );
                if( ackInfo.packetId == packetIdentifier )
                {
                    LogInfo( ( "Adding subscription to %.*s", ackInfo.topicFilterLength, ackInfo.pTopicFilter ) );
                    LogInfo( ( "Filter length: %d", ackInfo.topicFilterLength ) );
                    addSubscription( ackInfo.pTopicFilter,
                                     ackInfo.topicFilterLength,
                                     ackInfo.pCommandContext->pResponseQueue );
                }
                else
                {
                    status = MQTTBadResponse;
                }
                ackInfo.pCommandContext->returnStatus = status;
                if( ackInfo.callback != NULL )
                {
                    ackInfo.callback( ackInfo.pCommandContext );
                }
                break;

            case MQTT_PACKET_TYPE_UNSUBACK:
                ackInfo = popAck( packetIdentifier );
                if( ackInfo.packetId == packetIdentifier )
                {
                    LogInfo( ( "Removing subscription to %.*s", ackInfo.topicFilterLength, ackInfo.pTopicFilter ) );
                    removeSubscription( ackInfo.pTopicFilter, ackInfo.topicFilterLength, ackInfo.pCommandContext->pResponseQueue );
                }
                else
                {
                    status = MQTTBadResponse;
                }
                ackInfo.pCommandContext->returnStatus = status;
                if( ackInfo.callback != NULL )
                {
                    ackInfo.callback( ackInfo.pCommandContext );
                }
                
                break;

            case MQTT_PACKET_TYPE_PUBREC:
            case MQTT_PACKET_TYPE_PUBREL:
                break;

            case MQTT_PACKET_TYPE_PINGRESP:

                /* Nothing to be done from application as library handles
                 * PINGRESP. */
                LogWarn( ( "PINGRESP should not be handled by the application "
                           "callback when using MQTT_ProcessLoop.\n\n" ) );
                break;

            /* Any other packet type is invalid. */
            default:
                LogError( ( "Unknown packet type received:(%02x).\n\n",
                            pPacketInfo->type ) );
        }
    }
}

static void commandLoop()
{
    DeQueueElement_t * pElement;
    Command_t * pCommand;
    Command_t * pNewCommand = NULL;
    static int counter = 0;
    int32_t breakCounter = 2;
    // while ( pElement = DeQueue_PopFront( &commandQueue ) )
    // {
    //     pCommand = ( Command_t * ) ( pElement->pData );
    //     processCommand( pCommand );
    //     counter++;
    //     if( pCommand->commandType == PROCESSLOOP )
    //     {
    //         pNewCommand = createCommand( PROCESSLOOP, NULL, NULL, 0, NULL, NULL );
    //         addCommandToQueue( pNewCommand );
    //     }
    //     DeQueueElement_Destroy( pElement );
    // }
    while( 1 )
    {
        while( ( pElement = DeQueue_PopFront( &commandQueue ) ) )
        {
            pCommand = ( Command_t * ) ( pElement->pData );
            processCommand( pCommand );
            counter++;
            if( pCommand->commandType == PROCESSLOOP )
            {
                pNewCommand = createCommand( PROCESSLOOP, NULL, NULL, 0, NULL, NULL );
                addCommandToQueue( pNewCommand );
                counter--;
            }
            if( pCommand->commandType == UNSUBSCRIBE )
            {
                breakCounter = 1;
            }
            DeQueueElement_Destroy( pElement );
            //Pretty ugly but it's to signal that we should break after one more iteration.
            if( breakCounter == 0 )
            {
                break;
            }
            if( breakCounter == 1 )
            {
                breakCounter--;
            }
            if( counter >= PUBLISH_COUNT + 1)
            {
                //breakCounter = 0;
            }
        }
        Clock_SleepMs( 200 );
        if( counter >= PUBLISH_COUNT + 1)
        {
            break;
        }
    }
    LogInfo( ( "Creating Disconnect operation" ) );
    pNewCommand = createCommand( DISCONNECT, NULL, NULL, 0, NULL, NULL );
    processCommand( pNewCommand );
    LogInfo( ( "Disconnected from broker" ) );
    free( pNewCommand );
    return;
}

static void comCallback( CommandContext_t * pContext )
{
    pthread_mutex_lock( &( pContext->lock ) );
    pContext->complete = true;
    pthread_mutex_unlock( &( pContext->lock ) );
    pthread_cond_signal( &( pContext->cond ) );
    return;
}

void * thread1( void * args )
{
    ( void ) args;
    //DeQueueElement_t * pNewElement = NULL;
    Command_t * pCommand = NULL;
    MQTTPublishInfo_t publishInfo = { 0 };
    char payloadBuf[ 100 ];
    char topicBuf[ 100 ];
    publishInfo.qos = MQTTQoS2;
    //publishInfo.pTopicName = "thread/2/filter";
    snprintf( topicBuf, 100, "thread/1/1/filter");
    publishInfo.pTopicName = topicBuf;
    publishInfo.topicNameLength = strlen( publishInfo.pTopicName );
    snprintf( payloadBuf, 100, "Hello World! %d", 1 );
    publishInfo.pPayload = payloadBuf;
    publishInfo.payloadLength = strlen( publishInfo.pPayload );
    //FILE * tty1 = fopen( "/dev/ttys007", "w" );
    FILE * tty1 = stdout;

    // for( int i = 0; i < 10; i++ )
    // {
    //     pCommand = createCommand( CONNECT, NULL, NULL, 0, NULL, NULL );
    //     LogInfo( ("Adding connect command to queue from thread 1") );
    //     addCommandToQueue( pCommand );
    //     LogInfo( ("Thread 1 added connect command") );
    //     Clock_SleepMs( 250 );
    // }

    LogInfo( ( "Topic name: %.*s", publishInfo.topicNameLength, publishInfo.pTopicName ) );
    LogInfo( ( "Name length: %d", publishInfo.topicNameLength ) );

    CommandContext_t context = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };
    CommandContext_t * contexts[PUBLISH_COUNT] = { 0 };

    /* Synchronous publishes. */
    for( int i = 0; i < PUBLISH_COUNT / 2; i++ )
    {
        // contexts[ i ] = ( CommandContext_t * ) malloc( sizeof( CommandContext_t ) );
        // initializeCommandContext( contexts[ i ] );
        // contexts[ i ]->pResponseQueue = &responseQueue1;

        snprintf( payloadBuf, 100, "Hello World! %d", i+1 );
        publishInfo.payloadLength = strlen( payloadBuf );
        snprintf( topicBuf, 100, "thread/1/%i/filter", i+1 );
        publishInfo.topicNameLength = strlen( topicBuf );
        initializeCommandContext( &context );
        context.pResponseQueue = &responseQueue1;
        fprintf( tty1,  "Adding publish operation for message %s \non topic %.*s\n", payloadBuf, publishInfo.topicNameLength, publishInfo.pTopicName );
        pCommand = createCommand( PUBLISH, &publishInfo, NULL, 0, &context, comCallback );
        //pCommand = createCommand( PUBLISH, &publishInfo, NULL, 0, contexts[ i ], comCallback );
        addCommandToQueue( pCommand );

        fprintf( tty1, "Waiting for publish %d to complete.\n", i+1 );
        pthread_mutex_lock( &context.lock );
        while( !context.complete )
        {
            pthread_cond_timedwait( &context.cond, &context.lock, NULL );
        }
        pthread_mutex_unlock( &context.lock );
        destroyCommandContext( &context );
        fprintf( tty1, "Publish operation complete.\n" );
        fprintf( tty1, "\tPublish operation complete. Sleeping for %d ms.\n", PUBLISH_DELAY_MS );
        Clock_SleepMs( PUBLISH_DELAY_MS );
    }

    /* Now do asynchronous. */
    for( int i = PUBLISH_COUNT >> 1; i < PUBLISH_COUNT; i++ )
    {
        contexts[ i ] = ( CommandContext_t * ) malloc( sizeof( CommandContext_t ) );
        initializeCommandContext( contexts[ i ] );
        contexts[ i ]->pResponseQueue = &responseQueue1;
        snprintf( payloadBuf, 100, "Hello World! %d", i+1 );
        publishInfo.payloadLength = strlen( payloadBuf );
        snprintf( topicBuf, 100, "thread/1/%i/filter", i+1 );
        publishInfo.topicNameLength = strlen( topicBuf );
        context.pResponseQueue = &responseQueue1;
        fprintf( tty1,  "Adding publish operation for message %s \non topic %.*s\n", payloadBuf, publishInfo.topicNameLength, publishInfo.pTopicName );
        pCommand = createCommand( PUBLISH, &publishInfo, NULL, 0, contexts[ i ], comCallback );
        addCommandToQueue( pCommand );
        fprintf( tty1, "\tPublish operation complete. Sleeping for %d ms.\n", PUBLISH_DELAY_MS );
        Clock_SleepMs( PUBLISH_DELAY_MS );
    }

    fprintf( tty1, "Finished publishing\n" );
    /* Wait for publishes to finish before freeing context. */
    for( int i = 0; i < PUBLISH_COUNT; i++)
    {
        if( contexts[i] == NULL )
        {
            continue;
        }
        //fprintf( tty1, "Locking context %d\n", i );
        pthread_mutex_lock( &(contexts[i]->lock) );
        while( !(contexts[ i ]->complete ) )
        {
            pthread_cond_timedwait( &(contexts[ i ]->cond), &(contexts[i]->lock), NULL );
        }
        pthread_mutex_unlock( &(contexts[i]->lock) );
        destroyCommandContext( contexts[ i ] );
        //fprintf( tty1, "Freeing context %d\n", i );
        free( contexts[ i ] );
        contexts[ i ] = NULL;
    }
    //fclose( tty1 );

    // for( int i = 0; i < 10; i++ )
    // {
    //     fprintf( stdout, "Adding element %d to dequeue\n", i );
    //     pNewElement = DeQueueElement_Create( NULL, 0, free );
    //     DeQueue_PushBack( &globalDequeue, pNewElement );
    //     fprintf( stdout, "Sleeping 0.25 seconds\n");
    //     Clock_SleepMs( 250 );
    // }
    return NULL;
}

void * thread2( void * args )
{
    ( void ) args;
    DeQueueElement_t * pRemovedElement = NULL;
    MQTTSubscribeInfo_t subscribeInfo;
    Command_t * pCommand = NULL;
    MQTTPublishInfo_t * pReceivedPublish = NULL;
    static int subCounter = 0;
    subscribeInfo.qos = MQTTQoS0;
    subscribeInfo.pTopicFilter = "thread/1/+/filter";
    subscribeInfo.topicFilterLength = strlen( subscribeInfo.pTopicFilter );
    LogInfo( ( "Topic filter: %.*s", subscribeInfo.topicFilterLength, subscribeInfo.pTopicFilter ) );
    LogInfo( ( "Filter length: %d", subscribeInfo.topicFilterLength ) );

    //FILE * tty2 = fopen( "/dev/ttys008", "w" );
    FILE * tty2 = stdout;

    CommandContext_t context = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };
    initializeCommandContext( &context );
    context.pResponseQueue = &responseQueue2;
    LogInfo( ( "Adding subscribe operation" ) );
    pCommand = createCommand( SUBSCRIBE, NULL, &subscribeInfo, 1, &context, comCallback );
    LogInfo( ( "Topic filter: %.*s", pCommand->subscribeInfo.topicFilterLength, pCommand->subscribeInfo.pTopicFilter ) );
    LogInfo( ( "Topic filter: %.*s", pCommand->subscribeInfo.topicFilterLength, pCommand->pTopicName ) );
    LogInfo( ( "Topic filter: %.*s", pCommand->subscribeInfo.topicFilterLength, subscribeInfo.pTopicFilter ) );
    LogInfo( ( "Filter length: %d", pCommand->subscribeInfo.topicFilterLength ) );
    addCommandToQueue( pCommand );

    fprintf( tty2, "Starting wait on operation.\n" );
    pthread_mutex_lock( &context.lock );
    while( !context.complete )
    {
        pthread_cond_timedwait( &context.cond, &context.lock, NULL );
    }
    pthread_mutex_unlock( &context.lock );
    destroyCommandContext( &context );
    fprintf( tty2, "Operation wait complete.\n" );

    // for( int i = 0; i < 10; i++ )
    // {
    //     pCommand = createCommand( CONNECT, NULL, NULL, 0, NULL, NULL );
    //     LogInfo( ("Adding connect command to queue from thread 2") );
    //     addCommandToQueue( pCommand );
    //     LogInfo( ("Thread 2 added connect command") );
    //     Clock_SleepMs( 100 );
    // }

    while( 1 )
    {
        while( ( pRemovedElement = DeQueue_PopFront( &responseQueue2 ) ) )
        {
            pReceivedPublish = ( MQTTPublishInfo_t * ) ( pRemovedElement->pData );
            fprintf( tty2,  "Received publish on topic %.*s\n", pReceivedPublish->topicNameLength, pReceivedPublish->pTopicName );
            fprintf( tty2,  "Message payload: %.*s\n", ( int ) pReceivedPublish->payloadLength, ( const char * ) pReceivedPublish->pPayload );
            subCounter++;
            DeQueueElement_Destroy( pRemovedElement );
            if( subCounter >= PUBLISH_COUNT )
            {
                break;
            }
        }
        fprintf( tty2, "    No messages queued, received %d publishes, sleeping for %d ms\n", subCounter, 400 );
        Clock_SleepMs( 400 );
        if( subCounter >= PUBLISH_COUNT )
        {
            break;
        }
    }

    fprintf( tty2, "Finished receiving\n" );
    pCommand = createCommand( UNSUBSCRIBE, NULL, &subscribeInfo, 1, &context, comCallback );
    initializeCommandContext( &context );
    context.pResponseQueue = &responseQueue2;
    fprintf( tty2, "Adding unsubscribe operation\n" );
    addCommandToQueue( pCommand );
    fprintf( tty2, "Starting wait on operation\n" );
    pthread_mutex_lock( &context.lock );
    while( !context.complete )
    {
        pthread_cond_timedwait( &context.cond, &context.lock, NULL );
    }
    pthread_mutex_unlock( &context.lock );
    destroyCommandContext( &context );
    fprintf( tty2, "Operation wait complete.\n" );
    //fclose( tty2 );

    // for( int i = 0; i < 10; i++ )
    // {
    //     fprintf( stdout, "Removing element %d from dequeue\n", i );
    //     pRemovedElement = DeQueue_PopFront( &globalDequeue );
    //     if( pRemovedElement != NULL )
    //     {
    //         DeQueueElement_Destroy( pRemovedElement );
    //     }
    //     else
    //     {
    //         fprintf( stdout, "Removed NULL\n");
    //         i--;
    //     }
    //     fprintf( stdout, "Sleeping 0.5 seconds\n");
    //     Clock_SleepMs( 250 );
        
    // }
    return NULL;
}

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
int main( int argc,
          char ** argv )
{
    int returnStatus = EXIT_SUCCESS;
    NetworkContext_t networkContext;
    pthread_t t1, t2;

    ( void ) argc;
    ( void ) argv;

    globalDequeue = DeQueue_Create();
    commandQueue = DeQueue_Create();
    responseQueue1 = DeQueue_Create();
    responseQueue2 = DeQueue_Create();

    /* Create inital process loop command. */
    Command_t * pCommand = createCommand( PROCESSLOOP, NULL, NULL, 0, NULL, NULL );
    addCommandToQueue( pCommand );

    returnStatus = connectToServerWithBackoffRetries( &networkContext );
    returnStatus = establishMqttSession( &globalMqttContext, &networkContext );

    pthread_create( &t2, NULL, thread2, NULL );
    Clock_SleepMs( 100 );
    pthread_create( &t1, NULL, thread1, NULL );

    LogInfo( ( "Calling command loop" ) );
    commandLoop();
    pthread_join( t1, NULL );
    pthread_join( t2, NULL );

    DeQueue_Destroy( &globalDequeue );
    DeQueue_Destroy( &commandQueue );
    DeQueue_Destroy( &responseQueue1 );
    DeQueue_Destroy( &responseQueue2 );

    return returnStatus;
}

/*-----------------------------------------------------------*/
