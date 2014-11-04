/*
 *  Multi2Sim
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

#ifndef MEM_SYSTEM_MODULE_H
#define MEM_SYSTEM_MODULE_H

#include <stdio.h>
#include "cache.h"

/* Port */
struct mod_port_t
{
	/* Port lock status */
	int locked;
	long long lock_when;  /* Cycle when it was locked */
	struct mod_stack_t *stack;  /* Access locking port */

	/* Waiting list */
	struct mod_stack_t *waiting_list_head;
	struct mod_stack_t *waiting_list_tail;
	int waiting_list_count;
	int waiting_list_max;
};

/* String map for access type */
extern struct str_map_t mod_access_kind_map;

/*Transaction Type : This is used to update counters and can be used somehwere else. For utility, we have classified all types of transaction separately*/
enum mod_trans_type_t
{
	mod_trans_load = 0,
	mod_trans_store,
	mod_trans_read_request,
	mod_trans_writeback,
	mod_trans_eviction,
	mod_trans_downup_read_request,
	mod_trans_downup_eviction_request,
	mod_trans_downup_writeback_request,
	mod_trans_peer_request,
	mod_trans_invalidate
};

/* Access type */
enum mod_access_kind_t
{
	mod_access_invalid = 0,
	mod_access_load,
	mod_access_store,
	mod_access_nc_store,
	mod_access_prefetch
};

/* Module types */
enum mod_kind_t
{
	mod_kind_invalid = 0,
	mod_kind_cache,
	mod_kind_main_memory,
	mod_kind_local_memory
};

/* Any info that clients (cpu/gpu) can pass
 * to the memory system when mod_access() 
 * is called. */
struct mod_client_info_t
{
	/* This field is for use by the prefetcher. It is set
	 * to the PC of the instruction accessing the module */
	unsigned int prefetcher_eip;
};

/* Type of address range */
enum mod_range_kind_t
{
	mod_range_invalid = 0,
	mod_range_bounds,
	mod_range_interleaved
};

#define MOD_ACCESS_HASH_TABLE_SIZE  17

/* Memory module */
struct mod_t
{
	/* Parameters */
	enum mod_kind_t kind;
	char *name;
	int block_size;
	int log_block_size;
	int latency;
	int dir_latency;
	int mshr_size;

	/* Module level starting from entry points */
	int level;

	/* Address range served by module */
	enum mod_range_kind_t range_kind;
	union
	{
		/* For range_kind = mod_range_bounds */
		struct
		{
			unsigned int low;
			unsigned int high;
		} bounds;

		/* For range_kind = mod_range_interleaved */
		struct
		{
			unsigned int mod;
			unsigned int div;
			unsigned int eq;
		} interleaved;
	} range;

	/* Ports */
	struct mod_port_t *ports;
	int num_ports;
	int num_locked_ports;

	/* Accesses waiting to get a port */
	struct mod_stack_t *port_waiting_list_head;
	struct mod_stack_t *port_waiting_list_tail;
	int port_waiting_list_count;
	int port_waiting_list_max;

	/* Directory */
	struct dir_t *dir;
	int dir_size;
	int dir_assoc;
	int dir_num_sets;

	/* Waiting list of events */
	struct mod_stack_t *waiting_list_head;
	struct mod_stack_t *waiting_list_tail;
	int waiting_list_count;
	int waiting_list_max;

	/* Cache structure */
	struct cache_t *cache;

	/* Low and high memory modules */
	struct linked_list_t *high_mod_list;
	struct linked_list_t *low_mod_list;

	/* Smallest block size of high nodes. When there is no high node, the
	 * sub-block size is equal to the block size. */
	int sub_block_size;
	int num_sub_blocks;  /* block_size / sub_block_size */

	/* Interconnects */
	struct net_t *high_net;
	struct net_t *low_net;
	struct net_node_t *high_net_node;
	struct net_node_t *low_net_node;

	/* Access list */
	struct mod_stack_t *access_list_head;
	struct mod_stack_t *access_list_tail;
	int access_list_count;
	int access_list_max;

	/* Write access list */
	struct mod_stack_t *write_access_list_head;
	struct mod_stack_t *write_access_list_tail;
	int write_access_list_count;
	int write_access_list_max;

	/* Number of in-flight coalesced accesses. This is a number
	 * between 0 and 'access_list_count' at all times. */
	int access_list_coalesced_count;

	/* Clients (CPU/GPU) that use this module can fill in some
	 * optional information in the mod_client_info_t structure.
	 * Using a repos_t memory allocator for these structures. */
	struct repos_t *client_info_repos;

