/**
 * Copyright © 2016 Push Technology Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This example is written in C99. Please use an appropriate C99 capable compiler
 *
 * @author Push Technology Limited
 * @since 6.0
 */

/*
 * This example creates a "binary" or "json" topic, and updates it through the
 * use of update_value(), which sends deltas of change to the server instead
 * of the full topic content.
 *
 * @deprecated The API functions used in this example are deprecated. The topic update
 * examples are the preferred alternative to this.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifndef WIN32
#include <unistd.h>
#else
#define sleep(x) Sleep(1000 * x)
#endif

#include <apr.h>
#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>

#include "diffusion.h"
#include "args.h"
#include "conversation.h"

int active = 0;
int json = 0;
apr_pool_t *pool = NULL;
apr_thread_mutex_t *mutex = NULL;
apr_thread_cond_t *cond = NULL;

ARG_OPTS_T arg_opts[] = {
        ARG_OPTS_HELP,
        {'u', "url", "Diffusion server URL", ARG_OPTIONAL, ARG_HAS_VALUE, "ws://localhost:8080"},
        {'p', "principal", "Principal (username) for the connection", ARG_OPTIONAL, ARG_HAS_VALUE, "control"},
        {'c', "credentials", "Credentials (password) for the connection", ARG_OPTIONAL, ARG_HAS_VALUE, "password"},
        {'t', "topic", "Topic name to create and update", ARG_OPTIONAL, ARG_HAS_VALUE, "time"},
        {'s', "seconds", "Number of seconds to run for before exiting", ARG_OPTIONAL, ARG_HAS_VALUE, "30"},
        {'j', "json", "Use JSON instead of binary data", ARG_OPTIONAL, ARG_NO_VALUE, NULL},
        END_OF_ARG_OPTS
};

/*
 * Handlers for add topic feature.
 */
static int
on_topic_added(SESSION_T *session, TOPIC_ADD_RESULT_CODE result_code, void *context)
{
        printf("Added topic\n");
        apr_thread_mutex_lock(mutex);
        apr_thread_cond_broadcast(cond);
        apr_thread_mutex_unlock(mutex);
        return HANDLER_SUCCESS;
}

static int
on_topic_add_failed(SESSION_T *session, TOPIC_ADD_FAIL_RESULT_CODE result_code, const DIFFUSION_ERROR_T *error, void *context)
{
        printf("Failed to add topic (%d)\n", result_code);
        apr_thread_mutex_lock(mutex);
        apr_thread_cond_broadcast(cond);
        apr_thread_mutex_unlock(mutex);
        return HANDLER_SUCCESS;
}

static int
on_topic_add_discard(SESSION_T *session, void *context)
{
        apr_thread_mutex_lock(mutex);
        apr_thread_cond_broadcast(cond);
        apr_thread_mutex_unlock(mutex);
        return HANDLER_SUCCESS;
}

/*
 * Handlers for registration of update source feature
 */
static int
on_update_source_init(SESSION_T *session,
                      const CONVERSATION_ID_T *updater_id,
                      const SVC_UPDATE_REGISTRATION_RESPONSE_T *response,
                      void *context)
{
        char *id_str = conversation_id_to_string(*updater_id);
        printf("Topic source \"%s\" in init state\n", id_str);
        free(id_str);
        apr_thread_mutex_lock(mutex);
        apr_thread_cond_broadcast(cond);
        apr_thread_mutex_unlock(mutex);
        return HANDLER_SUCCESS;
}

static int
on_update_source_registered(SESSION_T *session,
                            const CONVERSATION_ID_T *updater_id,
                            const SVC_UPDATE_REGISTRATION_RESPONSE_T *response,
                            void *context)
{
        char *id_str = conversation_id_to_string(*updater_id);
        printf("Registered update source \"%s\"\n", id_str);
        free(id_str);
        apr_thread_mutex_lock(mutex);
        apr_thread_cond_broadcast(cond);
        apr_thread_mutex_unlock(mutex);
        return HANDLER_SUCCESS;
}

