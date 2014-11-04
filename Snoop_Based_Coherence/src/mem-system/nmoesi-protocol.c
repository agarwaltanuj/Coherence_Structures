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
 *  You should have received stack copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <assert.h>

#include <lib/esim/esim.h>
#include <lib/esim/trace.h>
#include <lib/util/debug.h>
#include <lib/util/linked-list.h>
#include <lib/util/list.h>
#include <lib/util/string.h>
#include <network/network.h>
#include <network/node.h>

#include "cache.h"
#include "mem-system.h"
#include "mod-stack.h"
#include "prefetcher.h"


/* Events */

int EV_MOD_NMOESI_LOAD;
int EV_MOD_NMOESI_LOAD_LOCK;
int EV_MOD_NMOESI_LOAD_ACTION;
int EV_MOD_NMOESI_LOAD_MISS;
int EV_MOD_NMOESI_LOAD_UNLOCK;
int EV_MOD_NMOESI_LOAD_FINISH;

int EV_MOD_NMOESI_STORE;
int EV_MOD_NMOESI_STORE_LOCK;
int EV_MOD_NMOESI_STORE_ACTION;
int EV_MOD_NMOESI_STORE_UNLOCK;
int EV_MOD_NMOESI_STORE_FINISH;

int EV_MOD_NMOESI_PREFETCH;
int EV_MOD_NMOESI_PREFETCH_LOCK;
int EV_MOD_NMOESI_PREFETCH_ACTION;
int EV_MOD_NMOESI_PREFETCH_MISS;
int EV_MOD_NMOESI_PREFETCH_UNLOCK;
int EV_MOD_NMOESI_PREFETCH_FINISH;

int EV_MOD_NMOESI_NC_STORE;
int EV_MOD_NMOESI_NC_STORE_LOCK;
int EV_MOD_NMOESI_NC_STORE_WRITEBACK;
int EV_MOD_NMOESI_NC_STORE_ACTION;
int EV_MOD_NMOESI_NC_STORE_MISS;
int EV_MOD_NMOESI_NC_STORE_UNLOCK;
int EV_MOD_NMOESI_NC_STORE_FINISH;

int EV_MOD_NMOESI_FIND_AND_LOCK;
int EV_MOD_NMOESI_FIND_AND_LOCK_PORT;
int EV_MOD_NMOESI_FIND_AND_LOCK_ACTION;
int EV_MOD_NMOESI_FIND_AND_LOCK_FINISH;

int EV_MOD_NMOESI_EVICT;
int EV_MOD_NMOESI_EVICT_INVALID;
int EV_MOD_NMOESI_EVICT_ACTION;
int EV_MOD_NMOESI_EVICT_RECEIVE;
int EV_MOD_NMOESI_EVICT_PROCESS;
int EV_MOD_NMOESI_EVICT_PROCESS_NONCOHERENT;
int EV_MOD_NMOESI_EVICT_REPLY;
int EV_MOD_NMOESI_EVICT_REPLY_RECEIVE;
int EV_MOD_NMOESI_EVICT_FINISH;

int EV_MOD_NMOESI_WRITE_REQUEST;
int EV_MOD_NMOESI_WRITE_REQUEST_RECEIVE;
int EV_MOD_NMOESI_WRITE_REQUEST_ACTION;
int EV_MOD_NMOESI_WRITE_REQUEST_EXCLUSIVE;
int EV_MOD_NMOESI_WRITE_REQUEST_UPDOWN;
int EV_MOD_NMOESI_WRITE_REQUEST_UPDOWN_FINISH;
int EV_MOD_NMOESI_WRITE_REQUEST_DOWNUP;
int EV_MOD_NMOESI_WRITE_REQUEST_DOWNUP_FINISH;
int EV_MOD_NMOESI_WRITE_REQUEST_REPLY;
int EV_MOD_NMOESI_WRITE_REQUEST_FINISH;

int EV_MOD_NMOESI_READ_REQUEST;
int EV_MOD_NMOESI_READ_REQUEST_RECEIVE;
int EV_MOD_NMOESI_READ_REQUEST_ACTION;
int EV_MOD_NMOESI_READ_REQUEST_UPDOWN;
int EV_MOD_NMOESI_READ_REQUEST_UPDOWN_MISS;
int EV_MOD_NMOESI_READ_REQUEST_UPDOWN_FINISH;
int EV_MOD_NMOESI_READ_REQUEST_DOWNUP;
int EV_MOD_NMOESI_READ_REQUEST_DOWNUP_WAIT_FOR_REQS;
int EV_MOD_NMOESI_READ_REQUEST_DOWNUP_FINISH;
int EV_MOD_NMOESI_READ_REQUEST_REPLY;
int EV_MOD_NMOESI_READ_REQUEST_FINISH;

int EV_MOD_NMOESI_INVALIDATE;
int EV_MOD_NMOESI_INVALIDATE_FINISH;

int EV_MOD_NMOESI_PEER_SEND;
int EV_MOD_NMOESI_PEER_RECEIVE;
int EV_MOD_NMOESI_PEER_REPLY;
int EV_MOD_NMOESI_PEER_FINISH;

int EV_MOD_NMOESI_MESSAGE;
int EV_MOD_NMOESI_MESSAGE_RECEIVE;
int EV_MOD_NMOESI_MESSAGE_ACTION;
int EV_MOD_NMOESI_MESSAGE_REPLY;
int EV_MOD_NMOESI_MESSAGE_FINISH;

// This finds the next block state based on shared and dirty flags. Used for up-down or load store requests only.
static int cache_block_next_state(int flag_shared, int flag_dirty)
{
	if(flag_shared)
	{
		if(flag_dirty)
			return cache_block_owned;
		else
			return cache_block_shared;
	}
	else
	{
		if(flag_dirty)
			return cache_block_modified;
		else
			return cache_block_exclusive;
	}
}

/* NMOESI Protocol */

void mod_handler_nmoesi_load(int event, void *data)
{
	struct mod_stack_t *stack = data;
	struct mod_stack_t *new_stack;

	struct mod_t *mod = stack->mod;


	if (event == EV_MOD_NMOESI_LOAD)
	{
		struct mod_stack_t *master_stack;

		mem_debug("%lld %lld 0x%x %s load\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.new_access name=\"A-%lld\" type=\"load\" "
			"state=\"%s:load\" addr=0x%x\n",
			stack->id, mod->name, stack->addr);

		/* Record access */
		mod_access_start(mod, stack, mod_access_load);
		
		// Set the read flag of the access to indicate a Load access
		stack->read = 1;

		// Record the start time of the Load access
		stack->access_start_cycle = esim_cycle();

		// Update the load access record at this time and update the counters.
		mod->num_load_requests++;
		mod_update_request_counters(mod, mod_trans_load);

		/* Coalesce access */
		master_stack = mod_can_coalesce(mod, mod_access_load, stack->addr, stack);
		if (master_stack)
		{
			mod->reads++;
			mod->coalesced_loads++;
			mod_coalesce(mod, master_stack, stack);
			mod_stack_wait_in_stack(stack, master_stack, EV_MOD_NMOESI_LOAD_FINISH);
			return;
		}

		// Update the simultaneous access request counters. Done after coalesced so that they aren't counted again.
		mod_update_simultaneous_flight_access_counters(mod, stack->addr, stack, mod_trans_load);

		/* Next event */
		esim_schedule_event(EV_MOD_NMOESI_LOAD_LOCK, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_LOAD_LOCK)
	{
		struct mod_stack_t *older_stack;

		mem_debug("  %lld %lld 0x%x %s load lock\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:load_lock\"\n",
			stack->id, mod->name);

		/* If there is any older write, wait for it */
		older_stack = mod_in_flight_write(mod, stack);
		if (older_stack)
		{
			mem_debug("    %lld wait for write %lld\n",
				stack->id, older_stack->id);
			mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_LOAD_LOCK);

			if(stack->load_access_waiting_for_store_start_cycle == 0)
			{
				stack->load_access_waiting_for_store_start_cycle = esim_cycle();
				mod->read_waiting_for_other_accesses++;
				mod->loads_waiting_for_stores++;
			}

			return;
		}
		
		if(stack->load_access_waiting_for_store_start_cycle)
		{
			stack->load_access_waiting_for_store_end_cycle = esim_cycle();
			stack->load_access_waiting_for_store_cycle     = stack->load_access_waiting_for_store_end_cycle - stack->load_access_waiting_for_store_start_cycle;
		}

		/* If there is any older access to the same address that this access could not
		 * be coalesced with, wait for it. */
		older_stack = mod_in_flight_address(mod, stack->addr, stack);
		if (older_stack)
		{
			mem_debug("    %lld wait for access %lld\n",
				stack->id, older_stack->id);
			mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_LOAD_LOCK);

			if(stack->load_access_waiting_start_cycle == 0)
			{
				stack->load_access_waiting_start_cycle = esim_cycle();
				mod->read_waiting_for_other_accesses++;
				mod->loads_waiting_for_non_coalesced_accesses++;
			}

			return;
		}
		
		if(stack->load_access_waiting_start_cycle)
		{
			stack->load_access_waiting_end_cycle = esim_cycle();
			stack->load_access_waiting_cycle     = stack->load_access_waiting_end_cycle - stack->load_access_waiting_start_cycle;
		}

		// Update the waiting counters
		if(stack->load_access_waiting_cycle || stack->load_access_waiting_for_store_cycle) 
			mod_update_waiting_counters(mod, stack, mod_trans_load);

		/* Call find and lock */
		new_stack = mod_stack_create(stack->id, mod, stack->addr,
			EV_MOD_NMOESI_LOAD_ACTION, stack);
		new_stack->orig_mod_id = mod->mod_id;
		new_stack->issue_mod_id = stack->issue_mod_id;
		new_stack->blocking = 1;
		new_stack->read = 1;
		new_stack->retry = stack->retry;
		esim_schedule_event(EV_MOD_NMOESI_FIND_AND_LOCK, new_stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_LOAD_ACTION)
	{
		int retry_lat;

		mem_debug("  %lld %lld 0x%x %s load action\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:load_action\"\n",
			stack->id, mod->name);

		/* Error locking */
		if (stack->err)
		{
			mod->read_retries++;
			retry_lat = mod_get_retry_latency(mod);
			mem_debug("    lock error, retrying in %d cycles\n", retry_lat);
			stack->retry = 1;
			esim_schedule_event(EV_MOD_NMOESI_LOAD_LOCK, stack, retry_lat);
			return;
		}

		/* Hit */
		if (stack->state)
		{
			esim_schedule_event(EV_MOD_NMOESI_LOAD_UNLOCK, stack, 0);

			/* The prefetcher may have prefetched this earlier and hence
			 * this is a hit now. Let the prefetcher know of this hit
			 * since without the prefetcher, this may have been a miss. */
			prefetcher_access_hit(stack, mod);

			return;
		}

		// Update counter for generation of a Up-down read request
		mod->updown_read_requests_generated++;

		/* Miss */
		new_stack = mod_stack_create(stack->id, mod, stack->tag,
			EV_MOD_NMOESI_LOAD_MISS, stack);
		new_stack->orig_mod_id = mod->mod_id;
		new_stack->issue_mod_id = stack->issue_mod_id;
		// new_stack->peer = mod_stack_set_peer(mod, stack->state);
		new_stack->target_mod = mod_get_low_mod(mod, stack->tag);
		new_stack->request_dir = mod_request_up_down;
		new_stack->read = 1;
		esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST, new_stack, 0);

		/* The prefetcher may be interested in this miss */
		prefetcher_access_miss(stack, mod);

		return;
	}

	if (event == EV_MOD_NMOESI_LOAD_MISS)
	{
		int retry_lat;
		int next_state;
		struct mod_t *check_mod;

		mem_debug("  %lld %lld 0x%x %s load miss\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:load_miss\"\n",
			stack->id, mod->name);

		/* Error on read request. Unlock block and retry load. */
		if (stack->err)
		{
			mod->read_retries++;
			retry_lat = mod_get_retry_latency(mod);
			cache_entry_unlock(mod->cache, stack->set, stack->way);
			mem_debug("    lock error, retrying in %d cycles\n", retry_lat);
			stack->retry = 1;
			esim_schedule_event(EV_MOD_NMOESI_LOAD_LOCK, stack, retry_lat);
			return;
		}

		/* Set block state to excl/shared depending on return var 'shared'.
		 * Also set the tag of the block. */
		
		// For MOESI protocol, in case the state is shared or exclusive, depending on response.
		next_state = cache_block_next_state(stack->shared, stack->dirty);

		cache_set_block(mod->cache, stack->set, stack->way, stack->tag, next_state);
		
		check_mod = mod_get_low_mod(mod, stack->tag);	
		mod_check_coherency_status(check_mod, mod, mod, stack->tag, next_state, 0, stack);

		mod_update_state_modification_counters(mod, stack->prev_state, next_state, mod_trans_load);

		/* Continue */
		esim_schedule_event(EV_MOD_NMOESI_LOAD_UNLOCK, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_LOAD_UNLOCK)
	{
		mem_debug("  %lld %lld 0x%x %s load unlock\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:load_unlock\"\n",
			stack->id, mod->name);

		/* Unlock cache entry */
		cache_entry_unlock(mod->cache, stack->set, stack->way);
		
		/* Impose the access latency before continuing */
		esim_schedule_event(EV_MOD_NMOESI_LOAD_FINISH, stack, 
			mod->latency);
		return;
	}

	if (event == EV_MOD_NMOESI_LOAD_FINISH)
	{
		mem_debug("%lld %lld 0x%x %s load finish\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:load_finish\"\n",
			stack->id, mod->name);
		mem_trace("mem.end_access name=\"A-%lld\"\n",
			stack->id);

		/* Increment witness variable */
		if (stack->witness_ptr)
			(*stack->witness_ptr)++;

		/* Return event queue element into event queue */
		if (stack->event_queue && stack->event_queue_item)
			linked_list_add(stack->event_queue, stack->event_queue_item);

		/* Free the mod_client_info object, if any */
		if (stack->client_info)
			mod_client_info_free(mod, stack->client_info);

		/* Finish access */
		mod_access_finish(mod, stack);

		// Record the finish/end time of the access
		stack->access_end_cycle = esim_cycle();
		// Record the latency of this axxess
		stack->access_latency = stack->access_end_cycle - stack->access_start_cycle;

		mod_update_latency_counters(stack->mod, stack->access_latency, mod_trans_load);

		// Update counters for controller occupancy statistics
		mod->num_load_requests--;
		mod_update_request_counters(mod, mod_trans_load);
		
		/* Return */
		mod_stack_return(stack);
		return;
	}

	abort();
}


