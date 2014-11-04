/*
 *  Copyright (C) 2012  Rafael Ubal (ubal@ece.neu.edu)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <arch/common/arch.h>
#include <lib/esim/esim.h>
#include <lib/esim/trace.h>
#include <lib/mhandle/mhandle.h>
#include <lib/util/debug.h>
#include <lib/util/file.h>
#include <lib/util/list.h>
#include <lib/util/string.h>
#include <network/network.h>
#include <network/node.h>

#include "cache.h"
#include "config.h"
#include "local-mem-protocol.h"
#include "mem-system.h"
#include "module.h"
#include "nmoesi-protocol.h"


/*
 * Global Variables
 */

int mem_debug_category;
int mem_trace_category;
int mem_peer_transfers;

/* Frequency domain, as returned by function 'esim_new_domain'. */
int mem_frequency = 1000;
int mem_domain_index;

struct mem_system_t *mem_system;

char *mem_report_file_name = "";
// This file contains all the information related to latency counters.
char *mem_report_file_name_latency_counter;
// This file contains all the information related to state transition.
char *mem_report_file_name_state_transition;
// This file contains all the information related to access statistics, their profiling information basically.
char *mem_report_file_name_access_statistics;

// Utility function - concatanating file names
void mem_append_file_name(char *dest, const char *src, const char *app_string)
{
    int i,j;
		// printf("\n %s", src);
    for(i = 0; src[i] != '\0'; i++)
      dest[i] = src[i];
		// printf("\n %s", app_string);
		for(j = 0; app_string[j] != '\0'; j++)
    	dest[i+j] = app_string[j];
		dest[i+j] = '\0';
}

/*
 * Memory System Object
 */

struct mem_system_t *mem_system_create(void)
{
	struct mem_system_t *mem_system;

	/* Initialize */
	mem_system = xcalloc(1, sizeof(struct mem_system_t));
	mem_system->net_list = list_create();
	mem_system->mod_list = list_create();

	/* Return */
	return mem_system;
}


void mem_system_free(struct mem_system_t *mem_system)
{
	/* Free memory modules */
	while (list_count(mem_system->mod_list))
		mod_free(list_pop(mem_system->mod_list));
	list_free(mem_system->mod_list);

	/* Free networks */
	while (list_count(mem_system->net_list))
		net_free(list_pop(mem_system->net_list));
	list_free(mem_system->net_list);

	/* Free memory system */
	free(mem_system);
}




/*
 * Public Functions
 */

static char *mem_err_timing =
	"\tA command-line option related with the memory hierarchy ('--mem' prefix)\n"
	"\thas been specified, by no architecture is running a detailed simulation.\n"
	"\tPlease specify at least one detailed simulation (e.g., with option\n"
	"\t'--x86-sim detailed'.\n";

