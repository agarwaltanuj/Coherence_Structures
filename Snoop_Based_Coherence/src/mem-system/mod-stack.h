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

#ifndef MEM_SYSTEM_MOD_STACK_H
#define MEM_SYSTEM_MOD_STACK_H

#include "module.h"


/* Current identifier for stack */
extern long long mod_stack_id;

/* Read/write request direction */
enum mod_request_dir_t
{
	mod_request_invalid = 0,
	mod_request_up_down,
	mod_request_down_up
};

/* ACK types */
enum mod_reply_type_t
{
	reply_none = 0,
	reply_ack ,
	reply_ack_data,
	reply_ack_data_sent_to_peer,
	reply_ack_error
};

/* Message types */
enum mod_message_type_t
{
	message_none = 0,
	message_clear_owner
};

/* Stack */
struct mod_stack_t
{
	long long id;
	enum mod_access_kind_t access_kind;
	enum mod_trans_type_t  trans_type;
	int *witness_ptr;

	struct linked_list_t *event_queue;
	void *event_queue_item;
	struct mod_client_info_t *client_info;

	struct mod_t *mod;
	struct mod_t *target_mod;
	struct mod_t *except_mod;
	struct mod_t *peer;

	// Creating Module ID. This is used to indicate which module initiated the transaction, so that the snoop request sent doesn't propagate back to the originating module and other verification purposes.
	int orig_mod_id;
 // This is to indicate which module started the transaction. "orig_mod_id" indicates which Module issued the transaction, say a L2 module sending invalidate to various upper level modules, issue_mod_id indicate which of the following started the original Load/Store/Prefetch instruction.	
	int issue_mod_id; 

	struct mod_port_t *port;

	unsigned int addr;
	int tag;
	int set;
	int way;
	int state;
	int prev_state;							// used to collect statistics

	int src_set;
	int src_way;
	int src_tag;

	//DTS
	int replace_tag;

	enum mod_request_dir_t request_dir;
	enum mod_message_type_t message;
	enum mod_reply_type_t reply;
	int reply_size;
	int retain_owner;
	int pending;

	/* Linked list of accesses in 'mod' */
	struct mod_stack_t *access_list_prev;
	struct mod_stack_t *access_list_next;

	/* Linked list of write accesses in 'mod' */
	struct mod_stack_t *write_access_list_prev;
	struct mod_stack_t *write_access_list_next;

	/* Bucket list of accesses in hash table in 'mod' */
	struct mod_stack_t *bucket_list_prev;
	struct mod_stack_t *bucket_list_next;

	/* Linked list of accesses in 'mod' */
	struct mod_stack_t *trans_access_list_prev;
	struct mod_stack_t *trans_access_list_next;

	/* Bucket list of accesses in hash table in 'mod' */
	struct mod_stack_t *trans_bucket_list_prev;
	struct mod_stack_t *trans_bucket_list_next;

	/* Linked list of Downup accesses in 'mod' */
	struct mod_stack_t *downup_access_list_prev;
	struct mod_stack_t *downup_access_list_next;

	/* Bucket list of Downup accesses in hash table in 'mod' */
	struct mod_stack_t *downup_bucket_list_prev;
	struct mod_stack_t *downup_bucket_list_next;

	/* Linked list of Updown read write accesses in 'mod' */
	struct mod_stack_t *read_write_req_list_prev;
	struct mod_stack_t *read_write_req_list_next;

	/* Bucket list of Updown read write accesses in hash table in 'mod' */
	struct mod_stack_t *read_write_req_bucket_list_prev;
	struct mod_stack_t *read_write_req_bucket_list_next;

	/* Linked list of Evict accesses in 'mod' */
	struct mod_stack_t *evict_list_prev;
	struct mod_stack_t *evict_list_next;

	/* Bucket list of Evict accesses in hash table in 'mod' */
	struct mod_stack_t *evict_bucket_list_prev;
	struct mod_stack_t *evict_bucket_list_next;

	/* Flags */
	int hit : 1;
	int err : 1;
	int shared : 1;
	int read : 1;
	int write : 1;
	int nc_write : 1;
	int prefetch : 1;
	int blocking : 1;
	int writeback : 1;
	int eviction : 1;
	int retry : 1;
	int coalesced : 1;
	int port_locked : 1;
	int read_request_in_progress : 1;
	int write_request_in_progress : 1;
	//------------------------------------------------
	// Flag to indicate that dirty line is being transferred to the requesting module.
	int dirty : 1;
	//------------------------------------------------
	//------------------------------------------------
	// Flag to indicate that WriteBack to upper level (up-down WB request) is a result of invalidation due to Load/Store/Eviction process.
	//------------------------------------------------
	int evict_trans : 1;
	int invalidate_eviction : 1;
	int wb_store : 1;
	int downup_read_request : 1;
	int downup_writeback_request : 1;