void mod_handler_nmoesi_store(int event, void *data)
{
	struct mod_stack_t *stack = data;
	struct mod_stack_t *new_stack;

	struct mod_t *mod = stack->mod;


	if (event == EV_MOD_NMOESI_STORE)
	{
		struct mod_stack_t *master_stack;

		mem_debug("%lld %lld 0x%x %s store\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.new_access name=\"A-%lld\" type=\"store\" "
			"state=\"%s:store\" addr=0x%x\n",
			stack->id, mod->name, stack->addr);

		/* Record access */
		mod_access_start(mod, stack, mod_access_store);

		// Set the write flag of the access to indicate a Store access
		stack->write = 1;

		stack->access_start_cycle = esim_cycle();

		mod->num_store_requests++;
		mod_update_request_counters(mod, mod_trans_store);

		/* Coalesce access */
		master_stack = mod_can_coalesce(mod, mod_access_store, stack->addr, stack);
		if (master_stack)
		{
			mod->writes++;
			mod->coalesced_stores++;
			mod_coalesce(mod, master_stack, stack);
			mod_stack_wait_in_stack(stack, master_stack, EV_MOD_NMOESI_STORE_FINISH);

			/* Increment witness variable */
			if (stack->witness_ptr)
				(*stack->witness_ptr)++;

			return;
		}

		// Update the simultaneous access request counters. Done after coalescing has been done, so that coalesced aren't counted again.
		mod_update_simultaneous_flight_access_counters(mod, stack->addr, stack, mod_trans_store);

		/* Continue */
		esim_schedule_event(EV_MOD_NMOESI_STORE_LOCK, stack, 0);
		return;
	}


	if (event == EV_MOD_NMOESI_STORE_LOCK)
	{
		struct mod_stack_t *older_stack;

		mem_debug("  %lld %lld 0x%x %s store lock\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:store_lock\"\n",
			stack->id, mod->name);


		/* If there is any older access, wait for it */
		older_stack = stack->access_list_prev;
		if (older_stack)
		{
			mem_debug("    %lld wait for access %lld\n",
				stack->id, older_stack->id);
			mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_STORE_LOCK);

			if(stack->store_access_waiting_start_cycle == 0)
			{
				stack->store_access_waiting_start_cycle = esim_cycle();
		  	mod->write_waiting_for_other_accesses++;
			}
			return;
		}

		stack->store_access_waiting_end_cycle = esim_cycle();
		stack->store_access_waiting_cycle     = stack->store_access_waiting_end_cycle - stack->store_access_waiting_start_cycle;
		
		// Update the waiting counters
		if(stack->store_access_waiting_cycle) mod_update_waiting_counters(mod, stack, mod_trans_store);

		/* Call find and lock */
		new_stack = mod_stack_create(stack->id, mod, stack->addr,
			EV_MOD_NMOESI_STORE_ACTION, stack);
		new_stack->orig_mod_id = mod->mod_id;
		new_stack->issue_mod_id = stack->issue_mod_id;
		new_stack->blocking = 1;
		new_stack->write = 1;
		new_stack->retry = stack->retry;
		new_stack->witness_ptr = stack->witness_ptr;
		esim_schedule_event(EV_MOD_NMOESI_FIND_AND_LOCK, new_stack, 0);

		/* Set witness variable to NULL so that retries from the same
		 * stack do not increment it multiple times */
		stack->witness_ptr = NULL;

		return;
	}

	if (event == EV_MOD_NMOESI_STORE_ACTION)
	{
		int retry_lat;

		mem_debug("  %lld %lld 0x%x %s store action\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:store_action\"\n",
			stack->id, mod->name);

		/* Error locking */
		if (stack->err)
		{
			mod->write_retries++;
			retry_lat = mod_get_retry_latency(mod);
			mem_debug("    lock error, retrying in %d cycles\n", retry_lat);
			stack->retry = 1;
			esim_schedule_event(EV_MOD_NMOESI_STORE_LOCK, stack, retry_lat);
			return;
		}

		/* Hit - state=M/E */

		if (stack->state == cache_block_modified ||
			stack->state == cache_block_exclusive)
		{
			esim_schedule_event(EV_MOD_NMOESI_STORE_UNLOCK, stack, 0);

			/* The prefetcher may have prefetched this earlier and hence
			 * this is a hit now. Let the prefetcher know of this hit
			 * since without the prefetcher, this may have been a miss. */
			prefetcher_access_hit(stack, mod);

			return;
		}

		// Update counter for generation of a Up-down WB request
		mod->updown_writeback_requests_generated++;
		/* Miss - state=O/S/I/N */
		new_stack = mod_stack_create(stack->id, mod, stack->tag,
			EV_MOD_NMOESI_STORE_UNLOCK, stack);
		new_stack->orig_mod_id = mod->mod_id;
		new_stack->issue_mod_id = stack->issue_mod_id;
		// new_stack->peer = mod_stack_set_peer(mod, stack->state);
		new_stack->target_mod = mod_get_low_mod(mod, stack->tag);
		new_stack->request_dir = mod_request_up_down;
		new_stack->write = 1;
		esim_schedule_event(EV_MOD_NMOESI_WRITE_REQUEST, new_stack, 0);

		/* The prefetcher may be interested in this miss */
		prefetcher_access_miss(stack, mod);

		return;
	}

	if (event == EV_MOD_NMOESI_STORE_UNLOCK)
	{
		int retry_lat;
		int next_state;
		struct mod_t *check_mod;

		mem_debug("  %lld %lld 0x%x %s store unlock\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:store_unlock\"\n",
			stack->id, mod->name);

		/* Error in write request, unlock block and retry store. */
		if (stack->err)
		{
			mod->write_retries++;
			retry_lat = mod_get_retry_latency(mod);
			cache_entry_unlock(mod->cache, stack->set, stack->way);
			mem_debug("    lock error, retrying in %d cycles\n", retry_lat);
			stack->retry = 1;
			esim_schedule_event(EV_MOD_NMOESI_STORE_LOCK, stack, retry_lat);
			return;
		}

		/* Update tag/state and unlock */
		cache_set_block(mod->cache, stack->set, stack->way,
			stack->tag, cache_block_modified);
		next_state = cache_block_modified;
		
		check_mod = mod_get_low_mod(mod, stack->tag);	
		mod_check_coherency_status(check_mod, mod, mod, stack->tag, next_state, 0, stack);

		cache_entry_unlock(mod->cache, stack->set, stack->way);

		mod_update_state_modification_counters(mod, stack->prev_state, next_state, mod_trans_store);

		/* Impose the access latency before continuing */
		esim_schedule_event(EV_MOD_NMOESI_STORE_FINISH, stack, 
			mod->latency);
		return;
	}

	if (event == EV_MOD_NMOESI_STORE_FINISH)
	{
		mem_debug("%lld %lld 0x%x %s store finish\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:store_finish\"\n",
			stack->id, mod->name);
		mem_trace("mem.end_access name=\"A-%lld\"\n",
			stack->id);

		/* Return event queue element into event queue */
		if (stack->event_queue && stack->event_queue_item)
			linked_list_add(stack->event_queue, stack->event_queue_item);

		/* Free the mod_client_info object, if any */
		if (stack->client_info)
			mod_client_info_free(mod, stack->client_info);

		/* Finish access */
		mod_access_finish(mod, stack);

		stack->access_end_cycle = esim_cycle();
		stack->access_latency   = stack->access_end_cycle - stack->access_start_cycle;

		mod_update_latency_counters(mod, stack->access_latency, mod_trans_store);

		// Update counters for controller occupancy statistics
		mod->num_store_requests--;
		mod_update_request_counters(mod, mod_trans_store);
		
		/* Return */
		mod_stack_return(stack);
		return;
	}

	abort();
}

