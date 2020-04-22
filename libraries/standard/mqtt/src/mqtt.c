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

#include <string.h>

#include "mqtt.h"

static int32_t sendPacket( MQTTContext_t * pContext, size_t bytesToSend )
{
    const uint8_t * pIndex = pContext->networkBuffer.pBuffer;
    size_t bytesRemaining = bytesToSend;
    int32_t totalBytesSent = 0, bytesSent;

    /* Record the time of transmission. */
    uint32_t sendTime = pContext->callbacks.getTime();

    /* Loop until the entire packet is sent. */
    while( bytesRemaining > 0 )
    {
        bytesSent = pContext->transportInterface.send( pContext->transportInterface.networkContext,
                                                       pIndex,
                                                       bytesRemaining );

        if( bytesSent > 0 )
        {
            bytesRemaining -= ( size_t ) bytesSent;
            totalBytesSent += bytesSent;
            pIndex += bytesSent;
        }
        else
        {
            totalBytesSent = -1;
            break;
        }
    }

    /* Update time of last transmission if the entire packet was successfully sent. */
    if( totalBytesSent > -1 )
    {
        pContext->lastPacketTime = sendTime;
    }

    return totalBytesSent;
}

void MQTT_Init( MQTTContext_t * const pContext,
                const MQTTTransportInterface_t * const pTransportInterface,
                const MQTTApplicationCallbacks_t * const pCallbacks,
                const MQTTFixedBuffer_t * const pNetworkBuffer )
{
    memset( pContext, 0x00, sizeof( MQTTContext_t ) );

    pContext->connectStatus = MQTTNotConnected;
    pContext->transportInterface = *pTransportInterface;
    pContext->callbacks = *pCallbacks;
    pContext->networkBuffer = *pNetworkBuffer;

    /* Zero is not a valid packet ID per MQTT spec. Start from 1. */
    pContext->nextPacketId = 1;
}

MQTTStatus_t MQTT_Connect( MQTTContext_t * const pContext,
                           const MQTTConnectInfo_t * const pConnectInfo,
                           const MQTTPublishInfo_t * const pWillInfo,
                           bool * const pSessionPresent )
{
    size_t remainingLength, packetSize;
    int32_t bytesSent;
    MQTTPacketInfo_t incomingPacket;

    MQTTStatus_t status = MQTT_GetConnectPacketSize( pConnectInfo,
                                                     pWillInfo,
                                                     &remainingLength,
                                                     &packetSize );

    if( status == MQTTSuccess )
    {
        status = MQTT_SerializeConnect( pConnectInfo,
                                        pWillInfo,
                                        remainingLength,
                                        &( pContext->networkBuffer ) );
    }

    if( status == MQTTSuccess )
    {
        bytesSent = sendPacket( pContext, packetSize );

        if( bytesSent < 0 )
        {
            status = MQTTSendFailed;
        }
    }

    if( status == MQTTSuccess )
    {
        status = MQTT_GetIncomingPacket( pContext->transportInterface.recv,
                                         pContext->transportInterface.networkContext,
                                         &incomingPacket );
    }

    if( status == MQTTSuccess )
    {
        if( incomingPacket.type == MQTT_PACKET_TYPE_CONNACK )
        {
            status = MQTT_DeserializeAck( &incomingPacket, NULL, pSessionPresent );
        }
        else
        {
            status = MQTTBadResponse;
        }
    }

    if( status == MQTTSuccess )
    {
        pContext->connectStatus = MQTTConnected;
    }

    return status;
}

MQTTStatus_t MQTT_Subscribe( MQTTContext_t * const pContext,
                             const MQTTSubscribeInfo_t * const pSubscriptionList,
                             size_t subscriptionCount )
{
    return MQTTSuccess;
}

MQTTStatus_t MQTT_Publish( MQTTContext_t * const pContext,
                           const MQTTPublishInfo_t * const pPublishInfo )
{
    return MQTTSuccess;
}

MQTTStatus_t MQTT_Ping( MQTTContext_t * const pContext )
{
    return MQTTSuccess;
}

MQTTStatus_t MQTT_Unsubscribe( MQTTContext_t * const pContext,
                               const MQTTSubscribeInfo_t * const pSubscriptionList,
                               size_t subscriptionCount )
{
    return MQTTSuccess;
}