	/* Hash table of accesses */
	struct
	{
		struct mod_stack_t *bucket_list_head;
		struct mod_stack_t *bucket_list_tail;
		int bucket_list_count;
		int bucket_list_max;
	} access_hash_table[MOD_ACCESS_HASH_TABLE_SIZE];

	/* Architecture accessing this module. For versions of Multi2Sim where it is
	 * allowed to have multiple architectures sharing the same subset of the
	 * memory hierarchy, the field is used to check this restriction. */
	struct arch_t *arch;
	
	/* Statistics */
	long long accesses;
	long long hits;

	long long reads;
	long long effective_reads;
	long long effective_read_hits;
	long long writes;
	long long effective_writes;
	long long effective_write_hits;
	long long nc_writes;
	long long effective_nc_writes;
	long long effective_nc_write_hits;
	long long prefetches;
	long long prefetch_aborts;
	long long useless_prefetches;
	long long evictions;

	long long blocking_reads;
	long long non_blocking_reads;
	long long read_hits;
	long long blocking_writes;
	long long non_blocking_writes;
	long long write_hits;
	long long blocking_nc_writes;
	long long non_blocking_nc_writes;
	long long nc_write_hits;

	long long read_retries;
	long long write_retries;
	long long nc_write_retries;

	long long no_retry_accesses;
	long long no_retry_hits;
	long long no_retry_reads;
	long long no_retry_read_hits;
	long long no_retry_writes;
	long long no_retry_write_hits;
	long long no_retry_nc_writes;
	long long no_retry_nc_write_hits;

	//---------------------------------------------------
	// STATISTICS FOR ACCESS COUNTS : number of loads (or up-down read requests for lower level modules), store (or up-down writeback requests for lower level modules), eviction requests generated, down-up read/writeback requests and up-down read and write-back requests.
	// Up-down read requests generated should be equal to load misses and Up-down writeback requests grenerated must be equal to store hits in M/O state (depending on the state of protocol).
 	// The statistics generated by the original code are the overall hits where there is no differentiation of Down-up or up-down requests.	
	// Evictions can be measured by the evictions statistics stated above.
	// In a directory based schemes, downup hits and misses donot make sense, but this has been kept to monitor the records in snoop based schemes.
	// Up-down Writeback is further divided as caused due to eviction or a store request, and its hits/misses are monitored separately. 
	//---------------------------------------------------
	long long load_requests;
	long long load_requests_hits;
	long long load_requests_misses;

	long long store_requests;
	long long store_requests_hits;
	long long store_requests_misses;

	long long downup_read_requests;
	long long downup_read_requests_hits;
	long long downup_read_requests_misses;

	long long downup_writeback_requests;
	long long downup_writeback_requests_hits;
	long long downup_writeback_requests_misses;
	
	long long updown_read_requests_generated;
	long long updown_writeback_requests_generated;

	long long writeback_due_to_eviction;
	long long writeback_due_to_eviction_hits;
	long long writeback_due_to_eviction_misses;

	//-----------------------------------------------------------
	// STATISTICS FOR Coalesced accesses and accesses that wait for other accesses, only for top module Load-Store accesses. Just to find out the scalability or locality and dependency in the controller. Anyways, stores wait for other accesses.
	//-----------------------------------------------------------
	long long coalesced_loads;
	long long coalesced_stores;
	long long loads_waiting_for_non_coalesced_accesses;
	long long loads_waiting_for_stores;
	
	//----------------------------------------------------------
	// STATISTICS for waiting times of accesses waiting in the process.
	// The ranges are 1-3, 4-7, 8-15, 16-31, >32 cycles.
	//----------------------------------------------------------
	long long loads_time_waiting_for_non_coalesced_accesses[5];
	long long loads_time_waiting_for_stores[5];
	long long stores_time_waiting[5];

	//---------------------------------------------------
	// STATISTICS FOR WAITING ACCESSES : An access can be waiting due to the following reasons, in the FIND & LOCK operation, waiting for a module port or waiting for the directory lock.
	// We categorise each access i.e. Read (load for top most level and up-down read request for the remaining lower levels), Write (store for top most level and up-down writeback request for remaining lower level), eviction, down-up writeback request and down-up read request. 
	// We also try to find out the latency in case of the mod_ports, though the division is 1-8 cycles, 9-24, 25-50 and greater than 50 cycles.
	//---------------------------------------------------
	long long read_waiting_for_mod_port;
	long long read_waiting_for_directory_lock; 
	long long max_sim_read_waiting_for_mod_port; // TBD
	long long max_sim_read_waiting_for_directory_lock; // TBD
	long long read_waiting_for_other_accesses;
	
