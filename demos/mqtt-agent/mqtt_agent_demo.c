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

/* Standard includes. */
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>

/* Demo Specific configs. */
#include "demo_config.h"

/* MQTT library includes. */
#include "core_mqtt.h"

/* MQTT agent include. */
#include "mqtt_agent.h"

/* Exponential backoff retry include. */
#include "backoff_algorithm.h"

/* OpenSSL sockets transport implementation. */
#include "openssl_posix.h"
/* Plaintext sockets transport implementation. */
#include "plaintext_posix.h"

/* Clock for timer. */
#include "clock.h"


/**
 * These configuration settings are required to run the demo.
 */
/**
 * These configuration settings are required to run the mutual auth demo.
 * Throw compilation error if the below configs are not defined.
 */
#ifndef AWS_IOT_ENDPOINT
    #error "Please define AWS IoT MQTT broker endpoint(AWS_IOT_ENDPOINT) in demo_config.h."
#endif
#ifndef ROOT_CA_CERT_PATH
    #error "Please define path to Root CA certificate of the MQTT broker(ROOT_CA_CERT_PATH) in demo_config.h."
#endif
#ifndef CLIENT_IDENTIFIER
    #error "Please define a unique client identifier, CLIENT_IDENTIFIER, in demo_config.h."
#endif

/* The AWS IoT message broker requires either a set of client certificate/private key
 * or username/password to authenticate the client. */
#ifndef CLIENT_USERNAME
    #ifndef CLIENT_CERT_PATH
        #error "Please define path to client certificate(CLIENT_CERT_PATH) in demo_config.h."
    #endif
    #ifndef CLIENT_PRIVATE_KEY_PATH
        #error "Please define path to client private key(CLIENT_PRIVATE_KEY_PATH) in demo_config.h."
    #endif
#else

/* If a username is defined, a client password also would need to be defined for
 * client authentication. */
    #ifndef CLIENT_PASSWORD
        #error "Please define client password(CLIENT_PASSWORD) in demo_config.h for client authentication based on username/password."
    #endif

/* AWS IoT MQTT broker port needs to be 443 for client authentication based on
 * username/password. */
    #if AWS_MQTT_PORT != 443
        #error "Broker port, AWS_MQTT_PORT, should be defined as 443 in demo_config.h for client authentication based on username/password."
    #endif
#endif /* ifndef CLIENT_USERNAME */

#ifndef BROKER_ENDPOINT
    #error "Please define an MQTT broker endpoint, BROKER_ENDPOINT, in demo_config.h."
#endif
#ifndef CLIENT_IDENTIFIER
    #error "Please define a unique CLIENT_IDENTIFIER in demo_config.h."
#endif
#define CLIENT_IDENTIFIER_LENGTH                 ( ( uint16_t ) ( sizeof( CLIENT_IDENTIFIER ) - 1 ) )
#define BROKER_ENDPOINT_LENGTH                   ( ( uint16_t ) ( sizeof( BROKER_ENDPOINT ) - 1 ) )
/**
 * Provide default values for undefined configuration settings.
 */
#ifndef BROKER_PORT
    #define BROKER_PORT    ( 1883 )
#endif
#ifndef AWS_MQTT_PORT
    #define AWS_MQTT_PORT    ( 8883 )
#endif

#define AWS_IOT_ENDPOINT_LENGTH         ( ( uint16_t ) ( sizeof( AWS_IOT_ENDPOINT ) - 1 ) )
#define AWS_IOT_MQTT_ALPN               "\x0ex-amzn-mqtt-ca"
#define AWS_IOT_MQTT_ALPN_LENGTH        ( ( uint16_t ) ( sizeof( AWS_IOT_MQTT_ALPN ) - 1 ) )
#define AWS_IOT_PASSWORD_ALPN           "\x04mqtt"
#define AWS_IOT_PASSWORD_ALPN_LENGTH    ( ( uint16_t ) ( sizeof( AWS_IOT_PASSWORD_ALPN ) - 1 ) )