void mod_handler_nmoesi_nc_store(int event, void *data)
{
	struct mod_stack_t *stack = data;
	struct mod_stack_t *new_stack;

	struct mod_t *mod = stack->mod;


	if (event == EV_MOD_NMOESI_NC_STORE)
	{
		struct mod_stack_t *master_stack;

		mem_debug("%lld %lld 0x%x %s nc store\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.new_access name=\"A-%lld\" type=\"nc_store\" "
			"state=\"%s:nc store\" addr=0x%x\n", stack->id, mod->name, stack->addr);

		/* Record access */
		mod_access_start(mod, stack, mod_access_nc_store);

		/* Coalesce access */
		master_stack = mod_can_coalesce(mod, mod_access_nc_store, stack->addr, stack);
		if (master_stack)
		{
			mod->nc_writes++;
			mod_coalesce(mod, master_stack, stack);
			mod_stack_wait_in_stack(stack, master_stack, EV_MOD_NMOESI_NC_STORE_FINISH);
			return;
		}

		/* Next event */
		esim_schedule_event(EV_MOD_NMOESI_NC_STORE_LOCK, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_NC_STORE_LOCK)
	{
		struct mod_stack_t *older_stack;

		mem_debug("  %lld %lld 0x%x %s nc store lock\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:nc_store_lock\"\n",
			stack->id, mod->name);

		/* If there is any older write, wait for it */
		older_stack = mod_in_flight_write(mod, stack);
		if (older_stack)
		{
			mem_debug("    %lld wait for write %lld\n", stack->id, older_stack->id);
			mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_NC_STORE_LOCK);
			return;
		}

		/* If there is any older access to the same address that this access could not
		 * be coalesced with, wait for it. */
		older_stack = mod_in_flight_address(mod, stack->addr, stack);
		if (older_stack)
		{
			mem_debug("    %lld wait for write %lld\n", stack->id, older_stack->id);
			mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_NC_STORE_LOCK);
			return;
		}

		/* Call find and lock */
		new_stack = mod_stack_create(stack->id, mod, stack->addr,
			EV_MOD_NMOESI_NC_STORE_WRITEBACK, stack);
		new_stack->orig_mod_id = mod->mod_id;
		new_stack->issue_mod_id = stack->issue_mod_id;
		new_stack->blocking = 1;
		new_stack->nc_write = 1;
		new_stack->retry = stack->retry;
		esim_schedule_event(EV_MOD_NMOESI_FIND_AND_LOCK, new_stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_NC_STORE_WRITEBACK)
	{
		int retry_lat;

		mem_debug("  %lld %lld 0x%x %s nc store writeback\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:nc_store_writeback\"\n",
			stack->id, mod->name);

		/* Error locking */
		if (stack->err)
		{
			mod->nc_write_retries++;
			retry_lat = mod_get_retry_latency(mod);
			mem_debug("    lock error, retrying in %d cycles\n", retry_lat);
			stack->retry = 1;
			esim_schedule_event(EV_MOD_NMOESI_NC_STORE_LOCK, stack, retry_lat);
			return;
		}

		/* If the block has modified data, evict it so that the lower-level cache will
		 * have the latest copy */
		if (stack->state == cache_block_modified || stack->state == cache_block_owned)
		{
			stack->eviction = 1;
			new_stack = mod_stack_create(stack->id, mod, 0,
				EV_MOD_NMOESI_NC_STORE_ACTION, stack);
			new_stack->orig_mod_id = mod->mod_id;
			new_stack->issue_mod_id = stack->issue_mod_id;
			new_stack->set = stack->set;
			new_stack->way = stack->way;
			new_stack->evict_trans = 1;
			esim_schedule_event(EV_MOD_NMOESI_EVICT, new_stack, 0);
			return;
		}

		esim_schedule_event(EV_MOD_NMOESI_NC_STORE_ACTION, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_NC_STORE_ACTION)
	{
		int retry_lat;

		mem_debug("  %lld %lld 0x%x %s nc store action\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:nc_store_action\"\n",
			stack->id, mod->name);

		/* Error locking */
		if (stack->err)
		{
			mod->nc_write_retries++;
			retry_lat = mod_get_retry_latency(mod);
			mem_debug("    lock error, retrying in %d cycles\n", retry_lat);
			stack->retry = 1;
			esim_schedule_event(EV_MOD_NMOESI_NC_STORE_LOCK, stack, retry_lat);
			return;
		}

		/* Main memory modules are a special case */
		if (mod->kind == mod_kind_main_memory)
		{
			/* For non-coherent stores, finding an E or M for the state of
			 * a cache block in the directory still requires a message to 
			 * the lower-level module so it can update its owner field.
			 * These messages should not be sent if the module is a main
			 * memory module. */
			esim_schedule_event(EV_MOD_NMOESI_NC_STORE_UNLOCK, stack, 0);
			return;
		}

		/* N/S are hit */
		if (stack->state == cache_block_shared || stack->state == cache_block_noncoherent)
		{
			esim_schedule_event(EV_MOD_NMOESI_NC_STORE_UNLOCK, stack, 0);
		}
		/* E state must tell the lower-level module to remove this module as an owner */
		else if (stack->state == cache_block_exclusive)
		{
			new_stack = mod_stack_create(stack->id, mod, stack->tag,
				EV_MOD_NMOESI_NC_STORE_MISS, stack);
			new_stack->orig_mod_id = mod->mod_id;
			new_stack->issue_mod_id = stack->issue_mod_id;
			new_stack->message = message_clear_owner;
			new_stack->target_mod = mod_get_low_mod(mod, stack->tag);
			esim_schedule_event(EV_MOD_NMOESI_MESSAGE, new_stack, 0);
		}
		/* Modified and Owned states need to call read request because we've already
		 * evicted the block so that the lower-level cache will have the latest value
		 * before it becomes non-coherent */
		else
		{
			new_stack = mod_stack_create(stack->id, mod, stack->tag,
				EV_MOD_NMOESI_NC_STORE_MISS, stack);
			new_stack->orig_mod_id = mod->mod_id;
			new_stack->issue_mod_id = stack->issue_mod_id;
			// new_stack->peer = mod_stack_set_peer(mod, stack->state);
			new_stack->nc_write = 1;
			new_stack->target_mod = mod_get_low_mod(mod, stack->tag);
			new_stack->request_dir = mod_request_up_down;
			new_stack->read = 1;
			esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST, new_stack, 0);
		}

		return;
	}

	if (event == EV_MOD_NMOESI_NC_STORE_MISS)
	{
		int retry_lat;

		mem_debug("  %lld %lld 0x%x %s nc store miss\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:nc_store_miss\"\n",
			stack->id, mod->name);

		/* Error on read request. Unlock block and retry nc store. */
		if (stack->err)
		{
			mod->nc_write_retries++;
			retry_lat = mod_get_retry_latency(mod);
			cache_entry_unlock(mod->cache, stack->set, stack->way);
			mem_debug("    lock error, retrying in %d cycles\n", retry_lat);
			stack->retry = 1;
			esim_schedule_event(EV_MOD_NMOESI_NC_STORE_LOCK, stack, retry_lat);
			return;
		}

		/* Continue */
		esim_schedule_event(EV_MOD_NMOESI_NC_STORE_UNLOCK, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_NC_STORE_UNLOCK)
	{
		mem_debug("  %lld %lld 0x%x %s nc store unlock\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:nc_store_unlock\"\n",
			stack->id, mod->name);

		/* Set block state to excl/shared depending on return var 'shared'.
		 * Also set the tag of the block. */
		cache_set_block(mod->cache, stack->set, stack->way, stack->tag,
			cache_block_noncoherent);

		/* Unlock directory entry */
		cache_entry_unlock(mod->cache, stack->set, stack->way);

		/* Impose the access latency before continuing */
		esim_schedule_event(EV_MOD_NMOESI_NC_STORE_FINISH, stack, 
			mod->latency);
		return;
	}

	if (event == EV_MOD_NMOESI_NC_STORE_FINISH)
	{
		mem_debug("%lld %lld 0x%x %s nc store finish\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:nc_store_finish\"\n",
			stack->id, mod->name);
		mem_trace("mem.end_access name=\"A-%lld\"\n",
			stack->id);

		/* Increment witness variable */
		if (stack->witness_ptr)
			(*stack->witness_ptr)++;

		/* Return event queue element into event queue */
		if (stack->event_queue && stack->event_queue_item)
			linked_list_add(stack->event_queue, stack->event_queue_item);

		/* Free the mod_client_info object, if any */
		if (stack->client_info)
			mod_client_info_free(mod, stack->client_info);

		/* Finish access */
		mod_access_finish(mod, stack);

		/* Return */
		mod_stack_return(stack);
		return;
	}

	abort();
}

void mod_handler_nmoesi_prefetch(int event, void *data)
{
	struct mod_stack_t *stack = data;
	struct mod_stack_t *new_stack;

	struct mod_t *mod = stack->mod;


	if (event == EV_MOD_NMOESI_PREFETCH)
	{
		struct mod_stack_t *master_stack;

		mem_debug("%lld %lld 0x%x %s prefetch\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.new_access name=\"A-%lld\" type=\"store\" "
			"state=\"%s:prefetch\" addr=0x%x\n",
			stack->id, mod->name, stack->addr);

		/* Record access */
		mod_access_start(mod, stack, mod_access_prefetch);

		/* Coalesce access */
		master_stack = mod_can_coalesce(mod, mod_access_prefetch, stack->addr, stack);
		if (master_stack)
		{
			/* Doesn't make sense to prefetch as the block is already being fetched */
			mem_debug("  %lld %lld 0x%x %s useless prefetch - already being fetched\n",
				  esim_time, stack->id, stack->addr, mod->name);

			mod->useless_prefetches++;
			esim_schedule_event(EV_MOD_NMOESI_PREFETCH_FINISH, stack, 0);

			/* Increment witness variable */
			if (stack->witness_ptr)
				(*stack->witness_ptr)++;

			return;
		}

		/* Continue */
		esim_schedule_event(EV_MOD_NMOESI_PREFETCH_LOCK, stack, 0);
		return;
	}


	if (event == EV_MOD_NMOESI_PREFETCH_LOCK)
	{
		struct mod_stack_t *older_stack;

		mem_debug("  %lld %lld 0x%x %s prefetch lock\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:prefetch_lock\"\n",
			stack->id, mod->name);

		/* If there is any older write, wait for it */
		older_stack = mod_in_flight_write(mod, stack);
		if (older_stack)
		{
			mem_debug("    %lld wait for write %lld\n",
				stack->id, older_stack->id);
			mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_PREFETCH_LOCK);
			return;
		}

		/* Call find and lock */
		new_stack = mod_stack_create(stack->id, mod, stack->addr,
			EV_MOD_NMOESI_PREFETCH_ACTION, stack);
		new_stack->orig_mod_id = mod->mod_id;
		new_stack->issue_mod_id = stack->issue_mod_id;
		new_stack->blocking = 0;
		new_stack->prefetch = 1;
		new_stack->retry = 0;
		new_stack->witness_ptr = stack->witness_ptr;
		esim_schedule_event(EV_MOD_NMOESI_FIND_AND_LOCK, new_stack, 0);

		/* Set witness variable to NULL so that retries from the same
		 * stack do not increment it multiple times */
		stack->witness_ptr = NULL;

		return;
	}

	if (event == EV_MOD_NMOESI_PREFETCH_ACTION)
	{
		mem_debug("  %lld %lld 0x%x %s prefetch action\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:prefetch_action\"\n",
			stack->id, mod->name);

		/* Error locking */
		if (stack->err)
		{
			/* Don't want to ever retry prefetches if getting a lock failed. 
			Effectively this means that prefetches are of low priority.
			This can be improved to not retry only when the current lock
			holder is writing to the block. */
			mod->prefetch_aborts++;
			mem_debug("    lock error, aborting prefetch\n");
			esim_schedule_event(EV_MOD_NMOESI_PREFETCH_FINISH, stack, 0);
			return;
		}

		/* Hit */
		if (stack->state)
		{
			/* block already in the cache */
			mem_debug("  %lld %lld 0x%x %s useless prefetch - cache hit\n",
				  esim_time, stack->id, stack->addr, mod->name);

			mod->useless_prefetches++;
			esim_schedule_event(EV_MOD_NMOESI_PREFETCH_UNLOCK, stack, 0);
			return;
		}

		/* Miss */
		new_stack = mod_stack_create(stack->id, mod, stack->tag,
			EV_MOD_NMOESI_PREFETCH_MISS, stack);
		new_stack->orig_mod_id = mod->mod_id;
		new_stack->issue_mod_id = stack->issue_mod_id;
		// new_stack->peer = mod_stack_set_peer(mod, stack->state);
		new_stack->target_mod = mod_get_low_mod(mod, stack->tag);
		new_stack->request_dir = mod_request_up_down;
		new_stack->prefetch = 1;
		new_stack->read = 1;
		esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST, new_stack, 0);
		return;
	}
	if (event == EV_MOD_NMOESI_PREFETCH_MISS)
	{
		int next_state;
		struct mod_t *check_mod;

		mem_debug("  %lld %lld 0x%x %s prefetch miss\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:prefetch_miss\"\n",
			stack->id, mod->name);

		/* Error on read request. Unlock block and abort. */
		if (stack->err)
		{
			/* Don't want to ever retry prefetches if read request failed. 
			 * Effectively this means that prefetches are of low priority.
			 * This can be improved depending on the reason for read request fail */
			mod->prefetch_aborts++;
			cache_entry_unlock(mod->cache, stack->set, stack->way);
			mem_debug("    lock error, aborting prefetch\n");
			esim_schedule_event(EV_MOD_NMOESI_PREFETCH_FINISH, stack, 0);
			return;
		}

		/* Set block state to excl/shared depending on return var 'shared'.
		 * Also set the tag of the block. */
		// For MOESI protocol, in case the state is shared/exclusive depending on response.
		
		next_state = cache_block_next_state(stack->shared, stack->dirty);

		cache_set_block(mod->cache, stack->set, stack->way, stack->tag, next_state);

		check_mod = mod_get_low_mod(mod, stack->tag);	
		mod_check_coherency_status(check_mod, mod, mod, stack->tag, next_state, 0, stack);

		/* Mark the prefetched block as prefetched. This is needed to let the 
		 * prefetcher know about an actual access to this block so that it
		 * is aware of all misses as they would be without the prefetcher. 
		 * TODO: The lower caches that will be filled because of this prefetch
		 * do not know if it was a prefetch or not. Need to have a way to mark
		 * them as prefetched too. */
		mod_block_set_prefetched(mod, stack->addr, 1);

		/* Continue */
		esim_schedule_event(EV_MOD_NMOESI_PREFETCH_UNLOCK, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_PREFETCH_UNLOCK)
	{
		mem_debug("  %lld %lld 0x%x %s prefetch unlock\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:prefetch_unlock\"\n",
			stack->id, mod->name);

		/* Unlock directory entry */
		cache_entry_unlock(mod->cache, stack->set, stack->way);

		/* Continue */
		esim_schedule_event(EV_MOD_NMOESI_PREFETCH_FINISH, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_PREFETCH_FINISH)
	{
		mem_debug("%lld %lld 0x%x %s prefetch\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:prefetch_finish\"\n",
			stack->id, mod->name);
		mem_trace("mem.end_access name=\"A-%lld\"\n",
			stack->id);

		/* Increment witness variable */
		if (stack->witness_ptr)
			(*stack->witness_ptr)++;

		/* Return event queue element into event queue */
		if (stack->event_queue && stack->event_queue_item)
			linked_list_add(stack->event_queue, stack->event_queue_item);

		/* Free the mod_client_info object, if any */
		if (stack->client_info)
			mod_client_info_free(mod, stack->client_info);

		/* Finish access */
		mod_access_finish(mod, stack);

		/* Return */
		mod_stack_return(stack);
		return;
	}

	abort();
}

void mod_handler_nmoesi_find_and_lock(int event, void *data)
{
	struct mod_stack_t *stack = data;
	struct mod_stack_t *ret = stack->ret_stack;
	struct mod_stack_t *new_stack;

	struct mod_t *mod = stack->mod;


	if (event == EV_MOD_NMOESI_FIND_AND_LOCK)
	{
		mem_debug("  %lld %lld 0x%x %s find and lock (blocking=%d)\n",
			esim_time, stack->id, stack->addr, mod->name, stack->blocking);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:find_and_lock\"\n",
			stack->id, mod->name);

		/* Default return values */
		ret->err = 0;

		/* If this stack has already been assigned a way, keep using it */
		stack->way = ret->way;

		/* Get a port */
		mod_lock_port(mod, stack, EV_MOD_NMOESI_FIND_AND_LOCK_PORT);
		return;
	}

	if (event == EV_MOD_NMOESI_FIND_AND_LOCK_PORT)
	{
		struct mod_port_t *port = stack->port;
		struct cache_lock_t *cache_lock; // This is a symbolic Lock not an actual lock

		assert(stack->port);
		mem_debug("  %lld %lld 0x%x %s find and lock port\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:find_and_lock_port\"\n",
			stack->id, mod->name);

		/* Set parent stack flag expressing that port has already been locked.
		 * This flag is checked by new writes to find out if it is already too
		 * late to coalesce. */
		ret->port_locked = 1;

		/* Look for block. */
		stack->hit = mod_find_block(mod, stack->addr, &stack->set,
			&stack->way, &stack->tag, &stack->state);
		
		// For statistics measurement
		if(stack->hit)
			ret->prev_state = stack->state;
		else
			ret->prev_state = cache_block_invalid;

		/* Debug */
		if (stack->hit)
			mem_debug("    %lld 0x%x %s hit: set=%d, way=%d, state=%s\n", stack->id,
				stack->tag, mod->name, stack->set, stack->way,
				str_map_value(&cache_block_state_map, stack->state));

		/* Statistics */
		mod->accesses++;
		if (stack->hit)
			mod->hits++;

		if (stack->read)
		{
			mod->reads++;

			if(stack->downup_read_request) mod->downup_read_requests++;
			else                    			 mod->load_requests++;

			mod->effective_reads++;
			stack->blocking ? mod->blocking_reads++ : mod->non_blocking_reads++;
			if (stack->hit)
			{
				mod->read_hits++;

				if(stack->downup_read_request) mod->downup_read_requests_hits++;
				else 													 mod->load_requests_hits++;

				if(ret->request_dir == mod_request_down_up)
				{
					switch(stack->state)
					{
						case cache_block_modified    : mod->sharer_req_state_modified++; break;
						case cache_block_owned	     : mod->sharer_req_state_owned++; break;
						case cache_block_exclusive   : mod->sharer_req_state_exclusive++; break;
						case cache_block_shared      : mod->sharer_req_state_shared++; break;
						case cache_block_noncoherent : mod->sharer_req_state_noncoherent++; break;
					}
				}
				else
				{
					switch(stack->state)
					{
						case cache_block_modified    : mod->read_state_modified++; break;
						case cache_block_owned	     : mod->read_state_owned++; break;
						case cache_block_exclusive   : mod->read_state_exclusive++; break;
						case cache_block_shared      : mod->read_state_shared++; break;
						case cache_block_noncoherent : mod->read_state_noncoherent++; break;
					}
				}
			}
			else
			{
				if(ret->request_dir == mod_request_down_up)
					mod->sharer_req_state_invalid++;
				else
					mod->read_state_invalid++;

		  	if(stack->downup_read_request) mod->downup_read_requests_misses++;
				else 													 mod->load_requests_misses++;
			}

		}
		else if (stack->prefetch)
		{
			mod->prefetches++;
		}
		else if (stack->nc_write)  /* Must go after read */
		{
			mod->nc_writes++;
			mod->effective_nc_writes++;
			stack->blocking ? mod->blocking_nc_writes++ : mod->non_blocking_nc_writes++;
			if (stack->hit)
				mod->nc_write_hits++;
		}
		else if (stack->write)
		{
			mod->writes++;
			mod->effective_writes++;
			stack->blocking ? mod->blocking_writes++ : mod->non_blocking_writes++;
			
			if(stack->evict_trans) mod->writeback_due_to_eviction++;

			// Here we donot consider the effects of evict transactions while considering a Store/Up-down writeback as for a lower level module it is the writeback requests.	
			if(stack->downup_writeback_request) mod->downup_writeback_requests++;
			else                                mod->store_requests++;

			/* Increment witness variable when port is locked */
			if (stack->witness_ptr)
			{
				(*stack->witness_ptr)++;
				stack->witness_ptr = NULL;
			}

			if (stack->hit)
			{
				mod->write_hits++;

				if(stack->evict_trans) mod->writeback_due_to_eviction_hits++;

				if(stack->downup_writeback_request) mod->downup_writeback_requests_hits++;
				else                                mod->store_requests_hits++;

				if(ret->request_dir == mod_request_down_up)
				{
					switch(stack->state)
					{
						case cache_block_modified    : mod->sharer_req_state_modified++; break;
						case cache_block_owned	     : mod->sharer_req_state_owned++; break;
						case cache_block_exclusive   : mod->sharer_req_state_exclusive++; break;
						case cache_block_shared      : mod->sharer_req_state_shared++; break;
						case cache_block_noncoherent : mod->sharer_req_state_noncoherent++; break;
					}
				}
				else
				{
					switch(stack->state)
					{
						case cache_block_modified    : mod->write_state_modified++; break;
						case cache_block_owned	     : mod->write_state_owned++; break;
						case cache_block_exclusive   : mod->write_state_exclusive++; break;
						case cache_block_shared      : mod->write_state_shared++; break;
						case cache_block_noncoherent : mod->write_state_noncoherent++; break;
					}
				}
			}
			else
			{
				if(ret->request_dir == mod_request_down_up)
					mod->sharer_req_state_invalid++;
				else
					mod->write_state_invalid++;
				
				if(stack->evict_trans) mod->writeback_due_to_eviction_misses++;

				if(stack->downup_writeback_request) mod->downup_writeback_requests_misses++;
				else                                mod->store_requests_misses++;
			}
		}
		else if (stack->message)
		{
			/* FIXME */
		}
		else 
		{
			fatal("Unknown memory operation type");
		}

		if (!stack->retry)
		{
			mod->no_retry_accesses++;
			if (stack->hit)
				mod->no_retry_hits++;
			
			if (stack->read)
			{
				mod->no_retry_reads++;
				if (stack->hit)
					mod->no_retry_read_hits++;
			}
			else if (stack->nc_write)  /* Must go after read */
			{
				mod->no_retry_nc_writes++;
				if (stack->hit)
					mod->no_retry_nc_write_hits++;
			}
			else if (stack->write)
			{
				mod->no_retry_writes++;
				if (stack->hit)
					mod->no_retry_write_hits++;
			}
			else if (stack->prefetch)
			{
				/* No retries currently for prefetches */
			}
			else if (stack->message)
			{
				/* FIXME */
			}
			else 
			{
				fatal("Unknown memory operation type");
			}
		}

		// For a Snoop based protocol the Load operation behaves similar to a Directory based protocol.
		// For an Evict transaction, the only thing that needs to be done is to try for a cache lock.
		if(!stack->downup_read_request && !stack->downup_writeback_request && !stack->evict_trans)
		{
			if (!stack->hit)
			{
				/* Find victim */
				if (stack->way < 0) 
				{
					stack->way = cache_replace_block(mod->cache, stack->set);
				}
			}
			assert(stack->way >= 0);
		}

		/* If directory entry is locked and the call to FIND_AND_LOCK is not
		 * blocking, release port and return error. */
		// Instead of a directory based check for a cache entry lock.
		// In case of a Snoop based protocol if there is a downup read or writeback request and it is a miss then bypass throughout without waiting for the lock.
		if(stack->hit || (!stack->downup_read_request && !stack->evict_trans && !stack->downup_writeback_request))
		{
			cache_lock = cache_lock_get(mod->cache, stack->set, stack->way);
			if (cache_lock->lock && !stack->blocking)
			{
				mem_debug("    %lld 0x%x %s block locked at set=%d, way=%d by A-%lld - aborting\n",
					stack->id, stack->tag, mod->name, stack->set, stack->way, cache_lock->stack_id);
				ret->err = 1;
				mod_unlock_port(mod, port, stack);
				ret->port_locked = 0;
				mod_stack_return(stack);
				return;
			}

			/* Lock directory entry. If lock fails, port needs to be released to prevent 
			 * deadlock.  When the directory entry is released, locking port and 
			 * directory entry will be retried. */
			if (!cache_entry_lock(mod->cache, stack->set, stack->way, EV_MOD_NMOESI_FIND_AND_LOCK, 
				stack))
			{
				mem_debug("    %lld 0x%x %s block locked at set=%d, way=%d by A-%lld - waiting\n",
					stack->id, stack->tag, mod->name, stack->set, stack->way, cache_lock->stack_id);

				mod_unlock_port(mod, port, stack);
				ret->port_locked = 0;
				return;
			}
			
			if(stack->request_dir != mod_request_down_up)
			{	
				if(stack->read)
					ret->read_request_in_progress = 1;
				if(stack->write)
					ret->write_request_in_progress = 1;
			}

		}

		/* Miss */
		if(!stack->downup_read_request && !stack->downup_writeback_request && !stack->evict_trans)
		{
			if (!stack->hit)
			{
				/* Find victim */
				cache_get_block(mod->cache, stack->set, stack->way, NULL, &stack->state);
//--	-----------------------------------------------
				// This assert may not be required as it may happen that the block we replace is an Invalid block itself.
				// However in this case doesn't his imply that a way or block is empty in the set ??
				// assert(stack->state || !dir_entry_group_shared_or_owned(mod->dir,
				// 	stack->set, stack->way));
//--	-----------------------------------------------
				mem_debug("    %lld 0x%x %s miss -> lru: set=%d, way=%d, state=%s\n",
					stack->id, stack->tag, mod->name, stack->set, stack->way,
					str_map_value(&cache_block_state_map, stack->state));
			}
		}


		/* Entry is locked. Record the transient tag so that a subsequent lookup
		 * detects that the block is being brought.
		 * Also, update LRU counters here. */
		if(stack->hit || (!stack->downup_read_request && !stack->evict_trans && !stack->downup_writeback_request))
		{
			cache_set_transient_tag(mod->cache, stack->set, stack->way, stack->tag);
			cache_access_block(mod->cache, stack->set, stack->way);
		}

		/* Access latency */
//-------------------------------------------------
		// esim_schedule_event(EV_MOD_NMOESI_FIND_AND_LOCK_ACTION, stack, mod->dir_latency);
//-------------------------------------------------
		esim_schedule_event(EV_MOD_NMOESI_FIND_AND_LOCK_ACTION, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_FIND_AND_LOCK_ACTION)
	{
		struct mod_port_t *port = stack->port;

		assert(port);
		mem_debug("  %lld %lld 0x%x %s find and lock action\n", esim_time, stack->id,
			stack->tag, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:find_and_lock_action\"\n",
			stack->id, mod->name);

		/* Release port */
		mod_unlock_port(mod, port, stack);
		ret->port_locked = 0;

		/* On miss, evict if victim is a valid block. */
		if(!stack->downup_read_request && !stack->downup_writeback_request && !stack->evict_trans)
		{
			if (!stack->hit && stack->state)
			{
				stack->eviction = 1;
				
				if(stack->read)
					mod->eviction_due_to_load++;
				else
					mod->eviction_due_to_store++;

				new_stack = mod_stack_create(stack->id, mod, 0,
					EV_MOD_NMOESI_FIND_AND_LOCK_FINISH, stack);
				new_stack->orig_mod_id = mod->mod_id;
				new_stack->issue_mod_id = stack->issue_mod_id;
				new_stack->set = stack->set;
				new_stack->way = stack->way;
				new_stack->evict_trans = 1;
				esim_schedule_event(EV_MOD_NMOESI_EVICT, new_stack, 0);
				return;
			}
		}

		/* Continue */
		esim_schedule_event(EV_MOD_NMOESI_FIND_AND_LOCK_FINISH, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_FIND_AND_LOCK_FINISH)
	{
		mem_debug("  %lld %lld 0x%x %s find and lock finish (err=%d)\n", esim_time, stack->id,
			stack->tag, mod->name, stack->err);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:find_and_lock_finish\"\n",
			stack->id, mod->name);

		/* If evict produced err, return err */
		if (stack->err)
		{
			cache_get_block(mod->cache, stack->set, stack->way, NULL, &stack->state);
			assert(stack->state);
			assert(stack->eviction);
			ret->err = 1;
			// This is done so as to avoid any unlocking of entry that was never locked.
			if(stack->hit || (!stack->downup_read_request && !stack->evict_trans && !stack->downup_writeback_request))
			{
				cache_entry_unlock(mod->cache, stack->set, stack->way);
			}
			mod_stack_return(stack);
			return;
		}

		/* Eviction */
		if (stack->eviction)
		{
			mod->evictions++;
			cache_get_block(mod->cache, stack->set, stack->way, NULL, &stack->state);
			assert(!stack->state);
		}

		/* If this is a main memory, the block is here. A previous miss was just a miss
		 * in the directory. */
		if (mod->kind == mod_kind_main_memory && !stack->state)
		{
			// For an MOESI protocol, state is set to exclusive.
			stack->state = cache_block_exclusive;
			cache_set_block(mod->cache, stack->set, stack->way,
				stack->tag, stack->state);
		}

		/* Return */
		ret->err = 0;
		ret->set = stack->set;
		ret->way = stack->way;
		ret->state = stack->state;
		ret->tag = stack->tag;
		mod_stack_return(stack);
		return;
	}

	abort();
}


void mod_handler_nmoesi_evict(int event, void *data)
{
	struct mod_stack_t *stack = data;
	struct mod_stack_t *ret = stack->ret_stack;
	struct mod_stack_t *new_stack;

	struct mod_t *mod = stack->mod;
	struct mod_t *target_mod = stack->target_mod;

	uint32_t cache_entry_tag, z;


	if (event == EV_MOD_NMOESI_EVICT)
	{
		/* Default return value */
		ret->err = 0;

		/* Get block info */
		cache_get_block(mod->cache, stack->set, stack->way, &stack->tag, &stack->state);
		mem_debug("  %lld %lld 0x%x %s evict (set=%d, way=%d, state=%s)\n", esim_time, stack->id,
			stack->tag, mod->name, stack->set, stack->way,
			str_map_value(&cache_block_state_map, stack->state));
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:evict\"\n",
			stack->id, mod->name);
		
		
		/* Save some data */
		stack->src_set = stack->set;
		stack->src_way = stack->way;
		stack->src_tag = stack->tag;
		stack->target_mod = mod_get_low_mod(mod, stack->tag);
		
		mod->num_eviction_requests++;
		mod_update_request_counters(mod, mod_trans_eviction);

		stack->access_start_cycle = esim_cycle();

		/* Send write request to all sharers */
		new_stack = mod_stack_create(stack->id, mod, 0, EV_MOD_NMOESI_EVICT_INVALID, stack);
		new_stack->orig_mod_id = mod->mod_id;
		new_stack->issue_mod_id = stack->issue_mod_id;
		new_stack->except_mod = NULL;
		new_stack->set = stack->set;
		new_stack->way = stack->way;
		new_stack->invalidate_eviction = 1;
		// Check
		new_stack->prev_state = stack->state;
		esim_schedule_event(EV_MOD_NMOESI_INVALIDATE, new_stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_EVICT_INVALID)
	{
		mem_debug("  %lld %lld 0x%x %s evict invalid\n", esim_time, stack->id,
			stack->tag, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:evict_invalid\"\n",
			stack->id, mod->name);

		/* If module is main memory, we just need to set the block as invalid, 
		 * and finish. */
		if (mod->kind == mod_kind_main_memory)
		{
			cache_set_block(mod->cache, stack->src_set, stack->src_way,
				0, cache_block_invalid);
			esim_schedule_event(EV_MOD_NMOESI_EVICT_FINISH, stack, 0);
			return;
		}

		/* Continue */
		esim_schedule_event(EV_MOD_NMOESI_EVICT_ACTION, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_EVICT_ACTION)
	{
		struct mod_t *low_mod;
		struct net_node_t *low_node;
		int msg_size;

		mem_debug("  %lld %lld 0x%x %s evict action\n", esim_time, stack->id,
			stack->tag, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:evict_action\"\n",
			stack->id, mod->name);

		/* Get low node */
		low_mod = stack->target_mod;
		low_node = low_mod->high_net_node;
		assert(low_mod != mod);
		assert(low_mod == mod_get_low_mod(mod, stack->tag));
		assert(low_node && low_node->user_data == low_mod);

		/* Update the cache state since it may have changed after its 
		 * higher-level modules were invalidated */
		cache_get_block(mod->cache, stack->set, stack->way, NULL, &stack->state);

		stack->prev_state = stack->state;
		
		// Check the previous state of this module, here you predict that this is the eviction request in this block that is going to happen, the data transfers may happen lesser than this, but must be an approximate to the value of modified/owned counters.
		switch(stack->prev_state)
		{
			case cache_block_invalid     : { mod->eviction_request_state_invalid++;break; }
			case cache_block_modified    : { mod->eviction_request_state_modified++;break; }
			case cache_block_owned       : { mod->eviction_request_state_owned++;break; }
			case cache_block_exclusive   : { mod->eviction_request_state_exclusive++;break; }
			case cache_block_shared      : { mod->eviction_request_state_shared++;break; }
			case cache_block_noncoherent : { mod->eviction_request_state_noncoherent++;break; }
		}
		
		/* State = I */
		if (stack->state == cache_block_invalid)
		{
			esim_schedule_event(EV_MOD_NMOESI_EVICT_FINISH, stack, 0);
			return;
		}

		/* If state is M/O/N, data must be sent to lower level mod */
		if (stack->state == cache_block_modified || stack->state == cache_block_owned ||
			stack->state == cache_block_noncoherent)
		{
			/* Need to transmit data to low module */
			msg_size = 8 + mod->block_size;
			stack->reply = reply_ack_data;
		}
		/* If state is E/S, just an ack needs to be sent */
		else 
		{
			msg_size = 8;
			stack->reply = reply_ack;
		}
		
		// Record the start time of network access.
		if(!stack->nw_send_request_latency_start_cycle)
			stack->nw_send_request_latency_start_cycle = esim_cycle();

		/* Send message */
		stack->msg = net_try_send_ev(mod->low_net, mod->low_net_node,
			low_node, msg_size, EV_MOD_NMOESI_EVICT_RECEIVE, stack, event, stack);
		
		// In case message was NULL, then updated the evcition send request retried counter	
		if(!stack->msg)
		{
			mod->eviction_send_requests_retried_nw++;
		}
		else
		{
			stack->nw_send_request_latency_end_cycle = esim_cycle();
			stack->nw_send_request_latency_cycle     = stack->nw_send_request_latency_end_cycle - stack->nw_send_request_latency_start_cycle;
			if(stack->nw_send_request_latency_cycle) mod_update_nw_send_request_delay_counters(mod, stack, mod_trans_eviction);
		}

		return;
	}

	if (event == EV_MOD_NMOESI_EVICT_RECEIVE)
	{
		mem_debug("  %lld %lld 0x%x %s evict receive\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:evict_receive\"\n",
			stack->id, target_mod->name);
		
		// record the start time of network access
		if(!stack->nw_receive_request_latency_start_cycle)
			stack->nw_receive_request_latency_start_cycle = esim_cycle();

		/* Receive message */
		if(!stack->updown_access_registered)
			net_receive(target_mod->high_net, target_mod->high_net_node, stack->msg);

		// Registering Evict to target module
		if(!stack->evict_access_registered)
		{
			if(mod->kind != mod_kind_main_memory)
			{
				mod_evict_start(target_mod, stack, mod_access_invalid);
				mod_update_request_queue_statistics(target_mod);
				mod_check_dependency_depth(target_mod, stack, mod_trans_eviction, stack->src_tag);
			}
		}

		// Record and update
		stack->nw_receive_request_latency_end_cycle = esim_cycle();
		stack->nw_receive_request_latency_cycle     = stack->nw_receive_request_latency_end_cycle - stack->nw_receive_request_latency_start_cycle;
		if(stack->nw_receive_request_latency_cycle) mod_update_nw_receive_request_delay_counters(target_mod, stack, mod_trans_eviction);

		/* Find and lock */
		if (stack->state == cache_block_noncoherent)
		{
			new_stack = mod_stack_create(stack->id, target_mod, stack->src_tag,
				EV_MOD_NMOESI_EVICT_PROCESS_NONCOHERENT, stack);
		}
		else 
		{
			new_stack = mod_stack_create(stack->id, target_mod, stack->src_tag,
				EV_MOD_NMOESI_EVICT_PROCESS, stack);
		}

		new_stack->orig_mod_id = target_mod->mod_id;
		new_stack->issue_mod_id = stack->issue_mod_id;
		/* FIXME It's not guaranteed to be a write */
		new_stack->blocking = 0;
		new_stack->write = 1;
		new_stack->retry = 0;
		new_stack->evict_trans = 1;
		new_stack->debug_flag = 1;
		esim_schedule_event(EV_MOD_NMOESI_FIND_AND_LOCK, new_stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_EVICT_PROCESS)
	{
		int next_state;

		mem_debug("  %lld %lld 0x%x %s evict process\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:evict_process\"\n",
			stack->id, target_mod->name);

		/* Error locking block */
		if (stack->err)
		{
			ret->err = 1;
			esim_schedule_event(EV_MOD_NMOESI_EVICT_REPLY, stack, 0);
			return;
		}

		/* If data was received, set the block to modified */
		if (stack->reply == reply_ack)
		{
			/* Nothing to do */
		}
		else if (stack->reply == reply_ack_data)
		{
			// For a Snoop Based protocol, there is no peer-peer transfer hence if data is transferred then following transitions need to be done
			// E,M -> M, S,O -> O, NC -> NC
			if(stack->state == cache_block_exclusive || stack->state == cache_block_modified)
			{
				cache_set_block(target_mod->cache, stack->set, stack->way, 
					stack->tag, cache_block_modified);
				next_state = cache_block_modified; 
			}
			else if(stack->state == cache_block_shared || stack->state == cache_block_owned)
			{
				cache_set_block(target_mod->cache, stack->set, stack->way, 
					stack->tag, cache_block_owned);
				next_state = cache_block_owned; 
			}
			else if(stack->state == cache_block_noncoherent)
			{
				cache_set_block(target_mod->cache, stack->set, stack->way, 
					stack->tag, cache_block_noncoherent);
				next_state = cache_block_noncoherent; 
			}
			else
			{
					fatal("%lld %s: Invalid cache block state: %d, Module %s Target Module %s, Tag %x, Set %x Way %x Hit %x\n", esim_cycle(), __FUNCTION__, stack->state, mod->name, target_mod->name, stack->tag, stack->set, stack->way, stack->hit);
			}

			// Using stack->mod just to confirm that the higher level module was the module that performed the data transfer from its end.
			stack->mod->data_transfer_eviction++;
			// Also update the state modification change happening due to eviction at target module. This is registered as a store event.
			mod_update_state_modification_counters(target_mod, stack->prev_state, next_state, mod_trans_store);
		}
		else 
		{
			fatal("%s: Invalid cache block state: %d\n", __FUNCTION__, 
				stack->state);
		}

		/* Unlock the cache entry */
		cache_entry_unlock(target_mod->cache, stack->set, stack->way);

		esim_schedule_event(EV_MOD_NMOESI_EVICT_REPLY, stack, target_mod->latency);
		return;
	}

	if (event == EV_MOD_NMOESI_EVICT_PROCESS_NONCOHERENT)
	{
		mem_debug("  %lld %lld 0x%x %s evict process noncoherent\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:evict_process_noncoherent\"\n",
			stack->id, target_mod->name);

		/* Error locking block */
		if (stack->err)
		{
			ret->err = 1;
			esim_schedule_event(EV_MOD_NMOESI_EVICT_REPLY, stack, 0);
			return;
		}

		/* If data was received, set the block to modified */
		if (stack->reply == reply_ack_data)
		{
			if (stack->state == cache_block_exclusive)
			{
				cache_set_block(target_mod->cache, stack->set, stack->way, 
					stack->tag, cache_block_modified);
			}
			else if (stack->state == cache_block_owned || 
				stack->state == cache_block_modified)
			{
				/* Nothing to do */
			}
			else if (stack->state == cache_block_shared ||
				stack->state == cache_block_noncoherent)
			{
				cache_set_block(target_mod->cache, stack->set, stack->way, 
					stack->tag, cache_block_noncoherent);
			}
			else
			{
				fatal("%s: Invalid cache block state: %d\n", __FUNCTION__, 
					stack->state);
			}
		}
		else 
		{
			fatal("%s: Invalid cache block state: %d\n", __FUNCTION__, 
				stack->state);
		}

		/* Unlock the directory entry */
		cache_entry_unlock(target_mod->cache, stack->set, stack->way);

		esim_schedule_event(EV_MOD_NMOESI_EVICT_REPLY, stack, target_mod->latency);
		return;
	}

	if (event == EV_MOD_NMOESI_EVICT_REPLY)
	{
		mem_debug("  %lld %lld 0x%x %s evict reply\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:evict_reply\"\n",
			stack->id, target_mod->name);
		
		// Record the start time for sending the reply message.
		if(stack->nw_send_reply_latency_start_cycle)
			stack->nw_send_reply_latency_start_cycle = esim_cycle();

		/* Send message */
		stack->msg = net_try_send_ev(target_mod->high_net, target_mod->high_net_node,
			mod->low_net_node, 8, EV_MOD_NMOESI_EVICT_REPLY_RECEIVE, stack,
			event, stack);
		
		// In case message was NULL, then updated the evcition send reply retried counter, else update the timings information.	
		if(!stack->msg)
		{
			target_mod->eviction_send_replies_retried_nw++;
		}
		else
		{
			stack->nw_send_reply_latency_end_cycle = esim_cycle();
			stack->nw_send_reply_latency_cycle     = stack->nw_send_reply_latency_end_cycle - stack->nw_send_reply_latency_start_cycle;
			if(stack->nw_send_reply_latency_cycle) mod_update_nw_send_reply_delay_counters(target_mod, stack, mod_trans_eviction);
		}

		return;

	}

	if (event == EV_MOD_NMOESI_EVICT_REPLY_RECEIVE)
	{
		mem_debug("  %lld %lld 0x%x %s evict reply receive\n", esim_time, stack->id,
			stack->tag, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:evict_reply_receive\"\n",
			stack->id, mod->name);
		
		// record the start time of the network access
		if(!stack->nw_receive_reply_latency_start_cycle)
			stack->nw_receive_reply_latency_start_cycle = esim_cycle();

		/* Receive message */
		net_receive(mod->low_net, mod->low_net_node, stack->msg);
		
		stack->nw_receive_reply_latency_end_cycle = esim_cycle();
		stack->nw_receive_reply_latency_cycle			= stack->nw_receive_reply_latency_end_cycle - stack->nw_receive_reply_latency_start_cycle;
		if(stack->nw_receive_reply_latency_cycle) mod_update_nw_receive_reply_delay_counters(mod, stack, mod_trans_eviction);

		/* Invalidate block if there was no error. */
		if (!stack->err)
		{
			cache_set_block(mod->cache, stack->src_set, stack->src_way,
				0, cache_block_invalid);
		}
		
		esim_schedule_event(EV_MOD_NMOESI_EVICT_FINISH, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_EVICT_FINISH)
	{
		mem_debug("  %lld %lld 0x%x %s evict finish\n", esim_time, stack->id,
			stack->tag, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:evict_finish\"\n",
			stack->id, mod->name);
		
		stack->access_end_cycle = esim_cycle();
		stack->access_latency   = stack->access_end_cycle - stack->access_start_cycle;

		mod_update_latency_counters(mod, stack->access_latency, mod_trans_eviction);

		// Update counters for controller occupancy statistics
		mod->num_eviction_requests--;
		mod_update_request_counters(mod, mod_trans_eviction);
			
		if(mod->kind != mod_kind_main_memory)
		{
			mod_evict_finish(target_mod, stack);
			mod_update_request_queue_statistics(target_mod);
			mod_check_dependency_depth(target_mod, stack, mod_trans_eviction, stack->src_tag);
		}

		mod_stack_return(stack);
		return;
	}

	abort();
}


void mod_handler_nmoesi_read_request(int event, void *data)
{
	struct mod_stack_t *stack = data;
	struct mod_stack_t *ret = stack->ret_stack;
	struct mod_stack_t *new_stack;

	struct mod_t *mod = stack->mod;
	struct mod_t *target_mod = stack->target_mod;

	uint32_t cache_entry_tag, z;

	if (event == EV_MOD_NMOESI_READ_REQUEST)
	{
		struct net_t *net;
		struct net_node_t *src_node;
		struct net_node_t *dst_node;

		mem_debug("  %lld %lld 0x%x %s read request\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:read_request\"\n",
			stack->id, mod->name);

		/* Default return values*/
		// It may be case that the request may override some return shared values, say a DU request that sets the shared variable but a later schedule transaction to its peer may override this.
		if(!ret->shared) ret->shared = 0;
		if(!ret->dirty) ret->dirty = 0;
		ret->err = 0;

		/* Checks */
		assert(stack->request_dir);
		assert(mod_get_low_mod(mod, stack->addr) == target_mod ||
			stack->request_dir == mod_request_down_up);
		assert(mod_get_low_mod(target_mod, stack->addr) == mod ||
			stack->request_dir == mod_request_up_down);
		
		stack->access_start_cycle = esim_cycle();

		/* Get source and destination nodes */
		if (stack->request_dir == mod_request_up_down)
		{
			net = mod->low_net;
			src_node = mod->low_net_node;
			dst_node = target_mod->high_net_node;
		}
		else
		{
			net = mod->high_net;
			src_node = mod->high_net_node;
			dst_node = target_mod->low_net_node;
		}
		
		// record the start time of the network access
		if(!stack->nw_send_request_latency_start_cycle)
			stack->nw_send_request_latency_start_cycle = esim_cycle();

		/* Send message */
		stack->msg = net_try_send_ev(net, src_node, dst_node, 8,
			EV_MOD_NMOESI_READ_REQUEST_RECEIVE, stack, event, stack);
		
		// In case message was NULL, then updated the read request send retried counter, else update the timings information.	
		if(!stack->msg)
		{
			if(stack->downup_read_request) mod->downup_read_send_requests_retried_nw++;
			else 													 mod->read_send_requests_retried_nw++;
		}
		else
		{
			stack->nw_send_request_latency_end_cycle = esim_cycle();
			stack->nw_send_request_latency_cycle     = stack->nw_send_request_latency_end_cycle - stack->nw_send_request_latency_start_cycle;

			if(stack->nw_send_request_latency_cycle)
 			{
				enum mod_trans_type_t trans_type = (stack->downup_read_request) ? mod_trans_downup_read_request : mod_trans_read_request ;
				mod_update_nw_send_request_delay_counters(mod, stack, trans_type);
			}
		}

		return;
	}

	if (event == EV_MOD_NMOESI_READ_REQUEST_RECEIVE)
	{
		enum mod_trans_type_t trans_type;

		mem_debug("  %lld %lld 0x%x %s read request receive\n", esim_time, stack->id,
			stack->addr, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:read_request_receive\"\n",
			stack->id, target_mod->name);
		
		if(!stack->nw_receive_request_latency_start_cycle)
			stack->nw_receive_request_latency_start_cycle = esim_cycle();

		/* Receive message */
		if (stack->request_dir == mod_request_up_down)
		{
			if(!stack->updown_access_registered)
			{
				net_receive(target_mod->high_net, target_mod->high_net_node, stack->msg);
				stack->read_write_evict_du_req_start_cycle = esim_cycle();
			}
		}
		else
		{
			if(!stack->downup_access_registered)
			{
				net_receive(target_mod->low_net, target_mod->low_net_node, stack->msg);
				stack->read_write_evict_du_req_start_cycle = esim_cycle();
			}
		}
		
		trans_type = (stack->downup_read_request) ? mod_trans_downup_read_request: mod_trans_read_request;

		stack->nw_receive_request_latency_end_cycle = esim_cycle();
		stack->nw_receive_request_latency_cycle     = stack->nw_receive_request_latency_end_cycle - stack->nw_receive_request_latency_start_cycle;
		if(!stack->nw_receive_request_latency_cycle) mod_update_nw_receive_request_delay_counters(target_mod, stack, trans_type);

		if(stack->request_dir == mod_request_up_down)
		{
			// This is done here to ensure that any network retries aren't counted again
			mod->num_read_requests++;
			mod_update_request_counters(mod, mod_trans_read_request);

			target_mod->num_load_requests++;
			mod_update_request_counters(target_mod, mod_trans_load);
		}
		else
		{
			target_mod->num_downup_read_requests++;
			mod_update_request_counters(target_mod, mod_trans_downup_read_request);
		}
		
		// Update the simultaneous access request counters
		mod_update_simultaneous_flight_access_counters(target_mod, stack->addr, stack, (stack->downup_read_request) ? mod_trans_downup_read_request : mod_trans_load);
			
		// This I am doing here to avoid making another in between transactions.	
		if(stack->request_dir == mod_request_down_up)
		{
			struct mod_stack_t *older_stack;
			
			if(!stack->downup_access_registered)
			{
				mod_downup_access_start(target_mod, stack, mod_access_invalid);
			}

			mod_check_dependency_depth(target_mod, stack, mod_trans_downup_read_request, stack->addr);

			mod_update_request_queue_statistics(target_mod);
			
			// These are checked at "mod" instead of "target_mod" so that they behave as if these were checked prior to sending.
			older_stack = mod_in_flight_evict_address(mod, stack->addr, stack);
			
			// recod the checking time
			if(!stack->wait_for_evict_req_start_cycle)
				stack->wait_for_evict_req_start_cycle = esim_cycle();

			if(older_stack)
			{
				mem_debug("    %lld wait for evict request %lld\n",
					stack->id, older_stack->id);
				
				// Update the waiting counter where a downup request waits for evict request.	
				mod->downup_req_waiting_to_be_sent_for_evict_req++;

				mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_READ_REQUEST_RECEIVE);
				return;
			}
			
			if(!stack->wait_for_evict_req_end_cycle)
			{
				stack->wait_for_evict_req_end_cycle = esim_cycle();
				stack->wait_for_evict_req_cycle = stack->wait_for_evict_req_end_cycle - stack->wait_for_evict_req_start_cycle;
			}

			// recod the checking time
			if(!stack->wait_for_read_write_req_start_cycle)
				stack->wait_for_read_write_req_start_cycle = esim_cycle();

			older_stack = mod_check_in_flight_address_dependency_for_downup_request(mod, stack->addr, stack);

			if(older_stack)
			{
				mem_debug("    %lld wait for read write request %lld\n",
					stack->id, older_stack->id);
				
			 	// Update the waiting counter where a downup request waits for read write request.	
			 	mod->downup_req_waiting_to_be_sent_for_read_write_req++;

				mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_READ_REQUEST_RECEIVE);
				return;
			}

			if(!stack->wait_for_read_write_req_end_cycle)
			{
				stack->wait_for_read_write_req_end_cycle = esim_cycle();
				stack->wait_for_read_write_req_cycle = stack->wait_for_read_write_req_end_cycle - stack->wait_for_read_write_req_start_cycle;
			}

			// recod the checking time
			if(!stack->wait_for_downup_req_start_cycle)
				stack->wait_for_downup_req_start_cycle = esim_cycle();

			// Check for all Down-up access to be in order
			older_stack = stack->downup_access_list_prev;

			if (older_stack)
			{
				mem_debug("    %lld wait for downup read request %lld\n",
					stack->id, older_stack->id);
				
				// Update the waiting counter where a downup request waits for a previous downup request.	
				target_mod->downup_req_waiting_to_be_processed_for_downup_req++;

				mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_READ_REQUEST_RECEIVE);
				return;
			}
			
			if(!stack->wait_for_downup_req_end_cycle)
			{
				stack->wait_for_downup_req_end_cycle = esim_cycle();
				stack->wait_for_downup_req_cycle = stack->wait_for_downup_req_end_cycle - stack->wait_for_downup_req_start_cycle;
			}
		}
		else
		{
			struct mod_stack_t *older_stack;

			if(!stack->updown_access_registered)
			{
				mod_read_write_req_access_start(target_mod, stack, mod_access_invalid);
			}
			
			mod_update_request_queue_statistics(target_mod);

			mod_check_dependency_depth(target_mod, stack, mod_trans_read_request, stack->addr);

			// recod the checking time
			if(!stack->wait_for_read_write_req_start_cycle)
				stack->wait_for_read_write_req_start_cycle = esim_cycle();
			
			older_stack = mod_in_flight_read_write_req_address(target_mod, stack->addr, stack);

			if(older_stack)
			{
				mem_debug("    %lld wait for read write request %lld\n",
					stack->id, older_stack->id);
				
				// Update the waiting counter where a read write request waits for a read write request.	
				target_mod->read_write_req_waiting_for_read_write_req++;

				mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_READ_REQUEST_RECEIVE);
				return;
			}
			
			if(!stack->wait_for_read_write_req_end_cycle)
			{	
				stack->wait_for_read_write_req_end_cycle = esim_cycle();
				stack->wait_for_read_write_req_cycle = stack->wait_for_read_write_req_end_cycle - stack->wait_for_read_write_req_start_cycle;
			}

			// recod the checking time
			if(!stack->wait_for_evict_req_start_cycle)
				stack->wait_for_evict_req_start_cycle = esim_cycle();
			
			older_stack = mod_in_flight_evict_address(target_mod, stack->addr, stack);

			if(older_stack)
			{
				mem_debug("    %lld wait for evict request %lld\n",
					stack->id, older_stack->id);
				
				// Update the waiting counter where a read write request waits for a evict request.	
				target_mod->read_write_req_waiting_for_evict_req++;

				mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_READ_REQUEST_RECEIVE);
				return;
			}
			
			if(!stack->wait_for_evict_req_end_cycle)
			{	
				stack->wait_for_evict_req_end_cycle = esim_cycle();
				stack->wait_for_evict_req_cycle = stack->wait_for_evict_req_end_cycle - stack->wait_for_evict_req_start_cycle;
			}
		}
		
		// At this point whether it waits or not, it'll clear all dependencies and this is time to record the end cycle time.
		stack->read_write_evict_du_req_end_cycle = esim_cycle();
		stack->read_write_evict_du_req_cycle = stack->read_write_evict_du_req_end_cycle - stack->read_write_evict_du_req_start_cycle;

		if(stack->request_dir == mod_request_down_up) 
			mod_update_snoop_waiting_cycle_counters(target_mod, stack, mod_trans_downup_read_request);
		else
			mod_update_snoop_waiting_cycle_counters(target_mod, stack, mod_trans_read_request);

		/* Find and lock */
		new_stack = mod_stack_create(stack->id, target_mod, stack->addr,
			EV_MOD_NMOESI_READ_REQUEST_ACTION, stack);
		new_stack->orig_mod_id = target_mod->mod_id;
		new_stack->issue_mod_id = stack->issue_mod_id;
		new_stack->blocking = stack->request_dir == mod_request_down_up;
		new_stack->read = 1;
		new_stack->retry = 0;
		if(stack->downup_read_request)
			new_stack->downup_read_request = 1;
		esim_schedule_event(EV_MOD_NMOESI_FIND_AND_LOCK, new_stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_READ_REQUEST_ACTION)
	{
		mem_debug("  %lld %lld 0x%x %s read request action\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:read_request_action\"\n",
			stack->id, target_mod->name);

		/* Check block locking error. If read request is down-up, there should not
		 * have been any error while locking. */
		if (stack->err)
		{
			assert(stack->request_dir == mod_request_up_down);
			ret->err = 1;
			mod_stack_set_reply(ret, reply_ack_error);
			stack->reply_size = 8;
			esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST_REPLY, stack, 0);
			return;
		}
		esim_schedule_event(stack->request_dir == mod_request_up_down ?
			EV_MOD_NMOESI_READ_REQUEST_UPDOWN : EV_MOD_NMOESI_READ_REQUEST_DOWNUP, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_READ_REQUEST_UPDOWN)
	{
		struct mod_t *owner;

		mem_debug("  %lld %lld 0x%x %s read request updown\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:read_request_updown\"\n",
			stack->id, target_mod->name);

		stack->pending = 1;

		/* Set the initial reply message and size.  This will be adjusted later if
		 * a transfer occur between peers. */
		stack->reply_size = mod->block_size + 8;
		mod_stack_set_reply(stack, reply_ack_data);

		// For a snoop protocol we need to monitor that first all snoops give response before we send a request to lower level module.
		// With this optimization where an inclusive cache is being taken into account if the lower level module doesnt have that block then its upper level sharers donot have that entry.
		if (stack->state)
		{
			/* Status = M/O/E/S/N
			 * Check: address is a multiple of requester's block_size
			 * Check: no sub-block requested by mod is already owned by mod */
			assert(stack->addr % mod->block_size == 0);
			/* TODO If there is only sharers, should one of them
			 *      send the data to mod instead of having target_mod do it? */

			/* Send read request to owners other than mod for all sub-blocks. */
			if(target_mod->num_nodes)
			{
				for(z=0; z<target_mod->num_sub_blocks; z++)
				{
					struct net_node_t *node;
					struct mod_t		  *sharer;

					cache_entry_tag = stack->tag + z * target_mod->sub_block_size;
					
					for(int i=0; i<target_mod->num_nodes; i++)
					{
						/* Get sharer mod */
						node = list_get(target_mod->high_net->node_list, i);

						// The request has not to be sent for the following cases.
						// 1.) The down-up write back request needn't be sent to the issuing module (Module that started the transaction) itself.
						// 2.) Node should be an end node.
						// 3.) Node shouldn't be the target or issuing request module itself.
						if(node->kind != net_node_end)
							continue;

						sharer = node->user_data;
						assert(sharer);

						if(sharer->mod_id == target_mod->mod_id)
							continue;

						if(sharer->mod_id == stack->orig_mod_id)
							continue;

						// if(sharer->mod_id == stack->issue_mod_id)
						// 	continue;

						/* Not the first sub-block */
						if (cache_entry_tag % sharer->block_size)
							continue;

						/* Send read request */
						stack->pending++;
						new_stack = mod_stack_create(stack->id, target_mod, cache_entry_tag,
							EV_MOD_NMOESI_READ_REQUEST_UPDOWN_FINISH, stack);
//----	-------------------------------------------
						/* Only set peer if its a subblock that was requested */
						// if (cache_entry_tag >= stack->addr && 
						// 	  cache_entry_tag < stack->addr + mod->block_size)
						// {
						// 	new_stack->peer = mod_stack_set_peer(mod, stack->state);
						// }
//----	-------------------------------------------
						new_stack->orig_mod_id = target_mod->mod_id;
						new_stack->target_mod = sharer;
						new_stack->request_dir = mod_request_down_up;
						new_stack->downup_read_request = 1;
						esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST, new_stack, 0);
					}
				}
			}
			esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST_UPDOWN_FINISH, stack, 0);

			/* The prefetcher may have prefetched this earlier and hence
			 * this is a hit now. Let the prefetcher know of this hit
			 * since without the prefetcher, this may have been a miss. 
			 * TODO: I'm not sure how relavant this is here for all states. */
			prefetcher_access_hit(stack, target_mod);
		}
		else
		{
			// Update counter for generation of a Up-down read request
			target_mod->updown_read_requests_generated++;

			/* State = I */
			new_stack = mod_stack_create(stack->id, target_mod, stack->tag,
				EV_MOD_NMOESI_READ_REQUEST_UPDOWN_MISS, stack);
			new_stack->orig_mod_id = target_mod->mod_id;
			new_stack->issue_mod_id = stack->issue_mod_id;
			/* Peer is NULL since we keep going up-down */
			new_stack->target_mod = mod_get_low_mod(target_mod, stack->tag);
			new_stack->request_dir = mod_request_up_down;
			new_stack->read = 1;

			esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST, new_stack, 0);

			/* The prefetcher may be interested in this miss */
			prefetcher_access_miss(stack, target_mod);

		}
		return;
	}

	if (event == EV_MOD_NMOESI_READ_REQUEST_UPDOWN_MISS)
	{
		int next_state;

		mem_debug("  %lld %lld 0x%x %s read request updown miss\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:read_request_updown_miss\"\n",
			stack->id, target_mod->name);

		/* Check error */
		if (stack->err)
		{
			cache_entry_unlock(target_mod->cache, stack->set, stack->way);
			ret->err = 1;
			mod_stack_set_reply(ret, reply_ack_error);
			stack->reply_size = 8;
			esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST_REPLY, stack, 0);
			return;
		}

		/* Set block state to excl/shared depending on the return value 'shared'
		 * that comes from a read request into the next cache level.
		 * Also set the tag of the block. */
		// MOESI Protocol

		next_state = cache_block_next_state(stack->shared, stack->dirty);

		cache_set_block(target_mod->cache, stack->set, stack->way, stack->tag, next_state);

		mod_update_state_modification_counters(target_mod, stack->prev_state, next_state, mod_trans_load);

		esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST_UPDOWN_FINISH, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_READ_REQUEST_UPDOWN_FINISH)
	{
		int shared;
		int next_state;

		/* Ensure that a reply was received */
		assert(stack->reply);

		/* Ignore while pending requests */
		assert(stack->pending > 0);
		stack->pending--;
		if (stack->pending)
			return;

		/* Trace */
		mem_debug("  %lld %lld 0x%x %s read request updown finish\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:read_request_updown_finish\"\n",
			stack->id, target_mod->name);
		
		// For a snoop based protocol we assume no peer to peer transfer and hence we always send data for an updown request.
		if(stack->reply_size >= 8)
		{
			mod_stack_set_reply(ret, reply_ack_data);
// ?? mod or traget_mod
			ret->reply_size = target_mod->block_size + 8;
		}
		else
		{
			fatal("Invalid reply size: %d", stack->reply_size);
		}

		shared = 0;

		/* For each sub-block requested by mod, set mod as sharer, and
		 * check whether there is other cache sharing it. */
		for (z = 0; z < target_mod->num_sub_blocks; z++)
		{
			cache_entry_tag = stack->tag + z * target_mod->sub_block_size;
			if (cache_entry_tag < stack->addr || cache_entry_tag >= stack->addr + mod->block_size)
				continue;
			if (stack->nc_write || stack->shared)
				shared = 1;

			/* If the block is owned, non-coherent, or shared,  
			 * mod (the higher-level cache) should never be exclusive */
			if (stack->state == cache_block_owned || 
				stack->state == cache_block_noncoherent ||
				stack->state == cache_block_shared )
				shared = 1;
		}

		/* If no sub-block requested by mod is shared by other cache, set mod
		 * as owner of all of them. Otherwise, notify requester that the block is
		 * shared by setting the 'shared' return value to true. */
		ret->shared = shared;

		// Fix it properly. This is a case where an DOWNUP Request set the shared flag and the next state has to be updated to shared or owned accordingly.
		next_state = cache_block_next_state(shared, stack->dirty);
		if(shared)
		  cache_set_block(target_mod->cache, stack->set, stack->way, stack->tag, next_state);
		
		cache_entry_unlock(target_mod->cache, stack->set, stack->way);

		int latency = stack->reply == reply_ack_data_sent_to_peer ? 0 : target_mod->latency;
		esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST_REPLY, stack, latency);
		return;
	}

	if (event == EV_MOD_NMOESI_READ_REQUEST_DOWNUP)
	{
		struct mod_t *owner;

		mem_debug("  %lld %lld 0x%x %s read request downup\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:read_request_downup\"\n",
			stack->id, target_mod->name);

		/* Check: state must not be invalid or shared.
		 * By default, only one pending request.
		 * Response depends on state */
		assert(stack->state != cache_block_invalid);
		assert(stack->state != cache_block_shared);
		assert(stack->state != cache_block_noncoherent);
		stack->pending = 1;

		/* Send a read request to the owner of each subblock. */
		// In place of owner, for a snoop based protocol request has to be sent to all upper level nodes.
		// dir = target_mod->dir;
		if(target_mod->num_nodes)
		{
			for (z = 0; z < target_mod->num_sub_blocks; z++)
			{
				struct net_node_t *node;
				struct mod_t *sharer;

				cache_entry_tag = stack->tag + z * target_mod->sub_block_size;
				assert(cache_entry_tag < stack->tag + target_mod->block_size);

				for(int i=0; i<target_mod->num_nodes; i++)
				{
					/* Get owner mod */
					node = list_get(target_mod->high_net->node_list, i);

					// The request has not to be sent for the following cases.
					// 1.) The down-up write back request needn't be sent to the issuing module (Module that started the transaction) itself.
					// 2.) Node should be an end node.
					// 3.) Node shouldn't be the target or issuing request module itself.
					if(node->kind != net_node_end)
						continue;

					sharer = node->user_data;

					if(sharer->mod_id == target_mod->mod_id)
						continue;

					if(sharer->mod_id == stack->orig_mod_id)
						continue;

					/* Not the first sub-block */
					if (cache_entry_tag % sharer->block_size)
						continue;

					stack->pending++;
					new_stack = mod_stack_create(stack->id, target_mod, cache_entry_tag,
						EV_MOD_NMOESI_READ_REQUEST_DOWNUP_WAIT_FOR_REQS, stack);
					new_stack->orig_mod_id = target_mod->mod_id;
					new_stack->issue_mod_id = stack->issue_mod_id;
					new_stack->target_mod = sharer;
					new_stack->request_dir = mod_request_down_up;
					new_stack->downup_read_request = 1;
					esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST, new_stack, 0);
				}
			}
		}

		esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST_DOWNUP_WAIT_FOR_REQS, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_READ_REQUEST_DOWNUP_WAIT_FOR_REQS)
	{
		/* Ignore while pending requests */
		assert(stack->pending > 0);
		stack->pending--;
		if (stack->pending)
			return;

		mem_debug("  %lld %lld 0x%x %s read request downup wait for reqs\n", 
			esim_time, stack->id, stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:read_request_downup_wait_for_reqs\"\n",
			stack->id, target_mod->name);


		/* Look for block. */
		stack->hit = mod_find_block(target_mod, stack->addr, &stack->set, &stack->way, &stack->tag, &stack->state);

		if(stack->hit)
		{
			mod_stack_set_reply(stack, reply_ack_data);
			stack->reply_size = mod->block_size+8;
		}
		else
		{
			mod_stack_set_reply(stack, reply_ack);
			stack->reply_size = 8;
		}

		
		// For a snoop based protocol there is no peer-peer data transfer.
		esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST_DOWNUP_FINISH, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_READ_REQUEST_DOWNUP_FINISH)
	{
		int next_state;

		mem_debug("  %lld %lld 0x%x %s read request downup finish\n", 
			esim_time, stack->id, stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:read_request_downup_finish\"\n",
			stack->id, target_mod->name);
		
		// For a Snoop based protocol, we adopt the following convention, in case of a valid state, data is always sent for a down-up request, no peer to peer transfer takes place and for an invalid state only ack is sent.
		// State Modifications are M,O -> O, E,S -> S, NC -> NC, I -> I.
		if(stack->state)
		{
			mod_stack_set_reply(stack, reply_ack_data);
			stack->reply_size = target_mod->block_size + 8;
			stack->reply_size = target_mod->block_size + 8;
		}
		else
		{
			mod_stack_set_reply(stack, reply_ack);
			stack->reply_size = 8;
		}

		switch(stack->state)
		{
			case cache_block_exclusive   : next_state = cache_block_shared; break;
			case cache_block_shared      : next_state = cache_block_shared; break;
			case cache_block_modified    : next_state = cache_block_owned; break;
			case cache_block_owned       : next_state = cache_block_owned; break;
			case cache_block_noncoherent : next_state = cache_block_noncoherent; break;
		}

		//if(stack->state && (stack->way >= 0) && (stack->way < target_mod->cache->assoc))
		if(stack->state)
		{
			cache_set_block(target_mod->cache, stack->set, stack->way, 
					stack->tag, next_state);
		
			cache_entry_unlock(target_mod->cache, stack->set, stack->way);
			// May be this is fix for coherency loss
			ret->shared = 1;

			if((stack->state == cache_block_modified) || (stack->state == cache_block_owned))
				ret->dirty = 1;
		}

		mod_update_state_modification_counters(target_mod, stack->prev_state, next_state, mod_trans_downup_read_request);

		int latency = stack->reply == reply_ack_data_sent_to_peer ? 0 : target_mod->latency;
		esim_schedule_event(EV_MOD_NMOESI_READ_REQUEST_REPLY, stack, latency);
		return;
	}

	if (event == EV_MOD_NMOESI_READ_REQUEST_REPLY)
	{
		struct net_t *net;
		struct net_node_t *src_node;
		struct net_node_t *dst_node;

		mem_debug("  %lld %lld 0x%x %s read request reply\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:read_request_reply\"\n",
			stack->id, target_mod->name);

		/* Checks */
		assert(stack->reply_size);
		assert(stack->request_dir);
		assert(mod_get_low_mod(mod, stack->addr) == target_mod ||
			mod_get_low_mod(target_mod, stack->addr) == mod);

		/* Get network and nodes */
		if (stack->request_dir == mod_request_up_down)
		{
			net = mod->low_net;
			src_node = target_mod->high_net_node;
			dst_node = mod->low_net_node;
		}
		else
		{
			net = mod->high_net;
			src_node = target_mod->low_net_node;
			dst_node = mod->high_net_node;
		}
		
		// record the start time of the network access
		if(!stack->nw_send_reply_latency_start_cycle)
			stack->nw_send_reply_latency_start_cycle = esim_cycle();

		/* Send message */
		stack->msg = net_try_send_ev(net, src_node, dst_node, stack->reply_size,
			EV_MOD_NMOESI_READ_REQUEST_FINISH, stack, event, stack);
		
		if(!stack->msg)
		{
			if(stack->downup_read_request) target_mod->downup_read_send_replies_retried_nw++;
			else 													 target_mod->read_send_replies_retried_nw++;
		}
		else
		{
			stack->nw_send_reply_latency_end_cycle = esim_cycle();
			stack->nw_send_reply_latency_cycle     = stack->nw_send_request_latency_end_cycle - stack->nw_send_reply_latency_start_cycle;
			if(stack->nw_send_reply_latency_cycle)
			{
				enum mod_trans_type_t trans_type = (stack->downup_read_request) ? mod_trans_downup_read_request: mod_trans_read_request;
				mod_update_nw_send_reply_delay_counters(target_mod, stack, trans_type);
			}
		}

		return;
	}

	if (event == EV_MOD_NMOESI_READ_REQUEST_FINISH)
	{
		enum mod_trans_type_t trans_type;

		mem_debug("  %lld %lld 0x%x %s read request finish\n", esim_time, stack->id,
			stack->tag, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:read_request_finish\"\n",
			stack->id, mod->name);
		
		// record the start time of network access
		if(!stack->nw_receive_reply_latency_start_cycle)
			stack->nw_receive_reply_latency_start_cycle = esim_cycle();

		/* Receive message */
		if (stack->request_dir == mod_request_up_down)
			net_receive(mod->low_net, mod->low_net_node, stack->msg);
		else
			net_receive(mod->high_net, mod->high_net_node, stack->msg);
		
		trans_type = (stack->downup_read_request) ? mod_trans_downup_read_request: mod_trans_read_request;

		stack->nw_receive_reply_latency_end_cycle = esim_cycle();
		stack->nw_receive_reply_latency_cycle     = stack->nw_receive_reply_latency_end_cycle - stack->nw_receive_reply_latency_start_cycle;
		if(!stack->nw_receive_reply_latency_cycle) mod_update_nw_receive_reply_delay_counters(mod, stack, trans_type);
		
		
		// Update the Tag, Set, Way of the return access here
		if(stack->request_dir != mod_request_down_up)
		{
			if(!stack->err)
			{
				cache_set_block(mod->cache, ret->set, ret->way,	ret->tag, cache_block_invalid);
			}
			stack->read_request_in_progress = 0;
		}
		
		// Update the controller occupancy statistics
		if(stack->request_dir == mod_request_up_down)
		{
			target_mod->num_load_requests--;
			mod_update_request_counters(target_mod, mod_trans_load);

			mod->num_read_requests--;
			mod_update_request_counters(mod, mod_trans_read_request);
		}
		else
		{
			target_mod->num_downup_read_requests--;
			mod_update_request_counters(target_mod, mod_trans_downup_read_request);
		}

		stack->access_end_cycle = esim_cycle();
		stack->access_latency   = stack->access_end_cycle - stack->access_start_cycle;

		if(stack->request_dir == mod_request_up_down)
			mod_update_latency_counters(mod, stack->access_latency, mod_trans_read_request);
		if(stack->request_dir == mod_request_down_up)
			mod_update_latency_counters(mod, stack->access_latency, mod_trans_downup_read_request);

		if(stack->request_dir == mod_request_down_up)
		{
			mod_downup_access_finish(target_mod, stack);
			mod_update_request_queue_statistics(target_mod);
			mod_check_dependency_depth(target_mod, stack, mod_trans_downup_read_request, stack->addr);
		}
		else
		{
			mod_read_write_req_access_finish(target_mod, stack);
			mod_update_request_queue_statistics(target_mod);
			mod_check_dependency_depth(target_mod, stack, mod_trans_read_request, stack->addr);
		}
		/* Return */
		mod_stack_return(stack);
		return;
	}

	abort();
}