void mem_system_init(void)
{
	int count;

	mem_report_file_name_latency_counter   = xmalloc(sizeof(mem_report_file_name)+(sizeof(char)*20));
	mem_report_file_name_state_transition  = xmalloc(sizeof(mem_report_file_name)+(sizeof(char)*20));
	mem_report_file_name_access_statistics = xmalloc(sizeof(mem_report_file_name)+(sizeof(char)*20));
	
	/* If any file name was specific for a command-line option related with the
	 * memory hierarchy, make sure that at least one architecture is running
	 * timing simulation. */
	count = arch_get_sim_kind_detailed_count();
	if (mem_report_file_name && *mem_report_file_name && !count)
		fatal("memory report file given, but no timing simulation.\n%s",
				mem_err_timing);
	if (mem_config_file_name && *mem_config_file_name && !count)
		fatal("memory configuration file given, but no timing simulation.\n%s",
				mem_err_timing);
	
	/* Create trace category. This needs to be done before reading the
	 * memory configuration file with 'mem_config_read', since the latter
	 * function generates the trace headers. */
	mem_trace_category = trace_new_category();

	/* Create global memory system. This needs to be done before reading the
	 * memory configuration file with 'mem_config_read', since the latter
	 * function inserts caches and networks in 'mem_system', and relies on
	 * these lists to have been created. */
	mem_system = mem_system_create();

	/* Read memory configuration file */
	mem_config_read();

	mem_append_file_name(mem_report_file_name_latency_counter, mem_report_file_name, "_latency_counter");
	mem_append_file_name(mem_report_file_name_state_transition, mem_report_file_name, "_state_transition");
	mem_append_file_name(mem_report_file_name_access_statistics, mem_report_file_name, "_access_statistics");

	/* Try to open report file */
	if (*mem_report_file_name && !file_can_open_for_write(mem_report_file_name))
		fatal("%s: cannot open GPU cache report file",
			mem_report_file_name);

	if (*mem_report_file_name && !file_can_open_for_write(mem_report_file_name_latency_counter))
		fatal("%s: cannot open GPU cache report file latency counter",
			mem_report_file_name);

	if (*mem_report_file_name && !file_can_open_for_write(mem_report_file_name_state_transition))
		fatal("%s: cannot open GPU cache report file state transition",
			mem_report_file_name);

	if (*mem_report_file_name && !file_can_open_for_write(mem_report_file_name_access_statistics))
		fatal("%s: cannot open GPU cache report file access statistics",
			mem_report_file_name);

	// Network Debugging
	if (*mem_report_file_name && !file_can_open_for_write("Network_Configuration"))
		fatal("%s: cannot open GPU cache report file Network Debug",
			mem_report_file_name);

	/* NMOESI memory event-driven simulation */

	EV_MOD_NMOESI_LOAD = esim_register_event_with_name(mod_handler_nmoesi_load,
			mem_domain_index, "mod_nmoesi_load");
	EV_MOD_NMOESI_LOAD_LOCK = esim_register_event_with_name(mod_handler_nmoesi_load,
			mem_domain_index, "mod_nmoesi_load_lock");
	EV_MOD_NMOESI_LOAD_ACTION = esim_register_event_with_name(mod_handler_nmoesi_load,
			mem_domain_index, "mod_nmoesi_load_action");
	EV_MOD_NMOESI_LOAD_MISS = esim_register_event_with_name(mod_handler_nmoesi_load,
			mem_domain_index, "mod_nmoesi_load_miss");
	EV_MOD_NMOESI_LOAD_UNLOCK = esim_register_event_with_name(mod_handler_nmoesi_load,
			mem_domain_index, "mod_nmoesi_load_unlock");
	EV_MOD_NMOESI_LOAD_FINISH = esim_register_event_with_name(mod_handler_nmoesi_load,
			mem_domain_index, "mod_nmoesi_load_finish");

	EV_MOD_NMOESI_STORE = esim_register_event_with_name(mod_handler_nmoesi_store,
			mem_domain_index, "mod_nmoesi_store");
	EV_MOD_NMOESI_STORE_LOCK = esim_register_event_with_name(mod_handler_nmoesi_store,
			mem_domain_index, "mod_nmoesi_store_lock");
	EV_MOD_NMOESI_STORE_ACTION = esim_register_event_with_name(mod_handler_nmoesi_store,
			mem_domain_index, "mod_nmoesi_store_action");
	EV_MOD_NMOESI_STORE_UNLOCK = esim_register_event_with_name(mod_handler_nmoesi_store,
			mem_domain_index, "mod_nmoesi_store_unlock");
	EV_MOD_NMOESI_STORE_FINISH = esim_register_event_with_name(mod_handler_nmoesi_store,
			mem_domain_index, "mod_nmoesi_store_finish");
	
	EV_MOD_NMOESI_NC_STORE = esim_register_event_with_name(mod_handler_nmoesi_nc_store,
			mem_domain_index, "mod_nmoesi_nc_store");
	EV_MOD_NMOESI_NC_STORE_LOCK = esim_register_event_with_name(mod_handler_nmoesi_nc_store,
			mem_domain_index, "mod_nmoesi_nc_store_lock");
	EV_MOD_NMOESI_NC_STORE_WRITEBACK = esim_register_event_with_name(mod_handler_nmoesi_nc_store,
			mem_domain_index, "mod_nmoesi_nc_store_writeback");
	EV_MOD_NMOESI_NC_STORE_ACTION = esim_register_event_with_name(mod_handler_nmoesi_nc_store,
			mem_domain_index, "mod_nmoesi_nc_store_action");
	EV_MOD_NMOESI_NC_STORE_MISS= esim_register_event_with_name(mod_handler_nmoesi_nc_store,
			mem_domain_index, "mod_nmoesi_nc_store_miss");
	EV_MOD_NMOESI_NC_STORE_UNLOCK = esim_register_event_with_name(mod_handler_nmoesi_nc_store,
			mem_domain_index, "mod_nmoesi_nc_store_unlock");
	EV_MOD_NMOESI_NC_STORE_FINISH = esim_register_event_with_name(mod_handler_nmoesi_nc_store,
			mem_domain_index, "mod_nmoesi_nc_store_finish");

	EV_MOD_NMOESI_PREFETCH = esim_register_event_with_name(mod_handler_nmoesi_prefetch,
			mem_domain_index, "mod_nmoesi_prefetch");
	EV_MOD_NMOESI_PREFETCH_LOCK = esim_register_event_with_name(mod_handler_nmoesi_prefetch,
			mem_domain_index, "mod_nmoesi_prefetch_lock");
	EV_MOD_NMOESI_PREFETCH_ACTION = esim_register_event_with_name(mod_handler_nmoesi_prefetch,
			mem_domain_index, "mod_nmoesi_prefetch_action");
	EV_MOD_NMOESI_PREFETCH_MISS = esim_register_event_with_name(mod_handler_nmoesi_prefetch,
			mem_domain_index, "mod_nmoesi_prefetch_miss");
	EV_MOD_NMOESI_PREFETCH_UNLOCK = esim_register_event_with_name(mod_handler_nmoesi_prefetch,
			mem_domain_index, "mod_nmoesi_prefetch_unlock");
	EV_MOD_NMOESI_PREFETCH_FINISH = esim_register_event_with_name(mod_handler_nmoesi_prefetch,
			mem_domain_index, "mod_nmoesi_prefetch_finish");

	EV_MOD_NMOESI_FIND_AND_LOCK = esim_register_event_with_name(mod_handler_nmoesi_find_and_lock,
			mem_domain_index, "mod_nmoesi_find_and_lock");
	EV_MOD_NMOESI_FIND_AND_LOCK_PORT = esim_register_event_with_name(mod_handler_nmoesi_find_and_lock,
			mem_domain_index, "mod_nmoesi_find_and_lock_port");
	EV_MOD_NMOESI_FIND_AND_LOCK_ACTION = esim_register_event_with_name(mod_handler_nmoesi_find_and_lock,
			mem_domain_index, "mod_nmoesi_find_and_lock_action");
	EV_MOD_NMOESI_FIND_AND_LOCK_FINISH = esim_register_event_with_name(mod_handler_nmoesi_find_and_lock,
			mem_domain_index, "mod_nmoesi_find_and_lock_finish");

	EV_MOD_NMOESI_EVICT = esim_register_event_with_name(mod_handler_nmoesi_evict,
			mem_domain_index, "mod_nmoesi_evict");
	EV_MOD_NMOESI_EVICT_INVALID = esim_register_event_with_name(mod_handler_nmoesi_evict,
			mem_domain_index, "mod_nmoesi_evict_invalid");
	EV_MOD_NMOESI_EVICT_ACTION = esim_register_event_with_name(mod_handler_nmoesi_evict,
			mem_domain_index, "mod_nmoesi_evict_action");
	EV_MOD_NMOESI_EVICT_RECEIVE = esim_register_event_with_name(mod_handler_nmoesi_evict,
			mem_domain_index, "mod_nmoesi_evict_receive");
	EV_MOD_NMOESI_EVICT_PROCESS = esim_register_event_with_name(mod_handler_nmoesi_evict,
			mem_domain_index, "mod_nmoesi_evict_process");
	EV_MOD_NMOESI_EVICT_PROCESS_NONCOHERENT = esim_register_event_with_name(mod_handler_nmoesi_evict,
			mem_domain_index, "mod_nmoesi_evict_process_noncoherent");
	EV_MOD_NMOESI_EVICT_REPLY = esim_register_event_with_name(mod_handler_nmoesi_evict,
			mem_domain_index, "mod_nmoesi_evict_reply");
	EV_MOD_NMOESI_EVICT_REPLY = esim_register_event_with_name(mod_handler_nmoesi_evict,
			mem_domain_index, "mod_nmoesi_evict_reply");
	EV_MOD_NMOESI_EVICT_REPLY_RECEIVE = esim_register_event_with_name(mod_handler_nmoesi_evict,
			mem_domain_index, "mod_nmoesi_evict_reply_receive");
	EV_MOD_NMOESI_EVICT_FINISH = esim_register_event_with_name(mod_handler_nmoesi_evict,
			mem_domain_index, "mod_nmoesi_evict_finish");

	EV_MOD_NMOESI_WRITE_REQUEST = esim_register_event_with_name(mod_handler_nmoesi_write_request,
			mem_domain_index, "mod_nmoesi_write_request");
	EV_MOD_NMOESI_WRITE_REQUEST_RECEIVE = esim_register_event_with_name(mod_handler_nmoesi_write_request,
			mem_domain_index, "mod_nmoesi_write_request_receive");
	EV_MOD_NMOESI_WRITE_REQUEST_ACTION = esim_register_event_with_name(mod_handler_nmoesi_write_request,
			mem_domain_index, "mod_nmoesi_write_request_action");
	EV_MOD_NMOESI_WRITE_REQUEST_EXCLUSIVE = esim_register_event_with_name(mod_handler_nmoesi_write_request,
			mem_domain_index, "mod_nmoesi_write_request_exclusive");
	EV_MOD_NMOESI_WRITE_REQUEST_UPDOWN = esim_register_event_with_name(mod_handler_nmoesi_write_request,
			mem_domain_index, "mod_nmoesi_write_request_updown");
	EV_MOD_NMOESI_WRITE_REQUEST_UPDOWN_FINISH = esim_register_event_with_name(mod_handler_nmoesi_write_request,
			mem_domain_index, "mod_nmoesi_write_request_updown_finish");
	EV_MOD_NMOESI_WRITE_REQUEST_DOWNUP = esim_register_event_with_name(mod_handler_nmoesi_write_request,
			mem_domain_index, "mod_nmoesi_write_request_downup");
	EV_MOD_NMOESI_WRITE_REQUEST_DOWNUP_FINISH = esim_register_event_with_name(mod_handler_nmoesi_write_request,
			mem_domain_index, "mod_nmoesi_write_request_downup_finish");
	EV_MOD_NMOESI_WRITE_REQUEST_REPLY = esim_register_event_with_name(mod_handler_nmoesi_write_request,
			mem_domain_index, "mod_nmoesi_write_request_reply");
	EV_MOD_NMOESI_WRITE_REQUEST_FINISH = esim_register_event_with_name(mod_handler_nmoesi_write_request,
			mem_domain_index, "mod_nmoesi_write_request_finish");

	EV_MOD_NMOESI_READ_REQUEST = esim_register_event_with_name(mod_handler_nmoesi_read_request,
			mem_domain_index, "mod_nmoesi_read_request");
	EV_MOD_NMOESI_READ_REQUEST_RECEIVE = esim_register_event_with_name(mod_handler_nmoesi_read_request,
			mem_domain_index, "mod_nmoesi_read_request_receive");
	EV_MOD_NMOESI_READ_REQUEST_ACTION = esim_register_event_with_name(mod_handler_nmoesi_read_request,
			mem_domain_index, "mod_nmoesi_read_request_action");
	EV_MOD_NMOESI_READ_REQUEST_UPDOWN = esim_register_event_with_name(mod_handler_nmoesi_read_request,
			mem_domain_index, "mod_nmoesi_read_request_updown");
	EV_MOD_NMOESI_READ_REQUEST_UPDOWN_MISS = esim_register_event_with_name(mod_handler_nmoesi_read_request,
			mem_domain_index, "mod_nmoesi_read_request_updown_miss");
	EV_MOD_NMOESI_READ_REQUEST_UPDOWN_FINISH = esim_register_event_with_name(mod_handler_nmoesi_read_request,
			mem_domain_index, "mod_nmoesi_read_request_updown_finish");
	EV_MOD_NMOESI_READ_REQUEST_DOWNUP = esim_register_event_with_name(mod_handler_nmoesi_read_request,
			mem_domain_index, "mod_nmoesi_read_request_downup");
	EV_MOD_NMOESI_READ_REQUEST_DOWNUP_WAIT_FOR_REQS = esim_register_event_with_name(mod_handler_nmoesi_read_request,
			mem_domain_index, "mod_nmoesi_read_request_downup_wait_for_reqs");
	EV_MOD_NMOESI_READ_REQUEST_DOWNUP_FINISH = esim_register_event_with_name(mod_handler_nmoesi_read_request,
			mem_domain_index, "mod_nmoesi_read_request_downup_finish");
	EV_MOD_NMOESI_READ_REQUEST_REPLY = esim_register_event_with_name(mod_handler_nmoesi_read_request,
			mem_domain_index, "mod_nmoesi_read_request_reply");
	EV_MOD_NMOESI_READ_REQUEST_FINISH = esim_register_event_with_name(mod_handler_nmoesi_read_request,
			mem_domain_index, "mod_nmoesi_read_request_finish");

	EV_MOD_NMOESI_INVALIDATE = esim_register_event_with_name(mod_handler_nmoesi_invalidate,
			mem_domain_index, "mod_nmoesi_invalidate");
	EV_MOD_NMOESI_INVALIDATE_FINISH = esim_register_event_with_name(mod_handler_nmoesi_invalidate,
			mem_domain_index, "mod_nmoesi_invalidate_finish");

	EV_MOD_NMOESI_PEER_SEND = esim_register_event_with_name(mod_handler_nmoesi_peer,
			mem_domain_index, "mod_nmoesi_peer_send");
	EV_MOD_NMOESI_PEER_RECEIVE = esim_register_event_with_name(mod_handler_nmoesi_peer,
			mem_domain_index, "mod_nmoesi_peer_receive");
	EV_MOD_NMOESI_PEER_REPLY = esim_register_event_with_name(mod_handler_nmoesi_peer,
			mem_domain_index, "mod_nmoesi_peer_reply");
	EV_MOD_NMOESI_PEER_FINISH = esim_register_event_with_name(mod_handler_nmoesi_peer,
			mem_domain_index, "mod_nmoesi_peer_finish");

	EV_MOD_NMOESI_MESSAGE = esim_register_event_with_name(mod_handler_nmoesi_message,
			mem_domain_index, "mod_nmoesi_message");
	EV_MOD_NMOESI_MESSAGE_RECEIVE = esim_register_event_with_name(mod_handler_nmoesi_message,
			mem_domain_index, "mod_nmoesi_message_receive");
	EV_MOD_NMOESI_MESSAGE_ACTION = esim_register_event_with_name(mod_handler_nmoesi_message,
			mem_domain_index, "mod_nmoesi_message_action");
	EV_MOD_NMOESI_MESSAGE_REPLY = esim_register_event_with_name(mod_handler_nmoesi_message,
			mem_domain_index, "mod_nmoesi_message_reply");
	EV_MOD_NMOESI_MESSAGE_FINISH = esim_register_event_with_name(mod_handler_nmoesi_message,
			mem_domain_index, "mod_nmoesi_message_finish");

	/* Local memory event driven simulation */

	EV_MOD_LOCAL_MEM_LOAD = esim_register_event_with_name(mod_handler_local_mem_load,
			mem_domain_index, "mod_local_mem_load");
	EV_MOD_LOCAL_MEM_LOAD_LOCK = esim_register_event_with_name(mod_handler_local_mem_load,
			mem_domain_index, "mod_local_mem_load_lock");
	EV_MOD_LOCAL_MEM_LOAD_FINISH = esim_register_event_with_name(mod_handler_local_mem_load,
			mem_domain_index, "mod_local_mem_load_finish");

	EV_MOD_LOCAL_MEM_STORE = esim_register_event_with_name(mod_handler_local_mem_store,
			mem_domain_index, "mod_local_mem_store");
	EV_MOD_LOCAL_MEM_STORE_LOCK = esim_register_event_with_name(mod_handler_local_mem_store,
			mem_domain_index, "mod_local_mem_store_lock");
	EV_MOD_LOCAL_MEM_STORE_FINISH = esim_register_event_with_name(mod_handler_local_mem_store,
			mem_domain_index, "mod_local_mem_store_finish");

	EV_MOD_LOCAL_MEM_FIND_AND_LOCK = esim_register_event_with_name(mod_handler_local_mem_find_and_lock,
			mem_domain_index, "mod_local_mem_find_and_lock");
	EV_MOD_LOCAL_MEM_FIND_AND_LOCK_PORT = esim_register_event_with_name(mod_handler_local_mem_find_and_lock,
			mem_domain_index, "mod_local_mem_find_and_lock_port");
	EV_MOD_LOCAL_MEM_FIND_AND_LOCK_ACTION = esim_register_event_with_name(mod_handler_local_mem_find_and_lock,
			mem_domain_index, "mod_local_mem_find_and_lock_action");
	EV_MOD_LOCAL_MEM_FIND_AND_LOCK_FINISH = esim_register_event_with_name(mod_handler_local_mem_find_and_lock,
			mem_domain_index, "mod_local_mem_find_and_lock_finish");
}


void mem_system_done(void)
{
	/* Dump report */
	mem_system_dump_report();

	/* Free memory system */
	mem_system_free(mem_system);
}


