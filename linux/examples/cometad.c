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
 * Cometa client demon application for vanilla linux systems.
 *
 */



/*
 * If the client needs to be multi-threaded and the cometa library is registered to receive the signal(SIGALRM),
 * the application should block SIGALARM in all other threads to be certain that it will be delivered to the libcometa thread. 
 * If the application isusing pthreads, signals are blocked on a thread per thread basis with pthread_sigmask().
 */
