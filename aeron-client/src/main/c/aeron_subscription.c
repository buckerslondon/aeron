/*
 * Copyright 2014-2020 Real Logic Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>

#include "aeron_subscription.h"
#include "aeron_alloc.h"
#include "util/aeron_error.h"

int aeron_subscription_create(
    aeron_subscription_t **subscription,
    aeron_client_conductor_t *conductor,
    const char *channel,
    int32_t stream_id,
    int64_t registration_id,
    aeron_on_available_image_t on_available_image,
    aeron_on_unavailable_image_t on_unavailable_image)
{
    aeron_subscription_t *_subscription;

    *subscription = NULL;
    if (aeron_alloc((void **)&_subscription, sizeof(aeron_subscription_t)) < 0)
    {
        int errcode = errno;

        aeron_set_err(errcode, "aeron_subscription_create (%d): %s", errcode, strerror(errcode));
        return -1;
    }

    _subscription->conductor_fields.image_lists_head.next_list = NULL;
    _subscription->conductor_fields.next_change_number = 0;
    _subscription->last_image_list_change_number = -1;

    _subscription->conductor = conductor;
    _subscription->channel = channel;
    _subscription->registration_id = registration_id;
    _subscription->stream_id = stream_id;
    _subscription->on_available_image = on_available_image;
    _subscription->on_unavailable_image = on_unavailable_image;

    _subscription->round_robin_index = 0;
    _subscription->is_closed = false;

    *subscription = _subscription;
    return -1;
}

int aeron_subscription_delete(aeron_subscription_t *subscription)
{
    aeron_free((void *)subscription->channel);
    aeron_free(subscription);
    return 0;
}

int aeron_subscription_alloc_image_list(aeron_image_list_t **image_list, size_t length)
{
    aeron_image_list_t *_image_list;

    *image_list = NULL;
    if (aeron_alloc((void **)&_image_list, AERON_IMAGE_LIST_ALLOC_SIZE(length)) < 0)
    {
        int errcode = errno;

        aeron_set_err(errcode, "aeron_subscription_create (%d): %s", errcode, strerror(errcode));
        return -1;
    }

    _image_list->change_number = -1;
    _image_list->array = (aeron_image_t **)((uint8_t *)_image_list + sizeof(aeron_image_list_t));
    _image_list->length = length;
    _image_list->next_list = NULL;

    *image_list = _image_list;
    return 0;
}

int aeron_client_conductor_subscription_new_image_list(
    aeron_subscription_t *subscription, aeron_image_list_t *image_list)
{
    /*
     * Called from the client conductor to add/remove images to the image list. A new image list is passed each time.
     */
    image_list->change_number = subscription->conductor_fields.next_change_number++;
    image_list->next_list = subscription->conductor_fields.image_lists_head.next_list;

    AERON_PUT_ORDERED(subscription->conductor_fields.image_lists_head.next_list, image_list);
    return 0;
}

int aeron_client_conductor_subscription_prune_image_lists(aeron_subscription_t *subscription)
{
    /*
     * Called from the client conductor to prune old image lists and free them up. Does not free Images.
     */
    volatile aeron_image_list_t *prune_lists_head = &subscription->conductor_fields.image_lists_head;
    int32_t last_change_number;

    AERON_GET_VOLATILE(last_change_number, subscription->last_image_list_change_number);

    while (NULL != prune_lists_head->next_list)
    {
        if (prune_lists_head->next_list->change_number >= last_change_number)
        {
            prune_lists_head = prune_lists_head->next_list;
        }
        else
        {
            volatile aeron_image_list_t *prune_list = prune_lists_head->next_list;

            prune_lists_head->next_list = prune_list->next_list;
            aeron_free((void *)prune_list);
        }
    }

    return 0;
}

int aeron_subscription_poll(
    aeron_subscription_t *subscription, aeron_fragment_handler_t handler, void *clientd, int fragment_limit)
{
    volatile aeron_image_list_t *image_list;

    AERON_GET_VOLATILE(image_list, subscription->conductor_fields.image_lists_head.next_list);

    size_t length = image_list->length;
    int fragments_read = 0;
    size_t starting_index = subscription->round_robin_index++;
    if (starting_index >= length)
    {
        subscription->round_robin_index = starting_index = 0;
    }

    for (size_t i = starting_index; i < length && fragments_read < fragment_limit; i++)
    {
        fragments_read += aeron_image_poll(image_list->array[i], handler, clientd, fragment_limit - fragments_read);
    }

    for (size_t i = 0; i < starting_index &&fragments_read < fragment_limit; i++)
    {
        fragments_read += aeron_image_poll(image_list->array[i], handler, clientd, fragment_limit - fragments_read);
    }

    if (image_list->change_number > subscription->last_image_list_change_number)
    {
        AERON_PUT_ORDERED(subscription->last_image_list_change_number, image_list->change_number);
    }

    return fragments_read;
}