	long long write_waiting_for_mod_port;
	long long write_waiting_for_directory_lock; 
	long long max_sim_write_waiting_for_mod_port; // TBD
	long long max_sim_write_waiting_for_directory_lock; // TBD
	long long write_waiting_for_other_accesses;
	
	long long eviction_waiting_for_mod_port;
	long long eviction_waiting_for_directory_lock; 
	long long max_sim_eviction_waiting_for_mod_port; // TBD
	long long max_sim_eviction_waiting_for_directory_lock; // TBD
	long long eviction_waiting_for_other_accesses;

	long long downup_read_waiting_for_mod_port;
	long long downup_read_waiting_for_directory_lock; 
	long long max_sim_downup_read_waiting_for_mod_port; // TBD
	long long max_sim_downup_read_waiting_for_directory_lock; // TBD
	long long downup_read_waiting_for_other_accesses;

	long long downup_writeback_waiting_for_mod_port;
	long long downup_writeback_waiting_for_directory_lock; 
	long long max_sim_downup_writeback_waiting_for_mod_port; // TBD
	long long max_sim_downup_writeback_waiting_for_directory_lock; // TBD
	long long downup_writeback_waiting_for_other_accesses;

	//-------------------------------------------------------
	// Waiting statistics while the event is waiting for a module port or a directory lock.
	//-------------------------------------------------------
	long long read_time_waiting_mod_port[6];
	long long write_time_waiting_mod_port[6];
	long long eviction_time_waiting_mod_port[6];
	long long downup_read_time_waiting_mod_port[6];
	long long downup_writeback_time_waiting_mod_port[6];
	
	long long read_time_waiting_directory_lock[6];
	long long write_time_waiting_directory_lock[6];
	long long eviction_time_waiting_directory_lock[6];
	long long downup_read_time_waiting_directory_lock[6];
	long long downup_writeback_time_waiting_directory_lock[6];

	//------------------------------------------------------
	// STATISTICS of simultaneous access, i.e. a down-up request and a processor request to the same line
	// This can be seen as following cases for same address accesses:
  // 1.) load access when any of exisiting load, store, eviction, down-up read or down-up writeback
  // 2.) store access when any of exisiting load, store, eviction, down-up read or down-up writeback
  // 3.) down-up read request access when any of exisiting load, store, eviction, down-up read or down-up writeback
  // 4.) down-up writeback request when any of exisitng load, store, eviction, down-up read or down-up writeback
	//------------------------------------------------------
	long long load_during_load_to_same_addr;
	long long load_during_store_to_same_addr;
	long long load_during_eviction_to_same_addr;
	long long load_during_downup_read_req_to_same_addr;
	long long load_during_downup_wb_req_to_same_addr;

	long long store_during_load_to_same_addr;
	long long store_during_store_to_same_addr;
	long long store_during_eviction_to_same_addr;
	long long store_during_downup_read_req_to_same_addr;
	long long store_during_downup_wb_req_to_same_addr;

	long long downup_read_req_during_load_to_same_addr;
	long long downup_read_req_during_store_to_same_addr;
	long long downup_read_req_during_eviction_to_same_addr;
	long long downup_read_req_during_downup_read_req_to_same_addr;
	long long downup_read_req_during_downup_wb_req_to_same_addr;

	long long downup_wb_req_during_load_to_same_addr;
	long long downup_wb_req_during_store_to_same_addr;
	long long downup_wb_req_during_eviction_to_same_addr;
	long long downup_wb_req_during_downup_read_req_to_same_addr;
	long long downup_wb_req_during_downup_wb_req_to_same_addr;

	//------------------------------------------------------
	// STATISTICS of data transfers
	//------------------------------------------------------
	//----------Down-up data transfers----------------------
	// Down-up data transfer implies that to fulfil/proceed the request the data had to be transferred from a higher level module
	//------------------------------------------------------
	// A higher level module, may be at same level, but not the peer or even above the requesting module sent the data for a load.
	long long data_transfer_downup_load_request;
	// A higher level module, may be at same level, but not the peer or even above the requesting module sent the data for store.
	long long data_transfer_downup_store_request;
	// A higher level module, at same level, or even above the requesting module sent the data for eviction.
	long long data_transfer_downup_eviction_request;
	// Peer module, at same level, sent the data for load.
	long long peer_data_transfer_downup_load_request;
	// Peer module, at same level, sent the data for store.
	long long peer_data_transfer_downup_store_request;
	// Lower level module, sent the data for load.
	long long data_transfer_updown_load_request;
	// Lower level module, sent the data for store.
	long long data_transfer_updown_store_request;
	// Data was transferred for eviction from the current module.
	long long data_transfer_eviction;
	
