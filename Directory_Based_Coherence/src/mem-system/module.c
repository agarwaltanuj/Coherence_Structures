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

#include <assert.h>

#include <lib/esim/esim.h>
#include <lib/mhandle/mhandle.h>
#include <lib/util/debug.h>
#include <lib/util/linked-list.h>
#include <lib/util/misc.h>
#include <lib/util/string.h>
#include <lib/util/repos.h>

#include "cache.h"
#include "directory.h"
#include "local-mem-protocol.h"
#include "mem-system.h"
#include "mod-stack.h"
#include "nmoesi-protocol.h"


/* String map for access type */
struct str_map_t mod_access_kind_map =
{
	3, {
		{ "Load", mod_access_load },
		{ "Store", mod_access_store },
		{ "NCStore", mod_access_nc_store },
		{ "Prefetch", mod_access_prefetch }
	}
};




/*
 * Public Functions
 */

struct mod_t *mod_create(char *name, enum mod_kind_t kind, int num_ports,
	int block_size, int latency)
{
	struct mod_t *mod;

	/* Initialize */
	mod = xcalloc(1, sizeof(struct mod_t));
	mod->name = xstrdup(name);
	mod->kind = kind;
	mod->latency = latency;

	/* Ports */
	mod->num_ports = num_ports;
	mod->ports = xcalloc(num_ports, sizeof(struct mod_port_t));

	/* Lists */
	mod->low_mod_list = linked_list_create();
	mod->high_mod_list = linked_list_create();

	/* Block size */
	mod->block_size = block_size;
	assert(!(block_size & (block_size - 1)) && block_size >= 4);
	mod->log_block_size = log_base2(block_size);

	mod->client_info_repos = repos_create(sizeof(struct mod_client_info_t), mod->name);

	return mod;
}


void mod_free(struct mod_t *mod)
{
	linked_list_free(mod->low_mod_list);
	linked_list_free(mod->high_mod_list);
	if (mod->cache)
		cache_free(mod->cache);
	if (mod->dir)
		dir_free(mod->dir);
	free(mod->ports);
	repos_free(mod->client_info_repos);
	free(mod->name);
	free(mod);
}


void mod_dump(struct mod_t *mod, FILE *f)
{
}


/* Access a memory module.
 * Variable 'witness', if specified, will be increased when the access completes.
 * The function returns a unique access ID.
 */
long long mod_access(struct mod_t *mod, enum mod_access_kind_t access_kind, 
	unsigned int addr, int *witness_ptr, struct linked_list_t *event_queue,
	void *event_queue_item, struct mod_client_info_t *client_info)
{
	struct mod_stack_t *stack;
	int event;

	/* Create module stack with new ID */
	mod_stack_id++;
	stack = mod_stack_create(mod_stack_id,
		mod, addr, ESIM_EV_NONE, NULL);

	/* Initialize */
	stack->witness_ptr = witness_ptr;
	stack->event_queue = event_queue;
	stack->event_queue_item = event_queue_item;
	stack->client_info = client_info;

	/* Select initial CPU/GPU event */
	if (mod->kind == mod_kind_cache || mod->kind == mod_kind_main_memory)
	{
		if (access_kind == mod_access_load)
		{
			event = EV_MOD_NMOESI_LOAD;
		}
		else if (access_kind == mod_access_store)
		{
			event = EV_MOD_NMOESI_STORE;
		}
		else if (access_kind == mod_access_nc_store)
		{
			event = EV_MOD_NMOESI_NC_STORE;
		}
		else if (access_kind == mod_access_prefetch)
		{
			event = EV_MOD_NMOESI_PREFETCH;
		}
		else 
		{
			panic("%s: invalid access kind", __FUNCTION__);
		}
	}
	else if (mod->kind == mod_kind_local_memory)
	{
		if (access_kind == mod_access_load)
		{
			event = EV_MOD_LOCAL_MEM_LOAD;
		}
		else if (access_kind == mod_access_store)
		{
			event = EV_MOD_LOCAL_MEM_STORE;
		}
		else
		{
			panic("%s: invalid access kind", __FUNCTION__);
		}
	}
	else
	{
		panic("%s: invalid mod kind", __FUNCTION__);
	}

	/* Schedule */
	esim_execute_event(event, stack);

	/* Return access ID */
	return stack->id;
}


/* Return true if module can be accessed. */
int mod_can_access(struct mod_t *mod, unsigned int addr)
{
	int non_coalesced_accesses;

	/* There must be a free port */
	assert(mod->num_locked_ports <= mod->num_ports);
	if (mod->num_locked_ports == mod->num_ports)
		return 0;

	/* If no MSHR is given, module can be accessed */
	if (!mod->mshr_size)
		return 1;

	/* Module can be accessed if number of non-coalesced in-flight
	 * accesses is smaller than the MSHR size. */
	non_coalesced_accesses = mod->access_list_count -
		mod->access_list_coalesced_count;
	return non_coalesced_accesses < mod->mshr_size;
}


/* Return {set, way, tag, state} for an address.
 * The function returns TRUE on hit, FALSE on miss. */
int mod_find_block(struct mod_t *mod, unsigned int addr, int *set_ptr,
	int *way_ptr, int *tag_ptr, int *state_ptr)
{
	struct cache_t *cache = mod->cache;
	struct cache_block_t *blk;
	struct dir_lock_t *dir_lock;

	int set;
	int way;
	int tag;

	/* A transient tag is considered a hit if the block is
	 * locked in the corresponding directory. */
	tag = addr & ~cache->block_mask;
	if (mod->range_kind == mod_range_interleaved)
	{
		unsigned int num_mods = mod->range.interleaved.mod;
		set = ((tag >> cache->log_block_size) / num_mods) % cache->num_sets;
	}
	else if (mod->range_kind == mod_range_bounds)
	{
		set = (tag >> cache->log_block_size) % cache->num_sets;
	}
	else 
	{
		panic("%s: invalid range kind (%d)", __FUNCTION__, mod->range_kind);
	}

	for (way = 0; way < cache->assoc; way++)
	{
		blk = &cache->sets[set].blocks[way];
		if (blk->tag == tag && blk->state)
			break;
		if (blk->transient_tag == tag)
		{
			dir_lock = dir_lock_get(mod->dir, set, way);
			if (dir_lock->lock)
				break;
		}
	}

	PTR_ASSIGN(set_ptr, set);
	PTR_ASSIGN(tag_ptr, tag);

	/* Miss */
	if (way == cache->assoc)
	{
	/*
		PTR_ASSIGN(way_ptr, 0);
		PTR_ASSIGN(state_ptr, 0);
	*/
		return 0;
	}

	/* Hit */
	PTR_ASSIGN(way_ptr, way);
	PTR_ASSIGN(state_ptr, cache->sets[set].blocks[way].state);
	return 1;
}

void mod_block_set_prefetched(struct mod_t *mod, unsigned int addr, int val)
{
	int set, way;

	assert(mod->kind == mod_kind_cache && mod->cache != NULL);
	if (mod->cache->prefetcher && mod_find_block(mod, addr, &set, &way, NULL, NULL))
	{
		mod->cache->sets[set].blocks[way].prefetched = val;
	}
}

int mod_block_get_prefetched(struct mod_t *mod, unsigned int addr)
{
	int set, way;

	assert(mod->kind == mod_kind_cache && mod->cache != NULL);
	if (mod->cache->prefetcher && mod_find_block(mod, addr, &set, &way, NULL, NULL))
	{
		return mod->cache->sets[set].blocks[way].prefetched;
	}

	return 0;
}

