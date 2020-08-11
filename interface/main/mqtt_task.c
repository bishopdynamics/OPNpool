/**
* @brief MQTT client task: interface with the MQTT broker
 *
 * CLOSED SOURCE, NOT FOR PUBLIC RELEASE
 * (c) Copyright 2020, Coert Vonk
 * All rights reserved. Use of copyright notice does not imply publication.
 * All text above must be included in any redistribution
 **/

#include <string.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <mqtt_client.h>
#include <esp_ota_ops.h>
#include <esp_core_dump.h>
#include <esp_flash.h>
//#include <esp_heap_trace.h>

#include "ipc/ipc.h"
#include "mqtt_task.h"
#include "coredump_to_server.h"

static char const * const TAG = "mqtt_task";

static EventGroupHandle_t _mqttEventGrp = NULL;
typedef enum {
	MQTT_EVENT_CONNECTED_BIT = BIT0
} mqttEvent_t;

typedef struct coredump_priv_t {
    esp_mqtt_client_handle_t const client;
    char * topic;
} coredump_priv_t;

static esp_mqtt_client_handle_t _connect2broker(ipc_t const * const ipc);  // forward decl

typedef struct mqtt_subscriber_t {
    char const * topic;
    bool local;
    struct mqtt_subscriber_t const * next;
} mqtt_subscriber_t;

mqtt_subscriber_t * _subscribers = NULL;

static void
_add_subscriber(char const * const topic, bool const local)
{
    mqtt_subscriber_t * subscriber = malloc(sizeof(mqtt_subscriber_t));
    subscriber->topic = topic,
    subscriber->local = local,
    subscriber->next = _subscribers,
    _subscribers = subscriber;  // insert at HEAD of linked list
}

static esp_err_t
_coredump_to_server_write_cb(void * priv_void, char const * const str)
{
    coredump_priv_t const * const priv = priv_void;

    esp_mqtt_client_publish(priv->client, priv->topic, str, strlen(str), 1, 0);
    return ESP_OK;
}

static void
_forwardCoredump(ipc_t * ipc, esp_mqtt_client_handle_t const client)
{
    coredump_priv_t priv = {
        .client = client,
    };
    assert( asprintf(&priv.topic, "%s/coredump/%s", CONFIG_POOL_MQTT_DATA_TOPIC, ipc->dev.name) >=0 );
    coredump_to_server_config_t coredump_cfg = {
        .start = NULL,
        .end = NULL,
        .write = _coredump_to_server_write_cb,
        .priv = &priv,
    };
    coredump_to_server(&coredump_cfg);
    free(priv.topic);
}

static void
_dispatch_restart(esp_mqtt_event_handle_t event, ipc_t const * const ipc)
{
    ipc_send_to_mqtt(IPC_TO_MQTT_TYP_RESTART, "{ \"response\": \"restarting\" }", ipc);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();
}

static void
_dispatch_who(esp_mqtt_event_handle_t event, ipc_t const * const ipc)
{
    esp_partition_t const * const running_part = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    ESP_ERROR_CHECK(esp_ota_get_partition_description(running_part, &running_app_info));

    wifi_ap_record_t ap_info;
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));

    char * payload;
    assert( asprintf(&payload,
            "{ \"name\": \"%s\", \"firmware\": { \"version\": \"%s.%s\", \"date\": \"%s %s\" }, \"wifi\": { \"connect\": %u, \"address\": \"%s\", \"SSID\": \"%s\", \"RSSI\": %d }, \"mqtt\": { \"connect\": %u }, \"mem\": { \"heap\": %u } }",
            ipc->dev.name,
            running_app_info.project_name, running_app_info.version,
            running_app_info.date, running_app_info.time,
            ipc->dev.count.wifiConnect, ipc->dev.ipAddr, ap_info.ssid, ap_info.rssi,
            ipc->dev.count.mqttConnect, heap_caps_get_free_size(MALLOC_CAP_8BIT) ) >= 0);
    ipc_send_to_mqtt(IPC_TO_MQTT_TYP_WHO, payload, ipc);
    free(payload);
}

static void
_heap_trace_start(esp_mqtt_event_handle_t event, ipc_t const * const ipc)
{
    //ESP_ERROR_CHECK( heap_trace_start(HEAP_TRACE_LEAKS) );
}

static void
_heap_trace_stop(esp_mqtt_event_handle_t event, ipc_t const * const ipc)
{
    //ESP_ERROR_CHECK( heap_trace_stop() );
    //heap_trace_dump();
}

typedef void (* mqtt_dispatch_fnc_t)(esp_mqtt_event_handle_t event, ipc_t const * const ipc);

typedef struct mqtt_dispatch_t {
    char * message;
    mqtt_dispatch_fnc_t fnc;
} mqtt_dispatch_t;

static mqtt_dispatch_t _mqtt_dispatches[] = {
    {"who", _dispatch_who},
    {"restart", _dispatch_restart},
    {"htstart", _heap_trace_start},
    {"htstop", _heap_trace_stop},
};

