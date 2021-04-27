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

#ifndef DEMO_QUEUE_H_
#define DEMO_QUEUE_H_

#include <unistd.h>
#include <pthread.h>

#include "demo_config.h"

struct DeQueueElement;
typedef struct DeQueueElement DeQueueElement_t;

struct DeQueue;
typedef struct DeQueue DeQueue_t;

typedef void (* DeQueueElementFreeFunc_t )( void * );

struct DeQueueElement
{
    void * pData;
    size_t dataLen;
    DeQueueElementFreeFunc_t freeFunc;
    DeQueueElement_t * pNext;
    DeQueueElement_t * pPrevious;
};

struct DeQueue
{
    pthread_mutex_t lock;
    pthread_cond_t cond;
    DeQueueElement_t * pHead;
    DeQueueElement_t * pTail;
    size_t len;
};

DeQueue_t * DeQueue_Create( DeQueue_t * pQueue );
void DeQueue_Destroy( DeQueue_t * pQueue );
void DeQueue_PushBack( DeQueue_t * pQueue, DeQueueElement_t * pElement );
void DeQueue_PushFront( DeQueue_t * pQueue, DeQueueElement_t * pElement );
DeQueueElement_t * DeQueue_PopFront( DeQueue_t * pQueue );
DeQueueElement_t * DeQueue_PopBack( DeQueue_t * pQueue );
DeQueueElement_t * DeQueue_PeekFront( DeQueue_t * pQueue );
DeQueueElement_t * DeQueue_PeekBack( DeQueue_t * pQueue );
void DeQueue_Remove( DeQueue_t * pQueue, DeQueueElement_t * pRemove );
uint32_t DeQueue_Len( DeQueue_t * pQueue );

DeQueueElement_t * DeQueueElement_Create( void * pData, size_t datalen, DeQueueElementFreeFunc_t freeFunc );
void DeQueueElement_Destroy( DeQueueElement_t * pElement );

#endif /* ifndef DEMO_QUEUE_H_ */