	//------------------------------------------------------
	// STATISTICS of evictions
	//------------------------------------------------------
	//-------------EVICTION SUMMARY-------------------------
	long long eviction_due_to_load;
	long long eviction_due_to_store;

	//-------------EVICTION STATISTICS : Requests to state on a cache------------
	//Assertions can be made that for MSI the counter for O/E should be 0, for MESI counter for O should be 0.
	long long eviction_request_state_invalid;
	long long eviction_request_state_modified;
	long long eviction_request_state_owned;
	long long eviction_request_state_exclusive;
	long long eviction_request_state_shared;
	long long eviction_request_state_noncoherent;

	//---------------------------------------------------------
	// STATISTICS for hit -> evict -> miss (TBD)
	// This measures special statistics where it may happen that a Load Miss caused an eviction in the cache and the replacement entry was required again thus causing an unnecessary fetch from the lower level modules, thus adding to further delay. these can serve as basic blocks for some foundations in the replacement techniques. The minimum criteria is a Hit to an access that was evicted within 1000 cycles of its eviction.
	//---------------------------------------------------------
	long long load_miss_due_to_eviction;
	long long store_miss_due_to_eviction;

	//----------------------------------------------------------
	// Variable to gather statistics for the number of simultaneous requests
	// gather this number and use it for coverage type work
	//----------------------------------------------------------
	long long num_load_requests;
	long long num_store_requests;
	long long num_eviction_requests;
	long long num_read_requests;
	long long num_writeback_requests;
	long long num_downup_read_requests;
	long long num_downup_writeback_requests;
	long long num_downup_eviction_requests;

	//--------------------------------------------------------
	// STATISTICS for controller occupancy : 
	// These staistics show what was the request count on  a particular module at a given time.
  // Load/Store requests indicate the Load/Store generated by processor on the higher most cache and Up-down read/write requests on the lower caches.
 	// Eviction indicate the number of eviction transactions happening on the level
  // read/writeback indicate the number of read/writeback requests issued to lower level of memory
  // Down-up Read and write back requests indicate the remote read and writeback requests received,
 	// Down-up eviction requests indicate the number of downup writeback requests received as a result of eviction from the other peer (same level)
 	// Total processor requests indicate the number of Load and Store
  // Controller requests indicate the total number of read, eviction and writeback requests
  // total updown requests indicate the sum of processor and controller requests
  // total downup requests indicate the sum of all down-up requests arising due to Load/Store/Eviction
  // total requests indicate the total number of requests on the controller.
	//--------------------------------------------------------
	long long request_load[10];
	long long request_store[10];
	long long request_eviction[10];
	long long request_read[10];
	long long request_writeback[10];
	long long request_downup_read[10];
	long long request_downup_writeback[10];
	long long request_downup_eviction[10];
	long long request_processor[11];
	long long request_controller[11];
	long long request_updown[11];
	long long request_downup[11];
	long long request_total[12];

	//---------------------------------------------------------
	// STATISTICS FOR LATENCY
	// Latency is measured in terms of cycle, here we measure latency as the the cycles that it takes to complete the request, rather than the arrival of first data stream for Load. The array indices represent the interval, say index i represents the interval [2^(i-1), 2^(i) - 1] and last indice represents all values >= 2^(i)
	// Latency for eviction is further divided into the eviction for dirty lines and clean lines.
	//---------------------------------------------------------
	long long load_latency[10];
	long long store_latency[10];
	long long eviction_latency[10];
	long long downup_read_request_latency[10];
	long long downup_writeback_request_latency[10];
	long long writeback_request_latency[10];
	long long read_request_latency[10];
	long long peer_latency[10];
	long long invalidate_latency[10];
	