#define CONNECTION_RETRY_MAX_ATTEMPTS            ( 5U )
#define CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS    ( 5000U )
#define CONNECTION_RETRY_BACKOFF_BASE_MS         ( 500U )

#define METRICS_STRING                      "?SDK=" OS_NAME "&Version=" OS_VERSION "&Platform=" HARDWARE_PLATFORM_NAME "&MQTTLib=" MQTT_LIB
#define METRICS_STRING_LENGTH               ( ( uint16_t ) ( sizeof( METRICS_STRING ) - 1 ) )

#ifdef CLIENT_USERNAME
/**
 * @brief Append the username with the metrics string if #CLIENT_USERNAME is defined.
 *
 * This is to support both metrics reporting and username/password based client
 * authentication by AWS IoT.
 */
    #define CLIENT_USERNAME_WITH_METRICS    CLIENT_USERNAME METRICS_STRING
#endif

/**
 * @brief Timeout for receiving CONNACK after sending an MQTT CONNECT packet.
 * Defined in milliseconds.
 */
#define CONNACK_RECV_TIMEOUT_MS           ( 1000U )

/**
 * @brief The maximum time interval in seconds which is allowed to elapse
 *  between two Control Packets.
 *
 *  It is the responsibility of the Client to ensure that the interval between
 *  Control Packets being sent does not exceed the this Keep Alive value. In the
 *  absence of sending any other Control Packets, the Client MUST send a
 *  PINGREQ Packet.
 *//*_RB_ Move to be the responsibility of the agent. */
#define MQTT_KEEP_ALIVE_INTERVAL_SECONDS       ( 60U )

/**
 * @brief Socket send and receive timeouts to use.  Specified in milliseconds.
 */
#define TRANSPORT_SEND_RECV_TIMEOUT_MS    ( 750 )

/* This demo uses both TLS and plaintext. */
struct NetworkContext
{
	void * pParams;
};


/*-----------------------------------------------------------*/

/**
 * @brief Initializes an MQTT context, including transport interface and
 * network buffer.
 *
 * @return `MQTTSuccess` if the initialization succeeds, else `MQTTBadParameter`.
 */
static MQTTStatus_t prvMQTTInit( int32_t mqttContextHandle );

/**
 * @brief Sends an MQTT Connect packet over the already connected TCP socket.
 *
 * @param[in] pxMQTTContext MQTT context pointer.
 * @param[in] xCleanSession If a clean session should be established.
 *
 * @return `MQTTSuccess` if connection succeeds, else appropriate error code
 * from MQTT_Connect.
 */
static MQTTStatus_t prvMQTTConnect( bool xCleanSession, int32_t mqttContextHandle );

/**
 * @brief Connect a TCP socket to the MQTT broker.
 *
 * @param[in] pxNetworkContext Network context.
 *
 * @return `true` if connection succeeds, else `false`.
 */
static bool prvSocketConnect( NetworkContext_t * pNetworkContext, int mqttContextHandle );

/**
 * @brief Disconnect a TCP connection.
 *
 * @param[in] pxNetworkContext Network context.
 *
 * @return `true` if disconnect succeeds, else `false`.
 */
static bool prvSocketDisconnect( NetworkContext_t * pxNetworkContext, int mqttContextHandle );

/**
 * @brief Logs any incoming publish messages received on topics to which there
 * are no subscriptions.  This can happen if the MQTT broker sends control
 * information to the MQTT client on special control topics.
 *
 * @param[in] pxPublishInfo Info of incoming publish.
 * @param[in] The context specified when the MQTT connection was created.
 */
static void prvUnsolicitedIncomingPublishCallback( MQTTPublishInfo_t * pxPublishInfo,
                                                   void * pvContext );


/**
 * @brief Task used to run the MQTT agent.  In this example the first task that
 * is created is responsible for creating all the other demo tasks.  Then,
 * rather than create prvMQTTAgentTask() as a separate task, it simply calls
 * prvMQTTAgentTask() to become the agent task itself.
 *
 * This task calls MQTTAgent_CommandLoop() in a loop, until MQTTAgent_Terminate()
 * is called. If an error occurs in the command loop, then it will reconnect the
 * TCP and MQTT connections.
 *
 * @param[in] pvParameters Parameters as passed at the time of task creation. Not
 * used in this example.
 */