static esp_err_t
_mqtt_event_cb(esp_mqtt_event_handle_t event) {

    ipc_t * const ipc = event->user_context;

	switch (event->event_id) {
        case MQTT_EVENT_DISCONNECTED:
            xEventGroupClearBits(_mqttEventGrp, MQTT_EVENT_CONNECTED_BIT);
            if (CONFIG_POOL_DBGLVL_MQTTTASK > 0) {
                ESP_LOGW(TAG, "Broker disconnected");
            }
        	// reconnect is part of the SDK
            break;
        case MQTT_EVENT_CONNECTED:
            xEventGroupSetBits(_mqttEventGrp, MQTT_EVENT_CONNECTED_BIT);
            ipc->dev.count.mqttConnect++;
            for (mqtt_subscriber_t const * subscriber = _subscribers; subscriber; subscriber = subscriber->next) {
                esp_mqtt_client_subscribe(event->client, subscriber->topic, 1);
                if (CONFIG_POOL_DBGLVL_MQTTTASK > 1) {
                    ESP_LOGI(TAG, "Broker connected, subscribed to \"%s\"", subscriber->topic);
                }
            }
            break;
        case MQTT_EVENT_DATA:
            if (event->topic && event->data_len == event->total_data_len) {  // quietly ignore chunked messaegs
                bool done = false;

                for (mqtt_subscriber_t const * subscriber = _subscribers; subscriber && !done; subscriber = subscriber->next) {
                    if (strncmp(subscriber->topic, event->topic, event->topic_len) == 0) {

                        //ESP_LOGW(TAG, " matched subscriber topic (%s)", subscriber->topic);

                        if (subscriber->local) {
                            mqtt_dispatch_t const * handler = _mqtt_dispatches;
                            for (uint ii = 0; ii < ARRAY_SIZE(_mqtt_dispatches) && !done; ii++, handler++) {
                                if (strncmp(handler->message, event->data, event->data_len) == 0) {
                                    handler->fnc(event, ipc);
                                }
                            }
                        } else {
                            //ESP_LOGW(TAG, " passing off to pool_task");
                            ipc_send_to_pool(IPC_TO_POOL_TYP_SET, event->topic, event->topic_len, event->data, event->data_len, ipc);
                        }
                        done = true;
                    }
                }
                if (!done) {  // perhaps `pool_task` knows what to do
                }
            }
            break;
        default:
            break;
	}
	return ESP_OK;
}

static esp_mqtt_client_handle_t
_connect2broker(ipc_t const * const ipc) {

    esp_mqtt_client_handle_t client;
    xEventGroupClearBits(_mqttEventGrp, MQTT_EVENT_CONNECTED_BIT);
    {
        const esp_mqtt_client_config_t mqtt_cfg = {
            .event_handle = _mqtt_event_cb,
            .user_context = (void *)ipc,
            .uri = CONFIG_POOL_MQTT_URL,
        };
        client = esp_mqtt_client_init(&mqtt_cfg);
        //ESP_ERROR_CHECK(esp_mqtt_client_set_uri(client, CONFIG_POOL_MQTT_URL));
        ESP_ERROR_CHECK(esp_mqtt_client_start(client));
    }
	assert(xEventGroupWaitBits(_mqttEventGrp, MQTT_EVENT_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY));
    return client;
}

void
mqtt_task(void * ipc_void)
{
 	ipc_t * ipc = ipc_void;
    {
        char * topic;
        assert( asprintf(&topic, "%s/%s", CONFIG_POOL_MQTT_CTRL_TOPIC, ipc->dev.name) >= 0 );
        _add_subscriber(topic, true);
        // don't free(topic) stays in subscribers linked list
        assert( asprintf(&topic, "%s", CONFIG_POOL_MQTT_CTRL_TOPIC) >= 0 );
        _add_subscriber(topic, true);
        // don't free(topic) stays in subscribers linked list
    }
	_mqttEventGrp = xEventGroupCreate();
    esp_mqtt_client_handle_t const client = _connect2broker(ipc);

    _forwardCoredump(ipc, client);

	while (1) {
        ipc_to_mqtt_msg_t msg;
		if (xQueueReceive(ipc->to_mqtt_q, &msg, (TickType_t)(1000L / portTICK_PERIOD_MS)) == pdPASS) {

            switch (msg.dataType) {
                case IPC_TO_MQTT_TYP_STATE:
                case IPC_TO_MQTT_TYP_RESTART:
                case IPC_TO_MQTT_TYP_WHO:
                case IPC_TO_MQTT_TYP_DBG: {
                    char * topic;
                    char const * const subtopic = ipc_to_mqtt_typ_str(msg.dataType);
                    if (subtopic) {
                        assert( asprintf(&topic, "%s/%s/%s", CONFIG_POOL_MQTT_DATA_TOPIC, subtopic, ipc->dev.name) >= 0);
                    } else {
                        assert( asprintf(&topic, "%s/%s", CONFIG_POOL_MQTT_DATA_TOPIC, ipc->dev.name) >= 0);
                    }
                    esp_mqtt_client_publish(client, topic, msg.data, strlen(msg.data), 1, 0);
                    free(topic);
                    free(msg.data);
                    break;
                }
                case IPC_TO_MQTT_TYP_SUBSCRIBE: {
                    // 2BD should remember, for when the connection to the broker gets reestablised ..
                    _add_subscriber(msg.data, false);  // used when reconnecting to broker
                    esp_mqtt_client_subscribe(client, msg.data, 1);
                    if (CONFIG_POOL_DBGLVL_MQTTTASK > 1) {
                        ESP_LOGI(TAG, "Temp subscribed to \"%s\"", msg.data);
                    }
                    // don't free(msg.data), stays in _subscribers linked list
                    break;
                }
                case IPC_TO_MQTT_TYP_PUBLISH: {
                    char const * const topic = msg.data;
                    char * message = strchr(msg.data, '\t');
                    *message++ = '\0';
                    if (CONFIG_POOL_DBGLVL_MQTTTASK >1) {
                        ESP_LOGI(TAG, "tx %s: %s", topic, message);
                    }
                    esp_mqtt_client_publish(client, topic, message, strlen(message), 1, 0);
                    free(msg.data);
                    break;
                }
            }
		}
	}
}