	//----------------------------------------------------
	// STATISTICS FOR NETWORK CONGESTION
	// These counters indicate the requests and replies that were retried on the network due to busy state. From the given module the request may be retried or corresponding replies may be retried based on the netowrk contention. Request contention is basically on Lower level network and reply contention on the higher level network, in case of no lower or higher network (e.g. main memory or top level cache) the request and reply counter accordingly is zero.
  // There are statistics for the amount of cycles that are spent busy waiting for the network. Currently they are classified as 1-3, 4-7, 8-15, 16-31, 32-63 and >64 cycles for each classification.	
	// This information is for send request and replies message only, there is no waiting at receive hence there are no  retried counters on reception.
	// Make delay counters for reception as well.
	//----------------------------------------------------
	long long read_send_requests_retried_nw;
	long long writeback_send_requests_retried_nw;
	long long eviction_send_requests_retried_nw;
	long long downup_read_send_requests_retried_nw;
	long long downup_writeback_send_requests_retried_nw;
	long long downup_eviction_send_requests_retried_nw;
	long long peer_send_requests_retried_nw;

	long long read_send_replies_retried_nw;
	long long writeback_send_replies_retried_nw;
	long long eviction_send_replies_retried_nw;
	long long downup_read_send_replies_retried_nw;
	long long downup_writeback_send_replies_retried_nw;
	long long downup_eviction_send_replies_retried_nw;
	long long peer_send_replies_retried_nw;

	long long read_send_requests_nw_cycles[6];
	long long writeback_send_requests_nw_cycles[6];
	long long eviction_send_requests_nw_cycles[6];
	long long downup_read_send_requests_nw_cycles[6];
	long long downup_writeback_send_requests_nw_cycles[6];
	long long downup_eviction_send_requests_nw_cycles[6];
	long long peer_send_requests_nw_cycles[6];

	long long read_send_replies_nw_cycles[6];
	long long writeback_send_replies_nw_cycles[6];
	long long eviction_send_replies_nw_cycles[6];
	long long downup_read_send_replies_nw_cycles[6];
	long long downup_writeback_send_replies_nw_cycles[6];
	long long downup_eviction_send_replies_nw_cycles[6];
	long long peer_send_replies_nw_cycles[6];

	long long read_receive_requests_nw_cycles[6];
	long long writeback_receive_requests_nw_cycles[6];
	long long eviction_receive_requests_nw_cycles[6];
	long long downup_read_receive_requests_nw_cycles[6];
	long long downup_writeback_receive_requests_nw_cycles[6];
	long long downup_eviction_receive_requests_nw_cycles[6];
	long long peer_receive_requests_nw_cycles[6];

	long long read_receive_replies_nw_cycles[6];
	long long writeback_receive_replies_nw_cycles[6];
	long long eviction_receive_replies_nw_cycles[6];
	long long downup_read_receive_replies_nw_cycles[6];
	long long downup_writeback_receive_replies_nw_cycles[6];
	long long downup_eviction_receive_replies_nw_cycles[6];
	long long peer_receive_replies_nw_cycles[6];

	//-----------------------------------------------
	// MISCELLANEOUS STATISTICS
	//-----------------------------------------------
	//-------------PEER TRANSFERS-----------------
	long long peer_transfers;
	//-------------DOWNUP INVALIDATION REQUESTS---
	long long sharer_req_for_invalidation;

	//----------------------------------------------------
	// STATISTICS FOR CACHE STATES - Statistics capturing what all states were present, when a read/write or a down-up request was made.
	//----------------------------------------------------
	long long read_state_invalid;
	long long read_state_noncoherent;
	long long read_state_modified;
	long long read_state_shared;
	long long read_state_owned;
	long long read_state_exclusive;

	long long write_state_invalid;
	long long write_state_noncoherent;
	long long write_state_modified;
	long long write_state_shared;
	long long write_state_owned;
	long long write_state_exclusive;

	long long sharer_req_state_invalid;
	long long sharer_req_state_noncoherent;
	long long sharer_req_state_modified;
	long long sharer_req_state_shared;
	long long sharer_req_state_owned;
	long long sharer_req_state_exclusive;
	
	//----------------------------------------------------
	// STATISTICS FOR CACHE STATE TRANSITION - Statistics capturing what all state transition happened.
	// The same counter can be used for a up-down request counter as well as lower level process doesn't issue any Load/Store hence the values observed can be used to increment the up-down values
	// We use statistic counter for each type of transition, i.e. Load, Store, Down-up Read, Down-up WriteBack transaction.
	// For a lower level module the eviction from a higher level transaction can cause a change/allocation in line, hence that is also noted here. (store_due_to_evict)
	// The statistics of down-up eviction and store due to eviction are a subset of down-up writeback and store accesses respectively and hence not considered here.
 	// There are some state transitions that can't happen but are kept just for model checking such as down-up writeback causing a valid to valid transition.
	//----------------------------------------------------
	// Statistics for Loads. A Load cannot have transitions like I -> O/M (for all), I -> E (for MSI) and S -> M/O/E/I (for all) and E -> M/O/S/I or E -> * (for MSI) and M -> O/S/E/I and O -> M/E/S/I or O->* (for MSI and MESI) as Load Hits are progressed without any advancement to system.
	long long load_state_invalid_to_invalid;
	long long load_state_invalid_to_noncoherent;
	long long load_state_invalid_to_modified;
	long long load_state_invalid_to_shared;
	long long load_state_invalid_to_owned;
	long long load_state_invalid_to_exclusive;