/* Lock a port, and schedule event when done.
 * If there is no free port, the access is enqueued in the port
 * waiting list, and it will retry once a port becomes available with a
 * call to 'mod_unlock_port'. */
void mod_lock_port(struct mod_t *mod, struct mod_stack_t *stack, int event)
{
	struct mod_port_t *port = NULL;
	int i;

	/* No free port */
	if (mod->num_locked_ports >= mod->num_ports)
	{
		assert(!DOUBLE_LINKED_LIST_MEMBER(mod, port_waiting, stack));

		/* If the request to lock the port is down-up, give it priority since 
		 * it is possibly holding up a large portion of the memory hierarchy */
		if (stack->request_dir == mod_request_down_up)
		{
			DOUBLE_LINKED_LIST_INSERT_HEAD(mod, port_waiting, stack);
		}
		else 
		{
			DOUBLE_LINKED_LIST_INSERT_TAIL(mod, port_waiting, stack);
		}
		stack->port_waiting_list_event = event;

		if(stack->read)
		{
			if(stack->downup_read_request)
				mod->downup_read_waiting_for_mod_port++;
			else
				mod->read_waiting_for_mod_port++;
		}

		if(stack->write)
		{
			if(stack->evict_trans)
				mod->eviction_waiting_for_mod_port++;

			if(stack->downup_writeback_request)
				mod->downup_writeback_waiting_for_mod_port++;
			else
				mod->write_waiting_for_mod_port++;
		}

		if(!stack->mod_port_waiting_start_cycle)
		{
			stack->mod_port_waiting_start_cycle = esim_cycle();
		}
		
		return;
	}

	/* Get free port */
	for (i = 0; i < mod->num_ports; i++)
	{
		port = &mod->ports[i];
		if (!port->stack)
			break;
	}

	/* Lock port */
	assert(port && i < mod->num_ports);
	port->stack = stack;
	stack->port = port;
	mod->num_locked_ports++;
	
	stack->mod_port_waiting_end_cycle = esim_cycle();
	stack->mod_port_waiting_cycle     = stack->mod_port_waiting_end_cycle - stack->mod_port_waiting_start_cycle;
	if(stack->mod_port_waiting_start_cycle) 
		mod_update_mod_port_waiting_counters(mod, stack);

	/* Debug */
	mem_debug("  %lld stack %lld %s port %d locked\n", esim_time, stack->id, mod->name, i);

	/* Schedule event */
	esim_schedule_event(event, stack, 0);
}


void mod_unlock_port(struct mod_t *mod, struct mod_port_t *port,
	struct mod_stack_t *stack)
{
	int event;

	/* Checks */
	assert(mod->num_locked_ports > 0);
	assert(stack->port == port && port->stack == stack);
	assert(stack->mod == mod);

	/* Unlock port */
	stack->port = NULL;
	port->stack = NULL;
	mod->num_locked_ports--;

	/* Debug */
	mem_debug("  %lld %lld %s port unlocked\n", esim_time,
		stack->id, mod->name);

	/* Check if there was any access waiting for free port */
	if (!mod->port_waiting_list_count)
		return;

	/* Wake up one access waiting for a free port */
	stack = mod->port_waiting_list_head;
	event = stack->port_waiting_list_event;
	assert(DOUBLE_LINKED_LIST_MEMBER(mod, port_waiting, stack));
	DOUBLE_LINKED_LIST_REMOVE(mod, port_waiting, stack);
	mod_lock_port(mod, stack, event);

}


void mod_access_start(struct mod_t *mod, struct mod_stack_t *stack,
	enum mod_access_kind_t access_kind)
{
	int index;

	/* Record access kind */
	stack->access_kind = access_kind;

	/* Insert in access list */
	DOUBLE_LINKED_LIST_INSERT_TAIL(mod, access, stack);

	/* Insert in write access list */
	if (access_kind == mod_access_store)
		DOUBLE_LINKED_LIST_INSERT_TAIL(mod, write_access, stack);

	/* Insert in access hash table */
	index = (stack->addr >> mod->log_block_size) % MOD_ACCESS_HASH_TABLE_SIZE;
	DOUBLE_LINKED_LIST_INSERT_TAIL(&mod->access_hash_table[index], bucket, stack);
}


void mod_access_finish(struct mod_t *mod, struct mod_stack_t *stack)
{
	int index;

	/* Remove from access list */
	DOUBLE_LINKED_LIST_REMOVE(mod, access, stack);

	/* Remove from write access list */
	assert(stack->access_kind);
	if (stack->access_kind == mod_access_store)
		DOUBLE_LINKED_LIST_REMOVE(mod, write_access, stack);

	/* Remove from hash table */
	index = (stack->addr >> mod->log_block_size) % MOD_ACCESS_HASH_TABLE_SIZE;
	DOUBLE_LINKED_LIST_REMOVE(&mod->access_hash_table[index], bucket, stack);

	/* If this was a coalesced access, update counter */
	if (stack->coalesced)
	{
		assert(mod->access_list_coalesced_count > 0);
		mod->access_list_coalesced_count--;
	}
}


/* Return true if the access with identifier 'id' is in flight.
 * The address of the access is passed as well because this lookup is done on the
 * access truth table, indexed by the access address.
 */
int mod_in_flight_access(struct mod_t *mod, long long id, unsigned int addr)
{
	struct mod_stack_t *stack;
	int index;

	/* Look for access */
	index = (addr >> mod->log_block_size) % MOD_ACCESS_HASH_TABLE_SIZE;
	for (stack = mod->access_hash_table[index].bucket_list_head; stack; stack = stack->bucket_list_next)
		if (stack->id == id)
			return 1;

	/* Not found */
	return 0;
}


/* Return the youngest in-flight access older than 'older_than_stack' to block containing 'addr'.
 * If 'older_than_stack' is NULL, return the youngest in-flight access containing 'addr'.
 * The function returns NULL if there is no in-flight access to block containing 'addr'.
 */
struct mod_stack_t *mod_in_flight_address(struct mod_t *mod, unsigned int addr,
	struct mod_stack_t *older_than_stack)
{
	struct mod_stack_t *stack;
	int index;

	/* Look for address */
	index = (addr >> mod->log_block_size) % MOD_ACCESS_HASH_TABLE_SIZE;
	for (stack = mod->access_hash_table[index].bucket_list_head; stack;
		stack = stack->bucket_list_next)
	{
		/* This stack is not older than 'older_than_stack' */
		if (older_than_stack && stack->id >= older_than_stack->id)
			continue;

		/* Address matches */
		if (stack->addr >> mod->log_block_size == addr >> mod->log_block_size)
			return stack;
	}

	/* Not found */
	return NULL;
}


/* Return the youngest in-flight write older than 'older_than_stack'. If 'older_than_stack'
 * is NULL, return the youngest in-flight write. Return NULL if there is no in-flight write.
 */
struct mod_stack_t *mod_in_flight_write(struct mod_t *mod,
	struct mod_stack_t *older_than_stack)
{
	struct mod_stack_t *stack;

	/* No 'older_than_stack' given, return youngest write */
	if (!older_than_stack)
		return mod->write_access_list_tail;

	/* Search */
	for (stack = older_than_stack->access_list_prev; stack;
		stack = stack->access_list_prev)
		if (stack->access_kind == mod_access_store)
			return stack;

	/* Not found */
	return NULL;
}


int mod_serves_address(struct mod_t *mod, unsigned int addr)
{
	/* Address bounds */
	if (mod->range_kind == mod_range_bounds)
		return addr >= mod->range.bounds.low &&
			addr <= mod->range.bounds.high;

	/* Interleaved addresses */
	if (mod->range_kind == mod_range_interleaved)
		return (addr / mod->range.interleaved.div) %
			mod->range.interleaved.mod ==
			mod->range.interleaved.eq;

	/* Invalid */
	panic("%s: invalid range kind", __FUNCTION__);
	return 0;
}