static int
on_update_source_deregistered(SESSION_T *session,
                              const CONVERSATION_ID_T *updater_id,
                              void *context)
{
        char *id_str = conversation_id_to_string(*updater_id);
        printf("Deregistered update source \"%s\"\n", id_str);
        free(id_str);
        apr_thread_mutex_lock(mutex);
        apr_thread_cond_broadcast(cond);
        apr_thread_mutex_unlock(mutex);
        return HANDLER_SUCCESS;}


static int
on_update_source_active(SESSION_T *session,
                        const CONVERSATION_ID_T *updater_id,
                        const SVC_UPDATE_REGISTRATION_RESPONSE_T *response,
                        void *context)
{
        char *id_str = conversation_id_to_string(*updater_id);
        printf("Topic source \"%s\" active\n", id_str);
        free(id_str);
        active = 1;
        apr_thread_mutex_lock(mutex);
        apr_thread_cond_broadcast(cond);
        apr_thread_mutex_unlock(mutex);
        return HANDLER_SUCCESS;
}

static int
on_update_source_standby(SESSION_T *session,
                         const CONVERSATION_ID_T *updater_id,
                         const SVC_UPDATE_REGISTRATION_RESPONSE_T *response,
                         void *context)
{
        char *id_str = conversation_id_to_string(*updater_id);
        printf("Topic source \"%s\" on standby\n", id_str);
        free(id_str);
        apr_thread_mutex_lock(mutex);
        apr_thread_cond_broadcast(cond);
        apr_thread_mutex_unlock(mutex);
        return HANDLER_SUCCESS;
}

static int
on_update_source_closed(SESSION_T *session,
                        const CONVERSATION_ID_T *updater_id,
                        const SVC_UPDATE_REGISTRATION_RESPONSE_T *response,
                        void *context)
{
        char *id_str = conversation_id_to_string(*updater_id);
        printf("Topic source \"%s\" closed\n", id_str);
        free(id_str);
        apr_thread_mutex_lock(mutex);
        apr_thread_cond_broadcast(cond);
        apr_thread_mutex_unlock(mutex);
        return HANDLER_SUCCESS;
}

/*
 * Handlers for update of data.
 */
static int
on_update_success(SESSION_T *session,
                  const CONVERSATION_ID_T *updater_id,
                  const SVC_UPDATE_RESPONSE_T *response,
                  void *context)
{
        char *id_str = conversation_id_to_string(*updater_id);
        printf("on_update_success for updater \"%s\"\n", id_str);
        free(id_str);
        return HANDLER_SUCCESS;
}

static int
on_update_failure(SESSION_T *session,
                  const CONVERSATION_ID_T *updater_id,
                  const SVC_UPDATE_RESPONSE_T *response,
                  void *context)
{
        char *id_str = conversation_id_to_string(*updater_id);
        printf("on_update_failure for updater \"%s\"\n", id_str);
        free(id_str);
        return HANDLER_SUCCESS;
}

/*
 * Program entry point.
 */
