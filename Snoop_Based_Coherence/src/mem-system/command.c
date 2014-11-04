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
#include <lib/util/list.h>
#include <lib/util/misc.h>
#include <lib/util/string.h>
#include <network/buffer.h>
#include <network/link.h>
#include <network/network.h>
#include <network/node.h>

#include "cache.h"
#include "mem-system.h"
#include "module.h"


int EV_MEM_SYSTEM_COMMAND;
int EV_MEM_SYSTEM_END_COMMAND;



/*
 * Private Functions
 */


static void mem_system_command_expect(struct list_t *token_list, char *command_line)
{
	if (!list_count(token_list))
		fatal("%s: unexpected end of line.\n\t> %s",
			__FUNCTION__, command_line);
}


static void mem_system_command_end(struct list_t *token_list, char *command_line)
{
	if (list_count(token_list))
		fatal("%s: %s: end of line expected.\n\t> %s",
			__FUNCTION__, str_token_list_first(token_list), command_line);
}


static unsigned int mem_system_command_get_hex(struct list_t *token_list,
	char *command_line)
{
	unsigned int value;

	/* Get value */
	mem_system_command_expect(token_list, command_line);
	if (sscanf(str_token_list_first(token_list), "0x%x", &value) != 1)
		fatal("%s: %s: invalid hex value.\n\t> %s",
			__FUNCTION__, str_token_list_first(token_list),
			command_line);
	str_token_list_shift(token_list);

	/* Return */
	return value;
}


static void mem_system_command_get_string(struct list_t *token_list,
	char *command_line, char *buf, int size)
{
	mem_system_command_expect(token_list, command_line);
	snprintf(buf, size, "%s", str_token_list_first(token_list));
	str_token_list_shift(token_list);
}


static long long mem_system_command_get_cycle(struct list_t *token_list,
	char *command_line)
{
	int err;
	long long command_cycle;

	/* Read cycle */
	mem_system_command_expect(token_list, command_line);
	command_cycle = str_to_llint(str_token_list_first(token_list), &err);
	if (err || command_cycle < 1)
		fatal("%s: %s: invalid cycle number, integer >= 1 expected.\n\t> %s",
			__FUNCTION__, str_token_list_first(token_list),
			command_line);
	
	/* Shift token and return */
	str_token_list_shift(token_list);
	return command_cycle;
}


int mem_system_command_get_high_low(struct list_t *token_list, char *command_line)
{
	char *direction;
	int high_net;

	/* Get direction string */
	mem_system_command_expect(token_list, command_line);
	direction = str_token_list_first(token_list);

	/* Determine direction */
	if (!strcasecmp(direction, "High"))
	{
		high_net = 1;
	}
	else if (!strcasecmp(direction, "Low"))
	{
		high_net = 0;
	}
	else 
	{
		fatal("%s: %s: invalid network direction.\n\t> %s",
			__FUNCTION__, direction, command_line);
	}

	str_token_list_shift(token_list);
	return high_net;
}


int mem_system_command_get_in_out(struct list_t *token_list, char *command_line)
{
	char *direction;
	int incoming;

	/* Get direction string */
	mem_system_command_expect(token_list, command_line);
	direction = str_token_list_first(token_list);

	/* Determine direction */
	if (!strcasecmp(direction, "In"))
	{
		incoming = 1;
	}
	else if (!strcasecmp(direction, "Out"))
	{
		incoming = 0;
	}
	else 
	{
		fatal("%s: %s: invalid network direction.\n\t> %s",
			__FUNCTION__, direction, command_line);
	}

	str_token_list_shift(token_list);
	return incoming;
}


static struct mod_t *mem_system_command_get_mod(struct list_t *token_list,
	char *command_line)
{
	struct mod_t *mod;

	char *mod_name;

	/* Get module name */
	mem_system_command_expect(token_list, command_line);
	mod_name = str_token_list_first(token_list);

	/* Find module */
	if (!strcasecmp(mod_name, "None"))
		mod = NULL;
	else
	{
		mod = mem_system_get_mod(mod_name);
		if (!mod)
			fatal("%s: %s: invalid module name.\n\t> %s",
				__FUNCTION__, mod_name, command_line);
	}

	/* Return module */
	str_token_list_shift(token_list);
	return mod;
}