/* Return the low module serving a given address. */
struct mod_t *mod_get_low_mod(struct mod_t *mod, unsigned int addr)
{
	struct mod_t *low_mod;
	struct mod_t *server_mod;

	/* Main memory does not have a low module */
	assert(mod_serves_address(mod, addr));
	if (mod->kind == mod_kind_main_memory)
	{
		assert(!linked_list_count(mod->low_mod_list));
		return NULL;
	}

	/* Check which low module serves address */
	server_mod = NULL;
	LINKED_LIST_FOR_EACH(mod->low_mod_list)
	{
		/* Get new low module */
		low_mod = linked_list_get(mod->low_mod_list);
		if (!mod_serves_address(low_mod, addr))
			continue;

		/* Address served by more than one module */
		if (server_mod)
			fatal("%s: low modules %s and %s both serve address 0x%x",
				mod->name, server_mod->name, low_mod->name, addr);

		/* Assign server */
		server_mod = low_mod;
	}

	/* Error if no low module serves address */
	if (!server_mod)
		fatal("module %s: no lower module serves address 0x%x",
			mod->name, addr);

	/* Return server module */
	return server_mod;
}


int mod_get_retry_latency(struct mod_t *mod)
{
	return random() % mod->latency + mod->latency;
}


/* Check if an access to a module can be coalesced with another access older
 * than 'older_than_stack'. If 'older_than_stack' is NULL, check if it can
 * be coalesced with any in-flight access.
 * If it can, return the access that it would be coalesced with. Otherwise,
 * return NULL. */
struct mod_stack_t *mod_can_coalesce(struct mod_t *mod,
	enum mod_access_kind_t access_kind, unsigned int addr,
	struct mod_stack_t *older_than_stack)
{
	struct mod_stack_t *stack;
	struct mod_stack_t *tail;

	/* For efficiency, first check in the hash table of accesses
	 * whether there is an access in flight to the same block. */
	assert(access_kind);
	if (!mod_in_flight_address(mod, addr, older_than_stack))
		return NULL;

	/* Get youngest access older than 'older_than_stack' */
	tail = older_than_stack ? older_than_stack->access_list_prev :
		mod->access_list_tail;

	/* Coalesce depending on access kind */
	switch (access_kind)
	{

	case mod_access_load:
	{
		for (stack = tail; stack; stack = stack->access_list_prev)
		{
			/* Only coalesce with groups of reads or prefetches at the tail */
			if (stack->access_kind != mod_access_load &&
			    stack->access_kind != mod_access_prefetch)
				return NULL;

			if (stack->addr >> mod->log_block_size ==
				addr >> mod->log_block_size)
				return stack->master_stack ? stack->master_stack : stack;
		}
		break;
	}

	case mod_access_store:
	{
		/* Only coalesce with last access */
		stack = tail;
		if (!stack)
			return NULL;

		/* Only if it is a write */
		if (stack->access_kind != mod_access_store)
			return NULL;

		/* Only if it is an access to the same block */
		if (stack->addr >> mod->log_block_size != addr >> mod->log_block_size)
			return NULL;

		/* Only if previous write has not started yet */
		if (stack->port_locked)
			return NULL;

		/* Coalesce */
		return stack->master_stack ? stack->master_stack : stack;
	}

	case mod_access_nc_store:
	{
		/* Only coalesce with last access */
		stack = tail;
		if (!stack)
			return NULL;

		/* Only if it is a non-coherent write */
		if (stack->access_kind != mod_access_nc_store)
			return NULL;

		/* Only if it is an access to the same block */
		if (stack->addr >> mod->log_block_size != addr >> mod->log_block_size)
			return NULL;

		/* Only if previous write has not started yet */
		if (stack->port_locked)
			return NULL;

		/* Coalesce */
		return stack->master_stack ? stack->master_stack : stack;
	}
	case mod_access_prefetch:
		/* At this point, we know that there is another access (load/store)
		 * to the same block already in flight. Just find and return it.
		 * The caller may abort the prefetch since the block is already
		 * being fetched. */
		for (stack = tail; stack; stack = stack->access_list_prev)
		{
			if (stack->addr >> mod->log_block_size ==
				addr >> mod->log_block_size)
				return stack;
		}
		assert(!"Hash table wrongly reported another access to same block.\n");
		break;


	default:
		panic("%s: invalid access type", __FUNCTION__);
		break;
	}

	/* No access found */
	return NULL;
}


void mod_coalesce(struct mod_t *mod, struct mod_stack_t *master_stack,
	struct mod_stack_t *stack)
{
	/* Debug */
	mem_debug("  %lld %lld 0x%x %s coalesce with %lld\n", esim_time,
		stack->id, stack->addr, mod->name, master_stack->id);

	/* Master stack must not have a parent. We only want one level of
	 * coalesced accesses. */
	assert(!master_stack->master_stack);

	/* Access must have been recorded already, which sets the access
	 * kind to a valid value. */
	assert(stack->access_kind);

	/* Set slave stack as a coalesced access */
	stack->coalesced = 1;
	stack->master_stack = master_stack;
	assert(mod->access_list_coalesced_count <= mod->access_list_count);

	/* Record in-flight coalesced access in module */
	mod->access_list_coalesced_count++;
}

struct mod_client_info_t *mod_client_info_create(struct mod_t *mod)
{
	struct mod_client_info_t *client_info;

	/* Create object */
	client_info = repos_create_object(mod->client_info_repos);

	/* Return */
	return client_info;
}

void mod_client_info_free(struct mod_t *mod, struct mod_client_info_t *client_info)
{
	repos_free_object(mod->client_info_repos, client_info);
}

int req_variable_in_range(int req_var, int lb, int ub)
{
	assert(lb <= ub);

	if((req_var >= lb) && (req_var <= ub))
 	 	return 1;
	
	return 0;
}

int pow_2(int i)
{
	if(i < 0) return 0;

	return (1 << i);
}