	long long load_state_noncoherent_to_invalid;
	long long load_state_noncoherent_to_noncoherent;
	long long load_state_noncoherent_to_modified;
	long long load_state_noncoherent_to_shared;
	long long load_state_noncoherent_to_owned;
	long long load_state_noncoherent_to_exclusive;

	long long load_state_modified_to_invalid;
	long long load_state_modified_to_noncoherent;
	long long load_state_modified_to_modified;
	long long load_state_modified_to_shared;
	long long load_state_modified_to_owned;
	long long load_state_modified_to_exclusive;

	long long load_state_shared_to_invalid;
	long long load_state_shared_to_noncoherent;
	long long load_state_shared_to_modified;
	long long load_state_shared_to_shared;
	long long load_state_shared_to_owned;
	long long load_state_shared_to_exclusive;

	long long load_state_owned_to_invalid;
	long long load_state_owned_to_noncoherent;
	long long load_state_owned_to_modified;
	long long load_state_owned_to_shared;
	long long load_state_owned_to_owned;
	long long load_state_owned_to_exclusive;

	long long load_state_exclusive_to_invalid;
	long long load_state_exclusive_to_noncoherent;
	long long load_state_exclusive_to_modified;
	long long load_state_exclusive_to_shared;
	long long load_state_exclusive_to_owned;
	long long load_state_exclusive_to_exclusive;

	// Statistics for Stores. A store cannot have transitions like I -> I/O/E/S (for all) and E -> O/S/I or E -> * (for MSI) and M -> O/S/E/I and O -> O/E/S/I or O->* (for MSI and MESI) as Stores result in final state of Modified only..
	long long store_state_invalid_to_invalid;
	long long store_state_invalid_to_noncoherent;
	long long store_state_invalid_to_modified;
	long long store_state_invalid_to_shared;
	long long store_state_invalid_to_owned;
	long long store_state_invalid_to_exclusive;

	long long store_state_noncoherent_to_invalid;
	long long store_state_noncoherent_to_noncoherent;
	long long store_state_noncoherent_to_modified;
	long long store_state_noncoherent_to_shared;
	long long store_state_noncoherent_to_owned;
	long long store_state_noncoherent_to_exclusive;

	long long store_state_modified_to_invalid;
	long long store_state_modified_to_noncoherent;
	long long store_state_modified_to_modified;
	long long store_state_modified_to_shared;
	long long store_state_modified_to_owned;
	long long store_state_modified_to_exclusive;

	long long store_state_shared_to_invalid;
	long long store_state_shared_to_noncoherent;
	long long store_state_shared_to_modified;
	long long store_state_shared_to_shared;
	long long store_state_shared_to_owned;
	long long store_state_shared_to_exclusive;

	long long store_state_owned_to_invalid;
	long long store_state_owned_to_noncoherent;
	long long store_state_owned_to_modified;
	long long store_state_owned_to_shared;
	long long store_state_owned_to_owned;
	long long store_state_owned_to_exclusive;

	long long store_state_exclusive_to_invalid;
	long long store_state_exclusive_to_noncoherent;
	long long store_state_exclusive_to_modified;
	long long store_state_exclusive_to_shared;
	long long store_state_exclusive_to_owned;
	long long store_state_exclusive_to_exclusive;

	//--------------------------------------------
	// STATISTICS FOR DOWN-UP REQUESTS
	//--------------------------------------------
	
	//-------------STATE TRANSITIONS BY A DOWNUP READ REQUEST--------------
	// A Downup read request cannot have transitions like * -> E/O (for MSI), * -> O for (MOSI) and * -> M in general.
	long long downup_read_req_state_invalid_to_invalid;
	long long downup_read_req_state_invalid_to_noncoherent;
	long long downup_read_req_state_invalid_to_modified;
	long long downup_read_req_state_invalid_to_shared;
	long long downup_read_req_state_invalid_to_owned;
	long long downup_read_req_state_invalid_to_exclusive;

