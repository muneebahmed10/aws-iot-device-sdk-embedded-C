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

#include "demo_queue.h"

DeQueue_t DeQueue_Create()
{
    DeQueue_t dequeue = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };
    /* Initialize mutex with default attributes. */
    pthread_mutex_init( &( dequeue.lock ), NULL );
    /* Condition variable is unused as of now since there wasn't anything that needed it. */
    pthread_cond_init( &( dequeue.cond ), NULL );
    return dequeue;
}

void DeQueue_Destroy( DeQueue_t * pQueue )
{
    DeQueueElement_t * pElement = pQueue->pHead;
    DeQueueElement_t * pTempElement = NULL;

    pthread_mutex_destroy( &( pQueue->lock ) );
    pthread_cond_destroy( &( pQueue->cond ) );

    while( pElement != NULL )
    {
        pTempElement = pElement->pNext;
        DeQueueElement_Destroy( pElement );
        pElement = pTempElement;
    }
}

void DeQueue_PushBack( DeQueue_t * pQueue, DeQueueElement_t * pElement )
{
    DeQueueElement_t * pOldBack = NULL;

    pthread_mutex_lock( &( pQueue->lock ) );

    pOldBack = pQueue->pTail;

    if( pOldBack == NULL )
    {
        pQueue->pHead = pElement;
        pQueue->pTail = pElement;
        pElement->pNext = NULL;
        pElement->pPrevious = NULL;
    }
    else
    {
        pOldBack->pNext = pElement;
        pElement->pPrevious = pOldBack;
        pElement->pNext = NULL;
        pQueue->pTail = pElement;
    }
    
    pthread_mutex_unlock( &( pQueue->lock ) );
}

void DeQueue_PushFront( DeQueue_t * pQueue, DeQueueElement_t * pElement )
{
    DeQueueElement_t * pOldFront = NULL;

    pthread_mutex_lock( &( pQueue->lock ) );

    pOldFront = pQueue->pHead;

    if( pOldFront == NULL )
    {
        pQueue->pHead = pElement;
        pQueue->pTail = pElement;
        pElement->pNext = NULL;
        pElement->pPrevious = NULL;
    }
    else
    {
        pOldFront->pPrevious = pElement;
        pElement->pNext = pOldFront;
        pElement->pPrevious = NULL;
        pQueue->pHead = pElement;
    }

    pthread_mutex_unlock( &( pQueue->lock ) );
}

DeQueueElement_t * DeQueue_PopFront( DeQueue_t * pQueue )
{
    DeQueueElement_t * pFront = NULL;
    DeQueueElement_t * pNewFront = NULL;

    pthread_mutex_lock( &( pQueue->lock ) );

    pFront = pQueue->pHead;
    if( pFront != NULL )
    {
        pNewFront = pFront->pNext;

        if( pNewFront != NULL )
        {
            pNewFront->pPrevious = NULL;
        }
        else
        {
            pQueue->pTail = NULL;
        }
        pQueue->pHead = pNewFront;
    }

    pthread_mutex_unlock( &( pQueue->lock ) );

    return pFront;
}

DeQueueElement_t * DeQueue_PopBack( DeQueue_t * pQueue )
{
    DeQueueElement_t * pBack = NULL;
    DeQueueElement_t * pNewBack = NULL;

    pthread_mutex_lock( &( pQueue->lock ) );

    pBack = pQueue->pTail;
    if( pBack != NULL )
    {
        pNewBack = pBack->pPrevious;

        if( pNewBack != NULL )
        {
            pNewBack->pNext = NULL;
        }
        else
        {
            pQueue->pHead = NULL;
        }
        pQueue->pHead = pNewBack;
    }

    pthread_mutex_unlock( &( pQueue->lock ) );

    return pBack;
}

DeQueueElement_t * DeQueue_PeekFront( DeQueue_t * pQueue )
{
    return pQueue->pHead;
}

DeQueueElement_t * DeQueue_PeekBack( DeQueue_t * pQueue )
{
    return pQueue->pTail;
}

void DeQueue_Remove( DeQueue_t * pQueue, DeQueueElement_t * pRemove )
{
    DeQueueElement_t * pCur = NULL;
    DeQueueElement_t * pPrevious = NULL;
    DeQueueElement_t * pNext = NULL;

    pthread_mutex_lock( &( pQueue->lock ) );

    pCur = pQueue->pHead;
    pPrevious = pRemove->pPrevious;
    pNext = pRemove->pNext;

    while( ( pCur != pRemove ) && ( pCur != NULL ) )
    {
        pCur = pCur->pNext;
    }

    if( pCur == pRemove )
    {
        if( pPrevious != NULL )
        {
            pPrevious->pNext = pNext;
        }
        else
        {
            pQueue->pHead = pNext;
        }

        if( pNext != NULL )
        {
            pNext->pPrevious = pPrevious;
        }
        else
        {
            pQueue->pTail = pPrevious;
        }
    }

    pthread_mutex_unlock( &( pQueue->lock ) );
}

DeQueueElement_t * DeQueueElement_Create( void * pData, size_t datalen, DeQueueElementFreeFunc_t freeFunc )
{
    DeQueueElement_t * pNewElement = ( DeQueueElement_t * ) malloc( sizeof( DeQueueElement_t ) );
    if( pNewElement != NULL )
    {
        pNewElement->pNext = NULL;
        pNewElement->pPrevious = NULL;
        pNewElement->pData = pData;
        pNewElement->dataLen = datalen;
        pNewElement->freeFunc = freeFunc;
    }
    return pNewElement;
}

void DeQueueElement_Destroy( DeQueueElement_t * pElement )
{
    if( pElement->freeFunc != NULL )
    {
        pElement->freeFunc( pElement->pData );
    }
    free( pElement );
}