static void * prvMQTTAgentTask( void * pvParameters );

/**
 * @brief The main task used in the MQTT demo.
 *
 * After creating the publisher and subscriber tasks, this task will enter a
 * loop, processing commands from the command queue and calling the MQTT API.
 * After the termination command is received on the command queue, the task
 * will break from the loop.
 *
 * @param[in] pvParameters Parameters as passed at the time of task creation. Not
 * used in this example.
 */
static void prvConnectAndCreateDemoTasks( void * pvParameters );

/**
 * @brief Connects a TCP socket to the MQTT broker, then creates and MQTT
 * connection to the same.
 */
static void prvConnectToMQTTBroker( int32_t mqttContextHandle );

/*
 * Functions that start the tasks demonstrated by this project.
 */

/*-----------------------------------------------------------*/

/**
 * @brief The network context used by the MQTT library transport interface.
 * See https://www.freertos.org/network-interface.html
 */
static NetworkContext_t networkContexts[ MQTT_AGENT_MAX_SIMULTANEOUS_CONNECTIONS ];

/**
 * @brief Global entry time into the application to use as a reference timestamp
 * in the #prvGetTimeMs function. #prvGetTimeMs will always return the difference
 * between the current time and the global entry time. This will reduce the chances
 * of overflow for the 32 bit unsigned integer used for holding the timestamp.
 */
static uint32_t ulGlobalEntryTimeMs;

/*-----------------------------------------------------------*/

static bool isTLSConnection( int mqttContextHandle )
{
    bool ret = false;
    if( mqttContextHandle )
    {
        ret = true;
    }
    return ret;
}

/*-----------------------------------------------------------*/

static MQTTStatus_t prvMQTTInit( int32_t mqttContextHandle )
{
    TransportInterface_t xTransport;
    MQTTStatus_t xReturn;

    /* Fill in Transport Interface send and receive function pointers. */
    xTransport.pNetworkContext = &networkContexts[ mqttContextHandle ];
    if( isTLSConnection( mqttContextHandle ) )
    {
        xTransport.send = Openssl_Send;
        xTransport.recv = Openssl_Recv;
    }else
    {
        xTransport.send = Plaintext_Send;
        xTransport.recv = Plaintext_Recv;
    }

    /* Initialize MQTT library. */
    xReturn = MQTTAgent_Init( mqttContextHandle,
                              &xTransport,
                              Clock_GetTimeMs,

                              /* Callback to execute if receiving publishes on
                               * topics for which there is no subscription. */
                              prvUnsolicitedIncomingPublishCallback,
                              /* Context to pass into the callback.  Not used. */
                              NULL );

    return xReturn;
}

/*-----------------------------------------------------------*/