void mod_update_request_counters(struct mod_t *mod , enum mod_trans_type_t trans_type)
{
	// Update individual counters based on the trans_type variable
	// Total requests on the module is updated irrespective of the transaction type.
	// Total processor requests is updated for all Load/Store/Eviction/WriteBack requests and total down up requests are updated for all downup read/writeback/eviction requests

	long long processor_request;
	long long controller_request;
	long long updown_request;
	long long downup_request;
	long long total_requests;

	assert(mod->num_load_requests >= 0);
	assert(mod->num_store_requests >= 0);
	assert(mod->num_eviction_requests >= 0);
	assert(mod->num_read_requests >= 0);
	assert(mod->num_writeback_requests >= 0);
	assert(mod->num_downup_read_requests >= 0);
	assert(mod->num_downup_writeback_requests >= 0);
	assert(mod->num_downup_eviction_requests  >= 0);
	
	switch(trans_type)
	{
		case mod_trans_load : 
			{
				if(mod->num_load_requests >= pow_2(9))
				{
					mod->request_load[9]++;
					break;
				}

				for(int i=0; i<10; i++)
					if(req_variable_in_range(mod->num_load_requests, pow_2(i-1), pow_2(i) - 1))
					{
						mod->request_load[i]++;
						break;
					}
 			 	break;
			}
		case mod_trans_store : 
			{
				if(mod->num_store_requests >= pow_2(9))
				{
					mod->request_store[9]++;
					break;
				}

				for(int i=0; i<10; i++)
					if(req_variable_in_range(mod->num_store_requests, pow_2(i-1), pow_2(i) - 1))
					{
						mod->request_store[i]++;
						break;
					}
 			 	break;
			}
		case mod_trans_writeback : 
			{
				if(mod->num_writeback_requests >= pow_2(9))
				{
					mod->request_writeback[9]++;
					break;
				}

				for(int i=0; i<10; i++)
					if(req_variable_in_range(mod->num_writeback_requests, pow_2(i-1), pow_2(i) - 1))
					{
						mod->request_writeback[i]++;
						break;
					}
 			 	break;
			}
		case mod_trans_eviction : 
			{
				if(mod->num_eviction_requests >= pow_2(9))
				{
					mod->request_eviction[9]++;
					break;
				}

				for(int i=0; i<10; i++)
					if(req_variable_in_range(mod->num_eviction_requests, pow_2(i-1), pow_2(i) - 1))
					{
						mod->request_eviction[i]++;
						break;
					}
 			 	break;
			}
		case mod_trans_downup_read_request : 
			{
				if(mod->num_downup_read_requests >= pow_2(9))
				{
					mod->request_downup_read[9]++;
					break;
				}

				for(int i=0; i<10; i++)
					if(req_variable_in_range(mod->num_downup_read_requests, pow_2(i-1), pow_2(i) - 1))
					{
						mod->request_downup_read[i]++;
						break;
					}
 			 	break;
			}
		case mod_trans_downup_writeback_request : 
			{
				if(mod->num_downup_writeback_requests >= pow_2(9))
				{
					mod->request_downup_writeback[9]++;
					break;
				}

				for(int i=0; i<10; i++)
					if(req_variable_in_range(mod->num_downup_writeback_requests, pow_2(i-1), pow_2(i) - 1))
					{
						mod->request_downup_writeback[i]++;
						break;
					}
 			 	break;
			}
		case mod_trans_downup_eviction_request : 
			{
				if(mod->num_downup_eviction_requests >= pow_2(9))
				{
					mod->request_downup_eviction[9]++;
					break;
				}

				for(int i=0; i<10; i++)
					if(req_variable_in_range(mod->num_downup_eviction_requests, pow_2(i-1), pow_2(i) - 1))
					{
						mod->request_downup_eviction[i]++;
						break;
					}
 			 	break;
			}
	}
	
	processor_request = mod->num_load_requests + mod->num_store_requests;
	controller_request = mod->num_read_requests + mod->num_writeback_requests + mod->num_eviction_requests;
	updown_request = processor_request + controller_request;
	downup_request = mod->num_downup_read_requests + mod->num_downup_writeback_requests + mod->num_downup_eviction_requests;
	total_requests = updown_request + downup_request;

	if(processor_request >= pow_2(10))
		mod->request_processor[10]++;
	else
		for(int i=0; i<11; i++)
			if(req_variable_in_range(processor_request, pow_2(i-1), pow_2(i) - 1))
			{
				mod->request_processor[i]++;
				break;
			}

	if(controller_request >= pow_2(10))
		mod->request_controller[10]++;
	else
		for(int i=0; i<11; i++)
			if(req_variable_in_range(controller_request, pow_2(i-1), pow_2(i) - 1))
			{
				mod->request_controller[i]++;
				break;
			}

	if(downup_request >= pow_2(10))
		mod->request_downup[10]++;
	else
		for(int i=0; i<11; i++)
			if(req_variable_in_range(downup_request, pow_2(i-1), pow_2(i) - 1))
			{
				mod->request_downup[i]++;
				break;
			}

	if(updown_request >= pow_2(10))
		mod->request_updown[10]++;
	else
		for(int i=0; i<11; i++)
			if(req_variable_in_range(updown_request, pow_2(i-1), pow_2(i) - 1))
			{
				mod->request_updown[i]++;
				break;
			}

	if(total_requests >= pow_2(11))
		mod->request_total[11]++;
	else
		for(int i=0; i<12; i++)
			if(req_variable_in_range(total_requests, pow_2(i-1), pow_2(i) - 1))
			{
				mod->request_total[i]++;
				break;
			}
}