static void mem_system_command_get_set_way(struct list_t *token_list, char *command_line, 
	struct mod_t *mod, int *set_ptr, int *way_ptr)
{
	int set;
	int way;

	/* Check valid module */
	if (!mod)
		fatal("%s: invalid module.\n\t> %s",
			__FUNCTION__, command_line);

	/* Get set */
	mem_system_command_expect(token_list, command_line);
	set = atoi(str_token_list_first(token_list));
	str_token_list_shift(token_list);

	/* Get way */
	mem_system_command_expect(token_list, command_line);
	way = atoi(str_token_list_first(token_list));
	str_token_list_shift(token_list);

	/* Check valid set/way */
	if (!IN_RANGE(set, 0, mod->cache->num_sets - 1))
		fatal("%s: %d: invalid set.\n\t> %s",
			__FUNCTION__, set, command_line);
	if (!IN_RANGE(way, 0, mod->cache->assoc - 1))
		fatal("%s: %d: invalid way.\n\t> %s",
			__FUNCTION__, set, command_line);

	/* Return */
	*set_ptr = set;
	*way_ptr = way;
}


static int mem_system_command_get_sub_block(struct list_t *token_list,
	char *command_line, struct mod_t *mod, int set, int way)
{
	int sub_block;

	/* Get sub-block */
	mem_system_command_expect(token_list, command_line);
	sub_block = atoi(str_token_list_first(token_list));
	str_token_list_shift(token_list);

	// TBD : Check DTS
	/*Check valid sub-block */
	if(!IN_RANGE(sub_block, 0, mod->num_sub_blocks - 1))
	 	fatal("%s: %d: invalid sub-block.\n\t> %s",
	 		__FUNCTION__, sub_block, command_line);
	/* Return */
	return sub_block;
}


static int mem_system_command_get_state(struct list_t *token_list,
	char *command_line)
{
	int state;

	/* Get state */
	mem_system_command_expect(token_list, command_line);
	state = str_map_string_case(&cache_block_state_map, str_token_list_first(token_list));
	if (!state && strcasecmp(str_token_list_first(token_list), "I"))
		fatal("%s: invalid state.\n\t> %s",
			__FUNCTION__, command_line);

	/* Return */
	str_token_list_shift(token_list);
	return state;
}


static enum mod_access_kind_t mem_system_command_get_mod_access(struct list_t *token_list,
	char *command_line)
{
	char mod_access_name[MAX_STRING_SIZE];
	int mod_access;

	/* Get access */
	mem_system_command_expect(token_list, command_line);
	snprintf(mod_access_name, sizeof mod_access_name, "%s", str_token_list_first(token_list));

	/* Decode access */
	mod_access = str_map_string_case(&mod_access_kind_map, mod_access_name);
	if (!mod_access)
		fatal("%s: %s: invalid access.\n\t> %s",
			__FUNCTION__, mod_access_name, command_line);
	
	/* Return */
	str_token_list_shift(token_list);
	return mod_access;
}




/*
 * Public Functions
 */


/* Event handler for EV_MEM_SYSTEM_COMMAND.
 * The event data is a string of type 'char *' that needs to be deallocated
 * after processing this event. */