void mod_handler_nmoesi_write_request(int event, void *data)
{
	struct mod_stack_t *stack = data;
	struct mod_stack_t *ret = stack->ret_stack;
	struct mod_stack_t *new_stack;

	struct mod_t *mod = stack->mod;
	struct mod_t *target_mod = stack->target_mod;

	uint32_t cache_entry_tag, z;

	if (event == EV_MOD_NMOESI_WRITE_REQUEST)
	{
		struct net_t *net;
		struct net_node_t *src_node;
		struct net_node_t *dst_node;

		mem_debug("  %lld %lld 0x%x %s write request\n", esim_time, stack->id,
			stack->addr, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:write_request\"\n",
			stack->id, mod->name);

		/* Default return values */
		ret->err = 0;

		if(!ret->dirty) ret->dirty = 0;
		
		stack->access_start_cycle = esim_cycle();

		/* For write requests, we need to set the initial reply size because
		 * in updown, peer transfers must be allowed to decrease this value
		 * (during invalidate). If the request turns out to be downup, then 
		 * these values will get overwritten. */
		stack->reply_size = mod->block_size + 8;
		mod_stack_set_reply(stack, reply_ack_data);

		/* Checks */
		assert(stack->request_dir);
		assert(mod_get_low_mod(mod, stack->addr) == target_mod ||
			stack->request_dir == mod_request_down_up);
		assert(mod_get_low_mod(target_mod, stack->addr) == mod ||
			stack->request_dir == mod_request_up_down);

		/* Get source and destination nodes */
		if (stack->request_dir == mod_request_up_down)
		{
			net = mod->low_net;
			src_node = mod->low_net_node;
			dst_node = target_mod->high_net_node;
		}
		else
		{
			net = mod->high_net;
			src_node = mod->high_net_node;
			dst_node = target_mod->low_net_node;
		}
		
		// record the network access start time
		if(!stack->nw_send_request_latency_start_cycle)
			stack->nw_send_request_latency_start_cycle = esim_cycle();

		/* Send message */
		stack->msg = net_try_send_ev(net, src_node, dst_node, 8,
			EV_MOD_NMOESI_WRITE_REQUEST_RECEIVE, stack, event, stack);

		if(!stack->msg)
		{
			if(stack->evict_trans) mod->downup_eviction_send_requests_retried_nw++;
			
			if(stack->downup_writeback_request) mod->downup_writeback_send_requests_retried_nw++;
			else mod->writeback_send_requests_retried_nw++;
		}
		else
		{
			stack->nw_send_request_latency_end_cycle = esim_cycle();
			stack->nw_send_request_latency_cycle     = stack->nw_send_request_latency_end_cycle - stack->nw_send_request_latency_start_cycle;
			if(stack->nw_send_request_latency_cycle)
			{
				enum mod_trans_type_t trans_type = (stack->downup_writeback_request) ? mod_trans_downup_writeback_request: mod_trans_writeback;

				if(stack->evict_trans) mod_update_nw_send_request_delay_counters(mod, stack, mod_trans_downup_read_request);

				mod_update_nw_send_request_delay_counters(mod, stack, trans_type);
			}
		}

		return;
	}

	if (event == EV_MOD_NMOESI_WRITE_REQUEST_RECEIVE)
	{
		enum mod_trans_type_t trans_type;
		enum mod_trans_type_t trans_type_for_nw;

		mem_debug("  %lld %lld 0x%x %s write request receive\n", esim_time, stack->id,
			stack->addr, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:write_request_receive\"\n",
			stack->id, target_mod->name);
		
		if(!stack->nw_receive_request_latency_start_cycle)
			stack->nw_receive_request_latency_start_cycle = esim_cycle();

		/* Receive message */
		if (stack->request_dir == mod_request_up_down)
		{
			if(!stack->updown_access_registered)
			{
				net_receive(target_mod->high_net, target_mod->high_net_node, stack->msg);
				stack->read_write_evict_du_req_start_cycle = esim_cycle();
			}
		}
		else
		{
			if(!stack->downup_access_registered)
			{
				net_receive(target_mod->low_net, target_mod->low_net_node, stack->msg);
				stack->read_write_evict_du_req_start_cycle = esim_cycle();
			}
		}
		
		trans_type_for_nw = (stack->downup_writeback_request) ? mod_trans_downup_writeback_request : mod_trans_writeback;

		stack->nw_receive_request_latency_end_cycle = esim_cycle();
		stack->nw_receive_request_latency_cycle			= stack->nw_receive_request_latency_end_cycle - stack->nw_receive_request_latency_start_cycle;
		if(stack->nw_receive_request_latency_cycle)
 		{
			if(stack->evict_trans)
				mod_update_nw_receive_request_delay_counters(target_mod, stack, mod_trans_downup_eviction_request);

			mod_update_nw_receive_request_delay_counters(target_mod, stack, trans_type_for_nw);
		}

		if(stack->request_dir == mod_request_up_down)
		{
			// This is done here to ensure that any network retries aren't counted again
			mod->num_writeback_requests++;
			mod_update_request_counters(mod, mod_trans_writeback);

			target_mod->num_store_requests++;
			mod_update_request_counters(target_mod, mod_trans_store);
		}
		else
		{
			// If the access was result of WB due to eviction then update eviction requests and if due to a store request or writeback due to store then update writeback requests.
			if(stack->invalidate_eviction)
			{
				target_mod->num_downup_eviction_requests++;
				mod_update_request_counters(target_mod, mod_trans_downup_eviction_request);
			}

			if(stack->wb_store)
			{
				target_mod->num_downup_writeback_requests++;
				mod_update_request_counters(target_mod, mod_trans_downup_writeback_request);
			}
		}

		if(stack->downup_writeback_request)
			trans_type = mod_trans_downup_writeback_request;
		else
			trans_type = mod_trans_store;

		// Update the simultaneous access request counters
		mod_update_simultaneous_flight_access_counters(target_mod, stack->addr, stack, trans_type);

		if(stack->evict_trans)
		{
			mod_update_simultaneous_flight_access_counters(target_mod, stack->addr, stack, mod_trans_downup_eviction_request);
		}


		// Current order to make them in order
		if(stack->request_dir == mod_request_down_up)
		{
			struct mod_stack_t *older_stack;
			
			if(!stack->downup_access_registered)
			{
				mod_downup_access_start(target_mod, stack, mod_access_invalid);
			}
			
			mod_check_dependency_depth(target_mod, stack, mod_trans_downup_writeback_request, stack->addr);

			mod_update_request_queue_statistics(target_mod);

			// recod the checking time
			if(!stack->wait_for_evict_req_start_cycle)
				stack->wait_for_evict_req_start_cycle = esim_cycle();

			older_stack = mod_in_flight_evict_address(mod, stack->addr, stack);

			if(older_stack)
			{
				mem_debug("    %lld wait for evict request %lld\n",
					stack->id, older_stack->id);

				// Update the waiting counter where a downup request waits for evict request.	
				mod->downup_req_waiting_to_be_sent_for_evict_req++;

				mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_WRITE_REQUEST_RECEIVE);
				return;
			}

			if(!stack->wait_for_evict_req_end_cycle)
			{	
				stack->wait_for_evict_req_end_cycle = esim_cycle();
				stack->wait_for_evict_req_cycle = stack->wait_for_evict_req_end_cycle - stack->wait_for_evict_req_start_cycle;
			}

			// recod the checking time
			if(!stack->wait_for_read_write_req_start_cycle)
				stack->wait_for_read_write_req_start_cycle = esim_cycle();

			older_stack = mod_check_in_flight_address_dependency_for_downup_request(mod, stack->addr, stack);

			if(older_stack)
			{
				mem_debug("    %lld wait for read write request %lld\n",
					stack->id, older_stack->id);

				// Update the waiting counter where a downup request waits for read write request.	
				mod->downup_req_waiting_to_be_sent_for_read_write_req++;

				mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_WRITE_REQUEST_RECEIVE);
				return;
			}

			if(!stack->wait_for_read_write_req_end_cycle)
			{	
				stack->wait_for_read_write_req_end_cycle = esim_cycle();
				stack->wait_for_read_write_req_cycle = stack->wait_for_read_write_req_end_cycle - stack->wait_for_read_write_req_start_cycle;
			}

			// recod the checking time
			if(!stack->wait_for_downup_req_start_cycle)
				stack->wait_for_downup_req_start_cycle = esim_cycle();

			older_stack = stack->downup_access_list_prev;

			if (older_stack)
			{
				mem_debug("    %lld wait for downup read request %lld\n",
					stack->id, older_stack->id);
				// Update the waiting counter where a downup request waits for read write request.	
				target_mod->downup_req_waiting_to_be_processed_for_downup_req++;
				
				mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_WRITE_REQUEST_RECEIVE);
				return;
			}

			if(!stack->wait_for_downup_req_end_cycle)
			{	
				stack->wait_for_downup_req_end_cycle = esim_cycle();
				stack->wait_for_downup_req_cycle = stack->wait_for_downup_req_end_cycle - stack->wait_for_downup_req_start_cycle;
			}

		}
		else
		{
			struct mod_stack_t *older_stack;

			if(!stack->updown_access_registered)
			{
				mod_read_write_req_access_start(target_mod, stack, mod_access_invalid);
			}
			
			mod_check_dependency_depth(target_mod, stack, mod_trans_writeback, stack->addr);

			mod_update_request_queue_statistics(target_mod);

			older_stack = mod_in_flight_read_write_req_address(target_mod, stack->addr, stack);

			if(older_stack)
			{
				mem_debug("    %lld wait for read write request %lld\n",
					stack->id, older_stack->id);
				
				mod->write_req_retry++;

				ret->err = 1;
				stack->reply_size = 8;
				esim_schedule_event(EV_MOD_NMOESI_WRITE_REQUEST_REPLY, stack, 0);
				return;
			}
			
			// recod the checking time
			if(!stack->wait_for_evict_req_start_cycle)
				stack->wait_for_evict_req_start_cycle = esim_cycle();

			older_stack = mod_in_flight_evict_address(target_mod, stack->addr, stack);

			if(older_stack)
			{
				mem_debug("    %lld wait for evict request %lld\n",
					stack->id, older_stack->id);
				
				// Update the waiting counter where a read write request waits for a read write request.	
				target_mod->read_write_req_waiting_for_read_write_req++;

				mod_stack_wait_in_stack(stack, older_stack, EV_MOD_NMOESI_WRITE_REQUEST_RECEIVE);
				return;
			}
			
			if(!stack->wait_for_evict_req_end_cycle)
			{	
				stack->wait_for_evict_req_end_cycle = esim_cycle();
				stack->wait_for_evict_req_cycle = stack->wait_for_evict_req_end_cycle - stack->wait_for_evict_req_start_cycle;
			}
		}

		// At this point whether it waits or not, it'll clear all dependencies and this is time to record the end cycle time. This records the response time of those requests that are aborted too.
		stack->read_write_evict_du_req_end_cycle = esim_cycle();
		stack->read_write_evict_du_req_cycle = stack->read_write_evict_du_req_end_cycle - stack->read_write_evict_du_req_start_cycle;

		if(stack->request_dir == mod_request_down_up) 
			mod_update_snoop_waiting_cycle_counters(target_mod, stack, mod_trans_downup_writeback_request);
		else
			mod_update_snoop_waiting_cycle_counters(target_mod, stack, mod_trans_writeback);


		/* Find and lock */
		new_stack = mod_stack_create(stack->id, target_mod, stack->addr,
			EV_MOD_NMOESI_WRITE_REQUEST_ACTION, stack);
		new_stack->orig_mod_id = target_mod->mod_id;
		new_stack->issue_mod_id = stack->issue_mod_id;
		new_stack->blocking = stack->request_dir == mod_request_down_up;
		new_stack->write = 1;
		new_stack->retry = 0;

		if(stack->downup_writeback_request)
			new_stack->downup_writeback_request = 1;
		if(stack->evict_trans)
			new_stack->evict_trans = 1;
		esim_schedule_event(EV_MOD_NMOESI_FIND_AND_LOCK, new_stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_WRITE_REQUEST_ACTION)
	{
		mem_debug("  %lld %lld 0x%x %s write request action\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:write_request_action\"\n",
			stack->id, target_mod->name);

		/* Check lock error. If write request is down-up, there should
		 * have been no error. */
		if (stack->err)
		{
			assert(stack->request_dir == mod_request_up_down);
			ret->err = 1;
			stack->reply_size = 8;
			esim_schedule_event(EV_MOD_NMOESI_WRITE_REQUEST_REPLY, stack, 0);
			return;
		}

		/* Invalidate the rest of upper level sharers */
		new_stack = mod_stack_create(stack->id, target_mod, 0,
			EV_MOD_NMOESI_WRITE_REQUEST_EXCLUSIVE, stack);
		new_stack->orig_mod_id = mod->mod_id;
		new_stack->issue_mod_id = stack->issue_mod_id;
		new_stack->except_mod = mod;
		new_stack->set = stack->set;
		new_stack->way = stack->way;
		// new_stack->peer = mod_stack_set_peer(stack->peer, stack->state);

		if(stack->invalidate_eviction)
			new_stack->invalidate_eviction = 1;
		// This is because if the writeback wasn't called by evict then it must be called by a store event.
		else
			new_stack->wb_store = 1;

		esim_schedule_event(EV_MOD_NMOESI_INVALIDATE, new_stack, 0);
		target_mod->sharer_req_for_invalidation++;
		return;
	}

	if (event == EV_MOD_NMOESI_WRITE_REQUEST_EXCLUSIVE)
	{
		mem_debug("  %lld %lld 0x%x %s write request exclusive\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:write_request_exclusive\"\n",
			stack->id, target_mod->name);

		if (stack->request_dir == mod_request_up_down)
			esim_schedule_event(EV_MOD_NMOESI_WRITE_REQUEST_UPDOWN, stack, 0);
		else
			esim_schedule_event(EV_MOD_NMOESI_WRITE_REQUEST_DOWNUP, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_WRITE_REQUEST_UPDOWN)
	{
		mem_debug("  %lld %lld 0x%x %s write request updown\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:write_request_updown\"\n",
			stack->id, target_mod->name);
		
		/* state = M/E */
		if (stack->state == cache_block_modified ||
			stack->state == cache_block_exclusive)
		{
			esim_schedule_event(EV_MOD_NMOESI_WRITE_REQUEST_UPDOWN_FINISH, stack, 0);
		}
		/* state = O/S/I/N */
		else if (stack->state == cache_block_owned || stack->state == cache_block_shared ||
			stack->state == cache_block_invalid || stack->state == cache_block_noncoherent)
		{
			// Update counter for generation of a Up-down WB request
			target_mod->updown_writeback_requests_generated++;

			new_stack = mod_stack_create(stack->id, target_mod, stack->tag,
				EV_MOD_NMOESI_WRITE_REQUEST_UPDOWN_FINISH, stack);
			// new_stack->peer = mod_stack_set_peer(mod, stack->state);
			new_stack->orig_mod_id = target_mod->mod_id;
			new_stack->target_mod = mod_get_low_mod(target_mod, stack->tag);
			new_stack->request_dir = mod_request_up_down;
			new_stack->write = 1;
			esim_schedule_event(EV_MOD_NMOESI_WRITE_REQUEST, new_stack, 0);

			if (stack->state == cache_block_invalid)
			{
				/* The prefetcher may be interested in this miss */
				prefetcher_access_miss(stack, target_mod);
			}
		}
		else 
		{
			fatal("Invalid cache block state: %d\n", stack->state);
		}

		if (stack->state != cache_block_invalid)
		{
			/* The prefetcher may have prefetched this earlier and hence
			 * this is a hit now. Let the prefetcher know of this hit
			 * since without the prefetcher, this may been a miss. 
			 * TODO: I'm not sure how relavant this is here for all states. */
			prefetcher_access_hit(stack, target_mod);
		}

		return;
	}

	if (event == EV_MOD_NMOESI_WRITE_REQUEST_UPDOWN_FINISH)
	{
		int next_state;

		mem_debug("  %lld %lld 0x%x %s write request updown finish\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:write_request_updown_finish\"\n",
			stack->id, target_mod->name);

		/* Ensure that a reply was received */
		assert(stack->reply);

		/* Error in write request to next cache level */
		if (stack->err)
		{
			ret->err = 1;
			mod_stack_set_reply(ret, reply_ack_error);
			stack->reply_size = 8;
			cache_entry_unlock(target_mod->cache, stack->set, stack->way);
			esim_schedule_event(EV_MOD_NMOESI_WRITE_REQUEST_REPLY, stack, 0);
			return;
		}

		/* Set state to exclusive */
		// MOESI Protocol, for a store request to proceed this entry has to be set as exclusive.
		next_state = cache_block_next_state(stack->shared, stack->dirty);

		cache_set_block(target_mod->cache, stack->set, stack->way, stack->tag, next_state);

		// No Peer-peer data transfer. Hence the data needs to be sent. However, UP-DOWN write request acts as CleanInvalid type of transaction thus we need to see when data needs to be sent and when not. Currenlty we send data for all transactions.
		if(stack->reply_size >= 8)
		{
			mod_stack_set_reply(ret, reply_ack_data);

			// Here the lower level module sends the data to the higher module in response to a store request form ahigher level module.
			stack->target_mod->data_transfer_updown_store_request++;
		}
		else
		{
			fatal("Invalid reply size: %d", stack->reply_size);
		}

		/* Unlock, reply_size is the data of the size of the requester's block. */
		cache_entry_unlock(target_mod->cache, stack->set, stack->way);

		mod_update_state_modification_counters(target_mod, stack->prev_state, next_state, mod_trans_store);

		int latency = stack->reply == reply_ack_data_sent_to_peer ? 0 : target_mod->latency;
		esim_schedule_event(EV_MOD_NMOESI_WRITE_REQUEST_REPLY, stack, latency);
		return;
	}

	if (event == EV_MOD_NMOESI_WRITE_REQUEST_DOWNUP)
	{
		mem_debug("  %lld %lld 0x%x %s write request downup\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:write_request_downup\"\n",
			stack->id, target_mod->name);

		/* Compute reply size */	
		if (stack->state == cache_block_exclusive || 
			stack->state == cache_block_shared) 
		{
			/* Exclusive and shared states send an ack */
			stack->reply_size = 8;
			mod_stack_set_reply(ret, reply_ack);
		}
		else if (stack->state == cache_block_noncoherent)
		{
			/* Non-coherent state sends data */
			stack->reply_size = target_mod->block_size + 8;
			mod_stack_set_reply(ret, reply_ack_data);

			// Update statistics for data transfer
			if(stack->invalidate_eviction)
				stack->target_mod->data_transfer_downup_eviction_request++;

			if(stack->wb_store)
				stack->target_mod->data_transfer_downup_store_request++;

		}
		else if (stack->state == cache_block_modified || 
			stack->state == cache_block_owned)
		{
			// No peer-peer data transfer for snoop based protocols
			mod_stack_set_reply(ret, reply_ack_data);
			stack->reply_size = target_mod->block_size + 8;

			// Setting the dirty flag
			ret->dirty = 1;

			// Update statistics for data transfer
			if(stack->invalidate_eviction)
				stack->target_mod->data_transfer_downup_eviction_request++;

			if(stack->wb_store)
				stack->target_mod->data_transfer_downup_store_request++;
		}
		else 
		{	
			// For snoop based protocol a Write Back can be sent to a Node having no copy in this case, this has to be treated as Exclusive or Shared.
			stack->reply_size = 8;
			mod_stack_set_reply(ret, reply_ack);
		}

		esim_schedule_event(EV_MOD_NMOESI_WRITE_REQUEST_DOWNUP_FINISH, stack, 0);

		return;
	}

	if (event == EV_MOD_NMOESI_WRITE_REQUEST_DOWNUP_FINISH)
	{
		int next_state;
		mem_debug("  %lld %lld 0x%x %s write request downup complete\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:write_request_downup_finish\"\n",
			stack->id, target_mod->name);

		/* Set state to I, unlock*/
		// For a snoop based protocol Invalidation has to be done for entries that are present.
		if(stack->state)
		{
			cache_set_block(target_mod->cache, stack->set, stack->way, 0, cache_block_invalid);
			next_state = cache_block_invalid;
			cache_entry_unlock(target_mod->cache, stack->set, stack->way);
			
			mod_update_state_modification_counters(target_mod, stack->prev_state, next_state, mod_trans_downup_writeback_request);	
		}

		int latency = ret->reply == reply_ack_data_sent_to_peer ? 0 : target_mod->latency;
		esim_schedule_event(EV_MOD_NMOESI_WRITE_REQUEST_REPLY, stack, latency);
		return;
	}

	if (event == EV_MOD_NMOESI_WRITE_REQUEST_REPLY)
	{
		struct net_t *net;
		struct net_node_t *src_node;
		struct net_node_t *dst_node;

		mem_debug("  %lld %lld 0x%x %s write request reply\n", esim_time, stack->id,
			stack->tag, target_mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:write_request_reply\"\n",
			stack->id, target_mod->name);

		/* Checks */
		assert(stack->reply_size);
		assert(mod_get_low_mod(mod, stack->addr) == target_mod ||
			mod_get_low_mod(target_mod, stack->addr) == mod);

		/* Get network and nodes */
		if (stack->request_dir == mod_request_up_down)
		{
			net = mod->low_net;
			src_node = target_mod->high_net_node;
			dst_node = mod->low_net_node;
		}
		else
		{
			net = mod->high_net;
			src_node = target_mod->low_net_node;
			dst_node = mod->high_net_node;
		}
		
		// record the start time of the network access
		if(!stack->nw_send_reply_latency_start_cycle)
			stack->nw_send_reply_latency_start_cycle = esim_cycle();

		stack->msg = net_try_send_ev(net, src_node, dst_node, stack->reply_size,
			EV_MOD_NMOESI_WRITE_REQUEST_FINISH, stack, event, stack);

		if(!stack->msg)
		{
			if(stack->evict_trans) target_mod->downup_eviction_send_replies_retried_nw++; 
			
			if(stack->downup_writeback_request) target_mod->downup_writeback_send_replies_retried_nw++;
			else target_mod->writeback_send_replies_retried_nw++;
		}
		else
		{
			stack->nw_send_reply_latency_end_cycle = esim_cycle();
			stack->nw_send_reply_latency_cycle     = stack->nw_send_reply_latency_end_cycle - stack->nw_send_reply_latency_start_cycle;
			if(stack->nw_send_reply_latency_cycle)
			{
				enum mod_trans_type_t trans_type = (stack->downup_writeback_request) ? mod_trans_downup_writeback_request: mod_trans_writeback;

				mod_update_nw_send_reply_delay_counters(target_mod, stack, trans_type);

				if(stack->evict_trans)
					mod_update_nw_send_reply_delay_counters(target_mod, stack, mod_trans_downup_eviction_request);
			}
		}

		return;
	}

	if (event == EV_MOD_NMOESI_WRITE_REQUEST_FINISH)
	{
		enum mod_trans_type_t trans_type;

		mem_debug("  %lld %lld 0x%x %s write request finish\n", esim_time, stack->id,
			stack->tag, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:write_request_finish\"\n",
			stack->id, mod->name);
		
		if(!stack->nw_receive_reply_latency_start_cycle)
			stack->nw_receive_reply_latency_start_cycle = esim_cycle();

		/* Receive message */
		if (stack->request_dir == mod_request_up_down)
		{
			net_receive(mod->low_net, mod->low_net_node, stack->msg);
		}
		else
		{
			net_receive(mod->high_net, mod->high_net_node, stack->msg);
		}
		
		// Update the Tag, Set, Way of the return access here
		if(stack->request_dir != mod_request_down_up)
		{
			if(!stack->err)
			{
				cache_set_block(mod->cache, ret->set, ret->way,	ret->tag, cache_block_invalid);
			}
			stack->write_request_in_progress = 0;
		}
		
		trans_type = (stack->downup_writeback_request) ? mod_trans_downup_writeback_request : mod_trans_writeback;

		stack->nw_receive_reply_latency_end_cycle = esim_cycle();
		stack->nw_receive_reply_latency_cycle			= stack->nw_receive_reply_latency_end_cycle - stack->nw_receive_reply_latency_start_cycle;
		if(stack->nw_receive_reply_latency_cycle)
 		{
			mod_update_nw_receive_reply_delay_counters(mod, stack, trans_type);

			if(stack->evict_trans)
				mod_update_nw_receive_reply_delay_counters(mod, stack, mod_trans_downup_eviction_request);
		}
		
		// Update the controller occupancy statistics
		if(stack->request_dir == mod_request_up_down)
		{
			mod->num_writeback_requests--;
			mod_update_request_counters(mod, mod_trans_writeback);

			target_mod->num_store_requests--;		
			mod_update_request_counters(target_mod, mod_trans_store);
		}
		else
		{
			if(stack->invalidate_eviction)
			{
				target_mod->num_downup_eviction_requests--;
				mod_update_request_counters(target_mod, mod_trans_downup_eviction_request);
			}

			if(stack->wb_store)
			{
				target_mod->num_downup_writeback_requests--;
				mod_update_request_counters(target_mod, mod_trans_downup_writeback_request);
			}
		}

		/* Return */
		
		stack->access_end_cycle = esim_cycle();
		stack->access_latency   = stack->access_end_cycle - stack->access_start_cycle;

		if(stack->request_dir == mod_request_up_down)
			mod_update_latency_counters(mod, stack->access_latency, mod_trans_writeback);
		if(stack->request_dir == mod_request_down_up)
			mod_update_latency_counters(mod, stack->access_latency, mod_trans_downup_writeback_request);

		if(stack->request_dir == mod_request_down_up)
		{
			mod_downup_access_finish(target_mod, stack);
			mod_update_request_queue_statistics(target_mod);
			mod_check_dependency_depth(target_mod, stack, mod_trans_downup_writeback_request, stack->addr);
		}
		else
		{
			mod_read_write_req_access_finish(target_mod, stack);
			mod_update_request_queue_statistics(target_mod);
			mod_check_dependency_depth(target_mod, stack, mod_trans_writeback, stack->addr);
		}

		mod_stack_return(stack);
		return;
	}

	abort();
}