void mod_update_state_modification_counters(struct mod_t *mod, enum cache_block_state_t prev_state, enum cache_block_state_t next_state, enum mod_trans_type_t trans_type)
{
	// Checkers
	assert((trans_type != mod_trans_load) && (trans_type != mod_trans_store) && (trans_type != mod_trans_downup_read_request) && (trans_type != mod_trans_downup_writeback_request));

	assert((trans_type == mod_trans_load) && (next_state == cache_block_invalid));
	assert((trans_type == mod_trans_load) && (prev_state != cache_block_invalid) && (next_state != prev_state));
	assert((trans_type == mod_trans_load) && (prev_state == cache_block_invalid) && (next_state != cache_block_exclusive) && (next_state != cache_block_shared));
	
	assert((trans_type == mod_trans_store) && (next_state != cache_block_modified));

	assert((trans_type == mod_trans_downup_read_request) && (prev_state == cache_block_invalid));
	assert((trans_type == mod_trans_downup_read_request) && (next_state == cache_block_modified));
	assert((trans_type == mod_trans_downup_read_request) && (next_state == cache_block_exclusive));

	assert((trans_type == mod_trans_downup_writeback_request) && (prev_state == cache_block_invalid));
	assert((trans_type == mod_trans_downup_writeback_request) && (next_state != cache_block_invalid));
	
	switch(trans_type)
	{
		case mod_trans_load :
		{
			switch(prev_state)
			{
				case cache_block_invalid : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->load_state_invalid_to_invalid++;break; }
							case cache_block_modified    : { mod->load_state_invalid_to_modified++;break; }
							case cache_block_owned       : { mod->load_state_invalid_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->load_state_invalid_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->load_state_invalid_to_shared++;break; }
							case cache_block_noncoherent : { mod->load_state_invalid_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_modified : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->load_state_modified_to_invalid++;break; }
							case cache_block_modified    : { mod->load_state_modified_to_modified++;break; }
							case cache_block_owned       : { mod->load_state_modified_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->load_state_modified_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->load_state_modified_to_shared++;break; }
							case cache_block_noncoherent : { mod->load_state_modified_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_owned : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->load_state_owned_to_invalid++;break; }
							case cache_block_modified    : { mod->load_state_owned_to_modified++;break; }
							case cache_block_owned       : { mod->load_state_owned_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->load_state_owned_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->load_state_owned_to_shared++;break; }
							case cache_block_noncoherent : { mod->load_state_owned_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_exclusive : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->load_state_exclusive_to_invalid++;break; }
							case cache_block_modified    : { mod->load_state_exclusive_to_modified++;break; }
							case cache_block_owned       : { mod->load_state_exclusive_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->load_state_exclusive_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->load_state_exclusive_to_shared++;break; }
							case cache_block_noncoherent : { mod->load_state_exclusive_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_shared : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->load_state_shared_to_invalid++;break; }
							case cache_block_modified    : { mod->load_state_shared_to_modified++;break; }
							case cache_block_owned       : { mod->load_state_shared_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->load_state_shared_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->load_state_shared_to_shared++;break; }
							case cache_block_noncoherent : { mod->load_state_shared_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_noncoherent : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->load_state_noncoherent_to_invalid++;break; }
							case cache_block_modified    : { mod->load_state_noncoherent_to_modified++;break; }
							case cache_block_owned       : { mod->load_state_noncoherent_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->load_state_noncoherent_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->load_state_noncoherent_to_shared++;break; }
							case cache_block_noncoherent : { mod->load_state_noncoherent_to_noncoherent++;break; }
						}
						break;
					}
			}
			break;
		}

		case mod_trans_store :
		{
			switch(prev_state)
			{
				case cache_block_invalid : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->store_state_invalid_to_invalid++;break; }
							case cache_block_modified    : { mod->store_state_invalid_to_modified++;break; }
							case cache_block_owned       : { mod->store_state_invalid_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->store_state_invalid_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->store_state_invalid_to_shared++;break; }
							case cache_block_noncoherent : { mod->store_state_invalid_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_modified : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->store_state_modified_to_invalid++;break; }
							case cache_block_modified    : { mod->store_state_modified_to_modified++;break; }
							case cache_block_owned       : { mod->store_state_modified_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->store_state_modified_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->store_state_modified_to_shared++;break; }
							case cache_block_noncoherent : { mod->store_state_modified_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_owned : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->store_state_owned_to_invalid++;break; }
							case cache_block_modified    : { mod->store_state_owned_to_modified++;break; }
							case cache_block_owned       : { mod->store_state_owned_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->store_state_owned_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->store_state_owned_to_shared++;break; }
							case cache_block_noncoherent : { mod->store_state_owned_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_exclusive : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->store_state_exclusive_to_invalid++;break; }
							case cache_block_modified    : { mod->store_state_exclusive_to_modified++;break; }
							case cache_block_owned       : { mod->store_state_exclusive_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->store_state_exclusive_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->store_state_exclusive_to_shared++;break; }
							case cache_block_noncoherent : { mod->store_state_exclusive_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_shared : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->store_state_shared_to_invalid++;break; }
							case cache_block_modified    : { mod->store_state_shared_to_modified++;break; }
							case cache_block_owned       : { mod->store_state_shared_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->store_state_shared_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->store_state_shared_to_shared++;break; }
							case cache_block_noncoherent : { mod->store_state_shared_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_noncoherent : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->store_state_noncoherent_to_invalid++;break; }
							case cache_block_modified    : { mod->store_state_noncoherent_to_modified++;break; }
							case cache_block_owned       : { mod->store_state_noncoherent_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->store_state_noncoherent_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->store_state_noncoherent_to_shared++;break; }
							case cache_block_noncoherent : { mod->store_state_noncoherent_to_noncoherent++;break; }
						}
						break;
					}
			}
			break;
		}

		case mod_trans_downup_read_request :
		{
			switch(prev_state)
			{
				case cache_block_invalid : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->downup_read_req_state_invalid_to_invalid++;break; }
							case cache_block_modified    : { mod->downup_read_req_state_invalid_to_modified++;break; }
							case cache_block_owned       : { mod->downup_read_req_state_invalid_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->downup_read_req_state_invalid_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->downup_read_req_state_invalid_to_shared++;break; }
							case cache_block_noncoherent : { mod->downup_read_req_state_invalid_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_modified : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->downup_read_req_state_modified_to_invalid++;break; }
							case cache_block_modified    : { mod->downup_read_req_state_modified_to_modified++;break; }
							case cache_block_owned       : { mod->downup_read_req_state_modified_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->downup_read_req_state_modified_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->downup_read_req_state_modified_to_shared++;break; }
							case cache_block_noncoherent : { mod->downup_read_req_state_modified_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_owned : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->downup_read_req_state_owned_to_invalid++;break; }
							case cache_block_modified    : { mod->downup_read_req_state_owned_to_modified++;break; }
							case cache_block_owned       : { mod->downup_read_req_state_owned_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->downup_read_req_state_owned_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->downup_read_req_state_owned_to_shared++;break; }
							case cache_block_noncoherent : { mod->downup_read_req_state_owned_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_exclusive : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->downup_read_req_state_exclusive_to_invalid++;break; }
							case cache_block_modified    : { mod->downup_read_req_state_exclusive_to_modified++;break; }
							case cache_block_owned       : { mod->downup_read_req_state_exclusive_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->downup_read_req_state_exclusive_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->downup_read_req_state_exclusive_to_shared++;break; }
							case cache_block_noncoherent : { mod->downup_read_req_state_exclusive_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_shared : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->downup_read_req_state_shared_to_invalid++;break; }
							case cache_block_modified    : { mod->downup_read_req_state_shared_to_modified++;break; }
							case cache_block_owned       : { mod->downup_read_req_state_shared_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->downup_read_req_state_shared_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->downup_read_req_state_shared_to_shared++;break; }
							case cache_block_noncoherent : { mod->downup_read_req_state_shared_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_noncoherent : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->downup_read_req_state_noncoherent_to_invalid++;break; }
							case cache_block_modified    : { mod->downup_read_req_state_noncoherent_to_modified++;break; }
							case cache_block_owned       : { mod->downup_read_req_state_noncoherent_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->downup_read_req_state_noncoherent_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->downup_read_req_state_noncoherent_to_shared++;break; }
							case cache_block_noncoherent : { mod->downup_read_req_state_noncoherent_to_noncoherent++;break; }
						}
						break;
					}
			}
			break;
		}

		case mod_trans_downup_writeback_request :
		{
			switch(prev_state)
			{
				case cache_block_invalid : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->downup_wb_req_state_invalid_to_invalid++;break; }
							case cache_block_modified    : { mod->downup_wb_req_state_invalid_to_modified++;break; }
							case cache_block_owned       : { mod->downup_wb_req_state_invalid_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->downup_wb_req_state_invalid_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->downup_wb_req_state_invalid_to_shared++;break; }
							case cache_block_noncoherent : { mod->downup_wb_req_state_invalid_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_modified : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->downup_wb_req_state_modified_to_invalid++;break; }
							case cache_block_modified    : { mod->downup_wb_req_state_modified_to_modified++;break; }
							case cache_block_owned       : { mod->downup_wb_req_state_modified_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->downup_wb_req_state_modified_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->downup_wb_req_state_modified_to_shared++;break; }
							case cache_block_noncoherent : { mod->downup_wb_req_state_modified_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_owned : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->downup_wb_req_state_owned_to_invalid++;break; }
							case cache_block_modified    : { mod->downup_wb_req_state_owned_to_modified++;break; }
							case cache_block_owned       : { mod->downup_wb_req_state_owned_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->downup_wb_req_state_owned_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->downup_wb_req_state_owned_to_shared++;break; }
							case cache_block_noncoherent : { mod->downup_wb_req_state_owned_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_exclusive : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->downup_wb_req_state_exclusive_to_invalid++;break; }
							case cache_block_modified    : { mod->downup_wb_req_state_exclusive_to_modified++;break; }
							case cache_block_owned       : { mod->downup_wb_req_state_exclusive_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->downup_wb_req_state_exclusive_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->downup_wb_req_state_exclusive_to_shared++;break; }
							case cache_block_noncoherent : { mod->downup_wb_req_state_exclusive_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_shared : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->downup_wb_req_state_shared_to_invalid++;break; }
							case cache_block_modified    : { mod->downup_wb_req_state_shared_to_modified++;break; }
							case cache_block_owned       : { mod->downup_wb_req_state_shared_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->downup_wb_req_state_shared_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->downup_wb_req_state_shared_to_shared++;break; }
							case cache_block_noncoherent : { mod->downup_wb_req_state_shared_to_noncoherent++;break; }
						}
						break;
					}
				case cache_block_noncoherent : 
					{
						switch(next_state)
						{
							case cache_block_invalid     : { mod->downup_wb_req_state_noncoherent_to_invalid++;break; }
							case cache_block_modified    : { mod->downup_wb_req_state_noncoherent_to_modified++;break; }
							case cache_block_owned       : { mod->downup_wb_req_state_noncoherent_to_owned++;break; }
							case cache_block_exclusive 	 : { mod->downup_wb_req_state_noncoherent_to_exclusive++;break; }
							case cache_block_shared 		 : { mod->downup_wb_req_state_noncoherent_to_shared++;break; }
							case cache_block_noncoherent : { mod->downup_wb_req_state_noncoherent_to_noncoherent++;break; }
						}
						break;
					}
			}
			break;
		}
	}
}