void mem_system_command_handler(int event, void *data)
{
	struct list_t *token_list;

	char *command_line = data;
	char command[MAX_STRING_SIZE];

	/* Get command */
	str_token(command, sizeof command, command_line, 0, " ");
	if (!command[0])
		fatal("%s: invalid command syntax.\n\t> %s",
			__FUNCTION__, command_line);

	/* Commands that need to be processed at the end of the simulation
	 * are ignored here. These are command prefixed with 'CheckXXX'. */
	if (!strncasecmp(command, "Check", 5))
	{
		esim_schedule_end_event(EV_MEM_SYSTEM_END_COMMAND, data);
		return;
	}

	/* Split command in tokens, skip command */
	token_list = str_token_list_create(command_line, " ");
	assert(list_count(token_list));
	str_token_list_shift(token_list);

	/* Command 'SetBlock' */
	if (!strcasecmp(command, "SetBlock"))
	{
		struct mod_t *mod;

		int set;
		int way;
		int tag;

		int set_check;
		int tag_check;

		int state;

		mod = mem_system_command_get_mod(token_list, command_line);
		mem_system_command_get_set_way(token_list, command_line, mod, &set, &way);
		tag = mem_system_command_get_hex(token_list, command_line);
		state = mem_system_command_get_state(token_list, command_line);
		mem_system_command_end(token_list, command_line);

		/* Check that module serves address */
		if (!mod_serves_address(mod, tag))
			fatal("%s: %s: module does not serve address 0x%x.\n\t> %s",
				__FUNCTION__, mod->name, tag, command_line);

		/* Check that tag goes to specified set */
		mod_find_block(mod, tag, &set_check, NULL, &tag_check, NULL);
		if (set != set_check)
			fatal("%s: %s: tag 0x%x belongs to set %d.\n\t> %s",
				__FUNCTION__, mod->name, tag, set_check, command_line);
		if (tag != tag_check)
			fatal("%s: %s: tag should be multiple of block size.\n\t> %s",
				__FUNCTION__, mod->name, command_line);

		/* Set tag */
		cache_set_block(mod->cache, set, way, tag, state);
	}

	/* Command 'SetSharers' */
	else if (!strcasecmp(command, "SetSharers"))
	{
		struct mod_t *mod;
		struct mod_t *sharer;

		int set;
		int way;

		int sub_block;

		/* Get first fields */
		mod = mem_system_command_get_mod(token_list, command_line);
		mem_system_command_get_set_way(token_list, command_line, mod, &set, &way);
		sub_block = mem_system_command_get_sub_block(token_list, command_line, mod, set, way);

		/* Get sharers */
		mem_system_command_expect(token_list, command_line);
		while (list_count(token_list))
		{
			/* Get sharer */
			sharer = mem_system_command_get_mod(token_list, command_line);
			if (!sharer)
				continue;

			/* Check that sharer is an immediate higher-level module */
			if (sharer->low_net != mod->high_net || !sharer->low_net)
				fatal("%s: %s is not a higher-level module of %s.\n\t> %s",
					__FUNCTION__, sharer->name, mod->name, command_line);

		}
	}

	/* Command 'Access' */
	else if (!strcasecmp(command, "Access"))
	{
		struct mod_t *mod;
		enum mod_access_kind_t access_kind;
		unsigned int addr;

		long long command_cycle;
		long long cycle;

		/* Get current cycle */
		cycle = esim_domain_cycle(mem_domain_index);

		/* Read fields */
		mod = mem_system_command_get_mod(token_list, command_line);
		command_cycle = mem_system_command_get_cycle(token_list, command_line);
		access_kind = mem_system_command_get_mod_access(token_list, command_line);
		addr = mem_system_command_get_hex(token_list, command_line);

		/* If command is scheduled for later, exit */
		if (command_cycle > cycle)
		{
			str_token_list_free(token_list);
			esim_schedule_event(EV_MEM_SYSTEM_COMMAND, data, command_cycle - cycle);
			return;
		}

		/* Access module */
		mod_access(mod, access_kind, addr, NULL, NULL, NULL, NULL);
	}

	/* Command not supported */
	else
		fatal("%s: %s: invalid command.\n\t> %s",
			__FUNCTION__, command, command_line);

	/* Free command */
	free(command_line);
	str_token_list_free(token_list);
}


/* Event handler for EV_MEM_SYSTEM_END_COMMAND.
 * The event data is a string of type 'char *' that needs to be deallocated
 * after processing this event. */