int
main(int argc, char** argv)
{
        /*
         * Standard command-line parsing.
         */
        const HASH_T *options = parse_cmdline(argc, argv, arg_opts);
        if(options == NULL || hash_get(options, "help") != NULL) {
                show_usage(argc, argv, arg_opts);
                return EXIT_FAILURE;
        }

        const char *url = hash_get(options, "url");
        const char *principal = hash_get(options, "principal");
        CREDENTIALS_T *credentials = NULL;
        const char *password = hash_get(options, "credentials");
        if(password != NULL) {
                credentials = credentials_create_password(password);
        }
        const char *topic_name = hash_get(options, "topic");
        const long seconds = atol(hash_get(options, "seconds"));

        json = (hash_get(options, "json") != NULL);

        /*
         * Setup for condition variable.
         */
        apr_initialize();
        apr_pool_create(&pool, NULL);
        apr_thread_mutex_create(&mutex, APR_THREAD_MUTEX_UNNESTED, pool);
        apr_thread_cond_create(&cond, pool);

        /*
         * Create a session with the Diffusion server.
         */
        SESSION_T *session;
        DIFFUSION_ERROR_T error = { 0 };
        session = session_create(url, principal, credentials, NULL, NULL, &error);
        if(session == NULL) {
                fprintf(stderr, "TEST: Failed to create session\n");
                fprintf(stderr, "ERR : %s\n", error.message);
                return EXIT_FAILURE;
        }

        /*
         * Create a topic holding binary or JSON content.
         */
        TOPIC_SPECIFICATION_T *topic_specification =
                json ? topic_specification_init(TOPIC_TYPE_JSON) : topic_specification_init(TOPIC_TYPE_BINARY);

        DIFFUSION_DATATYPE datatype = json ? DATATYPE_JSON : DATATYPE_BINARY;

        ADD_TOPIC_CALLBACK_T callback = {
                .on_topic_added_with_specification = on_topic_added,
                .on_topic_add_failed_with_specification = on_topic_add_failed,
                .on_discard = on_topic_add_discard
        };

        apr_thread_mutex_lock(mutex);
        add_topic_from_specification(session, topic_name, topic_specification, callback);
        apr_thread_cond_wait(cond, mutex);
        apr_thread_mutex_unlock(mutex);

        topic_specification_free(topic_specification);

        /*
         * Define the handlers for add_update_source()
         */
        const UPDATE_SOURCE_REGISTRATION_PARAMS_T update_reg_params = {
                .topic_path = topic_name,
                .on_init = on_update_source_init,
                .on_registered = on_update_source_registered,
                .on_active = on_update_source_active,
                .on_standby = on_update_source_standby,
                .on_close = on_update_source_closed
        };

        /*
         * Register an updater.
         */
        apr_thread_mutex_lock(mutex);
        CONVERSATION_ID_T *updater_id = register_update_source(session, update_reg_params);
        apr_thread_cond_wait(cond, mutex);
        apr_thread_mutex_unlock(mutex);

        /*
         * Define default parameters for an update source.
         */
        UPDATE_VALUE_PARAMS_T update_value_params_base = {
                .updater_id = updater_id,
                .topic_path = (char *)topic_name,
                .on_success = on_update_success,
                .on_failure = on_update_failure
        };

        time_t end_time = time(NULL) + seconds;

        while(time(NULL) < end_time) {

                if(active) {
                        /*
                         * Create an update structure containing the current time.
                         */
                        BUF_T *buf = buf_create();
                        const time_t time_now = time(NULL);

                        if(json) {
                                char time_update[50];
                                snprintf(time_update, 50, "{\"timestamp\":%ld}", time_now);

                                if(!write_diffusion_json_value(time_update, buf)) {
                                        printf("Error whilst writing json update\n");
                                        return EXIT_FAILURE;
                                }
                        }
                        else {
                                void *update = ctime(&time_now);
                                if(!write_diffusion_binary_value(update, buf, strlen(update))) {
                                        printf("Error whilst writing binary update\n");
                                        return EXIT_FAILURE;
                                }
                        }
                        UPDATE_VALUE_PARAMS_T update_value_params = update_value_params_base;
                        update_value_params.data = buf;

                        /*
                         * Update the topic.
                         */
                        update_value_with_datatype(session, datatype, update_value_params);
                        buf_free(buf);
                }

                sleep(1);
        }

        if(active) {
                UPDATE_SOURCE_DEREGISTRATION_PARAMS_T update_dereg_params = {
                        .updater_id = updater_id,
                        .on_deregistered = on_update_source_deregistered
                };

                apr_thread_mutex_lock(mutex);
                deregister_update_source(session, update_dereg_params);
                apr_thread_cond_wait(cond, mutex);
                apr_thread_mutex_unlock(mutex);
        }

        /*
         * Close session and free resources.
         */
        session_close(session, NULL);
        session_free(session);

        conversation_id_free(updater_id);
        credentials_free(credentials);

        apr_thread_mutex_destroy(mutex);
        apr_thread_cond_destroy(cond);
        apr_pool_destroy(pool);
        apr_terminate();

        return EXIT_SUCCESS;

}