void mod_update_latency_counters(struct mod_t *mod, long long latency, enum mod_trans_type_t trans_type)
{
	if(latency >= pow_2(9))
	{
		switch(trans_type)
		{
			case mod_trans_load :                     mod->load_latency[9]++; break;
			case mod_trans_store :                    mod->store_latency[9]++; break;
			case mod_trans_read_request :             mod->read_request_latency[9]++; break;
			case mod_trans_writeback :                mod->writeback_request_latency[9]++; break;
			case mod_trans_eviction :                 mod->eviction_latency[9]++; break;
			case mod_trans_downup_read_request :      mod->downup_read_request_latency[9]++; break;
			case mod_trans_downup_writeback_request : mod->downup_writeback_request_latency[9]++; break;
			case mod_trans_peer_request :             mod->peer_latency[9]++; break;
			case mod_trans_invalidate :               mod->invalidate_latency[9]++; break;
		}
		return;
	}

	for(int i=0; i<10; i++)
	{
		if(req_variable_in_range(latency, pow_2(i-1), pow_2(i) - 1))
		{
			switch(trans_type)
			{
				case mod_trans_load :                     mod->load_latency[i]++; break;
				case mod_trans_store :                    mod->store_latency[i]++; break;
				case mod_trans_read_request :             mod->read_request_latency[i]++; break;
				case mod_trans_writeback :                mod->writeback_request_latency[i]++; break;
				case mod_trans_eviction :                 mod->eviction_latency[i]++; break;
				case mod_trans_downup_read_request :      mod->downup_read_request_latency[i]++; break;
				case mod_trans_downup_writeback_request : mod->downup_writeback_request_latency[i]++; break;
				case mod_trans_peer_request :             mod->peer_latency[i]++; break;
				case mod_trans_invalidate :               mod->invalidate_latency[i]++; break;
			}
			return;
		}
	}
}

void mod_update_mod_port_waiting_counters(struct mod_t *mod, struct mod_stack_t *stack)
{
	for(int i=0; i<6; i++)
	{
		if(req_variable_in_range(stack->mod_port_waiting_cycle, pow_2(i), pow_2(i+1) - 1))
		{
			if(stack->read)
			{
				if(stack->downup_read_request)
					mod->downup_read_time_waiting_mod_port[i]++;
				else
					mod->read_time_waiting_mod_port[i]++;
			}

			if(stack->write)
			{
				if(stack->evict_trans)
					mod->eviction_time_waiting_mod_port[i]++;

				if(stack->downup_writeback_request)
					mod->downup_writeback_time_waiting_mod_port[i]++;
				else
					mod->write_time_waiting_mod_port[i]++;
			}

			return;
		}
	}

	if(stack->read)
	{
		if(stack->downup_read_request)
			mod->downup_read_time_waiting_mod_port[5]++;
		else
			mod->read_time_waiting_mod_port[5]++;
	}

	if(stack->write)
	{
		if(stack->evict_trans)
			mod->eviction_time_waiting_mod_port[5]++;
		
		if(stack->downup_writeback_request)
			mod->downup_writeback_time_waiting_mod_port[5]++;
		else
			mod->write_time_waiting_mod_port[5]++;
	}
	
	return;
}

void mod_update_directory_lock_waiting_counters(struct mod_t *mod, struct mod_stack_t *stack)
{
	for(int i=0; i<6; i++)
	{
		if(req_variable_in_range(stack->directory_lock_waiting_cycle, pow_2(i), pow_2(i+1) - 1))
		{
			if(stack->read)
			{
				if(stack->downup_read_request)
					mod->downup_read_time_waiting_directory_lock[i]++;
				else
					mod->read_time_waiting_directory_lock[i]++;
			}

			if(stack->write)
			{
				if(stack->evict_trans)
					mod->eviction_time_waiting_directory_lock[i]++;

				if(stack->downup_writeback_request)
					mod->downup_writeback_time_waiting_directory_lock[i]++;
				else
					mod->write_time_waiting_directory_lock[i]++;
			}

			return;
		}
	}

	if(stack->read)
	{
		if(stack->downup_read_request)
			mod->downup_read_time_waiting_directory_lock[5]++;
		else
			mod->read_time_waiting_directory_lock[5]++;
	}

	if(stack->write)
	{
		if(stack->evict_trans)
			mod->eviction_time_waiting_directory_lock[5]++;

		if(stack->downup_writeback_request)
			mod->downup_writeback_time_waiting_directory_lock[5]++;
		else
			mod->write_time_waiting_directory_lock[5]++;
	}
	
	return;
}

void mod_update_waiting_counters(struct mod_t *mod, struct mod_stack_t *stack, enum mod_trans_type_t trans_type)
{
	assert(stack->load_access_waiting_for_store_cycle > 0);
	assert(stack->load_access_waiting_cycle > 0);
	assert(stack->store_access_waiting_cycle > 0);

	if(trans_type == mod_trans_load)
	{
		if(stack->load_access_waiting_for_store_cycle == 1)  { mod->loads_time_waiting_for_stores[0]++; return; }
		if(stack->load_access_waiting_for_store_cycle >= 32) { mod->loads_time_waiting_for_stores[4]++; return; }
		if(stack->load_access_waiting_cycle == 1)  { mod->loads_time_waiting_for_non_coalesced_accesses[0]++; return; }
		if(stack->load_access_waiting_cycle >= 32) { mod->loads_time_waiting_for_non_coalesced_accesses[4]++; return; }
	}

	if(trans_type == mod_trans_store)
	{
		if(stack->store_access_waiting_cycle == 1)  { mod->stores_time_waiting[0]++; return; }
		if(stack->store_access_waiting_cycle >= 32) { mod->stores_time_waiting[4]++; return; }
	}

	for(int i=0; i<5; i++)
	{
		if(trans_type == mod_trans_load)
		{
			if(req_variable_in_range(stack->load_access_waiting_for_store_cycle, pow_2(i+1), pow_2(i+2) - 1))
				mod->loads_time_waiting_for_stores[i]++;
			if(req_variable_in_range(stack->load_access_waiting_cycle, pow_2(i+1), pow_2(i+2) - 1))
				mod->loads_time_waiting_for_non_coalesced_accesses[i]++;
		}
		if(trans_type == mod_trans_store)
		{
			if(req_variable_in_range(stack->store_access_waiting_cycle, pow_2(i+1), pow_2(i+2) - 1))
				mod->stores_time_waiting[i]++;
		}
	}

	return;

}