void mem_system_end_command_handler(int event, void *data)
{
	char *command_line = data;
	char command[MAX_STRING_SIZE];

	char msg[MAX_STRING_SIZE];
	char msg_detail[MAX_STRING_SIZE];

	char *msg_str = msg;
	int msg_size = sizeof msg;

	char *msg_detail_str = msg_detail;
	int msg_detail_size = sizeof msg_detail;

	int test_failed;

	struct list_t *token_list;

	/* Split command in tokens, skip command */
	token_list = str_token_list_create(command_line, " ");
	assert(list_count(token_list));

	/* Get command */
	mem_system_command_get_string(token_list, command_line, command, sizeof command);

	/* Messages */
	test_failed = 0;
	*msg_str = '\0';
	*msg_detail_str = '\0';

	/* Command 'SetBlock' */
	if (!strcasecmp(command, "CheckBlock"))
	{
		struct mod_t *mod;

		int set;
		int way;
		int tag;
		int tag_check;

		int state;
		int state_check;

		mod = mem_system_command_get_mod(token_list, command_line);
		mem_system_command_get_set_way(token_list, command_line, mod, &set, &way);
		tag = mem_system_command_get_hex(token_list, command_line);
		state = mem_system_command_get_state(token_list, command_line);
		mem_system_command_end(token_list, command_line);

		/* Check that module serves address */
		if (!mod_serves_address(mod, tag))
			fatal("%s: %s: module does not serve address 0x%x.\n\t> %s",
				__FUNCTION__, mod->name, tag, command_line);

		/* Output */
		str_printf(&msg_str, &msg_size,
			"check module %s, set %d, way %d - state %s, tag 0x%x",
			mod->name, set, way, str_map_value(&cache_block_state_map, state), tag);

		/* Check */
		cache_get_block(mod->cache, set, way, &tag_check, &state_check);
		if (tag != tag_check)
		{
			test_failed = 1;
			str_printf(&msg_detail_str, &msg_detail_size,
				"\ttag 0x%x found, but 0x%x expected\n",
				tag_check, tag);
		}
		if (state != state_check)
		{
			test_failed = 1;
			str_printf(&msg_detail_str, &msg_detail_size,
				"\tstate %s found, but %s expected\n",
				str_map_value(&cache_block_state_map, state_check),
				str_map_value(&cache_block_state_map, state));
		}
	}

	/* Command 'CheckLink' */
	else if (!strcasecmp(command, "CheckLink"))
	{
		struct mod_t *mod;
		struct net_node_t *node;
		struct net_buffer_t *buffer;
		struct list_t *buffer_list;
		struct net_link_t *link;
		char expected_bytes_str[MAX_STRING_SIZE];
		unsigned long long expected_bytes;
		int high;
		int in;

		/* Get module */
		mod = mem_system_command_get_mod(token_list, command_line);

		/* Get network information */
		high = mem_system_command_get_high_low(token_list, command_line);
		in = mem_system_command_get_in_out(token_list, command_line);

		/* Get expected transfer information */
		mem_system_command_get_string(token_list, command_line, expected_bytes_str, 
			sizeof expected_bytes_str);
		expected_bytes = (unsigned long long)atoll(expected_bytes_str);

		if (high)
		{
			node = mod->high_net_node;
		}
		else 
		{
			node = mod->low_net_node;
		}

		if (in)
		{
			buffer_list = node->input_buffer_list;
		}
		else 
		{
			buffer_list = node->output_buffer_list;
		}

		assert(list_count(buffer_list) == 1);

		buffer = list_get(buffer_list, 0);
		/*New change because of BUS implementation */
		/* FIXME: Check to see if the Virtual channel capability is considered. */
		if (buffer->kind == net_buffer_link)
		{
			link = buffer->link;
			assert(link);
			/* Output */
			str_printf(&msg_str, &msg_size, "check bytes on %s", link->name);

			if (expected_bytes != link-> transferred_bytes)
			{
				test_failed = 1;
				str_printf(&msg_detail_str, &msg_detail_size,
						"\t%s expected %llu bytes transferred, but %llu found\n",
						link->name, expected_bytes, link->transferred_bytes);
			}
		}
		else if (buffer->kind == net_buffer_bus)
		{
			fprintf(stderr, "The connection is a BUS. BUS check is not yet implemented \n");
		}
		/* FIXME: The same calculation may be required for BUS connections. */
	}

	/* Invalid command */
	else
		fatal("%s: %s: invalid command.\n\t> %s",
			__FUNCTION__, command, command_line);

	/* Output */
	fprintf(stderr, ">>> %s - %s\n", msg, test_failed ? "failed" : "passed");
	fprintf(stderr, "%s", msg_detail);

	/* Free command */
	free(command_line);
	str_token_list_free(token_list);
}