	int downup_access_registered : 1;
	int updown_access_registered : 1;
	int evict_access_registered : 1;
	//----------------
	//Debug Flag
	//----------------
	int wait_for_lock : 1;
	int debug_flag : 1;

	/* Message sent through interconnect */
	struct net_msg_t *msg;

	/* Linked list for waiting events */
	int waiting_list_event;  /* Event to schedule when stack is waken up */
	struct mod_stack_t *waiting_list_prev;
	struct mod_stack_t *waiting_list_next;

	/* Waiting list for locking a port. */
	int port_waiting_list_event;
	struct mod_stack_t *port_waiting_list_prev;
	struct mod_stack_t *port_waiting_list_next;

	/* Waiting list. Contains other stacks waiting for this one to finish.
	 * Waiting stacks corresponds to slave coalesced accesses waiting for
	 * the current one to finish. */
	struct mod_stack_t *waiting_list_head;
	struct mod_stack_t *waiting_list_tail;
	int waiting_list_count;
	int waiting_list_max;

	/* Master stack that the current access has been coalesced with.
	 * This field has a value other than NULL only if 'coalesced' is TRUE. */
	struct mod_stack_t *master_stack;

	/* Events waiting in cache lock */
	int cache_lock_event;
	struct mod_stack_t *cache_lock_next;

	/* Return stack */
	struct mod_stack_t *ret_stack;
	int ret_event;

	//--------------------------------------------------
	// Parameters of measuring latency
	//--------------------------------------------------	
	long long access_latency;	
	long long access_start_cycle;
	long long access_end_cycle;

	long long mod_port_waiting_start_cycle;
	long long mod_port_waiting_end_cycle;
	long long mod_port_waiting_cycle;
	
	long long directory_lock_waiting_start_cycle;
	long long directory_lock_waiting_end_cycle;
	long long directory_lock_waiting_cycle;

	long long load_access_waiting_for_store_start_cycle;
	long long load_access_waiting_for_store_end_cycle;
	long long load_access_waiting_for_store_cycle;

	long long load_access_waiting_start_cycle;
	long long load_access_waiting_end_cycle;
	long long load_access_waiting_cycle;

	long long store_access_waiting_start_cycle;
	long long store_access_waiting_end_cycle;
	long long store_access_waiting_cycle;

	long long nw_send_request_latency_start_cycle;
	long long nw_send_request_latency_end_cycle;
	long long nw_send_request_latency_cycle;

	long long nw_send_reply_latency_start_cycle;
	long long nw_send_reply_latency_end_cycle;
	long long nw_send_reply_latency_cycle;

	long long nw_receive_request_latency_start_cycle;
	long long nw_receive_request_latency_end_cycle;
	long long nw_receive_request_latency_cycle;

	long long nw_receive_reply_latency_start_cycle;
	long long nw_receive_reply_latency_end_cycle;
	long long nw_receive_reply_latency_cycle;

	long long read_write_evict_du_req_start_cycle;
	long long read_write_evict_du_req_end_cycle;
	long long read_write_evict_du_req_cycle;

	long long wait_for_read_write_req_start_cycle;
	long long wait_for_read_write_req_end_cycle;
	long long wait_for_read_write_req_cycle;

	long long wait_for_downup_req_start_cycle;
	long long wait_for_downup_req_end_cycle;
	long long wait_for_downup_req_cycle;

	long long wait_for_evict_req_start_cycle;
	long long wait_for_evict_req_end_cycle;
	long long wait_for_evict_req_cycle;
};

struct mod_stack_t *mod_stack_create(long long id, struct mod_t *mod,
		unsigned int addr, int ret_event, struct mod_stack_t *ret_stack);
void mod_stack_return(struct mod_stack_t *stack);

void mod_stack_wait_in_mod(struct mod_stack_t *stack,
	struct mod_t *mod, int event);
void mod_stack_wakeup_mod(struct mod_t *mod);

void mod_stack_wait_in_port(struct mod_stack_t *stack,
	struct mod_port_t *port, int event);
void mod_stack_wakeup_port(struct mod_port_t *port);

void mod_stack_wait_in_stack(struct mod_stack_t *stack,
	struct mod_stack_t *master_stack, int event);
void mod_stack_wakeup_stack(struct mod_stack_t *master_stack);


#endif

