/* This file is part of Ingen.  Copyright (C) 2006 Dave Robillard.
 * 
 * Ingen is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 * 
 * Ingen is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <iostream>
#include <cstddef>
#include <signal.h>
#include "config.h"
#include "util.h"
#include "cmdline.h"
#include "Engine.h"
#ifdef HAVE_LASH
#include "LashDriver.h"
#endif
#ifdef BUILD_IN_PROCESS_ENGINE
#include <jack/jack.h>
#include <jack/intclient.h>
#endif

using std::cout; using std::endl; using std::cerr;


Ingen::Engine* engine;


void
catch_int(int)
{
	signal(SIGINT, catch_int);
	signal(SIGTERM, catch_int);

	std::cout << "[Main] Ingen interrupted." << std::endl;
	engine->quit();
}


#ifdef BUILD_IN_PROCESS_ENGINE

jack_client_t*   jack_client;
jack_intclient_t jack_intclient;


void
unload_in_process_engine(int)
{
	jack_status_t status;
	int ret = EXIT_SUCCESS;

	cout << "Unloading...";
	status = jack_internal_client_unload(jack_client, jack_intclient);
	if (status & JackFailure) {
		cout << "failed" << endl;
		ret = EXIT_FAILURE;
	} else {
		cout << "done" << endl;
	}
	jack_client_close(jack_client);
	exit(ret);
}


int
load_in_process_engine(const char* port)
{
	int ret = EXIT_SUCCESS;
	
	jack_status_t status;

	if ((jack_client = jack_client_open("om_load", JackNoStartServer,
	                                    &status)) != NULL) {
		jack_intclient =
		    jack_internal_client_load(jack_client, "Ingen",
		                               (jack_options_t)(JackLoadName|JackLoadInit),
		                               &status, "om", port);
		if (status == 0) {
			cout << "Engine loaded" << endl;
			signal(SIGINT, unload_in_process_engine);
			signal(SIGTERM, unload_in_process_engine);

			while (1) {
				sleep(1);
			}
		} else if (status & JackFailure) {
			cerr << "Could not load om.so" << endl;
			ret = EXIT_FAILURE;
		}

		jack_client_close(jack_client);
	} else {
		cerr << "jack_client_open failed" << endl;
		ret = EXIT_FAILURE;
	}
}

#endif // BUILD_IN_PROCESS_ENGINE


int
main(int argc, char** argv)
{
#ifdef HAVE_LASH
	lash_args_t* lash_args = lash_extract_args(&argc, &argv);
#endif

	int ret = EXIT_SUCCESS;

	/* Parse command line options */
	gengetopt_args_info args_info;
	if (cmdline_parser (argc, argv, &args_info) != 0)
		return EXIT_FAILURE;


	if (args_info.in_jackd_flag) {
#ifdef BUILD_IN_PROCESS_ENGINE
		ret = load_in_process_engine(args_info.port_arg);
#else
		cerr << "In-process Jack client support not enabled in this build." << endl;
		ret = EXIT_FAILURE;
#endif
	} else {
		signal(SIGINT, catch_int);
		signal(SIGTERM, catch_int);

		Ingen::set_denormal_flags();

		engine = new Ingen::Engine(args_info.port_arg);

#ifdef HAVE_LASH
		Ingen::lash_driver = new Ingen::LashDriver(Ingen::om, lash_args);
#endif

		engine->main();

#ifdef HAVE_LASH
		delete Ingen::lash_driver;
#endif

		delete engine;
	}
	
	return ret;
}