void mod_handler_nmoesi_peer(int event, void *data)
{
	struct mod_stack_t *stack = data;
	struct mod_t *src = stack->target_mod;
	struct mod_t *peer = stack->peer;

	if (event == EV_MOD_NMOESI_PEER_SEND) 
	{
		mem_debug("  %lld %lld 0x%x %s %s peer send\n", esim_time, stack->id,
			stack->tag, src->name, peer->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:peer\"\n",
			stack->id, src->name);

		stack->access_start_cycle = esim_cycle();
		
		// Record the start time of the network access
		if(!stack->nw_send_request_latency_start_cycle)
			stack->nw_send_request_latency_start_cycle = esim_cycle();

		/* Send message from src to peer */
		stack->msg = net_try_send_ev(src->low_net, src->low_net_node, peer->low_net_node, 
			src->block_size + 8, EV_MOD_NMOESI_PEER_RECEIVE, stack, event, stack);
		
		if(!stack->msg)
		{
			src->peer_send_requests_retried_nw++;
		}
		else
		{
			stack->nw_send_request_latency_end_cycle = esim_cycle();
			stack->nw_send_request_latency_cycle		 = stack->nw_send_request_latency_end_cycle - stack->nw_send_request_latency_start_cycle;

			if(stack->nw_send_request_latency_cycle) mod_update_nw_send_request_delay_counters(src, stack, mod_trans_peer_request);
		}

		return;
	}

	if (event == EV_MOD_NMOESI_PEER_RECEIVE) 
	{
		mem_debug("  %lld %lld 0x%x %s %s peer receive\n", esim_time, stack->id,
			stack->tag, src->name, peer->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:peer_receive\"\n",
			stack->id, peer->name);
		
		if(!stack->nw_receive_request_latency_start_cycle)
			stack->nw_receive_request_latency_start_cycle = esim_cycle();

		/* Receive message from src */
		net_receive(peer->low_net, peer->low_net_node, stack->msg);
		
		stack->nw_receive_request_latency_end_cycle = esim_cycle();
		stack->nw_receive_request_latency_cycle			= stack->nw_receive_request_latency_end_cycle - stack->nw_receive_request_latency_start_cycle;
		if(stack->nw_receive_request_latency_cycle) mod_update_nw_receive_request_delay_counters(peer, stack, mod_trans_peer_request);

		esim_schedule_event(EV_MOD_NMOESI_PEER_REPLY, stack, 0);

		return;
	}

	if (event == EV_MOD_NMOESI_PEER_REPLY) 
	{
		mem_debug("  %lld %lld 0x%x %s %s peer reply ack\n", esim_time, stack->id,
			stack->tag, src->name, peer->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:peer_reply_ack\"\n",
			stack->id, peer->name);

		// Record the start time of the network access
		if(!stack->nw_send_reply_latency_start_cycle)
			stack->nw_send_reply_latency_start_cycle = esim_cycle();

		/* Send ack from peer to src */
		stack->msg = net_try_send_ev(peer->low_net, peer->low_net_node, src->low_net_node, 
				8, EV_MOD_NMOESI_PEER_FINISH, stack, event, stack); 
		
		if(!stack->msg)
		{
			peer->peer_send_replies_retried_nw++;
		}
		else
		{
			stack->nw_send_reply_latency_end_cycle = esim_cycle();
			stack->nw_send_reply_latency_cycle     = stack->nw_send_reply_latency_end_cycle - stack->nw_send_reply_latency_start_cycle;

			if(stack->nw_send_reply_latency_cycle) mod_update_nw_send_reply_delay_counters(peer, stack, mod_trans_peer_request);
		}

		return;
	}

	if (event == EV_MOD_NMOESI_PEER_FINISH) 
	{
		mem_debug("  %lld %lld 0x%x %s %s peer finish\n", esim_time, stack->id,
			stack->tag, src->name, peer->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:peer_finish\"\n",
			stack->id, src->name);
		
		if(!stack->nw_receive_reply_latency_start_cycle)
			stack->nw_receive_reply_latency_start_cycle = esim_cycle();

		/* Receive message from src */
		net_receive(src->low_net, src->low_net_node, stack->msg);
		
		stack->nw_receive_reply_latency_end_cycle = esim_cycle();
		stack->nw_receive_reply_latency_cycle			= stack->nw_receive_reply_latency_end_cycle - stack->nw_receive_reply_latency_start_cycle;
		if(stack->nw_receive_reply_latency_cycle) mod_update_nw_receive_reply_delay_counters(src, stack, mod_trans_peer_request);

		stack->access_end_cycle = esim_cycle();
		stack->access_latency   = stack->access_end_cycle - stack->access_start_cycle;

		mod_update_latency_counters(stack->mod, stack->access_latency, mod_trans_peer_request);

		mod_stack_return(stack);
		return;
	}

	abort();
}