void mod_update_simultaneous_flight_access_counters(struct mod_t *mod, unsigned int addr, struct mod_stack_t *older_than_stack, enum mod_trans_type_t trans_type)
{
	struct mod_stack_t *stack;
	struct mod_stack_t *current;

	int set_flag_load_exist = 0;
	int set_flag_store_exist = 0;
	int set_flag_eviction_exist = 0;
	int set_flag_downup_read_req_exist = 0;
	int set_flag_downup_wb_req_exist = 0;
	
	current = older_than_stack;

	/* For efficiency, first check in the hash table of accesses
	 * whether there is an access in flight to the same block. */
	do
	{
		stack = mod_in_flight_address(mod, addr, current);
		if(stack)
		{
			if(stack->read)
			{
				if(stack->downup_read_request)
					set_flag_downup_read_req_exist = 1;
				else
					set_flag_load_exist = 1;
		  }

			if(stack->write)
			{
				if(stack->evict_trans)
					set_flag_eviction_exist = 1;
				
				if(stack->downup_writeback_request)
					set_flag_downup_wb_req_exist = 1;
				else
					set_flag_store_exist = 1;
			}
		}
		current = stack;
	} while(stack);

	switch(trans_type)
	{
		case mod_trans_load : 
			{
			  if(set_flag_load_exist) mod->load_during_load_to_same_addr++;
			  if(set_flag_store_exist) mod->load_during_store_to_same_addr++;
			  if(set_flag_eviction_exist) mod->load_during_eviction_to_same_addr++;
			  if(set_flag_downup_read_req_exist) mod->load_during_downup_read_req_to_same_addr++;
			  if(set_flag_downup_wb_req_exist) mod->load_during_downup_wb_req_to_same_addr++;
			  break;
			}

		case mod_trans_store : 
			{
			  if(set_flag_load_exist) mod->store_during_load_to_same_addr++;
			  if(set_flag_store_exist) mod->store_during_store_to_same_addr++;
			  if(set_flag_eviction_exist) mod->store_during_eviction_to_same_addr++;
			  if(set_flag_downup_read_req_exist) mod->store_during_downup_read_req_to_same_addr++;
			  if(set_flag_downup_wb_req_exist) mod->store_during_downup_wb_req_to_same_addr++;
			  break;
			}
		
		case mod_trans_downup_read_request : 
			{									 		 
			  if(set_flag_load_exist) mod->downup_read_req_during_load_to_same_addr++;
			  if(set_flag_store_exist) mod->downup_read_req_during_store_to_same_addr++;
			  if(set_flag_eviction_exist) mod->downup_read_req_during_eviction_to_same_addr++;
			  if(set_flag_downup_read_req_exist) mod->downup_read_req_during_downup_read_req_to_same_addr++;
			  if(set_flag_downup_wb_req_exist) mod->downup_read_req_during_downup_wb_req_to_same_addr++;
			  break;
			}
	
		case mod_trans_downup_writeback_request : 
			{									 		 
				if(set_flag_load_exist) mod->downup_wb_req_during_load_to_same_addr++;
				if(set_flag_store_exist) mod->downup_wb_req_during_store_to_same_addr++;
				if(set_flag_eviction_exist) mod->downup_wb_req_during_eviction_to_same_addr++;
				if(set_flag_downup_read_req_exist) mod->downup_wb_req_during_downup_read_req_to_same_addr++;
				if(set_flag_downup_wb_req_exist) mod->downup_wb_req_during_downup_wb_req_to_same_addr++;
				break;
			}
	}
}
// mod_trans_load and mod_trans_store depict the same behaviour as read request and writeback on networks.
void mod_update_nw_send_request_delay_counters(struct mod_t *mod, struct mod_stack_t *stack, enum mod_trans_type_t trans_type)
{
	assert(stack->nw_send_request_latency_cycle > 0);

	if(req_variable_in_range(stack->nw_send_request_latency_cycle, 1, 1))
	{
		switch(trans_type)
		{
			case mod_trans_load 										:
			case mod_trans_read_request             : mod->read_send_requests_nw_cycles[0]++; break;
			case mod_trans_store 										:
			case mod_trans_writeback                : mod->writeback_send_requests_nw_cycles[0]++; break;
			case mod_trans_eviction                 : mod->eviction_send_requests_nw_cycles[0]++; break;
			case mod_trans_downup_read_request      : mod->downup_read_send_requests_nw_cycles[0]++; break;
			case mod_trans_downup_writeback_request : mod->downup_writeback_send_requests_nw_cycles[0]++; break;
			case mod_trans_downup_eviction_request  : mod->downup_eviction_send_requests_nw_cycles[0]++; break;
			case mod_trans_peer_request  						: mod->peer_send_requests_nw_cycles[0]++; break;
		}

		return;
	}
	
	if(stack->nw_send_request_latency_cycle >= 64)
	{
		switch(trans_type)
		{
			case mod_trans_load 										:
			case mod_trans_read_request             : mod->read_send_requests_nw_cycles[5]++; break;
			case mod_trans_store 										:
			case mod_trans_writeback                : mod->writeback_send_requests_nw_cycles[5]++; break;
			case mod_trans_eviction                 : mod->eviction_send_requests_nw_cycles[5]++; break;
			case mod_trans_downup_read_request      : mod->downup_read_send_requests_nw_cycles[5]++; break;
			case mod_trans_downup_writeback_request : mod->downup_writeback_send_requests_nw_cycles[5]++; break;
			case mod_trans_downup_eviction_request  : mod->downup_eviction_send_requests_nw_cycles[5]++; break;
			case mod_trans_peer_request  						: mod->peer_send_requests_nw_cycles[5]++; break;
		}

		return;
	}

	for(int i=0; i<6; i++)
	{
		if(req_variable_in_range(stack->nw_send_request_latency_cycle, pow_2(i+1), pow_2(i+2) - 1))
		{
			switch(trans_type)
			{
				case mod_trans_load 										:
				case mod_trans_read_request             : mod->read_send_requests_nw_cycles[i]++; break;
				case mod_trans_store 										:
				case mod_trans_writeback                : mod->writeback_send_requests_nw_cycles[i]++; break;
				case mod_trans_eviction                 : mod->eviction_send_requests_nw_cycles[i]++; break;
				case mod_trans_downup_read_request      : mod->downup_read_send_requests_nw_cycles[i]++; break;
				case mod_trans_downup_writeback_request : mod->downup_writeback_send_requests_nw_cycles[i]++; break;
				case mod_trans_downup_eviction_request  : mod->downup_eviction_send_requests_nw_cycles[i]++; break;
				case mod_trans_peer_request  						: mod->peer_send_requests_nw_cycles[i]++; break;
			}
			break;
		}
	}

}

void mod_update_nw_send_reply_delay_counters(struct mod_t *mod, struct mod_stack_t *stack, enum mod_trans_type_t trans_type)
{
	assert(stack->nw_send_reply_latency_cycle > 0);

	if(req_variable_in_range(stack->nw_send_reply_latency_cycle, 1, 1))
	{
		switch(trans_type)
		{
			case mod_trans_load 										:
			case mod_trans_read_request             : mod->read_send_replies_nw_cycles[0]++; break;
			case mod_trans_store 										:
			case mod_trans_writeback                : mod->writeback_send_replies_nw_cycles[0]++; break;
			case mod_trans_eviction                 : mod->eviction_send_replies_nw_cycles[0]++; break;
			case mod_trans_downup_read_request      : mod->downup_read_send_replies_nw_cycles[0]++; break;
			case mod_trans_downup_writeback_request : mod->downup_writeback_send_replies_nw_cycles[0]++; break;
			case mod_trans_downup_eviction_request  : mod->downup_eviction_send_replies_nw_cycles[0]++; break;
			case mod_trans_peer_request  						: mod->peer_send_replies_nw_cycles[0]++; break;
		}

		return;
	}
	
	if(stack->nw_send_reply_latency_cycle >= 64)
	{
		switch(trans_type)
		{
			case mod_trans_load 										:
			case mod_trans_read_request             : mod->read_send_replies_nw_cycles[5]++; break;
			case mod_trans_store 										:
			case mod_trans_writeback                : mod->writeback_send_replies_nw_cycles[5]++; break;
			case mod_trans_eviction                 : mod->eviction_send_replies_nw_cycles[5]++; break;
			case mod_trans_downup_read_request      : mod->downup_read_send_replies_nw_cycles[5]++; break;
			case mod_trans_downup_writeback_request : mod->downup_writeback_send_replies_nw_cycles[5]++; break;
			case mod_trans_downup_eviction_request  : mod->downup_eviction_send_replies_nw_cycles[5]++; break;
			case mod_trans_peer_request  						: mod->peer_send_replies_nw_cycles[5]++; break;
		}

		return;
	}

	for(int i=0; i<6; i++)
	{
		if(req_variable_in_range(stack->nw_send_reply_latency_cycle, pow_2(i+1), pow_2(i+2) - 1))
		{
			switch(trans_type)
			{
				case mod_trans_load 										:
				case mod_trans_read_request             : mod->read_send_replies_nw_cycles[i]++; break;
				case mod_trans_store 										:
				case mod_trans_writeback                : mod->writeback_send_replies_nw_cycles[i]++; break;
				case mod_trans_eviction                 : mod->eviction_send_replies_nw_cycles[i]++; break;
				case mod_trans_downup_read_request      : mod->downup_read_send_replies_nw_cycles[i]++; break;
				case mod_trans_downup_writeback_request : mod->downup_writeback_send_replies_nw_cycles[i]++; break;
				case mod_trans_downup_eviction_request  : mod->downup_eviction_send_replies_nw_cycles[i]++; break;
				case mod_trans_peer_request  						: mod->peer_send_replies_nw_cycles[i]++; break;
			}
			break;
		}
	}
}