MQTTStatus_t MQTT_Disconnect( MQTTContext_t * const pContext )
{
    return MQTTSuccess;
}

typedef enum MQTTLoopStatus
{
    MQTTReceiveLength,
    MQTTReceiveMessage,
    MQTTPartialReceive,
    MQTTDumpPacket,     /* Used when remaining length exceeds buffer size. */
    MQTTHandleMessage,
    MQTTPing,
    MQTTLoopDone,
    MQTTLoopError,
} MQTTLoopStatus_t;

MQTTStatus_t _sendPublishAcks( MQTTContext_t * const pContext,
                               uint16_t packetId,
                               MQTTPublishState_t * pPublishState,
                               MQTTQoS_t qos )
{
    MQTTStatus_t status = MQTTSuccess;
    MQTTPublishState_t newState;
    /* pContext->controlPacketSent doesn't seem to be part of the context.
     * Does it need to be added? 
     * Additionally, need to update message timestamp if keep alive info part of
     * timestamp. */
    switch( *pPublishState )
    {
        case MQTTPubAckSend:
            /* Send PubAck. Is there a function for this? */
            //status = _sendPacket( pContext, packetId, MQTTPuback );
            //if( status == MQTTSuccess )
            newState = _MQTT_UpdateState( pContext,
                                          packetId,
                                          MQTTPuback,
                                          MQTT_SEND,
                                          MQTTQoS1 );
            if( newState != MQTTPublishDone )
            {
                status = MQTTIllegalState;
            }
            break;
        case MQTTPubRecSend:
            /* Send PubRec. Is there a function for this? */
            //status = _sendPacket( pContext, packetId, MQTTPubrec );
            //if( status == MQTTSuccess )
            newState = _MQTT_UpdateState( pContext,
                                          packetId,
                                          MQTTPubrec,
                                          MQTT_SEND,
                                          MQTTQoS2 );
            if( newState != MQTTPubRelPending )
            {
                status = MQTTIllegalState;
            }
            break;
        case MQTTPubRelSend:
            /* Send PubRel. Is there a function for this? */
            //status = _sendPacket( pContext, packetId, MQTTPubrel );
            //if( status == MQTTSuccess )
            newState = _MQTT_UpdateState( pContext,
                                          packetId,
                                          MQTTPubrel,
                                          MQTT_SEND,
                                          MQTTQoS2 );
            if( newState != MQTTPubCompPending )
            {
                status = MQTTIllegalState;
            }
            break;
        case MQTTPubCompSend:
            /* Send PubComp. Is there a function for this? */
            //status = _sendPacket( pContext, packetId, MQTTPubcomp );
            //if( status == MQTTSuccess )
            newState = _MQTT_UpdateState( pContext,
                                          packetId,
                                          MQTTPubcomp,
                                          MQTT_SEND,
                                          MQTTQoS2 );
            if( newState != MQTTPublishDone )
            {
                status = MQTTIllegalState;
            }
            break;
        default:
            /* Nothing to send. */
            break;
    }
    if( status == MQTTSuccess )
    {
        *pPublishState = newState;
    }
    return status;
}