static MQTTStatus_t prvMQTTConnect( bool xCleanSession, int32_t mqttContextHandle )
{
    MQTTStatus_t xResult;
    bool xSessionPresent = false;
    MQTTConnectInfo_t connectInfo = { 0 };

    /* Many fields are not used in this demo so start with everything at 0. */
    memset( &connectInfo, 0x00, sizeof( connectInfo ) );

    /* Start with a clean session i.e. direct the MQTT broker to discard any
     * previous session data. Also, establishing a connection with clean session
     * will ensure that the broker does not store any data when this client
     * gets disconnected. */
    connectInfo.cleanSession = xCleanSession;

    /* The client identifier is used to uniquely identify this MQTT client to
     * the MQTT broker. In a production device the identifier can be something
     * unique, such as a device serial number. */
    connectInfo.pClientIdentifier = CLIENT_IDENTIFIER;
    connectInfo.clientIdentifierLength = CLIENT_IDENTIFIER_LENGTH;

    /* Set MQTT keep-alive period. It is the responsibility of the application
     * to ensure that the interval between Control Packets being sent does not
     * exceed the Keep Alive value. In the absence of sending any other Control
     * Packets, the Client MUST send a PINGREQ Packet.  This responsibility will
     * be moved inside the agent. */
    connectInfo.keepAliveSeconds = MQTT_KEEP_ALIVE_INTERVAL_SECONDS;

    /* Append metrics when connecting to the AWS IoT Core broker. */
    #ifdef democonfigUSE_AWS_IOT_CORE_BROKER
        #ifdef CLIENT_USERNAME
            connectInfo.pUserName = CLIENT_USERNAME_WITH_METRICS;
            connectInfo.userNameLength = strlen( CLIENT_USERNAME_WITH_METRICS );
            connectInfo.pPassword = CLIENT_PASSWORD;
            connectInfo.passwordLength = strlen( CLIENT_PASSWORD );
        #else
            connectInfo.pUserName = METRICS_STRING;
            connectInfo.userNameLength = METRICS_STRING_LENGTH;
            /* Password for authentication is not used. */
            connectInfo.pPassword = NULL;
            connectInfo.passwordLength = 0U;
        #endif
    #else /* ifdef democonfigUSE_AWS_IOT_CORE_BROKER */
        #ifdef CLIENT_USERNAME
            connectInfo.pUserName = CLIENT_USERNAME;
            connectInfo.userNameLength = ( uint16_t ) strlen( CLIENT_USERNAME );
            connectInfo.pPassword = CLIENT_PASSWORD;
            connectInfo.passwordLength = ( uint16_t ) strlen( CLIENT_PASSWORD );
        #endif /* ifdef CLIENT_USERNAME */
    #endif /* ifdef democonfigUSE_AWS_IOT_CORE_BROKER */

    /* Send MQTT CONNECT packet to broker. MQTT's Last Will and Testament feature
     * is not used in this demo, so it is passed as NULL. */
    xResult = MQTTAgent_Connect( mqttContextHandle,
                                 &connectInfo,
                                 NULL,
                                 CONNACK_RECV_TIMEOUT_MS,
                                 &xSessionPresent );

    LogInfo( ( "Session present: %d\n", xSessionPresent ) );

    /* Resume a session if desired. */
    if( ( xResult == MQTTSuccess ) && !xCleanSession )
    {
        xResult = MQTTAgent_ResumeSession( mqttContextHandle, xSessionPresent );
    }

    return xResult;
}

/*-----------------------------------------------------------*/

static uint32_t generateRandomNumber()
{
    return( rand() );
}

/*-----------------------------------------------------------*/