	long long downup_read_req_state_noncoherent_to_invalid;
	long long downup_read_req_state_noncoherent_to_noncoherent;
	long long downup_read_req_state_noncoherent_to_modified;
	long long downup_read_req_state_noncoherent_to_shared;
	long long downup_read_req_state_noncoherent_to_owned;
	long long downup_read_req_state_noncoherent_to_exclusive;

	long long downup_read_req_state_modified_to_invalid;
	long long downup_read_req_state_modified_to_noncoherent;
	long long downup_read_req_state_modified_to_modified;
	long long downup_read_req_state_modified_to_shared;
	long long downup_read_req_state_modified_to_owned;
	long long downup_read_req_state_modified_to_exclusive;

	long long downup_read_req_state_shared_to_invalid;
	long long downup_read_req_state_shared_to_noncoherent;
	long long downup_read_req_state_shared_to_modified;
	long long downup_read_req_state_shared_to_shared;
	long long downup_read_req_state_shared_to_owned;
	long long downup_read_req_state_shared_to_exclusive;

	long long downup_read_req_state_owned_to_invalid;
	long long downup_read_req_state_owned_to_noncoherent;
	long long downup_read_req_state_owned_to_modified;
	long long downup_read_req_state_owned_to_shared;
	long long downup_read_req_state_owned_to_owned;
	long long downup_read_req_state_owned_to_exclusive;

	long long downup_read_req_state_exclusive_to_invalid;
	long long downup_read_req_state_exclusive_to_noncoherent;
	long long downup_read_req_state_exclusive_to_modified;
	long long downup_read_req_state_exclusive_to_shared;
	long long downup_read_req_state_exclusive_to_owned;
	long long downup_read_req_state_exclusive_to_exclusive;
	
	//-------------STATE TRANSITIONS BY A DOWNUP WRITEBACK REQUEST--------------
	// A Downup writeback request cannot have transitions like * -> E/O  and E/O -> *(for MSI), * -> O and O -> * for (MOSI) and * -> M or I -> * or M/O/E/S -> M/O/E/S in general.
	long long downup_wb_req_state_invalid_to_invalid;
	long long downup_wb_req_state_invalid_to_noncoherent;
	long long downup_wb_req_state_invalid_to_modified;
	long long downup_wb_req_state_invalid_to_shared;
	long long downup_wb_req_state_invalid_to_owned;
	long long downup_wb_req_state_invalid_to_exclusive;

	long long downup_wb_req_state_noncoherent_to_invalid;
	long long downup_wb_req_state_noncoherent_to_noncoherent;
	long long downup_wb_req_state_noncoherent_to_modified;
	long long downup_wb_req_state_noncoherent_to_shared;
	long long downup_wb_req_state_noncoherent_to_owned;
	long long downup_wb_req_state_noncoherent_to_exclusive;

	long long downup_wb_req_state_modified_to_invalid;
	long long downup_wb_req_state_modified_to_noncoherent;
	long long downup_wb_req_state_modified_to_modified;
	long long downup_wb_req_state_modified_to_shared;
	long long downup_wb_req_state_modified_to_owned;
	long long downup_wb_req_state_modified_to_exclusive;

	long long downup_wb_req_state_shared_to_invalid;
	long long downup_wb_req_state_shared_to_noncoherent;
	long long downup_wb_req_state_shared_to_modified;
	long long downup_wb_req_state_shared_to_shared;
	long long downup_wb_req_state_shared_to_owned;
	long long downup_wb_req_state_shared_to_exclusive;

	long long downup_wb_req_state_owned_to_invalid;
	long long downup_wb_req_state_owned_to_noncoherent;
	long long downup_wb_req_state_owned_to_modified;
	long long downup_wb_req_state_owned_to_shared;
	long long downup_wb_req_state_owned_to_owned;
	long long downup_wb_req_state_owned_to_exclusive;

	long long downup_wb_req_state_exclusive_to_invalid;
	long long downup_wb_req_state_exclusive_to_noncoherent;
	long long downup_wb_req_state_exclusive_to_modified;
	long long downup_wb_req_state_exclusive_to_shared;
	long long downup_wb_req_state_exclusive_to_owned;
	long long downup_wb_req_state_exclusive_to_exclusive;

	//----------------------------------------------------
	// Some statistics for ACE Bus :
  // Accesses issued as INCR/WRAP.
	// What is the continuity or length of these accesses, intervals are 0-3, 4-7, 8-15, >16 ?
	//----------------------------------------------------
	long long num_accesses_incr;
	long long num_accesses_wrap;