void mem_system_dump_report(void)
{
	struct net_t *net;
	struct mod_t *mod;
	struct cache_t *cache;

	FILE *f;
	FILE *f_as;
	FILE *f_lc;
	FILE *f_st;
	// Network Debugging
	FILE *f_nt;

	int i;

	/* Open file */
	f    = file_open_for_write(mem_report_file_name);
	f_st = file_open_for_write(mem_report_file_name_state_transition);
	f_lc = file_open_for_write(mem_report_file_name_latency_counter);
	f_as = file_open_for_write(mem_report_file_name_access_statistics);
	// Network Debugging
	f_nt = file_open_for_write("Network_Configuration");

	if (!f)
		return;
	
	/* Intro */
	fprintf(f, "; Report for caches, TLBs, and main memory\n");
	fprintf(f, ";    Accesses - Total number of accesses\n");
	fprintf(f, ";    Hits, Misses - Accesses resulting in hits/misses\n");
	fprintf(f, ";    HitRatio - Hits divided by accesses\n");
	fprintf(f, ";    Evictions - Invalidated or replaced cache blocks\n");
	fprintf(f, ";    Retries - For L1 caches, accesses that were retried\n");
	fprintf(f, ";    ReadRetries, WriteRetries, NCWriteRetries - Read/Write retried accesses\n");
	fprintf(f, ";    NoRetryAccesses - Number of accesses that were not retried\n");
	fprintf(f, ";    NoRetryHits, NoRetryMisses - Hits and misses for not retried accesses\n");
	fprintf(f, ";    NoRetryHitRatio - NoRetryHits divided by NoRetryAccesses\n");
	fprintf(f, ";    NoRetryReads, NoRetryWrites - Not retried reads and writes\n");
	fprintf(f, ";    Reads, Writes, NCWrites - Total read/write accesses\n");
	fprintf(f, ";    BlockingReads, BlockingWrites, BlockingNCWrites - Reads/writes coming from lower-level cache\n");
	fprintf(f, ";    NonBlockingReads, NonBlockingWrites, NonBlockingNCWrites - Coming from upper-level cache\n");
	fprintf(f, "\n\n");
	
	/* Report for each cache */
	for (i = 0; i < list_count(mem_system->mod_list); i++)
	{
		mod = list_get(mem_system->mod_list, i);
		cache = mod->cache;
		fprintf(f, "[ %s ]\n", mod->name);
		fprintf(f_as, "[ %s ]\n", mod->name);
		fprintf(f_st, "[ %s ]\n", mod->name);
		fprintf(f_lc, "[ %s ]\n", mod->name);
		fprintf(f_nt, "[ %s ]\n", mod->name);
		fprintf(f, "\n");
		fprintf(f_as, "\n");
		fprintf(f_st, "\n");
		fprintf(f_lc, "\n");
		fprintf(f_nt, "\n");

		/* Configuration */
		if (cache) {
			fprintf(f, "Sets = %d\n", cache->num_sets);
			fprintf(f, "Assoc = %d\n", cache->assoc);
			fprintf(f, "Policy = %s\n", str_map_value(&cache_policy_map, cache->policy));
		}
		fprintf(f, "BlockSize = %d\n", mod->block_size);
		fprintf(f, "Latency = %d\n", mod->latency);
		fprintf(f, "Ports = %d\n", mod->num_ports);
		fprintf(f, "\n");

		/* Statistics */
		fprintf(f, "Accesses = %lld\n", mod->accesses);
		fprintf(f, "Hits = %lld\n", mod->hits);
		fprintf(f, "Misses = %lld\n", mod->accesses - mod->hits);
		fprintf(f, "HitRatio = %.4g\n", mod->accesses ?
			(double) mod->hits / mod->accesses : 0.0);
		fprintf(f, "Evictions = %lld\n", mod->evictions);
		fprintf(f, "Retries = %lld\n", mod->read_retries + mod->write_retries + 
			mod->nc_write_retries);
		fprintf(f, "\n");
		fprintf(f, "Reads = %lld\n", mod->reads);
		fprintf(f, "ReadRetries = %lld\n", mod->read_retries);
		fprintf(f, "BlockingReads = %lld\n", mod->blocking_reads);
		fprintf(f, "NonBlockingReads = %lld\n", mod->non_blocking_reads);
		fprintf(f, "ReadHits = %lld\n", mod->read_hits);
		fprintf(f, "ReadMisses = %lld\n", mod->reads - mod->read_hits);
		fprintf(f, "\n");
		fprintf(f, "Writes = %lld\n", mod->writes);
		fprintf(f, "WriteRetries = %lld\n", mod->write_retries);
		fprintf(f, "BlockingWrites = %lld\n", mod->blocking_writes);
		fprintf(f, "NonBlockingWrites = %lld\n", mod->non_blocking_writes);
		fprintf(f, "WriteHits = %lld\n", mod->write_hits);
		fprintf(f, "WriteMisses = %lld\n", mod->writes - mod->write_hits);
		fprintf(f, "\n");
		fprintf(f, "NCWrites = %lld\n", mod->nc_writes);
		fprintf(f, "NCWriteRetries = %lld\n", mod->nc_write_retries);
		fprintf(f, "NCBlockingWrites = %lld\n", mod->blocking_nc_writes);
		fprintf(f, "NCNonBlockingWrites = %lld\n", mod->non_blocking_nc_writes);
		fprintf(f, "NCWriteHits = %lld\n", mod->nc_write_hits);
		fprintf(f, "NCWriteMisses = %lld\n", mod->nc_writes - mod->nc_write_hits);
		fprintf(f, "Prefetches = %lld\n", mod->prefetches);
		fprintf(f, "PrefetchAborts = %lld\n", mod->prefetch_aborts);
		fprintf(f, "UselessPrefetches = %lld\n", mod->useless_prefetches);
		fprintf(f, "\n");
		fprintf(f, "NoRetryAccesses = %lld\n", mod->no_retry_accesses);
		fprintf(f, "NoRetryHits = %lld\n", mod->no_retry_hits);
		fprintf(f, "NoRetryMisses = %lld\n", mod->no_retry_accesses - mod->no_retry_hits);
		fprintf(f, "NoRetryHitRatio = %.4g\n", mod->no_retry_accesses ?
			(double) mod->no_retry_hits / mod->no_retry_accesses : 0.0);
		fprintf(f, "NoRetryReads = %lld\n", mod->no_retry_reads);
		fprintf(f, "NoRetryReadHits = %lld\n", mod->no_retry_read_hits);
		fprintf(f, "NoRetryReadMisses = %lld\n", (mod->no_retry_reads -
			mod->no_retry_read_hits));
		fprintf(f, "NoRetryWrites = %lld\n", mod->no_retry_writes);
		fprintf(f, "NoRetryWriteHits = %lld\n", mod->no_retry_write_hits);
		fprintf(f, "NoRetryWriteMisses = %lld\n", mod->no_retry_writes
			- mod->no_retry_write_hits);
		fprintf(f, "NoRetryNCWrites = %lld\n", mod->no_retry_nc_writes);
		fprintf(f, "NoRetryNCWriteHits = %lld\n", mod->no_retry_nc_write_hits);
		fprintf(f, "NoRetryNCWriteMisses = %lld\n", mod->no_retry_nc_writes
			- mod->no_retry_nc_write_hits);

		if(mod->num_load_requests)             fprintf(f_as, "num_load_requests = %lld\n",             mod->num_load_requests);
		if(mod->num_store_requests)            fprintf(f_as, "num_store_requests = %lld\n",            mod->num_store_requests);
		if(mod->num_eviction_requests)         fprintf(f_as, "num_eviction_requests = %lld\n",         mod->num_eviction_requests);
		if(mod->num_read_requests)             fprintf(f_as, "num_read_requests = %lld\n",             mod->num_read_requests);
		if(mod->num_writeback_requests)        fprintf(f_as, "num_writeback_requests = %lld\n",        mod->num_writeback_requests);
		if(mod->num_downup_read_requests)      fprintf(f_as, "num_downup_read_requests = %lld\n",      mod->num_downup_read_requests);
		if(mod->num_downup_writeback_requests) fprintf(f_as, "num_downup_writeback_requests = %lld\n", mod->num_downup_writeback_requests);
		if(mod->num_downup_eviction_requests)  fprintf(f_as, "num_downup_eviction_requests = %lld\n",  mod->num_downup_eviction_requests);

		
		//-------------------------------------------------------
		// Required Statistics printing
		//-------------------------------------------------------

		// Statistics for request counters
		fprintf(f_as, "===============REQUEST COUNT==============================\n");
		fprintf(f_as, "load_requests = %lld\n",                       mod->load_requests);
		fprintf(f_as, "store_requests = %lld\n",                      mod->store_requests);
		fprintf(f_as, "downup_read_requests = %lld\n",                mod->downup_read_requests);
		fprintf(f_as, "downup_writeback_requests = %lld\n",           mod->downup_writeback_requests);
		fprintf(f_as, "writeback_due_to_eviction = %lld\n",           mod->writeback_due_to_eviction);
		fprintf(f_as, "\n===============REQUEST HITS==============================\n");
		fprintf(f_as, "load_requests_hits = %lld\n",                  mod->load_requests_hits);
		fprintf(f_as, "store_requests_hits = %lld\n",                 mod->store_requests_hits);
		fprintf(f_as, "downup_read_requests_hits = %lld\n",           mod->downup_read_requests_hits);
		fprintf(f_as, "downup_writeback_requests_hits = %lld\n",      mod->downup_writeback_requests_hits);
		fprintf(f_as, "writeback_due_to_eviction_hits = %lld\n",      mod->writeback_due_to_eviction_hits);
		fprintf(f_as, "\n===============REQUEST MISSES==============================\n");
		fprintf(f_as, "load_requests_misses = %lld\n",                mod->load_requests_misses);
		fprintf(f_as, "store_requests_misses = %lld\n",               mod->store_requests_misses);
		fprintf(f_as, "downup_read_requests_misses = %lld\n",         mod->downup_read_requests_misses);
		fprintf(f_as, "downup_writeback_requests_misses = %lld\n",    mod->downup_writeback_requests_misses);
		fprintf(f_as, "writeback_due_to_eviction_misses = %lld\n",    mod->writeback_due_to_eviction_misses);
		fprintf(f_as, "\n===============GENERATED REQUESTS==============================\n");
		fprintf(f_as, "updown_read_requests_generated = %lld\n",      mod->updown_read_requests_generated);
		fprintf(f_as, "updown_writeback_requests_generated = %lld\n", mod->updown_writeback_requests_generated);
		fprintf(f_as, "\n===============EVICTION STATISTICS==============================\n");
		fprintf(f_as, "Evictions = %lld\n", mod->evictions);
		fprintf(f_as, "eviction_due_to_load = %lld\n",		            mod->eviction_due_to_load);
		fprintf(f_as, "eviction_due_to_store = %lld\n", 	            mod->eviction_due_to_store);
		fprintf(f_as, "\n===============COALESCED AND OTHER WAITING ACCESSES==============================\n");
		fprintf(f_as, "coalesced_loads = %lld\n",                         mod->coalesced_loads);
		fprintf(f_as, "coalesced_stores = %lld\n",                        mod->coalesced_stores);
		fprintf(f_as, "loads_waiting_for_non_coalesced_accesses = %lld\n",mod->loads_waiting_for_non_coalesced_accesses);
		fprintf(f_as, "loads_waiting_for_stores = %lld\n",                mod->loads_waiting_for_stores);
		fprintf(f_as, "read_waiting_for_other_accesses = %lld\n",         mod->read_waiting_for_other_accesses);
		fprintf(f_as, "write_waiting_for_other_accesses = %lld\n",        mod->write_waiting_for_other_accesses);
		
		// Statistics for waiting accesses

		fprintf(f_as, "\n===============WAITING COUNTERS==============================\n");
		fprintf(f_as, "read_waiting_for_mod_port = %lld\n"                          , mod->read_waiting_for_mod_port);
		fprintf(f_as, "read_waiting_for_directory_lock = %lld\n"                    , mod->read_waiting_for_directory_lock);
		// fprintf(f, "max_sim_read_waiting_for_mod_port = %lld\n"                  , mod->max_sim_read_waiting_for_mod_port);
		// fprintf(f, "max_sim_read_waiting_for_directory_lock = %lld\n"            , mod->max_sim_read_waiting_for_directory_lock);
		fprintf(f_as, "write_waiting_for_mod_port = %lld\n"                         , mod->write_waiting_for_mod_port);
		fprintf(f_as, "write_waiting_for_directory_lock = %lld\n"                   , mod->write_waiting_for_directory_lock);
		// fprintf(f, "max_sim_write_waiting_for_mod_port = %lld\n"                 , mod->max_sim_write_waiting_for_mod_port);
		// fprintf(f, "max_sim_write_waiting_for_directory_lock = %lld\n"           , mod->max_sim_write_waiting_for_directory_lock);
		fprintf(f_as, "eviction_waiting_for_mod_port = %lld\n"                      , mod->eviction_waiting_for_mod_port);
		fprintf(f_as, "eviction_waiting_for_directory_lock = %lld\n"                , mod->eviction_waiting_for_directory_lock);
		// fprintf(f, "max_sim_eviction_waiting_for_mod_port = %lld\n"              , mod->max_sim_eviction_waiting_for_mod_port);
		// fprintf(f, "max_sim_eviction_waiting_for_directory_lock = %lld\n"        , mod->max_sim_eviction_waiting_for_directory_lock);
		// fprintf(f, "eviction_waiting_for_other_accesses = %lld\n"                , mod->eviction_waiting_for_other_accesses);
		fprintf(f_as, "downup_read_waiting_for_mod_port = %lld\n"                   , mod->downup_read_waiting_for_mod_port);
		fprintf(f_as, "downup_read_waiting_for_directory_lock = %lld\n"             , mod->downup_read_waiting_for_directory_lock);
		// fprintf(f, "max_sim_downup_read_waiting_for_mod_port = %lld\n"           , mod->max_sim_downup_read_waiting_for_mod_port);
		// fprintf(f, "max_sim_downup_read_waiting_for_directory_lock = %lld\n"     , mod->max_sim_downup_read_waiting_for_directory_lock);
		// fprintf(f, "downup_read_waiting_for_other_accesses = %lld\n"             , mod->downup_read_waiting_for_other_accesses);
		fprintf(f_as, "downup_writeback_waiting_for_mod_port = %lld\n"              , mod->downup_writeback_waiting_for_mod_port);
		fprintf(f_as, "downup_writeback_waiting_for_directory_lock = %lld\n"        , mod->downup_writeback_waiting_for_directory_lock);
		// fprintf(f, "max_sim_downup_writeback_waiting_for_mod_port = %lld\n"      , mod->max_sim_downup_writeback_waiting_for_mod_port);
		// fprintf(f, "max_sim_downup_writeback_waiting_for_directory_lock = %lld\n", mod->max_sim_downup_writeback_waiting_for_directory_lock);
		// fprintf(f, "downup_writeback_waiting_for_other_accesses = %lld\n"        , mod->downup_writeback_waiting_for_other_accesses);
		
		fprintf(f_as, "\n===============NETWORK REQUESTS WAITING==============================\n");
		fprintf(f_as, "read_send_requests_retried_nw = %lld\n",            	mod->read_send_requests_retried_nw);
		fprintf(f_as, "writeback_send_requests_retried_nw = %lld\n",       	mod->writeback_send_requests_retried_nw);
		fprintf(f_as, "eviction_send_requests_retried_nw = %lld\n",        	mod->eviction_send_requests_retried_nw);
		fprintf(f_as, "downup_read_send_requests_retried_nw = %lld\n",     	mod->downup_read_send_requests_retried_nw);
		fprintf(f_as, "downup_writeback_send_requests_retried_nw = %lld\n",	mod->downup_writeback_send_requests_retried_nw);
		fprintf(f_as, "downup_eviction_send_requests_retried_nw = %lld\n", 	mod->downup_eviction_send_requests_retried_nw);
		fprintf(f_as, "peer_send_requests_retried_nw = %lld\n",            	mod->peer_send_requests_retried_nw);

		fprintf(f_as, "\n===============NETWORK REPLIES WAITING==============================\n");
		fprintf(f_as, "read_send_replies_retried_nw = %lld\n",             	mod->read_send_replies_retried_nw);
		fprintf(f_as, "writeback_send_replies_retried_nw = %lld\n",        	mod->writeback_send_replies_retried_nw);
		fprintf(f_as, "eviction_send_replies_retried_nw = %lld\n",         	mod->eviction_send_replies_retried_nw);
		fprintf(f_as, "downup_read_send_replies_retried_nw = %lld\n",      	mod->downup_read_send_replies_retried_nw);
		fprintf(f_as, "downup_writeback_send_replies_retried_nw = %lld\n", 	mod->downup_writeback_send_replies_retried_nw);
		fprintf(f_as, "downup_eviction_send_replies_retried_nw = %lld\n",  	mod->downup_eviction_send_replies_retried_nw);
		fprintf(f_as, "peer_send_replies_retried_nw = %lld\n",             	mod->peer_send_replies_retried_nw);

		fprintf(f_lc, "\n===============WAITING COUNTERS FOR MOD PORTS==============================\n");
		for(int i=0; i<6; i++)	
			if(mod->read_time_waiting_mod_port[i]) fprintf(f_lc, "read_time_waiting_mod_port_range_%d_to_%d = %lld\n", pow_2(i), pow_2(i+1) -1, mod->read_time_waiting_mod_port[i]);
		for(int i=0; i<6; i++)	
			if(mod->write_time_waiting_mod_port[i]) fprintf(f_lc, "write_time_waiting_mod_port_range_%d_to_%d = %lld\n", pow_2(i), pow_2(i+1) -1, mod->write_time_waiting_mod_port[i]);
		for(int i=0; i<6; i++)	
			if(mod->eviction_time_waiting_mod_port[i]) fprintf(f_lc, "eviction_time_waiting_mod_port_range_%d_to_%d = %lld\n", pow_2(i), pow_2(i+1) -1, mod->eviction_time_waiting_mod_port[i]);
		for(int i=0; i<6; i++)	
			if(mod->downup_read_time_waiting_mod_port[i]) fprintf(f_lc, "downup_read_time_waiting_mod_port_range_%d_to_%d = %lld\n", pow_2(i), pow_2(i+1) -1, mod->downup_read_time_waiting_mod_port[i]);
		for(int i=0; i<6; i++)	
			if(mod->downup_writeback_time_waiting_mod_port[i]) fprintf(f_lc, "downup_writeback_time_waiting_mod_port_range_%d_to_%d = %lld\n", pow_2(i), pow_2(i+1) -1, mod->downup_writeback_time_waiting_mod_port[i]);
		
		fprintf(f_lc, "\n===============WAITING COUNTERS FOR DIRECTORY LOCKS==============================\n");
		for(int i=0; i<6; i++)	
			if(mod->read_time_waiting_directory_lock[i]) fprintf(f_lc, "read_time_waiting_directory_lock_range_%d_to_%d = %lld\n", pow_2(i), pow_2(i+1) -1, mod->read_time_waiting_directory_lock[i]);
		for(int i=0; i<6; i++)	
			if(mod->write_time_waiting_directory_lock[i]) fprintf(f_lc, "write_time_waiting_directory_lock_range_%d_to_%d = %lld\n", pow_2(i), pow_2(i+1) -1, mod->write_time_waiting_directory_lock[i]);
		for(int i=0; i<6; i++)	
			if(mod->eviction_time_waiting_directory_lock[i]) fprintf(f_lc, "eviction_time_waiting_directory_lock_range_%d_to_%d = %lld\n", pow_2(i), pow_2(i+1) -1, mod->eviction_time_waiting_directory_lock[i]);
		for(int i=0; i<6; i++)	
			if(mod->downup_read_time_waiting_directory_lock[i]) fprintf(f_lc, "downup_read_time_waiting_directory_lock_range_%d_to_%d = %lld\n", pow_2(i), pow_2(i+1) -1, mod->downup_read_time_waiting_directory_lock[i]);
		for(int i=0; i<6; i++)	
			if(mod->downup_writeback_time_waiting_directory_lock[i]) fprintf(f_lc, "downup_writeback_time_waiting_directory_lock_range_%d_to_%d = %lld\n", pow_2(i), pow_2(i+1) -1, mod->downup_writeback_time_waiting_directory_lock[i]);

		fprintf(f_lc, "\n===============WAITING COUNTERS FOR OTHER ACCESSES==============================\n");
		for(int i=0; i<5; i++)
			if(mod->loads_time_waiting_for_non_coalesced_accesses[i]) fprintf(f_lc, "loads_time_waiting_for_non_coalesced_accesses_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->loads_time_waiting_for_non_coalesced_accesses[i]);
		for(int i=0; i<5; i++)
			if(mod->loads_time_waiting_for_stores[i]) fprintf(f_lc, "loads_time_waiting_for_stores_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->loads_time_waiting_for_stores[i]);
		for(int i=0; i<5; i++)
			if(mod->stores_time_waiting[i]) fprintf(f_lc, "stores_time_waiting_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->stores_time_waiting[i]);
		
		fprintf(f_lc, "\n===============WAITING COUNTERS FOR NETWORK REQUESTS SEND==============================\n");
		for(int i=0; i<6; i++)
			if(mod->read_send_requests_nw_cycles[i]) fprintf(f_lc, "read_send_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->read_send_requests_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->writeback_send_requests_nw_cycles[i]) fprintf(f_lc, "writeback_send_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->writeback_send_requests_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->eviction_send_requests_nw_cycles[i]) fprintf(f_lc, "eviction_send_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->eviction_send_requests_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->downup_read_send_requests_nw_cycles[i]) fprintf(f_lc, "downup_read_send_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->downup_read_send_requests_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->downup_writeback_send_requests_nw_cycles[i]) fprintf(f_lc, "downup_writeback_send_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->downup_writeback_send_requests_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->downup_eviction_send_requests_nw_cycles[i]) fprintf(f_lc, "downup_eviction_send_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->downup_eviction_send_requests_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->peer_send_requests_nw_cycles[i]) fprintf(f_lc, "peer_send_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->peer_send_requests_nw_cycles[i]);
			
		fprintf(f_lc, "\n===============WAITING COUNTERS FOR NETWORK REPLIES SEND==============================\n");
		for(int i=0; i<6; i++)
			if(mod->read_send_replies_nw_cycles[i]) fprintf(f_lc, "read_send_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->read_send_replies_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->writeback_send_replies_nw_cycles[i]) fprintf(f_lc, "writeback_send_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->writeback_send_replies_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->eviction_send_replies_nw_cycles[i]) fprintf(f_lc, "eviction_send_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->eviction_send_replies_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->downup_read_send_replies_nw_cycles[i]) fprintf(f_lc, "downup_read_send_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->downup_read_send_replies_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->downup_writeback_send_replies_nw_cycles[i]) fprintf(f_lc, "downup_writeback_send_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->downup_writeback_send_replies_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->downup_eviction_send_replies_nw_cycles[i]) fprintf(f_lc, "downup_eviction_send_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->downup_eviction_send_replies_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->peer_send_replies_nw_cycles[i]) fprintf(f_lc, "peer_send_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->peer_send_replies_nw_cycles[i]);
			
		fprintf(f_lc, "\n===============WAITING COUNTERS FOR NETWORK REQUESTS RECEIVE==============================\n");
		for(int i=0; i<6; i++)
			if(mod->read_receive_requests_nw_cycles[i]) fprintf(f_lc, "read_receive_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->read_receive_requests_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->writeback_receive_requests_nw_cycles[i]) fprintf(f_lc, "writeback_receive_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->writeback_receive_requests_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->eviction_receive_requests_nw_cycles[i]) fprintf(f_lc, "eviction_receive_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->eviction_receive_requests_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->downup_read_receive_requests_nw_cycles[i]) fprintf(f_lc, "downup_read_receive_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->downup_read_receive_requests_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->downup_writeback_receive_requests_nw_cycles[i]) fprintf(f_lc, "downup_writeback_receive_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->downup_writeback_receive_requests_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->downup_eviction_receive_requests_nw_cycles[i]) fprintf(f_lc, "downup_eviction_receive_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->downup_eviction_receive_requests_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->peer_receive_requests_nw_cycles[i]) fprintf(f_lc, "peer_receive_requests_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) -1, mod->peer_receive_requests_nw_cycles[i]);
			
		fprintf(f_lc, "\n===============WAITING COUNTERS FOR NETWORK REPLIES RECEIVE==============================\n");
		for(int i=0; i<6; i++)
			if(mod->read_receive_replies_nw_cycles[i]) fprintf(f_lc, "read_receive_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->read_receive_replies_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->writeback_receive_replies_nw_cycles[i]) fprintf(f_lc, "writeback_receive_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->writeback_receive_replies_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->eviction_receive_replies_nw_cycles[i]) fprintf(f_lc, "eviction_receive_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->eviction_receive_replies_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->downup_read_receive_replies_nw_cycles[i]) fprintf(f_lc, "downup_read_receive_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->downup_read_receive_replies_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->downup_writeback_receive_replies_nw_cycles[i]) fprintf(f_lc, "downup_writeback_receive_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->downup_writeback_receive_replies_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->downup_eviction_receive_replies_nw_cycles[i]) fprintf(f_lc, "downup_eviction_receive_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->downup_eviction_receive_replies_nw_cycles[i]);
		for(int i=0; i<6; i++)
			if(mod->peer_receive_replies_nw_cycles[i]) fprintf(f_lc, "peer_receive_replies_nw_cycles_range_%d_to_%d = %lld\n", pow_2(i+1), pow_2(i+2) - 1, mod->peer_receive_replies_nw_cycles[i]);
		
		 fprintf(f_as, "\n===============STATES ACCESSED IN REQUESTS==============================\n");
		 fprintf(f_as, "read_state_invalid = %lld\n"                           , mod->read_state_invalid);
		 fprintf(f_as, "read_state_noncoherent = %lld\n"                       , mod->read_state_noncoherent);
		 fprintf(f_as, "read_state_modified = %lld\n"                          , mod->read_state_modified);
		 fprintf(f_as, "read_state_shared = %lld\n"                            , mod->read_state_shared);
		 fprintf(f_as, "read_state_owned = %lld\n"                             , mod->read_state_owned);
		 fprintf(f_as, "read_state_exclusive = %lld\n"                         , mod->read_state_exclusive);
		 fprintf(f_as, "write_state_invalid = %lld\n"                          , mod->write_state_invalid);
		 fprintf(f_as, "write_state_noncoherent = %lld\n"                      , mod->write_state_noncoherent);
		 fprintf(f_as, "write_state_modified = %lld\n"                         , mod->write_state_modified);
		 fprintf(f_as, "write_state_shared = %lld\n"                           , mod->write_state_shared);
		 fprintf(f_as, "write_state_owned = %lld\n"                            , mod->write_state_owned);
		 fprintf(f_as, "write_state_exclusive = %lld\n"                        , mod->write_state_exclusive);
		 fprintf(f_as, "sharer_req_state_invalid = %lld\n"                     , mod->sharer_req_state_invalid);
		 fprintf(f_as, "sharer_req_state_noncoherent = %lld\n"                 , mod->sharer_req_state_noncoherent);
		 fprintf(f_as, "sharer_req_state_modified = %lld\n"                    , mod->sharer_req_state_modified);
		 fprintf(f_as, "sharer_req_state_shared = %lld\n"                      , mod->sharer_req_state_shared);
		 fprintf(f_as, "sharer_req_state_owned = %lld\n"                       , mod->sharer_req_state_owned);
		 fprintf(f_as, "sharer_req_state_exclusive = %lld\n"                   , mod->sharer_req_state_exclusive);

		 fprintf(f_as, "\n===============PEER TRANSFERS==============================\n");
		 fprintf(f_as, "peer_transfers = %lld\n"                               , mod->peer_transfers);
		 fprintf(f_as, "\n===============SHARER REQUESTS FOR INVALIDATION==============================\n");
		 fprintf(f_as, "sharer_req_for_invalidation = %lld\n"                  , mod->sharer_req_for_invalidation);

		 fprintf(f_st, "\n===============STATES TRANSITIONS IN REQUESTS==============================\n");
		 fprintf(f_st, "\n===================LOAD REQUESTS===========================================\n");
		 if(mod->load_state_invalid_to_invalid)        	 fprintf(f_st, "load_state_invalid_to_invalid = %lld\n"                , mod->load_state_invalid_to_invalid);
		 if(mod->load_state_invalid_to_noncoherent)    	 fprintf(f_st, "load_state_invalid_to_noncoherent = %lld\n"            , mod->load_state_invalid_to_noncoherent);
		 if(mod->load_state_invalid_to_modified)       	 fprintf(f_st, "load_state_invalid_to_modified = %lld\n"               , mod->load_state_invalid_to_modified);
		 if(mod->load_state_invalid_to_shared)         	 fprintf(f_st, "load_state_invalid_to_shared = %lld\n"                 , mod->load_state_invalid_to_shared);
		 if(mod->load_state_invalid_to_owned)          	 fprintf(f_st, "load_state_invalid_to_owned = %lld\n"                  , mod->load_state_invalid_to_owned);
		 if(mod->load_state_invalid_to_exclusive)      	 fprintf(f_st, "load_state_invalid_to_exclusive = %lld\n"              , mod->load_state_invalid_to_exclusive);
		 if(mod->load_state_noncoherent_to_invalid)    	 fprintf(f_st, "load_state_noncoherent_to_invalid = %lld\n"            , mod->load_state_noncoherent_to_invalid);
		 if(mod->load_state_noncoherent_to_noncoherent)	 fprintf(f_st, "load_state_noncoherent_to_noncoherent = %lld\n"        , mod->load_state_noncoherent_to_noncoherent);
		 if(mod->load_state_noncoherent_to_modified)   	 fprintf(f_st, "load_state_noncoherent_to_modified = %lld\n"           , mod->load_state_noncoherent_to_modified);
		 if(mod->load_state_noncoherent_to_shared)     	 fprintf(f_st, "load_state_noncoherent_to_shared = %lld\n"             , mod->load_state_noncoherent_to_shared);
		 if(mod->load_state_noncoherent_to_owned)      	 fprintf(f_st, "load_state_noncoherent_to_owned = %lld\n"              , mod->load_state_noncoherent_to_owned);
		 if(mod->load_state_noncoherent_to_exclusive)  	 fprintf(f_st, "load_state_noncoherent_to_exclusive = %lld\n"          , mod->load_state_noncoherent_to_exclusive);
		 if(mod->load_state_modified_to_invalid)       	 fprintf(f_st, "load_state_modified_to_invalid = %lld\n"               , mod->load_state_modified_to_invalid);
		 if(mod->load_state_modified_to_noncoherent)   	 fprintf(f_st, "load_state_modified_to_noncoherent = %lld\n"           , mod->load_state_modified_to_noncoherent);
		 if(mod->load_state_modified_to_modified)      	 fprintf(f_st, "load_state_modified_to_modified = %lld\n"              , mod->load_state_modified_to_modified);
		 if(mod->load_state_modified_to_shared)        	 fprintf(f_st, "load_state_modified_to_shared = %lld\n"                , mod->load_state_modified_to_shared);
		 if(mod->load_state_modified_to_owned)         	 fprintf(f_st, "load_state_modified_to_owned = %lld\n"                 , mod->load_state_modified_to_owned);
		 if(mod->load_state_modified_to_exclusive)     	 fprintf(f_st, "load_state_modified_to_exclusive = %lld\n"             , mod->load_state_modified_to_exclusive);
		 if(mod->load_state_shared_to_invalid)         	 fprintf(f_st, "load_state_shared_to_invalid = %lld\n"                 , mod->load_state_shared_to_invalid);
		 if(mod->load_state_shared_to_noncoherent)     	 fprintf(f_st, "load_state_shared_to_noncoherent = %lld\n"             , mod->load_state_shared_to_noncoherent);
		 if(mod->load_state_shared_to_modified)        	 fprintf(f_st, "load_state_shared_to_modified = %lld\n"                , mod->load_state_shared_to_modified);
		 if(mod->load_state_shared_to_shared)          	 fprintf(f_st, "load_state_shared_to_shared = %lld\n"                  , mod->load_state_shared_to_shared);
		 if(mod->load_state_shared_to_owned)           	 fprintf(f_st, "load_state_shared_to_owned = %lld\n"                   , mod->load_state_shared_to_owned);
		 if(mod->load_state_shared_to_exclusive)       	 fprintf(f_st, "load_state_shared_to_exclusive = %lld\n"               , mod->load_state_shared_to_exclusive);
		 if(mod->load_state_owned_to_invalid)          	 fprintf(f_st, "load_state_owned_to_invalid = %lld\n"                  , mod->load_state_owned_to_invalid);
		 if(mod->load_state_owned_to_noncoherent)      	 fprintf(f_st, "load_state_owned_to_noncoherent = %lld\n"              , mod->load_state_owned_to_noncoherent);
		 if(mod->load_state_owned_to_modified)         	 fprintf(f_st, "load_state_owned_to_modified = %lld\n"                 , mod->load_state_owned_to_modified);
		 if(mod->load_state_owned_to_shared)           	 fprintf(f_st, "load_state_owned_to_shared = %lld\n"                   , mod->load_state_owned_to_shared);
		 if(mod->load_state_owned_to_owned)            	 fprintf(f_st, "load_state_owned_to_owned = %lld\n"                    , mod->load_state_owned_to_owned);
		 if(mod->load_state_owned_to_exclusive)        	 fprintf(f_st, "load_state_owned_to_exclusive = %lld\n"                , mod->load_state_owned_to_exclusive);
		 if(mod->load_state_exclusive_to_invalid)      	 fprintf(f_st, "load_state_exclusive_to_invalid = %lld\n"              , mod->load_state_exclusive_to_invalid);
		 if(mod->load_state_exclusive_to_noncoherent)  	 fprintf(f_st, "load_state_exclusive_to_noncoherent = %lld\n"          , mod->load_state_exclusive_to_noncoherent);
		 if(mod->load_state_exclusive_to_modified)     	 fprintf(f_st, "load_state_exclusive_to_modified = %lld\n"             , mod->load_state_exclusive_to_modified);
		 if(mod->load_state_exclusive_to_shared)       	 fprintf(f_st, "load_state_exclusive_to_shared = %lld\n"               , mod->load_state_exclusive_to_shared);
		 if(mod->load_state_exclusive_to_owned)        	 fprintf(f_st, "load_state_exclusive_to_owned = %lld\n"                , mod->load_state_exclusive_to_owned);
		 if(mod->load_state_exclusive_to_exclusive)    	 fprintf(f_st, "load_state_exclusive_to_exclusive = %lld\n"            , mod->load_state_exclusive_to_exclusive);
		 fprintf(f_st, "\n===================STORE REQUESTS===========================================\n");
		 if(mod->store_state_invalid_to_invalid)        	 fprintf(f_st, "store_state_invalid_to_invalid = %lld\n"               , mod->store_state_invalid_to_invalid);
		 if(mod->store_state_invalid_to_noncoherent)    	 fprintf(f_st, "store_state_invalid_to_noncoherent = %lld\n"           , mod->store_state_invalid_to_noncoherent);
		 if(mod->store_state_invalid_to_modified)       	 fprintf(f_st, "store_state_invalid_to_modified = %lld\n"              , mod->store_state_invalid_to_modified);
		 if(mod->store_state_invalid_to_shared)         	 fprintf(f_st, "store_state_invalid_to_shared = %lld\n"                , mod->store_state_invalid_to_shared);
		 if(mod->store_state_invalid_to_owned)          	 fprintf(f_st, "store_state_invalid_to_owned = %lld\n"                 , mod->store_state_invalid_to_owned);
		 if(mod->store_state_invalid_to_exclusive)      	 fprintf(f_st, "store_state_invalid_to_exclusive = %lld\n"             , mod->store_state_invalid_to_exclusive);
		 if(mod->store_state_noncoherent_to_invalid)    	 fprintf(f_st, "store_state_noncoherent_to_invalid = %lld\n"           , mod->store_state_noncoherent_to_invalid);
		 if(mod->store_state_noncoherent_to_noncoherent)	 fprintf(f_st, "store_state_noncoherent_to_noncoherent = %lld\n"       , mod->store_state_noncoherent_to_noncoherent);
		 if(mod->store_state_noncoherent_to_modified)   	 fprintf(f_st, "store_state_noncoherent_to_modified = %lld\n"          , mod->store_state_noncoherent_to_modified);
		 if(mod->store_state_noncoherent_to_shared)     	 fprintf(f_st, "store_state_noncoherent_to_shared = %lld\n"            , mod->store_state_noncoherent_to_shared);
		 if(mod->store_state_noncoherent_to_owned)      	 fprintf(f_st, "store_state_noncoherent_to_owned = %lld\n"             , mod->store_state_noncoherent_to_owned);
		 if(mod->store_state_noncoherent_to_exclusive)  	 fprintf(f_st, "store_state_noncoherent_to_exclusive = %lld\n"         , mod->store_state_noncoherent_to_exclusive);
		 if(mod->store_state_modified_to_invalid)       	 fprintf(f_st, "store_state_modified_to_invalid = %lld\n"              , mod->store_state_modified_to_invalid);
		 if(mod->store_state_modified_to_noncoherent)   	 fprintf(f_st, "store_state_modified_to_noncoherent = %lld\n"          , mod->store_state_modified_to_noncoherent);
		 if(mod->store_state_modified_to_modified)      	 fprintf(f_st, "store_state_modified_to_modified = %lld\n"             , mod->store_state_modified_to_modified);
		 if(mod->store_state_modified_to_shared)        	 fprintf(f_st, "store_state_modified_to_shared = %lld\n"               , mod->store_state_modified_to_shared);
		 if(mod->store_state_modified_to_owned)         	 fprintf(f_st, "store_state_modified_to_owned = %lld\n"                , mod->store_state_modified_to_owned);
		 if(mod->store_state_modified_to_exclusive)     	 fprintf(f_st, "store_state_modified_to_exclusive = %lld\n"            , mod->store_state_modified_to_exclusive);
		 if(mod->store_state_shared_to_invalid)         	 fprintf(f_st, "store_state_shared_to_invalid = %lld\n"                , mod->store_state_shared_to_invalid);
		 if(mod->store_state_shared_to_noncoherent)     	 fprintf(f_st, "store_state_shared_to_noncoherent = %lld\n"            , mod->store_state_shared_to_noncoherent);
		 if(mod->store_state_shared_to_modified)        	 fprintf(f_st, "store_state_shared_to_modified = %lld\n"               , mod->store_state_shared_to_modified);
		 if(mod->store_state_shared_to_shared)          	 fprintf(f_st, "store_state_shared_to_shared = %lld\n"                 , mod->store_state_shared_to_shared);
		 if(mod->store_state_shared_to_owned)           	 fprintf(f_st, "store_state_shared_to_owned = %lld\n"                  , mod->store_state_shared_to_owned);
		 if(mod->store_state_shared_to_exclusive)       	 fprintf(f_st, "store_state_shared_to_exclusive = %lld\n"              , mod->store_state_shared_to_exclusive);
		 if(mod->store_state_owned_to_invalid)          	 fprintf(f_st, "store_state_owned_to_invalid = %lld\n"                 , mod->store_state_owned_to_invalid);
		 if(mod->store_state_owned_to_noncoherent)      	 fprintf(f_st, "store_state_owned_to_noncoherent = %lld\n"             , mod->store_state_owned_to_noncoherent);
		 if(mod->store_state_owned_to_modified)         	 fprintf(f_st, "store_state_owned_to_modified = %lld\n"                , mod->store_state_owned_to_modified);
		 if(mod->store_state_owned_to_shared)           	 fprintf(f_st, "store_state_owned_to_shared = %lld\n"                  , mod->store_state_owned_to_shared);
		 if(mod->store_state_owned_to_owned)            	 fprintf(f_st, "store_state_owned_to_owned = %lld\n"                   , mod->store_state_owned_to_owned);
		 if(mod->store_state_owned_to_exclusive)        	 fprintf(f_st, "store_state_owned_to_exclusive = %lld\n"               , mod->store_state_owned_to_exclusive);
		 if(mod->store_state_exclusive_to_invalid)      	 fprintf(f_st, "store_state_exclusive_to_invalid = %lld\n"             , mod->store_state_exclusive_to_invalid);
		 if(mod->store_state_exclusive_to_noncoherent)  	 fprintf(f_st, "store_state_exclusive_to_noncoherent = %lld\n"         , mod->store_state_exclusive_to_noncoherent);
		 if(mod->store_state_exclusive_to_modified)     	 fprintf(f_st, "store_state_exclusive_to_modified = %lld\n"            , mod->store_state_exclusive_to_modified);
		 if(mod->store_state_exclusive_to_shared)       	 fprintf(f_st, "store_state_exclusive_to_shared = %lld\n"              , mod->store_state_exclusive_to_shared);
		 if(mod->store_state_exclusive_to_owned)        	 fprintf(f_st, "store_state_exclusive_to_owned = %lld\n"               , mod->store_state_exclusive_to_owned);
		 if(mod->store_state_exclusive_to_exclusive)    	 fprintf(f_st, "store_state_exclusive_to_exclusive = %lld\n"           , mod->store_state_exclusive_to_exclusive);
		 fprintf(f_st, "\n===================DOWNUP READ REQUESTS===========================================\n");
		 if(mod->downup_read_req_state_invalid_to_invalid)        	 fprintf(f_st, "downup_read_req_state_invalid_to_invalid = %lld\n"          , mod->downup_read_req_state_invalid_to_invalid);
		 if(mod->downup_read_req_state_invalid_to_noncoherent)    	 fprintf(f_st, "downup_read_req_state_invalid_to_noncoherent = %lld\n"      , mod->downup_read_req_state_invalid_to_noncoherent);
		 if(mod->downup_read_req_state_invalid_to_modified)       	 fprintf(f_st, "downup_read_req_state_invalid_to_modified = %lld\n"         , mod->downup_read_req_state_invalid_to_modified);
		 if(mod->downup_read_req_state_invalid_to_shared)         	 fprintf(f_st, "downup_read_req_state_invalid_to_shared = %lld\n"           , mod->downup_read_req_state_invalid_to_shared);
		 if(mod->downup_read_req_state_invalid_to_owned)          	 fprintf(f_st, "downup_read_req_state_invalid_to_owned = %lld\n"            , mod->downup_read_req_state_invalid_to_owned);
		 if(mod->downup_read_req_state_invalid_to_exclusive)      	 fprintf(f_st, "downup_read_req_state_invalid_to_exclusive = %lld\n"        , mod->downup_read_req_state_invalid_to_exclusive);
		 if(mod->downup_read_req_state_noncoherent_to_invalid)    	 fprintf(f_st, "downup_read_req_state_noncoherent_to_invalid = %lld\n"      , mod->downup_read_req_state_noncoherent_to_invalid);
		 if(mod->downup_read_req_state_noncoherent_to_noncoherent)	 fprintf(f_st, "downup_read_req_state_noncoherent_to_noncoherent = %lld\n"  , mod->downup_read_req_state_noncoherent_to_noncoherent);
		 if(mod->downup_read_req_state_noncoherent_to_modified)   	 fprintf(f_st, "downup_read_req_state_noncoherent_to_modified = %lld\n"     , mod->downup_read_req_state_noncoherent_to_modified);
		 if(mod->downup_read_req_state_noncoherent_to_shared)     	 fprintf(f_st, "downup_read_req_state_noncoherent_to_shared = %lld\n"       , mod->downup_read_req_state_noncoherent_to_shared);
		 if(mod->downup_read_req_state_noncoherent_to_owned)      	 fprintf(f_st, "downup_read_req_state_noncoherent_to_owned = %lld\n"        , mod->downup_read_req_state_noncoherent_to_owned);
		 if(mod->downup_read_req_state_noncoherent_to_exclusive)  	 fprintf(f_st, "downup_read_req_state_noncoherent_to_exclusive = %lld\n"    , mod->downup_read_req_state_noncoherent_to_exclusive);
		 if(mod->downup_read_req_state_modified_to_invalid)       	 fprintf(f_st, "downup_read_req_state_modified_to_invalid = %lld\n"         , mod->downup_read_req_state_modified_to_invalid);
		 if(mod->downup_read_req_state_modified_to_noncoherent)   	 fprintf(f_st, "downup_read_req_state_modified_to_noncoherent = %lld\n"     , mod->downup_read_req_state_modified_to_noncoherent);
		 if(mod->downup_read_req_state_modified_to_modified)      	 fprintf(f_st, "downup_read_req_state_modified_to_modified = %lld\n"        , mod->downup_read_req_state_modified_to_modified);
		 if(mod->downup_read_req_state_modified_to_shared)        	 fprintf(f_st, "downup_read_req_state_modified_to_shared = %lld\n"          , mod->downup_read_req_state_modified_to_shared);
		 if(mod->downup_read_req_state_modified_to_owned)         	 fprintf(f_st, "downup_read_req_state_modified_to_owned = %lld\n"           , mod->downup_read_req_state_modified_to_owned);
		 if(mod->downup_read_req_state_modified_to_exclusive)     	 fprintf(f_st, "downup_read_req_state_modified_to_exclusive = %lld\n"       , mod->downup_read_req_state_modified_to_exclusive);
		 if(mod->downup_read_req_state_shared_to_invalid)         	 fprintf(f_st, "downup_read_req_state_shared_to_invalid = %lld\n"           , mod->downup_read_req_state_shared_to_invalid);
		 if(mod->downup_read_req_state_shared_to_noncoherent)     	 fprintf(f_st, "downup_read_req_state_shared_to_noncoherent = %lld\n"       , mod->downup_read_req_state_shared_to_noncoherent);
		 if(mod->downup_read_req_state_shared_to_modified)        	 fprintf(f_st, "downup_read_req_state_shared_to_modified = %lld\n"          , mod->downup_read_req_state_shared_to_modified);
		 if(mod->downup_read_req_state_shared_to_shared)          	 fprintf(f_st, "downup_read_req_state_shared_to_shared = %lld\n"            , mod->downup_read_req_state_shared_to_shared);
		 if(mod->downup_read_req_state_shared_to_owned)           	 fprintf(f_st, "downup_read_req_state_shared_to_owned = %lld\n"             , mod->downup_read_req_state_shared_to_owned);
		 if(mod->downup_read_req_state_shared_to_exclusive)       	 fprintf(f_st, "downup_read_req_state_shared_to_exclusive = %lld\n"         , mod->downup_read_req_state_shared_to_exclusive);
		 if(mod->downup_read_req_state_owned_to_invalid)          	 fprintf(f_st, "downup_read_req_state_owned_to_invalid = %lld\n"            , mod->downup_read_req_state_owned_to_invalid);
		 if(mod->downup_read_req_state_owned_to_noncoherent)      	 fprintf(f_st, "downup_read_req_state_owned_to_noncoherent = %lld\n"        , mod->downup_read_req_state_owned_to_noncoherent);
		 if(mod->downup_read_req_state_owned_to_modified)         	 fprintf(f_st, "downup_read_req_state_owned_to_modified = %lld\n"           , mod->downup_read_req_state_owned_to_modified);
		 if(mod->downup_read_req_state_owned_to_shared)           	 fprintf(f_st, "downup_read_req_state_owned_to_shared = %lld\n"             , mod->downup_read_req_state_owned_to_shared);
		 if(mod->downup_read_req_state_owned_to_owned)            	 fprintf(f_st, "downup_read_req_state_owned_to_owned = %lld\n"              , mod->downup_read_req_state_owned_to_owned);
		 if(mod->downup_read_req_state_owned_to_exclusive)        	 fprintf(f_st, "downup_read_req_state_owned_to_exclusive = %lld\n"          , mod->downup_read_req_state_owned_to_exclusive);
		 if(mod->downup_read_req_state_exclusive_to_invalid)      	 fprintf(f_st, "downup_read_req_state_exclusive_to_invalid = %lld\n"        , mod->downup_read_req_state_exclusive_to_invalid);
		 if(mod->downup_read_req_state_exclusive_to_noncoherent)  	 fprintf(f_st, "downup_read_req_state_exclusive_to_noncoherent = %lld\n"    , mod->downup_read_req_state_exclusive_to_noncoherent);
		 if(mod->downup_read_req_state_exclusive_to_modified)     	 fprintf(f_st, "downup_read_req_state_exclusive_to_modified = %lld\n"       , mod->downup_read_req_state_exclusive_to_modified);
		 if(mod->downup_read_req_state_exclusive_to_shared)       	 fprintf(f_st, "downup_read_req_state_exclusive_to_shared = %lld\n"         , mod->downup_read_req_state_exclusive_to_shared);
		 if(mod->downup_read_req_state_exclusive_to_owned)        	 fprintf(f_st, "downup_read_req_state_exclusive_to_owned = %lld\n"          , mod->downup_read_req_state_exclusive_to_owned);
		 if(mod->downup_read_req_state_exclusive_to_exclusive)    	 fprintf(f_st, "downup_read_req_state_exclusive_to_exclusive = %lld\n"      , mod->downup_read_req_state_exclusive_to_exclusive);
		 fprintf(f_st, "\n===================DOWNUP WRITEBACK REQUESTS===========================================\n");
		 if(mod->downup_wb_req_state_invalid_to_invalid)        	 fprintf(f_st, "downup_wb_req_state_invalid_to_invalid = %lld\n"          , mod->downup_wb_req_state_invalid_to_invalid);
		 if(mod->downup_wb_req_state_invalid_to_noncoherent)    	 fprintf(f_st, "downup_wb_req_state_invalid_to_noncoherent = %lld\n"      , mod->downup_wb_req_state_invalid_to_noncoherent);
		 if(mod->downup_wb_req_state_invalid_to_modified)       	 fprintf(f_st, "downup_wb_req_state_invalid_to_modified = %lld\n"         , mod->downup_wb_req_state_invalid_to_modified);
		 if(mod->downup_wb_req_state_invalid_to_shared)         	 fprintf(f_st, "downup_wb_req_state_invalid_to_shared = %lld\n"           , mod->downup_wb_req_state_invalid_to_shared);
		 if(mod->downup_wb_req_state_invalid_to_owned)          	 fprintf(f_st, "downup_wb_req_state_invalid_to_owned = %lld\n"            , mod->downup_wb_req_state_invalid_to_owned);
		 if(mod->downup_wb_req_state_invalid_to_exclusive)      	 fprintf(f_st, "downup_wb_req_state_invalid_to_exclusive = %lld\n"        , mod->downup_wb_req_state_invalid_to_exclusive);
		 if(mod->downup_wb_req_state_noncoherent_to_invalid)    	 fprintf(f_st, "downup_wb_req_state_noncoherent_to_invalid = %lld\n"      , mod->downup_wb_req_state_noncoherent_to_invalid);
		 if(mod->downup_wb_req_state_noncoherent_to_noncoherent)	 fprintf(f_st, "downup_wb_req_state_noncoherent_to_noncoherent = %lld\n"  , mod->downup_wb_req_state_noncoherent_to_noncoherent);
		 if(mod->downup_wb_req_state_noncoherent_to_modified)   	 fprintf(f_st, "downup_wb_req_state_noncoherent_to_modified = %lld\n"     , mod->downup_wb_req_state_noncoherent_to_modified);
		 if(mod->downup_wb_req_state_noncoherent_to_shared)     	 fprintf(f_st, "downup_wb_req_state_noncoherent_to_shared = %lld\n"       , mod->downup_wb_req_state_noncoherent_to_shared);
		 if(mod->downup_wb_req_state_noncoherent_to_owned)      	 fprintf(f_st, "downup_wb_req_state_noncoherent_to_owned = %lld\n"        , mod->downup_wb_req_state_noncoherent_to_owned);
		 if(mod->downup_wb_req_state_noncoherent_to_exclusive)  	 fprintf(f_st, "downup_wb_req_state_noncoherent_to_exclusive = %lld\n"    , mod->downup_wb_req_state_noncoherent_to_exclusive);
		 if(mod->downup_wb_req_state_modified_to_invalid)       	 fprintf(f_st, "downup_wb_req_state_modified_to_invalid = %lld\n"         , mod->downup_wb_req_state_modified_to_invalid);
		 if(mod->downup_wb_req_state_modified_to_noncoherent)   	 fprintf(f_st, "downup_wb_req_state_modified_to_noncoherent = %lld\n"     , mod->downup_wb_req_state_modified_to_noncoherent);
		 if(mod->downup_wb_req_state_modified_to_modified)      	 fprintf(f_st, "downup_wb_req_state_modified_to_modified = %lld\n"        , mod->downup_wb_req_state_modified_to_modified);
		 if(mod->downup_wb_req_state_modified_to_shared)        	 fprintf(f_st, "downup_wb_req_state_modified_to_shared = %lld\n"          , mod->downup_wb_req_state_modified_to_shared);
		 if(mod->downup_wb_req_state_modified_to_owned)         	 fprintf(f_st, "downup_wb_req_state_modified_to_owned = %lld\n"           , mod->downup_wb_req_state_modified_to_owned);
		 if(mod->downup_wb_req_state_modified_to_exclusive)     	 fprintf(f_st, "downup_wb_req_state_modified_to_exclusive = %lld\n"       , mod->downup_wb_req_state_modified_to_exclusive);
		 if(mod->downup_wb_req_state_shared_to_invalid)         	 fprintf(f_st, "downup_wb_req_state_shared_to_invalid = %lld\n"           , mod->downup_wb_req_state_shared_to_invalid);
		 if(mod->downup_wb_req_state_shared_to_noncoherent)     	 fprintf(f_st, "downup_wb_req_state_shared_to_noncoherent = %lld\n"       , mod->downup_wb_req_state_shared_to_noncoherent);
		 if(mod->downup_wb_req_state_shared_to_modified)        	 fprintf(f_st, "downup_wb_req_state_shared_to_modified = %lld\n"          , mod->downup_wb_req_state_shared_to_modified);
		 if(mod->downup_wb_req_state_shared_to_shared)          	 fprintf(f_st, "downup_wb_req_state_shared_to_shared = %lld\n"            , mod->downup_wb_req_state_shared_to_shared);
		 if(mod->downup_wb_req_state_shared_to_owned)           	 fprintf(f_st, "downup_wb_req_state_shared_to_owned = %lld\n"             , mod->downup_wb_req_state_shared_to_owned);
		 if(mod->downup_wb_req_state_shared_to_exclusive)       	 fprintf(f_st, "downup_wb_req_state_shared_to_exclusive = %lld\n"         , mod->downup_wb_req_state_shared_to_exclusive);
		 if(mod->downup_wb_req_state_owned_to_invalid)          	 fprintf(f_st, "downup_wb_req_state_owned_to_invalid = %lld\n"            , mod->downup_wb_req_state_owned_to_invalid);
		 if(mod->downup_wb_req_state_owned_to_noncoherent)      	 fprintf(f_st, "downup_wb_req_state_owned_to_noncoherent = %lld\n"        , mod->downup_wb_req_state_owned_to_noncoherent);
		 if(mod->downup_wb_req_state_owned_to_modified)         	 fprintf(f_st, "downup_wb_req_state_owned_to_modified = %lld\n"           , mod->downup_wb_req_state_owned_to_modified);
		 if(mod->downup_wb_req_state_owned_to_shared)           	 fprintf(f_st, "downup_wb_req_state_owned_to_shared = %lld\n"             , mod->downup_wb_req_state_owned_to_shared);
		 if(mod->downup_wb_req_state_owned_to_owned)            	 fprintf(f_st, "downup_wb_req_state_owned_to_owned = %lld\n"              , mod->downup_wb_req_state_owned_to_owned);
		 if(mod->downup_wb_req_state_owned_to_exclusive)        	 fprintf(f_st, "downup_wb_req_state_owned_to_exclusive = %lld\n"          , mod->downup_wb_req_state_owned_to_exclusive);
		 if(mod->downup_wb_req_state_exclusive_to_invalid)      	 fprintf(f_st, "downup_wb_req_state_exclusive_to_invalid = %lld\n"        , mod->downup_wb_req_state_exclusive_to_invalid);
		 if(mod->downup_wb_req_state_exclusive_to_noncoherent)  	 fprintf(f_st, "downup_wb_req_state_exclusive_to_noncoherent = %lld\n"    , mod->downup_wb_req_state_exclusive_to_noncoherent);
		 if(mod->downup_wb_req_state_exclusive_to_modified)     	 fprintf(f_st, "downup_wb_req_state_exclusive_to_modified = %lld\n"       , mod->downup_wb_req_state_exclusive_to_modified);
		 if(mod->downup_wb_req_state_exclusive_to_shared)       	 fprintf(f_st, "downup_wb_req_state_exclusive_to_shared = %lld\n"         , mod->downup_wb_req_state_exclusive_to_shared);
		 if(mod->downup_wb_req_state_exclusive_to_owned)        	 fprintf(f_st, "downup_wb_req_state_exclusive_to_owned = %lld\n"          , mod->downup_wb_req_state_exclusive_to_owned);
		 if(mod->downup_wb_req_state_exclusive_to_exclusive)    	 fprintf(f_st, "downup_wb_req_state_exclusive_to_exclusive = %lld\n"      , mod->downup_wb_req_state_exclusive_to_exclusive);

		 fprintf(f_st, "\n===============EVICTION REQUEST STATE SUMMARY==============================\n");
		 fprintf(f_st, "eviction_request_state_invalid = %lld\n"            , mod->eviction_request_state_invalid);
		 fprintf(f_st, "eviction_request_state_modified = %lld\n"           , mod->eviction_request_state_modified);
		 fprintf(f_st, "eviction_request_state_owned = %lld\n"              , mod->eviction_request_state_owned);
		 fprintf(f_st, "eviction_request_state_exclusive = %lld\n"          , mod->eviction_request_state_exclusive);
		 fprintf(f_st, "eviction_request_state_shared = %lld\n"             , mod->eviction_request_state_shared);
		 fprintf(f_st, "eviction_request_state_noncoherent = %lld\n"        , mod->eviction_request_state_noncoherent);
	 	 
		 fprintf(f_as, "\n===============REQUEST ACCESS DISTRIBUTION==============================\n");
		 for(int i=0; i<10; i++)
		 	if(mod->request_load[i]) fprintf(f_as, "request_load_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->request_load[i]);
		 for(int i=0; i<10; i++)
		 	if(mod->request_store[i]) fprintf(f_as, "request_store_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->request_store[i]);
		 for(int i=0; i<10; i++)
		 	if(mod->request_eviction[i]) fprintf(f_as, "request_eviction_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->request_eviction[i]);
		 for(int i=0; i<10; i++)
		 	if(mod->request_read[i]) fprintf(f_as, "request_read_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->request_read[i]);
		 for(int i=0; i<10; i++)
		 	if(mod->request_writeback[i]) fprintf(f_as, "request_writeback_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->request_writeback[i]);
		 for(int i=0; i<10; i++)
		 	if(mod->request_downup_read[i]) fprintf(f_as, "request_downup_read_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->request_downup_read[i]);
		 for(int i=0; i<10; i++)
		 	if(mod->request_downup_writeback[i]) fprintf(f_as, "request_downup_writeback_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1,mod->request_downup_writeback[i]);
		 for(int i=0; i<10; i++)
		 	if(mod->request_downup_eviction[i]) fprintf(f_as, "request_downup_eviction_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->request_downup_eviction[i]);
		 for(int i=0; i<11; i++)
		 	if(mod->request_processor[i]) fprintf(f_as, "request_processor_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->request_processor[i]);
		 for(int i=0; i<11; i++)
		 	if(mod->request_controller[i]) fprintf(f_as, "request_controller_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->request_controller[i]);
		 for(int i=0; i<11; i++)
		 	if(mod->request_updown[i]) fprintf(f_as, "request_updown_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->request_updown[i]);
		 for(int i=0; i<11; i++)
		 	if(mod->request_downup[i]) fprintf(f_as, "request_downup_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->request_downup[i]);
		 for(int i=0; i<12; i++)
		 	if(mod->request_total[i]) fprintf(f_as, "request_total_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->request_total[i]);
		
		 fprintf(f_lc, "\n===============LATENCY COUNTER DISTRIBUTION==============================\n");
			for(int i=0; i<10; i++)
				if(mod->load_latency[i]) fprintf(f_lc, "load_latency_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->load_latency[i]);
			for(int i=0; i<10; i++)
				if(mod->store_latency[i]) fprintf(f_lc, "store_latency_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->store_latency[i]);
			for(int i=0; i<10; i++)
				if(mod->eviction_latency[i])	fprintf(f_lc, "eviction_latency_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->eviction_latency[i]);
			for(int i=0; i<10; i++)
				if(mod->downup_read_request_latency[i])	fprintf(f_lc, "downup_read_request_latency_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->downup_read_request_latency[i]);
			for(int i=0; i<10; i++)
				if(mod->downup_writeback_request_latency[i]) fprintf(f_lc, "downup_writeback_request_latency_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1,mod->downup_writeback_request_latency[i]);
			for(int i=0; i<10; i++)
				if(mod->writeback_request_latency[i])	fprintf(f_lc, "writeback_request_latency_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->writeback_request_latency[i]);
			for(int i=0; i<10; i++)
				if(mod->read_request_latency[i]) fprintf(f_lc, "read_request_latency_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->read_request_latency[i]);
			for(int i=0; i<10; i++)
				if(mod->peer_latency[i]) fprintf(f_lc, "peer_latency_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->peer_latency[i]);
			for(int i=0; i<10; i++)
				if(mod->invalidate_latency[i]) fprintf(f_lc, "invalidate_latency_range_%d_to_%d = %lld\n", pow_2(i-1), pow_2(i) - 1, mod->invalidate_latency[i]);
		
		  fprintf(f_as, "\n===============DOWN-UP ACCESS SPECIAL STATISTICS==============================\n");
			fprintf(f_as, "load_during_load_to_same_addr = %lld\n",                       mod->load_during_load_to_same_addr);
			fprintf(f_as, "load_during_store_to_same_addr = %lld\n",                      mod->load_during_store_to_same_addr);
			fprintf(f_as, "load_during_eviction_to_same_addr = %lld\n",                   mod->load_during_eviction_to_same_addr);
			fprintf(f_as, "load_during_downup_read_req_to_same_addr = %lld\n",            mod->load_during_downup_read_req_to_same_addr);
			fprintf(f_as, "load_during_downup_wb_req_to_same_addr = %lld\n",              mod->load_during_downup_wb_req_to_same_addr);
			fprintf(f_as, "store_during_load_to_same_addr = %lld\n",                      mod->store_during_load_to_same_addr);
			fprintf(f_as, "store_during_store_to_same_addr = %lld\n",                     mod->store_during_store_to_same_addr);
			fprintf(f_as, "store_during_eviction_to_same_addr = %lld\n",                  mod->store_during_eviction_to_same_addr);
			fprintf(f_as, "store_during_downup_read_req_to_same_addr = %lld\n",           mod->store_during_downup_read_req_to_same_addr);
			fprintf(f_as, "store_during_downup_wb_req_to_same_addr = %lld\n",             mod->store_during_downup_wb_req_to_same_addr);
			fprintf(f_as, "downup_read_req_during_load_to_same_addr = %lld\n",            mod->downup_read_req_during_load_to_same_addr);
			fprintf(f_as, "downup_read_req_during_store_to_same_addr = %lld\n",           mod->downup_read_req_during_store_to_same_addr);
			fprintf(f_as, "downup_read_req_during_eviction_to_same_addr = %lld\n",        mod->downup_read_req_during_eviction_to_same_addr);
			fprintf(f_as, "downup_read_req_during_downup_read_req_to_same_addr = %lld\n", mod->downup_read_req_during_downup_read_req_to_same_addr);
			fprintf(f_as, "downup_read_req_during_downup_wb_req_to_same_addr = %lld\n",   mod->downup_read_req_during_downup_wb_req_to_same_addr);
			fprintf(f_as, "downup_wb_req_during_load_to_same_addr = %lld\n",              mod->downup_wb_req_during_load_to_same_addr);
			fprintf(f_as, "downup_wb_req_during_store_to_same_addr = %lld\n",             mod->downup_wb_req_during_store_to_same_addr);
			fprintf(f_as, "downup_wb_req_during_eviction_to_same_addr = %lld\n",          mod->downup_wb_req_during_eviction_to_same_addr);
			fprintf(f_as, "downup_wb_req_during_downup_read_req_to_same_addr = %lld\n",   mod->downup_wb_req_during_downup_read_req_to_same_addr);
			fprintf(f_as, "downup_wb_req_during_downup_wb_req_to_same_addr = %lld\n",     mod->downup_wb_req_during_downup_wb_req_to_same_addr);
			
		  fprintf(f_as, "\n===============DATA STATISTICS==============================\n");
		  fprintf(f_as, "data_transfer_downup_load_request = %lld\n"            , mod->data_transfer_downup_load_request);
		  fprintf(f_as, "data_transfer_downup_store_request = %lld\n"           , mod->data_transfer_downup_store_request);
		  fprintf(f_as, "data_transfer_downup_eviction_request = %lld\n"        , mod->data_transfer_downup_eviction_request);
		  fprintf(f_as, "peer_data_transfer_downup_load_request = %lld\n"       , mod->peer_data_transfer_downup_load_request);
		  fprintf(f_as, "peer_data_transfer_downup_store_request = %lld\n"      , mod->peer_data_transfer_downup_store_request);
		  fprintf(f_as, "data_transfer_updown_load_request = %lld\n"            , mod->data_transfer_updown_load_request);
		  fprintf(f_as, "data_transfer_updown_store_request = %lld\n"           , mod->data_transfer_updown_store_request);
		  fprintf(f_as, "data_transfer_eviction = %lld\n"                       , mod->data_transfer_eviction);
		
		//Network Debugging
			if(mod->high_net)
			{
				fprintf(f_nt, "High Level Network = %s\n", mod->high_net->name);
				fprintf(f_nt, "Node List Count = %d\n", list_count(mod->high_net->node_list));
				fprintf(f_nt, "Node Count = %d\n", mod->high_net->node_count);
				fprintf(f_nt, "End Node Count = %d\n", mod->high_net->end_node_count);

				for(int j=0; j<list_count(mod->high_net->node_list) ; j++)
				{
					struct net_node_t *node;
					node = list_get(mod->high_net->node_list, j);
					if(node)
					{
						char *node_type = (node->kind == net_node_end) ? "END NODE" : (node->kind == net_node_switch) ? "SWITCH": "BUS";
						fprintf(f_nt, "Node Index = %d\n", node->index);
						if(node->name)  fprintf(f_nt, "Node Name = %s\n", node->name);
						if(node_type) fprintf(f_nt, "Node Type = %s\n", node_type);
					}
				}
			}
			else
			{
				fprintf(f_nt, "No High Level Network\n");
			}

			if(mod->low_net)
			{
				fprintf(f_nt, "Low Level Network = %s\n", mod->low_net->name);
				fprintf(f_nt, "Node List Count = %d\n", list_count(mod->low_net->node_list));
				fprintf(f_nt, "Node Count = %d\n", mod->low_net->node_count);
				fprintf(f_nt, "End Node Count = %d\n", mod->low_net->end_node_count);
				for(int j=0; j<list_count(mod->low_net->node_list); j++)
				{
					struct net_node_t *node;
					node = list_get(mod->low_net->node_list, j);
					if(node)
					{
						char *node_type = (node->kind == net_node_end) ? "END NODE" : (node->kind == net_node_switch) ? "SWITCH": "BUS";
						fprintf(f_nt, "Node Index = %d\n", node->index);
						if(node->name)  fprintf(f_nt, "Node Name = %s\n", node->name);
						// fprintf(f_nt, "Node Type = %s\n", (node->kind == net_node_end) ? "END NODE" : (node->kind == net_node_switch) ? "SWITCH": "BUS");
						if(node_type) fprintf(f_nt, "Node Type = %s\n", node_type);
					}
				}
			}
			else
			{
				fprintf(f_nt, "No Low Level Network\n");
			}

		fprintf(f, "\n\n");
		fprintf(f_st, "\n\n");
		fprintf(f_as, "\n\n");
		fprintf(f_lc, "\n\n");
		fprintf(f_nt, "\n\n");
	}

	/* Dump report for networks */
	for (i = 0; i < list_count(mem_system->net_list); i++)
	{
		net = list_get(mem_system->net_list, i);
		net_dump_report(net, f);
	}
	
	/* Done */
	fclose(f);
	fclose(f_as);
	fclose(f_st);
	fclose(f_lc);
	fclose(f_nt);
}


struct mod_t *mem_system_get_mod(char *mod_name)
{
	struct mod_t *mod;

	int mod_id;

	/* Look for module */
	LIST_FOR_EACH(mem_system->mod_list, mod_id)
	{
		mod = list_get(mem_system->mod_list, mod_id);
		if (!strcasecmp(mod->name, mod_name))
			return mod;
	}

	/* Not found */
	return NULL;
}


struct net_t *mem_system_get_net(char *net_name)
{
	struct net_t *net;

	int net_id;

	/* Look for network */
	LIST_FOR_EACH(mem_system->net_list, net_id)
	{
		net = list_get(mem_system->net_list, net_id);
		if (!strcasecmp(net->name, net_name))
			return net;
	}

	/* Not found */
	return NULL;
}