MQTTStatus_t MQTT_Process( MQTTContext_t * const pContext,
                           uint32_t timeoutMs )
{
    MQTTStatus_t status = MQTTSuccess;
    MQTTGetCurrentTimeFunc_t getCurrentTime = pContext->callbacks.getTime;
    uint32_t entryTime = getCurrentTime();
    MQTTPacketInfo_t incomingPacket;
    MQTTPublishInfo_t publishInfo;
    MQTTPubAckInfo_t puback;
    MQTTLoopStatus_t loopState = MQTTReceiveMessage;
    MQTTPublishState_t publishRecordState = MQTTPublishDone;
    int32_t bytesReceived = 0;
    size_t bytesToReceive = 0;
    int32_t totalBytesReceived = 0;
    MQTTQoS_t ackQoS = MQTTQoS0;
    MQTTPubAckType_t ackType = MQTTPuback;
    bool exitConditionMet = false;

    while( true )
    {
        switch( loopState )
        {
            case MQTTReceiveLength:
                if( ( getCurrentTime() - entryTime ) < timeoutMs )
                {
                    status = MQTT_GetIncomingPacketTypeAndLength( pContext->transportInterface.recv,
                                                                  &incomingPacket );
                    if( status == MQTTSuccess )
                    {
                        loopState = MQTTReceiveMessage;
                        //pContext->lastPacketTime = getCurrentTime();
                    }
                    else if( status == MQTTNoDataAvailable )
                    {
                        loopState = MQTTReceiveLength;
                        /* TODO: if this function should handle keep alive,
                         * then set loopState = MQTTPing. */
                    }
                    else
                    {
                        /* Bad response or network error. */
                        loopState = MQTTLoopError;
                    }
                }
                else
                {
                    /* Time limit reached. */
                    loopState = MQTTLoopDone;
                }
                break;
            case MQTTReceiveMessage:
                if( incomingPacket.remainingLength > pContext->networkBuffer.size )
                {
                    bytesToReceive = pContext->networkBuffer.size;
                }
                else
                {
                    bytesToReceive = incomingPacket.remainingLength;
                }
                
                bytesReceived = pContext->transportInterface.recv( pContext->transportInterface.networkContext,
                                                                   pContext->networkBuffer.pBuffer,
                                                                   bytesToReceive );
                if( bytesReceived < bytesToReceive )
                {
                    /* Partial receive, try again. */
                    loopState = MQTTPartialReceive;
                }
                else if( bytesReceived > bytesToReceive )
                {
                    /* Received too much, error. */
                    loopState = MQTTLoopError;
                    status = MQTTRecvFailed;
                }
                else if( incomingPacket.remainingLength > bytesToReceive )
                {
                    /* Packet exceeds buffer, dump it. */
                    loopState = MQTTDumpPacket;
                }
                else
                {
                    /* Receive succeeded. */
                    loopState = MQTTHandleMessage;
                }
                totalBytesReceived = bytesToReceive;
                break;
            case MQTTDumpPacket:
                /* bytesToReceive must == network buffer size at this point. */
                while( totalBytesReceived < bytesToReceive )
                {
                    bytesReceived = pContext->transportInterface.recv( pContext->transportInterface.networkContext,
                                                                       pContext->networkBuffer.pBuffer,
                                                                       bytesToReceive );
                    totalBytesReceived += bytesReceived;
                    if( bytesReceived != bytesReceived )
                    {
                        /* if a partial receive happens while we are trying to dump
                         * the packet, return an error. */
                        loopState = MQTTLoopError;
                        status = MQTTRecvFailed;
                        break;
                    }
                }

                if( totalBytesReceived == bytesReceived )
                {
                    /* Start receive loop again. */
                    loopState = MQTTReceiveLength;
                }
                break;
            case MQTTPartialReceive:
                /* Receive rest of packet. */
                bytesReceived = pContext->transportInterface.recv( pContext->transportInterface.networkContext,
                                                                   ( pContext->networkBuffer.pBuffer + bytesReceived ),
                                                                   ( bytesToReceive - bytesReceived) );
                totalBytesReceived += bytesReceived;
                if( totalBytesReceived != bytesToReceive )
                {
                    /* Another partial read, return an error this time.
                     * Could also increment a loop variable and go back to
                     * MQTTPartialReceive state if we want additional retries. */
                    loopState = MQTTLoopError;
                    status = MQTTRecvFailed;
                }
                else
                {
                    /* Receive succeeded. */
                    loopState = MQTTHandleMessage;
                }
                break;
            case MQTTHandleMessage:
                incomingPacket.pRemainingData = pContext->networkBuffer.pBuffer;
                /* Set QoS for acks. */
                switch( incomingPacket.type )
                {
                    case MQTT_PACKET_TYPE_PUBACK:
                        ackQoS = MQTTQoS1;
                        ackType = MQTTPuback;
                        break;
                    case MQTT_PACKET_TYPE_PUBREC:
                        ackQoS = MQTTQoS2;
                        ackType = MQTTPubrec;
                        break;
                    case MQTT_PACKET_TYPE_PUBREL:
                        ackQoS = MQTTQoS2;
                        ackType = MQTTPubrel;
                        break;
                    case MQTT_PACKET_TYPE_PUBCOMP:
                        ackQoS = MQTTQoS2;
                        ackType = MQTTPubcomp;
                        break;
                    default:
                        /* Not an ack. */
                        ackQoS = MQTTQoS0;
                        break;
                }
                switch( incomingPacket.type )
                {
                    case MQTT_PACKET_TYPE_PUBLISH:
                        /* TODO: Not sure about pPacketId arg since that's already in incoming packet. */
                        status = MQTT_DeserializePublish( &incomingPacket, NULL, &publishInfo );
                        if( status != MQTTSuccess )
                        {
                            /* Error. */
                            loopState = MQTTLoopError;
                        }
                        else
                        {
                            publishRecordState = _MQTT_UpdateState( pContext,
                                                                    incomingPacket.packetIdentifier,
                                                                    MQTTPublish,
                                                                    MQTT_RECEIVE,
                                                                    publishInfo.qos );
                            status = _sendPublishAcks( pContext,
                                                       incomingPacket.packetIdentifier,
                                                       &publishRecordState,
                                                       publishInfo.qos );
                            if( status == MQTTSuccess )
                            {
                                /* TODO: Should the publish info be passed instead? */
                                pContext->callbacks.appCallback( pContext, &incomingPacket );
                                loopState = MQTTReceiveLength;
                            }
                            else
                            {
                                loopState = MQTTLoopError;
                            }
                        }
                        
                    case MQTT_PACKET_TYPE_PUBACK:
                    case MQTT_PACKET_TYPE_PUBREC:
                    case MQTT_PACKET_TYPE_PUBREL:
                    case MQTT_PACKET_TYPE_PUBCOMP:
                        /* TODO: Not sure about pPacketId and pSessionPresent since they don't appear in API doc. */
                        status = MQTT_DeserializeAck( &incomingPacket, NULL,  NULL );
                        if( status != MQTTSuccess )
                        {
                            loopState = MQTTLoopError;
                        }
                        else
                        {
                            publishRecordState = _MQTT_UpdateState( pContext,
                                                                    incomingPacket.packetIdentifier,
                                                                    ackType,
                                                                    MQTT_RECEIVE,
                                                                    ackQoS );
                            status = _sendPublishAcks( pContext,
                                                       incomingPacket.packetIdentifier,
                                                       &publishRecordState,
                                                       ackQoS );
                            if( status == MQTTSuccess )
                            {
                                /* TODO: There doesn't seem to be a separate callback for acks. */
                                pContext->callbacks.appCallback( pContext, &incomingPacket );
                                loopState = MQTTReceiveLength;
                            }
                            else
                            {
                                loopState = MQTTLoopError;
                            }
                        }
                    case MQTT_PACKET_TYPE_PINGRESP:
                        //pContext->waitingForPingResp = false;
                    case MQTT_PACKET_TYPE_SUBACK:
                        /* Give these to the app provided callback. */
                        pContext->callbacks.appCallback( pContext, &incomingPacket );
                        loopState = MQTTReceiveLength;
                    default:
                        /* Not a publish packet or ack. */
                        break;
                }
                break;
            // case MQTTPing:
            //     if( ( getCurrentTime() - pContext->lastMessageTimestamp ) > pContext->keepAliveInterval )
            //     {
            //         if( pContext->waitingForPingResp )
            //         {
            //             /* Should have received a pingresp by now. */
            //             loopState = MQTTLoopError;
            //             /* TODO: Set status to timeout. */
            //         }
            //         else
            //         {
            //             /* Send PINGREQ. */
            //             MQTT_Ping( pContext );
            //             pContext->waitingForPingResp = true;
            //         }
            //     }
            //     break;
            case MQTTLoopError:
                /* TODO: Signal connection to close. */
                exitConditionMet = true;
                break;
            case MQTTLoopDone:
                exitConditionMet = true;
                break;
            default:
                break;
        }
        if( exitConditionMet )
        {
            break;
        }
    }

    return status;
}

uint16_t MQTT_GetPacketId( MQTTContext_t * const pContext )
{
    uint16_t packetId = pContext->nextPacketId;

    pContext->nextPacketId++;

    if( pContext->nextPacketId == 0 )
    {
        pContext->nextPacketId = 1;
    }

    return packetId;
}