	long long num_accesses_incr_range[4];
	long long num_accesses_wrap_range[4];

	//---------------------------------------------------
	// Statistics for writeback activity, usually contains the number of time a block was in shared/exclusive state while the modified state was chosen.
	//---------------------------------------------------
	long long num_accesses_modified_over_shared;
};

struct mod_t *mod_create(char *name, enum mod_kind_t kind, int num_ports,
	int block_size, int latency);
void mod_free(struct mod_t *mod);
void mod_dump(struct mod_t *mod, FILE *f);
void mod_stack_set_reply(struct mod_stack_t *stack, int reply);
struct mod_t *mod_stack_set_peer(struct mod_t *peer, int state);

long long mod_access(struct mod_t *mod, enum mod_access_kind_t access_kind, 
	unsigned int addr, int *witness_ptr, struct linked_list_t *event_queue,
	void *event_queue_item, struct mod_client_info_t *client_info);
int mod_can_access(struct mod_t *mod, unsigned int addr);

int mod_find_block(struct mod_t *mod, unsigned int addr, int *set_ptr, int *way_ptr, 
	int *tag_ptr, int *state_ptr);

void mod_block_set_prefetched(struct mod_t *mod, unsigned int addr, int val);
int mod_block_get_prefetched(struct mod_t *mod, unsigned int addr);

void mod_lock_port(struct mod_t *mod, struct mod_stack_t *stack, int event);
void mod_unlock_port(struct mod_t *mod, struct mod_port_t *port,
	struct mod_stack_t *stack);

void mod_access_start(struct mod_t *mod, struct mod_stack_t *stack,
	enum mod_access_kind_t access_kind);
void mod_access_finish(struct mod_t *mod, struct mod_stack_t *stack);

int mod_in_flight_access(struct mod_t *mod, long long id, unsigned int addr);
struct mod_stack_t *mod_in_flight_address(struct mod_t *mod, unsigned int addr,
	struct mod_stack_t *older_than_stack);
struct mod_stack_t *mod_in_flight_write(struct mod_t *mod,
	struct mod_stack_t *older_than_stack);

int mod_serves_address(struct mod_t *mod, unsigned int addr);
struct mod_t *mod_get_low_mod(struct mod_t *mod, unsigned int addr);

int mod_get_retry_latency(struct mod_t *mod);

struct mod_stack_t *mod_can_coalesce(struct mod_t *mod,
	enum mod_access_kind_t access_kind, unsigned int addr,
	struct mod_stack_t *older_than_stack);
void mod_coalesce(struct mod_t *mod, struct mod_stack_t *master_stack,
	struct mod_stack_t *stack);

struct mod_client_info_t *mod_client_info_create(struct mod_t *mod);
void mod_client_info_free(struct mod_t *mod, struct mod_client_info_t *client_info);

// FUNCTIONS ADDED
int req_variable_in_range(int req_var, int lb, int ub);
int pow_2(int i);

void mod_update_request_counters(struct mod_t *mod, enum mod_trans_type_t trans_type);

void mod_update_state_modification_counters(struct mod_t *mod, enum cache_block_state_t prev_state, enum cache_block_state_t next_state, enum mod_trans_type_t trans_type);

void mod_update_latency_counters(struct mod_t *mod, long long latency, enum mod_trans_type_t trans_type);

void mod_update_nw_send_request_delay_counters(struct mod_t *mod, struct mod_stack_t *stack, enum mod_trans_type_t trans_type);
void mod_update_nw_send_reply_delay_counters(struct mod_t *mod, struct mod_stack_t *stack, enum mod_trans_type_t trans_type);
void mod_update_nw_receive_request_delay_counters(struct mod_t *mod, struct mod_stack_t *stack, enum mod_trans_type_t trans_type);
void mod_update_nw_receive_reply_delay_counters(struct mod_t *mod, struct mod_stack_t *stack, enum mod_trans_type_t trans_type);

void mod_update_mod_port_waiting_counters(struct mod_t *mod, struct mod_stack_t *stack);
void mod_update_directory_lock_waiting_counters(struct mod_t *mod, struct mod_stack_t *stack);
void mod_update_waiting_counters(struct mod_t *mod, struct mod_stack_t *stack, enum mod_trans_type_t trans_type);

void mod_update_simultaneous_flight_access_counters(struct mod_t *mod, unsigned int addr, struct mod_stack_t *older_than_stack, enum mod_trans_type_t trans_type);
#endif

