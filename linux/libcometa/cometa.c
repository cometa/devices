/*
 * 	Cometa is a cloud infrastructure for embedded systems and connected 
 *  devices developed by Visible Energy, Inc.
 *
 *	Copyright (C) 2013, Visible Energy, Inc.
 * 
 *	Licensed under the Apache License, Version 2.0 (the "License");
 * 	you may not use this file except in compliance with the License.
 * 	You may obtain a copy of the License at
 *
 *	http://www.apache.org/licenses/LICENSE-2.0
 *
 *	Unless required by applicable law or agreed to in writing, software
 *	distributed under the License is distributed on an "AS IS" BASIS,
 *	WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *	See the License for the specific language governing permissions and
 *	limitations under the License.
 */

/* @file
 * Cometa client library for vanilla linux systems.
 *
 */

#include <string.h>
#include "http_server.h"
#include "wwd_assert.h"
#include "wiced.h"

/******************************************************
 *                      Macros
 ******************************************************/

/******************************************************
 *                    Constants
 ******************************************************/
/******************************************************
 *                   Enumerations
 ******************************************************/

/******************************************************
 *                 Type Definitions
 ******************************************************/

/******************************************************
 *                    Structures
 ******************************************************/

/******************************************************
 *               Function Declarations
 ******************************************************/

static wiced_result_t http_server_process_packet(const wiced_http_page_t* page_database, wiced_tcp_socket_t* socket, wiced_packet_t* packet);
static wiced_result_t process_url_request( wiced_tcp_socket_t* socket, const wiced_http_page_t* server_url_list, char * url, int url_len );
static void http_server_thread_main(uint32_t arg);
static uint16_t escaped_string_copy(char* output, uint16_t output_length, const char* input, int16_t input_length);

/******************************************************
 *                 Static Variables
 ******************************************************/

/******************************************************
 *               Function Definitions
 ******************************************************/

/*
 * Execute a non-select query.  If a connection to the database is not open,
 * open it.
 *
 * @param sql   - The sql command to execute.
 *
 * @return      - Success or Failure
 */