static bool prvSocketConnect( NetworkContext_t * pNetworkContext, int mqttContextHandle )
{
    bool xConnected = false;
    BackoffAlgorithmStatus_t backoffAlgStatus = BackoffAlgorithmSuccess;
    BackoffAlgorithmContext_t reconnectParams;
    ServerInfo_t serverInfo;
    OpensslCredentials_t opensslCredentials;
    uint16_t nextRetryBackOff;
    OpensslStatus_t opensslStatus = OPENSSL_SUCCESS;
    SocketStatus_t socketStatus = SOCKETS_SUCCESS;
    int32_t socketDescriptor = 0;

    if( isTLSConnection( mqttContextHandle ) )
    {
	    /* Initialize information to connect to the MQTT broker. */
        serverInfo.pHostName = AWS_IOT_ENDPOINT;
        serverInfo.hostNameLength = AWS_IOT_ENDPOINT_LENGTH;
        serverInfo.port = AWS_MQTT_PORT;

        /* Initialize credentials for establishing TLS session. */
        memset( &opensslCredentials, 0, sizeof( OpensslCredentials_t ) );
        opensslCredentials.pRootCaPath = ROOT_CA_CERT_PATH;

        /* If #CLIENT_USERNAME is defined, username/password is used for authenticating
         * the client. */
        #ifndef CLIENT_USERNAME
            opensslCredentials.pClientCertPath = CLIENT_CERT_PATH;
            opensslCredentials.pPrivateKeyPath = CLIENT_PRIVATE_KEY_PATH;
        #endif

        opensslCredentials.sniHostName = AWS_IOT_ENDPOINT;

        if( AWS_MQTT_PORT == 443 )
        {
            /* Pass the ALPN protocol name depending on the port being used.
             * Please see more details about the ALPN protocol for the AWS IoT MQTT
             * endpoint in the link below.
             * https://aws.amazon.com/blogs/iot/mqtt-with-tls-client-authentication-on-port-443-why-it-is-useful-and-how-it-works/
             *
             * For username and password based authentication in AWS IoT,
             * #AWS_IOT_PASSWORD_ALPN is used. More details can be found in the
             * link below.
             * https://docs.aws.amazon.com/iot/latest/developerguide/enhanced-custom-auth-using.html
             */
            #ifdef CLIENT_USERNAME
                opensslCredentials.pAlpnProtos = AWS_IOT_PASSWORD_ALPN;
                opensslCredentials.alpnProtosLen = AWS_IOT_PASSWORD_ALPN_LENGTH;
            #else
                opensslCredentials.pAlpnProtos = AWS_IOT_MQTT_ALPN;
                opensslCredentials.alpnProtosLen = AWS_IOT_MQTT_ALPN_LENGTH;
            #endif
        }
    }
    else
    {
        /* Initialize information to connect to the MQTT broker. */
        serverInfo.pHostName = BROKER_ENDPOINT;
        serverInfo.hostNameLength = BROKER_ENDPOINT_LENGTH;
        serverInfo.port = BROKER_PORT;
    }

    /* Initialize reconnect attempts and interval */
    BackoffAlgorithm_InitializeParams( &reconnectParams,
                                       CONNECTION_RETRY_BACKOFF_BASE_MS,
                                       CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS,
                                       CONNECTION_RETRY_MAX_ATTEMPTS );

    /* Attempt to connect to MQTT broker. If connection fails, retry after a
     * timeout. Timeout value will exponentially increase until the maximum
     * number of attempts are reached.
     */
    do
    {
        /* Establish a TCP connection with the MQTT broker. This example connects to
         * the MQTT broker as specified in democonfigMQTT_BROKER_ENDPOINT and
         * democonfigMQTT_BROKER_PORT at the top of this file. */
        if( isTLSConnection( mqttContextHandle ) )
        {
            LogInfo( ( "Establishing a TLS session to %.*s:%d.",
                       AWS_IOT_ENDPOINT_LENGTH,
                       AWS_IOT_ENDPOINT,
                       AWS_MQTT_PORT ) );
            opensslStatus = Openssl_Connect( pNetworkContext,
                                             &serverInfo,
                                             &opensslCredentials,
                                             TRANSPORT_SEND_RECV_TIMEOUT_MS,
                                             TRANSPORT_SEND_RECV_TIMEOUT_MS );
            xConnected = ( opensslStatus == OPENSSL_SUCCESS ) ? true : false;
        }
        else
        {
            LogInfo( ( "Creating a TCP connection to %.*s:%d.",
                       BROKER_ENDPOINT_LENGTH,
                       BROKER_ENDPOINT,
                       BROKER_PORT ) );
            socketStatus = Plaintext_Connect( pNetworkContext,
                                              &serverInfo,
                                              TRANSPORT_SEND_RECV_TIMEOUT_MS,
                                              TRANSPORT_SEND_RECV_TIMEOUT_MS );
            xConnected = ( socketStatus == SOCKETS_SUCCESS ) ? true : false;
        }

        if( !xConnected )
        {
            LogWarn( ( "Connection to the broker failed. Retrying connection with backoff and jitter." ) );
            backoffAlgStatus = BackoffAlgorithm_GetNextBackoff( &reconnectParams, generateRandomNumber(), &nextRetryBackOff );
        }

        if( backoffAlgStatus == BackoffAlgorithmRetriesExhausted )
        {
            LogError( ( "Connection to the broker failed. All attempts exhausted." ) );
        }
        else if( !xConnected )
        {
            Clock_SleepMs( nextRetryBackOff );
        }
    } while( ( xConnected != true ) && ( backoffAlgStatus == BackoffAlgorithmSuccess ) );

    /* Set the socket wakeup callback and ensure the read block time. */
    if( xConnected )
    {
        if( isTLSConnection( mqttContextHandle ) )
            socketDescriptor = ( ( OpensslParams_t * ) pNetworkContext->pParams )->socketDescriptor;
        else
            socketDescriptor = ( ( PlaintextParams_t * ) pNetworkContext->pParams )->socketDescriptor;

        struct timeval transportTimeout;

        transportTimeout.tv_sec = 0;
        transportTimeout.tv_usec = 0;

        if( !isTLSConnection( mqttContextHandle ) )
        {
            ( void ) setsockopt( socketDescriptor,
                                 SOL_SOCKET,
                                 SO_RCVTIMEO,
                                 &transportTimeout,
                                 ( socklen_t ) sizeof( transportTimeout ) );
        }
    }

    return xConnected;
}