void mod_handler_nmoesi_invalidate(int event, void *data)
{
	struct mod_stack_t *stack = data;
	struct mod_stack_t *new_stack;

	struct mod_t *mod = stack->mod;

	uint32_t cache_entry_tag;
	uint32_t z;

	if (event == EV_MOD_NMOESI_INVALIDATE)
	{
		struct mod_t *sharer;
		int i;

		/* Get block info */
		cache_get_block(mod->cache, stack->set, stack->way, &stack->tag, &stack->state);
		mem_debug("  %lld %lld 0x%x %s invalidate (set=%d, way=%d, state=%s)\n", esim_time, stack->id,
			stack->tag, mod->name, stack->set, stack->way,
			str_map_value(&cache_block_state_map, stack->state));
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:invalidate\"\n",
			stack->id, mod->name);

		/* At least one pending reply */
		stack->pending = 1;

		// Set the previous state
		stack->prev_state = stack->state;

		stack->access_start_cycle = esim_cycle();
		
		/* Send write request to all upper level sharers except 'except_mod' */
		// dir = mod->dir;
		if(mod->num_nodes && stack->state)
		{
			for (z = 0; z < mod->num_sub_blocks; z++)
			{
				cache_entry_tag = stack->tag + z * mod->sub_block_size;
				assert(cache_entry_tag < stack->tag + mod->block_size);
				for (i = 0; i < mod->num_nodes; i++)
				{
					struct net_node_t *node;
					
					node = list_get(mod->high_net->node_list, i);
					sharer = node->user_data;
					// The request has not to be sent for the following cases.
					// 1.) The down-up write back request needn't be sent to the issuing module (Module that started the transaction) itself.
					// 2.) Node should be an end node.
					// 3.) Node shouldn't be the target or issuing request module itself.
					if(node->kind != net_node_end)
						continue;

					if (sharer == stack->except_mod)
						continue;

					if(sharer->mod_id == mod->mod_id)
						continue;

					if(sharer->mod_id == stack->orig_mod_id)
						continue;

					/* Send write request upwards if beginning of block */
					if (cache_entry_tag % sharer->block_size)
						continue;
					new_stack = mod_stack_create(stack->id, mod, cache_entry_tag,
						EV_MOD_NMOESI_INVALIDATE_FINISH, stack);
					new_stack->orig_mod_id = mod->mod_id;
					new_stack->issue_mod_id = stack->issue_mod_id;
					new_stack->target_mod = sharer;
					new_stack->request_dir = mod_request_down_up;
					
					if(stack->invalidate_eviction)
						new_stack->invalidate_eviction = 1;
					if(stack->wb_store)
						new_stack->wb_store = 1;
					
					new_stack->downup_writeback_request = 1;
					if(stack->invalidate_eviction) new_stack->evict_trans = 1;
					esim_schedule_event(EV_MOD_NMOESI_WRITE_REQUEST, new_stack, 0);
					stack->pending++;
				}
			}
		}

		esim_schedule_event(EV_MOD_NMOESI_INVALIDATE_FINISH, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_INVALIDATE_FINISH)
	{
		int next_state;

		mem_debug("  %lld %lld 0x%x %s invalidate finish\n", esim_time, stack->id,
			stack->tag, mod->name);
		mem_trace("mem.access name=\"A-%lld\" state=\"%s:invalidate_finish\"\n",
			stack->id, mod->name);

		if (stack->reply == reply_ack_data)
		{
			cache_set_block(mod->cache, stack->set, stack->way, stack->tag,
				cache_block_modified);
			next_state = cache_block_modified;
			// Check	
			mod_update_state_modification_counters(mod, stack->prev_state, next_state, mod_trans_store);
		}

		/* Ignore while pending */
		assert(stack->pending > 0);
		stack->pending--;
		if (stack->pending)
			return;

		stack->access_end_cycle = esim_cycle();
		stack->access_latency   = stack->access_end_cycle - stack->access_start_cycle;

		mod_update_latency_counters(stack->mod, stack->access_latency, mod_trans_invalidate);

		mod_stack_return(stack);
		return;
	}

	abort();
}