void mod_update_nw_receive_request_delay_counters(struct mod_t *mod, struct mod_stack_t *stack, enum mod_trans_type_t trans_type)
{
	assert(stack->nw_receive_request_latency_cycle > 0);

	if(req_variable_in_range(stack->nw_receive_request_latency_cycle, 1, 1))
	{
		switch(trans_type)
		{
			case mod_trans_load 										:
			case mod_trans_read_request             : mod->read_receive_requests_nw_cycles[0]++; break;
			case mod_trans_store 										:
			case mod_trans_writeback                : mod->writeback_receive_requests_nw_cycles[0]++; break;
			case mod_trans_eviction                 : mod->eviction_receive_requests_nw_cycles[0]++; break;
			case mod_trans_downup_read_request      : mod->downup_read_receive_requests_nw_cycles[0]++; break;
			case mod_trans_downup_writeback_request : mod->downup_writeback_receive_requests_nw_cycles[0]++; break;
			case mod_trans_downup_eviction_request  : mod->downup_eviction_receive_requests_nw_cycles[0]++; break;
			case mod_trans_peer_request  						: mod->peer_receive_requests_nw_cycles[0]++; break;
		}

		return;
	}
	
	if(stack->nw_receive_request_latency_cycle >= 64)
	{
		switch(trans_type)
		{
			case mod_trans_load 										:
			case mod_trans_read_request             : mod->read_receive_requests_nw_cycles[5]++; break;
			case mod_trans_store 										:
			case mod_trans_writeback                : mod->writeback_receive_requests_nw_cycles[5]++; break;
			case mod_trans_eviction                 : mod->eviction_receive_requests_nw_cycles[5]++; break;
			case mod_trans_downup_read_request      : mod->downup_read_receive_requests_nw_cycles[5]++; break;
			case mod_trans_downup_writeback_request : mod->downup_writeback_receive_requests_nw_cycles[5]++; break;
			case mod_trans_downup_eviction_request  : mod->downup_eviction_receive_requests_nw_cycles[5]++; break;
			case mod_trans_peer_request  						: mod->peer_receive_requests_nw_cycles[5]++; break;
		}

		return;
	}

	for(int i=0; i<6; i++)
	{
		if(req_variable_in_range(stack->nw_receive_request_latency_cycle, pow_2(i+1), pow_2(i+2) - 1))
		{
			switch(trans_type)
			{
				case mod_trans_load 										:
				case mod_trans_read_request             : mod->read_receive_requests_nw_cycles[i]++; break;
				case mod_trans_store 										:
				case mod_trans_writeback                : mod->writeback_receive_requests_nw_cycles[i]++; break;
				case mod_trans_eviction                 : mod->eviction_receive_requests_nw_cycles[i]++; break;
				case mod_trans_downup_read_request      : mod->downup_read_receive_requests_nw_cycles[i]++; break;
				case mod_trans_downup_writeback_request : mod->downup_writeback_receive_requests_nw_cycles[i]++; break;
				case mod_trans_downup_eviction_request  : mod->downup_eviction_receive_requests_nw_cycles[i]++; break;
				case mod_trans_peer_request  						: mod->peer_receive_requests_nw_cycles[i]++; break;
			}
			break;
		}
	}

}

void mod_update_nw_receive_reply_delay_counters(struct mod_t *mod, struct mod_stack_t *stack, enum mod_trans_type_t trans_type)
{
	assert(stack->nw_receive_reply_latency_cycle > 0);

	if(req_variable_in_range(stack->nw_receive_reply_latency_cycle, 1, 1))
	{
		switch(trans_type)
		{
			case mod_trans_load 										:
			case mod_trans_read_request             : mod->read_receive_replies_nw_cycles[0]++; break;
			case mod_trans_store 										:
			case mod_trans_writeback                : mod->writeback_receive_replies_nw_cycles[0]++; break;
			case mod_trans_eviction                 : mod->eviction_receive_replies_nw_cycles[0]++; break;
			case mod_trans_downup_read_request      : mod->downup_read_receive_replies_nw_cycles[0]++; break;
			case mod_trans_downup_writeback_request : mod->downup_writeback_receive_replies_nw_cycles[0]++; break;
			case mod_trans_downup_eviction_request  : mod->downup_eviction_receive_replies_nw_cycles[0]++; break;
			case mod_trans_peer_request  						: mod->peer_receive_replies_nw_cycles[0]++; break;
		}

		return;
	}
	
	if(stack->nw_receive_reply_latency_cycle >= 64)
	{
		switch(trans_type)
		{
			case mod_trans_load 										:
			case mod_trans_read_request             : mod->read_receive_replies_nw_cycles[5]++; break;
			case mod_trans_store 										:
			case mod_trans_writeback                : mod->writeback_receive_replies_nw_cycles[5]++; break;
			case mod_trans_eviction                 : mod->eviction_receive_replies_nw_cycles[5]++; break;
			case mod_trans_downup_read_request      : mod->downup_read_receive_replies_nw_cycles[5]++; break;
			case mod_trans_downup_writeback_request : mod->downup_writeback_receive_replies_nw_cycles[5]++; break;
			case mod_trans_downup_eviction_request  : mod->downup_eviction_receive_replies_nw_cycles[5]++; break;
			case mod_trans_peer_request  						: mod->peer_receive_replies_nw_cycles[5]++; break;
		}

		return;
	}

	for(int i=0; i<6; i++)
	{
		if(req_variable_in_range(stack->nw_receive_reply_latency_cycle, pow_2(i+1), pow_2(i+2) - 1))
		{
			switch(trans_type)
			{
				case mod_trans_load 										:
				case mod_trans_read_request             : mod->read_receive_replies_nw_cycles[i]++; break;
				case mod_trans_store 										:
				case mod_trans_writeback                : mod->writeback_receive_replies_nw_cycles[i]++; break;
				case mod_trans_eviction                 : mod->eviction_receive_replies_nw_cycles[i]++; break;
				case mod_trans_downup_read_request      : mod->downup_read_receive_replies_nw_cycles[i]++; break;
				case mod_trans_downup_writeback_request : mod->downup_writeback_receive_replies_nw_cycles[i]++; break;
				case mod_trans_downup_eviction_request  : mod->downup_eviction_receive_replies_nw_cycles[i]++; break;
				case mod_trans_peer_request  						: mod->peer_receive_replies_nw_cycles[i]++; break;
			}
			break;
		}
	}
}