/*-----------------------------------------------------------*/

static bool prvSocketDisconnect( NetworkContext_t * pxNetworkContext, int mqttContextHandle )
{
    bool xDisconnected = false;

    if( isTLSConnection( mqttContextHandle ) )
    {
        LogInfo( ( "Disconnecting TLS connection.\n" ) );
        Openssl_Disconnect( pxNetworkContext );
        xDisconnected = true;
    }
    else
    {
        LogInfo( ( "Disconnecting TCP connection.\n" ) );
        ( void ) Plaintext_Disconnect( pxNetworkContext );
        xDisconnected = true;
    }
    return xDisconnected;
}

/*-----------------------------------------------------------*/

/*-----------------------------------------------------------*/

static void prvUnsolicitedIncomingPublishCallback( MQTTPublishInfo_t * pxPublishInfo,
                                                   void * pvNotUsed )
{
    char cOriginalChar, * pcLocation;

    ( void ) pvNotUsed;

    /* Ensure the topic string is terminated for printing.  This will over-
     * write the message ID, which is restored afterwards. */
    pcLocation = ( char * ) &( pxPublishInfo->pTopicName[ pxPublishInfo->topicNameLength ] );
    cOriginalChar = *pcLocation;
    *pcLocation = 0x00;
    LogWarn( ( "WARN:  Received an unsolicited publish from topic %s", pxPublishInfo->pTopicName ) );
    *pcLocation = cOriginalChar;
}

/*-----------------------------------------------------------*/

static void * prvMQTTAgentTask( void * pvParameters )
{
    bool xNetworkResult = false;
    MQTTStatus_t xMQTTStatus = MQTTSuccess;
    MQTTContext_t * pMqttContext = NULL;
    MQTTContextHandle_t mqttContextHandle = MQTT_AGENT_MAX_SIMULTANEOUS_CONNECTIONS;

    ( void ) pvParameters;

    do
    {
        /* MQTTAgent_CommandLoop() is effectively the agent implementation.  It
         * will manage the MQTT protocol until such time that an error occurs,
         * which could be a disconnect.  If an error occurs the MQTT context on
         * which the error happened is returned so there can be an attempt to
         * clean up and reconnect however the application writer prefers. */
        pMqttContext = MQTTAgent_CommandLoop( &mqttContextHandle );

        /* Context is only returned if error occurred which will may have
         * disconnected the socket from the MQTT broker already. */
        if( pMqttContext != NULL )
        {
            assert( mqttContextHandle < MQTT_AGENT_MAX_SIMULTANEOUS_CONNECTIONS );
            /* Reconnect TCP. */
            xNetworkResult = prvSocketDisconnect( &networkContexts[ mqttContextHandle ], mqttContextHandle );
            xNetworkResult = prvSocketConnect( &networkContexts[ mqttContextHandle ], mqttContextHandle );
            assert( xNetworkResult == true );
            pMqttContext->connectStatus = MQTTNotConnected;
            /* MQTT Connect with a persistent session. */
            xMQTTStatus = prvMQTTConnect( false, mqttContextHandle );
        }
    } while( pMqttContext );

    return NULL;
}