void mod_handler_nmoesi_message(int event, void *data)
{
	struct mod_stack_t *stack = data;
	struct mod_stack_t *ret = stack->ret_stack;
	struct mod_stack_t *new_stack;

	struct mod_t *mod = stack->mod;
	struct mod_t *target_mod = stack->target_mod;

	uint32_t z;

	if (event == EV_MOD_NMOESI_MESSAGE)
	{
		struct net_t *net;
		struct net_node_t *src_node;
		struct net_node_t *dst_node;

		mem_debug("  %lld %lld 0x%x %s message\n", esim_time, stack->id,
			stack->addr, mod->name);

		stack->reply_size = 8;
		stack->reply = reply_ack;

		/* Default return values*/
		ret->err = 0;

		/* Checks */
		assert(stack->message);

		/* Get source and destination nodes */
		net = mod->low_net;
		src_node = mod->low_net_node;
		dst_node = target_mod->high_net_node;

		/* Send message */
		stack->msg = net_try_send_ev(net, src_node, dst_node, 8,
			EV_MOD_NMOESI_MESSAGE_RECEIVE, stack, event, stack);
		return;
	}

	if (event == EV_MOD_NMOESI_MESSAGE_RECEIVE)
	{
		mem_debug("  %lld %lld 0x%x %s message receive\n", esim_time, stack->id,
			stack->addr, target_mod->name);

		/* Receive message */
		net_receive(target_mod->high_net, target_mod->high_net_node, stack->msg);
		
		/* Find and lock */
		new_stack = mod_stack_create(stack->id, target_mod, stack->addr,
			EV_MOD_NMOESI_MESSAGE_ACTION, stack);
		new_stack->orig_mod_id = target_mod->mod_id;
		new_stack->issue_mod_id = stack->issue_mod_id;
		new_stack->message = stack->message;
		new_stack->blocking = 0;
		new_stack->retry = 0;
		esim_schedule_event(EV_MOD_NMOESI_FIND_AND_LOCK, new_stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_MESSAGE_ACTION)
	{
		mem_debug("  %lld %lld 0x%x %s clear owner action\n", esim_time, stack->id,
			stack->tag, target_mod->name);

		assert(stack->message);

		/* Check block locking error. */
		mem_debug("stack err = %u\n", stack->err);
		if (stack->err)
		{
			ret->err = 1;
			mod_stack_set_reply(ret, reply_ack_error);
			esim_schedule_event(EV_MOD_NMOESI_MESSAGE_REPLY, stack, 0);
			return;
		}

		if (stack->message == message_clear_owner)
		{
		}
		else
		{
			fatal("Unexpected message");
		}

		/* Unlock the directory entry */
		cache_entry_unlock(target_mod->cache, stack->set, stack->way);

		esim_schedule_event(EV_MOD_NMOESI_MESSAGE_REPLY, stack, 0);
		return;
	}

	if (event == EV_MOD_NMOESI_MESSAGE_REPLY)
	{
		struct net_t *net;
		struct net_node_t *src_node;
		struct net_node_t *dst_node;

		mem_debug("  %lld %lld 0x%x %s message reply\n", esim_time, stack->id,
			stack->tag, target_mod->name);

		/* Checks */
		assert(mod_get_low_mod(mod, stack->addr) == target_mod ||
			mod_get_low_mod(target_mod, stack->addr) == mod);

		/* Get network and nodes */
		net = mod->low_net;
		src_node = target_mod->high_net_node;
		dst_node = mod->low_net_node;

		/* Send message */
		stack->msg = net_try_send_ev(net, src_node, dst_node, stack->reply_size,
			EV_MOD_NMOESI_MESSAGE_FINISH, stack, event, stack);
		return;
	}

	if (event == EV_MOD_NMOESI_MESSAGE_FINISH)
	{
		mem_debug("  %lld %lld 0x%x %s message finish\n", esim_time, stack->id,
			stack->tag, mod->name);

		/* Receive message */
		net_receive(mod->low_net, mod->low_net_node, stack->msg);

		/* Return */
		mod_stack_return(stack);
		return;
	}

	abort();
}