/*-----------------------------------------------------------*/

static void prvConnectToMQTTBroker( int32_t mqttContextHandle )
{
    bool xNetworkStatus = false;
    MQTTStatus_t xMQTTStatus;

    /* Connect a TCP socket to the broker. */
    xNetworkStatus = prvSocketConnect( &networkContexts[ mqttContextHandle ], mqttContextHandle );

    /* Initialise the MQTT context with the buffer and transport interface. */
    xMQTTStatus = prvMQTTInit( mqttContextHandle );

    /* Form an MQTT connection without a persistent session. */
    xMQTTStatus = prvMQTTConnect( true, mqttContextHandle );
    assert( xMQTTStatus == MQTTSuccess );
}
/*-----------------------------------------------------------*/

extern void * plaintext_demo( void * args );
extern void * mutual_auth_demo( void * args );
extern void * shadow_demo( void * args );
extern void * defender_demo( void * args );

struct threadContext{
    MQTTContextHandle_t handle;
    int num;
};

#define NUM_PLAINTEXT_THREADS 5

static void prvConnectAndCreateDemoTasks( void * pvParameters )
{
    ( void ) pvParameters;
    pthread_t t1, t2, t3;
    pthread_t t4;
    pthread_t plaintexts[ NUM_PLAINTEXT_THREADS ];
    struct threadContext ctx[ NUM_PLAINTEXT_THREADS ];

    /* Miscellaneous initialisation. */
    ulGlobalEntryTimeMs = Clock_GetTimeMs();
    PlaintextParams_t plaintextParams = { 0 };
    networkContexts[ 0 ].pParams = &plaintextParams;
    OpensslParams_t opensslParams = { 0 };
    networkContexts[ 1 ].pParams = &opensslParams;

    /* Create the TCP connection to the broker, then the MQTT connection to the
     * same. */
    prvConnectToMQTTBroker( 0 );
    prvConnectToMQTTBroker( 1 );
    MQTTPublishInfo_t publishInfo = { 0 };
    publishInfo.pTopicName = "/topic/name";
    publishInfo.topicNameLength = sizeof( "/topic/name" ) - 1;
    publishInfo.pPayload = "Hello World!";
    publishInfo.payloadLength = sizeof( "Hello World!" ) - 1;
    publishInfo.qos = MQTTQoS1;
    // for ( int i = 0; i < 5; i++ )
    //     MQTTAgent_Publish(0 , &publishInfo, NULL, NULL, 0 );
    pthread_create( &t1, NULL, prvMQTTAgentTask, NULL );
    //pthread_create( &t2, NULL, plaintext_demo, ( void * ) 0 );
    pthread_create( &t3, NULL, mutual_auth_demo, ( void * ) 1 );
    pthread_create( &t4, NULL, shadow_demo, ( void * ) 1 );
    for( int i = 0; i < NUM_PLAINTEXT_THREADS; i++ )
    {
        ctx[ i ].handle = 0;
        ctx[ i ].num = i;
        pthread_create( &plaintexts[ i ], NULL, plaintext_demo, ( void * ) &ctx[ i ] );
    }

    /* Selectively create demo tasks as per the compile time constant settings. */

    /* This task has nothing left to do, so rather than create the MQTT
     * agent as a separate thread, it simply calls the function that implements
     * the agent - in effect turning itself into the agent. */
    //prvMQTTAgentTask( NULL );
    //pthread_join( t2, NULL );
    pthread_join( t3, NULL );
    pthread_join( t4, NULL );
    for( int i = 0; i < NUM_PLAINTEXT_THREADS; i++ )
    {
        pthread_join( plaintexts[ i ], NULL );
    }
    MQTTAgent_Disconnect( 0, NULL, NULL, 0 );
    MQTTAgent_Disconnect( 1, NULL, NULL, 0 );
    MQTTAgent_Terminate( 0 );
    pthread_join( t1, NULL );
}

int main( int argc, const char **argv )
{
    ( void ) argc;
    ( void ) argv;
    prvConnectAndCreateDemoTasks( NULL );
    return 0;
}
