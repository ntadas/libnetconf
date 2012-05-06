#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <libnetconf.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libxml/parser.h>

#include "commands.h"
#include "mreadline.h"

#define NC_CAP_CANDIDATE_ID "urn:ietf:params:netconf:capability:candidate:1.0"
#define NC_CAP_STARTUP_ID   "urn:ietf:params:netconf:capability:startup:1.0"
#define NC_CAP_ROLLBACK_ID  "urn:ietf:params:netconf:capability:rollback-on-error:1.0"

extern int done;
volatile int verb_level = 0;

void print_version ();

struct nc_session* session = NULL;

#define BUFFER_SIZE 1024

COMMAND commands[] = {
		{"connect", cmd_connect, "Connect to the NETCONF server"},
		{"disconnect", cmd_disconnect, "Disconnect from the NETCONF server"},
		{"edit-config", cmd_editconfig, "NETCONF <edit-config> operation"},
		{"get", cmd_get, "NETCONF <get> operation"},
		{"get-config", cmd_getconfig, "NETCONF <get-config> operation"},
		{"help", cmd_help, "Display this text"},
		{"quit", cmd_quit, "Quit the program"},
		{"status", cmd_status, "Print information about current NETCONF session"},
		{"verbose", cmd_verbose, "Enable/disable verbose messages"},
/* synonyms for previous commands */
		{"debug", cmd_debug, NULL},
		{"?", cmd_help, NULL},
		{"exit", cmd_quit, NULL},
		{NULL, NULL, NULL}
};

struct arglist {
	char **list;
	int count;
	int size;
};

/**
 * \brief Initiate arglist to defined values
 *
 * \param args          pointer to the arglist structure
 * \return              0 if success, non-zero otherwise
 */
void init_arglist (struct arglist *args)
{
	if (args != NULL) {
		args->list = NULL;
		args->count = 0;
		args->size = 0;
	}
}

/**
 * \brief Clear arglist including free up allocated memory
 *
 * \param args          pointer to the arglist structure
 * \return              none
 */
void clear_arglist (struct arglist *args)
{
	int i = 0;

	if (args && args->list) {
		for (i = 0; i < args->count; i++) {
			if (args->list[i]) {
				free (args->list[i]);
			}
		}
		free (args->list);
	}

	init_arglist (args);
}

/**
 * \brief add arguments to arglist
 *
 * Adds erguments to arglist's structure. Arglist's list variable
 * is used to building execv() arguments.
 *
 * \param args          arglist to store arguments
 * \param format        arguments to add to the arglist
 */
void addargs (struct arglist *args, char *format, ...)
{
	va_list arguments;
	char *aux = NULL, *aux1 = NULL;
	int len;

	if (args == NULL)
	return;

	/* store arguments to aux string */
	va_start(arguments, format);
	if ((len = vasprintf(&aux, format, arguments)) == -1)
	perror("addargs - vasprintf");
	va_end(arguments);

	/* parse aux string and store it to the arglist */
	/* find \n and \t characters and replace them by space */
	while ((aux1 = strpbrk(aux, "\n\t")) != NULL) {
		*aux1 = ' ';
	}
	/* remember the begining of the aux string to free it after operations */
	aux1 = aux;

	/*
	 * get word by word from given string and store words separately into
	 * the arglist
	 */
	for (aux = strtok(aux, " "); aux != NULL; aux = strtok(NULL, " ")) {
		if (!strcmp(aux, ""))
		continue;

		if (args->list == NULL) { /* initial memory allocation */
			if ((args->list = (char **) malloc(8 * sizeof(char *))) == NULL)
			perror("Fatal error while allocating memmory");
			args->size = 8;
			args->count = 0;
		} else if (args->count + 2 >= args->size) {
			/*
			 * list is too short to add next to word so we have to
			 * extend it
			 */
			args->size += 8;
			args->list = realloc(args->list, args->size * sizeof(char *));
		}
		/* add word in the end of the list */
		if ((args->list[args->count] = (char *) malloc((strlen(aux) + 1) * sizeof(char))) == NULL)
		perror("Fatal error while allocating memmory");
		strcpy(args->list[args->count], aux);
		args->list[++args->count] = NULL; /* last argument */
	}
	/* clean up */
	free(aux1);
}

int cmd_status (char* arg)
{
	char *s;
	struct nc_cpblts* cpblts;

	if (session == NULL) {
		fprintf (stdout, "Client is not connected to any NETCONF server.\n");
	} else {
		fprintf (stdout, "Current NETCONF session:\n");
		fprintf (stdout, "  ID          : %s\n", s = nc_session_get_id (session));
		if (s != NULL) {
			free (s);
		}
		fprintf (stdout, "  Host        : %s\n", s = nc_session_get_host (session));
		if (s != NULL) {
			free (s);
		}
		fprintf (stdout, "  Port        : %s\n", s = nc_session_get_port (session));
		if (s != NULL) {
			free (s);
		}
		fprintf (stdout, "  User        : %s\n", s = nc_session_get_user (session));
		if (s != NULL) {
			free (s);
		}
		fprintf (stdout, "  Capabilities:\n");
		cpblts = nc_session_get_cpblts (session);
		if (cpblts != NULL) {
			nc_cpblts_iter_start (cpblts);
			while ((s = nc_cpblts_iter_next (cpblts)) != NULL) {
				fprintf (stdout, "\t%s\n", s);
				free (s);
			}
		}
	}

	return (EXIT_SUCCESS);
}

static struct nc_filter *set_filter(const char* operation, const char *file)
{
	int filter_fd;
	struct stat filter_stat;
	char *filter_s;
	struct nc_filter *filter = NULL;

	if (operation == NULL) {
		return (NULL);
	}

	if (file) {
		/* open filter from the file */
		filter_fd = open(optarg, O_RDONLY);
		if (filter_fd == -1) {
			ERROR(operation, "unable to open filter file (%s).", strerror(errno));
			return (NULL);
		}

		/* map content of the file into the memory */
		fstat(filter_fd, &filter_stat);
		filter_s = (char*) mmap(NULL, filter_stat.st_size, PROT_READ, MAP_PRIVATE, filter_fd, 0);
		if (filter_s == MAP_FAILED) {
			ERROR(operation, "mmapping filter file failed (%s).", strerror(errno));
			close(filter_fd);
			return (NULL);
		}

		/* create the filter according to the file content */
		filter = nc_filter_new(NC_FILTER_SUBTREE, filter_s);

		/* unmap filter file and close it */
		munmap(filter_s, filter_stat.st_size);
		close(filter_fd);
	} else {
		/* let user write filter interactively */
		INSTRUCTION("Type the filter (close editor by Ctrl-D):\n");
		filter_s = mreadline(NULL);
		if (filter_s == NULL) {
			ERROR(operation, "reading filter failed.");
			return (NULL);
		}

		/* create the filter according to the file content */
		filter = nc_filter_new(NC_FILTER_SUBTREE, filter_s);

		/* cleanup */
		free(filter_s);
	}

	return (filter);
}

void cmd_editconfig_help()
{
	char *rollback;

	if (session == NULL || nc_cpblts_enabled (session, NC_CAP_ROLLBACK_ID)) {
		rollback = "|rollback";
	} else {
		rollback = "";
	}

	/* if session not established, print complete help for all capabilities */
	fprintf (stdout, "edit-config [--help] [--defop <merge|replace|none>] [--error <stop|continue%s>] [--config <file>] running", rollback);
	if (session == NULL || nc_cpblts_enabled (session, NC_CAP_STARTUP_ID)) {
		fprintf (stdout, "|startup");
	}
	if (session == NULL || nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
		fprintf (stdout, "|candidate");
	}
	fprintf (stdout, "\n");
}

int cmd_editconfig (char *arg)
{
	struct arglist cmd;
	struct option long_options[] ={
			{"config", 1, 0, 'c'},
			{"defop", 1, 0, 'd'},
			{"error", 1, 0, 'e'},
			{"help", 0, 0, 'h'},
			{0, 0, 0, 0}
	};
	int option_index = 0;

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session == NULL) {
		ERROR("get", "NETCONF session not established, use \'connect\' command.");
		return (EXIT_FAILURE);
	}

	init_arglist (&cmd);
	addargs (&cmd, "%s", arg);

	cmd_editconfig_help();

	clear_arglist(&cmd);

	return (EXIT_SUCCESS);
}

void cmd_get_help ()
{
	fprintf (stdout, "get [--help] [--filter[=filepath]]\n");
}


int cmd_get (char *arg)
{
	int c;
	char *data = NULL;
	struct nc_filter *filter = NULL;
	nc_rpc *rpc = NULL;
	nc_reply *reply = NULL;
	struct arglist cmd;
	struct option long_options[] ={
			{"filter", 2, 0, 'f'},
			{"help", 0, 0, 'h'},
			{0, 0, 0, 0}
	};
	int option_index = 0;

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session == NULL) {
		ERROR("get", "NETCONF session not established, use \'connect\' command.");
		return (EXIT_FAILURE);
	}

	init_arglist (&cmd);
	addargs (&cmd, "%s", arg);

	while ((c = getopt_long (cmd.count, cmd.list, "f:h", long_options, &option_index)) != -1) {
		switch (c) {
		case 'f':
			filter = set_filter("get", optarg);
			if (filter == NULL) {
				clear_arglist(&cmd);
				return (EXIT_FAILURE);
			}
			break;
		case 'h':
			cmd_get_help ();
			clear_arglist(&cmd);
			return (EXIT_SUCCESS);
			break;
		default:
			ERROR("get", "unknown option -%c.", c);
			cmd_get_help ();
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
	}

	if (optind > cmd.count) {
		ERROR("get", "invalid parameters, see \'get --help\'.");
		clear_arglist(&cmd);
		return (EXIT_FAILURE);
	}

	/* arglist is no more needed */
	clear_arglist(&cmd);

	/* create requests */
	rpc = nc_rpc_get (filter);
	nc_filter_free(filter);
	if (rpc == NULL) {
		ERROR("get", "creating rpc request failed.");
		return (EXIT_FAILURE);
	}
	/* send the request and get the reply */
	nc_session_send_rpc (session, rpc);
	if (nc_session_recv_reply (session, &reply) == 0) {
		ERROR("get", "receiving rpc-reply failed.");
		nc_rpc_free (rpc);
		return (EXIT_FAILURE);
	}
	nc_rpc_free (rpc);

	switch (nc_reply_get_type (reply)) {
	case NC_REPLY_DATA:
		INSTRUCTION("Result:\n%s\n", data = nc_reply_get_data (reply));
		break;
	case NC_REPLY_ERROR:
		ERROR("get", "operation failed (%s).", data = nc_reply_get_errormsg (reply));
		break;
	default:
		ERROR("get", "unexpected operation result.");
		break;
	}
	nc_reply_free(reply);
	if (data) {
		free (data);
	}

	return (EXIT_SUCCESS);
}


void cmd_getconfig_help ()
{
	/* if session not established, print complete help for all capabilities */
	fprintf (stdout, "get-config [--help] [--filter[=file]] running");
	if (session == NULL || nc_cpblts_enabled (session, NC_CAP_STARTUP_ID)) {
		fprintf (stdout, "|startup");
	}
	if (session == NULL || nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
		fprintf (stdout, "|candidate");
	}
	fprintf (stdout, "\n");
}

int cmd_getconfig (char *arg)
{
	int c, param_free = 0, valid = 0;
	char *datastore = NULL;
	char *data = NULL;
	NC_DATASTORE_TYPE target;
	struct nc_filter *filter = NULL;
	nc_rpc *rpc = NULL;
	nc_reply *reply = NULL;
	struct arglist cmd;
	struct option long_options[] ={
			{"filter", 2, 0, 'f'},
			{"help", 0, 0, 'h'},
			{0, 0, 0, 0}
	};
	int option_index = 0;

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session == NULL) {
		ERROR("get-config", "NETCONF session not established, use \'connect\' command.");
		return (EXIT_FAILURE);
	}

	init_arglist (&cmd);
	addargs (&cmd, "%s", arg);

	while ((c = getopt_long (cmd.count, cmd.list, "f:h", long_options, &option_index)) != -1) {
		switch (c) {
		case 'f':
			filter = set_filter("get-config", optarg);
			if (filter == NULL) {
				clear_arglist(&cmd);
				return (EXIT_FAILURE);
			}
			break;
		case 'h':
			cmd_getconfig_help ();
			clear_arglist(&cmd);
			return (EXIT_SUCCESS);
			break;
		default:
			ERROR("get-config", "unknown option -%c.", c);
			cmd_getconfig_help ();
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
	}

	if (optind == cmd.count) {

userinput:

		datastore = malloc (sizeof(char) * BUFFER_SIZE);
		if (datastore == NULL) {
			ERROR("get-config", "memory allocation error (%s).", strerror (errno));
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
		param_free = 1;

		/* repeat user input until valid datastore is selected */
		while (!valid) {
			/* get mandatory argument */
			INSTRUCTION("Select target datastore (running");
			if (nc_cpblts_enabled (session, NC_CAP_STARTUP_ID)) {
				fprintf (stdout, "|startup");
			}
			if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID)) {
				fprintf (stdout, "|candidate");
			}
			fprintf (stdout, "): ");
			scanf ("%1023s", datastore);

			/* validate argument */
			if (strcmp (datastore, "running") == 0) {
				valid = 1;
				target = NC_DATASTORE_RUNNING;
			}
			if (nc_cpblts_enabled (session, NC_CAP_STARTUP_ID) &&
					strcmp (datastore, "startup") == 0) {
				valid = 1;
				target = NC_DATASTORE_STARTUP;
			}
			if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID) &&
					strcmp (datastore, "candidate") == 0) {
				valid = 1;
				target = NC_DATASTORE_CANDIDATE;
			}

			if (!valid) {
				ERROR("get-config", "invalid target datastore type.");
			}
		}
	} else if ((optind + 1) == cmd.count) {
		datastore = cmd.list[optind];

		/* validate argument */
		if (strcmp (datastore, "running") == 0) {
			valid = 1;
			target = NC_DATASTORE_RUNNING;
		}
		if (nc_cpblts_enabled (session, NC_CAP_STARTUP_ID) &&
				strcmp (datastore, "startup") == 0) {
			valid = 1;
			target = NC_DATASTORE_STARTUP;
		}
		if (nc_cpblts_enabled (session, NC_CAP_CANDIDATE_ID) &&
				strcmp (datastore, "candidate") == 0) {
			valid = 1;
			target = NC_DATASTORE_CANDIDATE;
		}

		if (!valid) {
			goto userinput;
		}
	} else {
		ERROR("get-config", "invalid parameters, see \'get-config --help\'.");
		clear_arglist(&cmd);
		free (datastore);
		return (EXIT_FAILURE);
	}

	if (param_free) {
		free (datastore);
	}
	/* arglist is no more needed */
	clear_arglist(&cmd);

	/* create requests */
	rpc = nc_rpc_getconfig (target, filter);
	nc_filter_free(filter);
	if (rpc == NULL) {
		ERROR("get-config", "creating rpc request failed.");
		return (EXIT_FAILURE);
	}
	/* send the request and get the reply */
	nc_session_send_rpc (session, rpc);
	if (nc_session_recv_reply (session, &reply) == 0) {
		ERROR("get-config", "receiving rpc-reply failed.");
		nc_rpc_free (rpc);
		return (EXIT_FAILURE);
	}
	nc_rpc_free (rpc);

	switch (nc_reply_get_type (reply)) {
	case NC_REPLY_DATA:
		INSTRUCTION("Result:\n%s\n", data = nc_reply_get_data (reply));
		break;
	case NC_REPLY_ERROR:
		ERROR("get-config", "operation failed (%s).", data = nc_reply_get_errormsg (reply));
		break;
	default:
		ERROR("get-config", "unexpected operation result.");
		break;
	}
	nc_reply_free(reply);
	if (data) {
		free (data);
	}

	return (EXIT_SUCCESS);
}

void cmd_connect_help ()
{
	fprintf (stdout, "connect [--help] [--port <num>] [--login <username>] host\n");
}

int cmd_connect (char* arg)
{
	char *host = NULL, *user = NULL;
	int hostfree = 0;
	unsigned short port = 0;
	int c;
	struct arglist cmd;
	struct option long_options[] = {
			{"help", 0, 0, 'h'},
			{"port", 1, 0, 'p'},
			{"login", 1, 0, 'l'},
			{0, 0, 0, 0}
	};
	int option_index = 0;

	/* set back to start to be able to use getopt() repeatedly */
	optind = 0;

	if (session != NULL) {
		ERROR("connect", "already connected to %s.", host = nc_session_get_host (session));
		if (host != NULL) {
			free (host);
		}
		return (EXIT_FAILURE);
	}

	/* process given arguments */
	init_arglist (&cmd);
	addargs (&cmd, "%s", arg);

	while ((c = getopt_long (cmd.count, cmd.list, "hp:l:", long_options, &option_index)) != -1) {
		switch (c) {
		case 'h':
			cmd_connect_help ();
			return (EXIT_SUCCESS);
			break;
		case 'p':
			port = (unsigned short) atoi (optarg);
			break;
		case 'l':
			user = optarg;
			break;
		default:
			ERROR("connect", "unknown option -%c.", c);
			cmd_connect_help ();
			return (EXIT_FAILURE);
		}
	}
	if (optind == cmd.count) {
		/* get mandatory argument */
		host = malloc (sizeof(char) * BUFFER_SIZE);
		if (host == NULL) {
			ERROR("connect", "memory allocation error (%s).", strerror (errno));
			clear_arglist(&cmd);
			return (EXIT_FAILURE);
		}
		hostfree = 1;
		INSTRUCTION("Hostname to connect to: ");
		scanf ("%1023s", host);
	} else if ((optind + 1) == cmd.count) {
		host = cmd.list[optind];
	}

	/* create the session */
	session = nc_session_connect (host, port, user, NULL);
	if (session == NULL) {
		ERROR("connect", "connecting to the %s failed.", host);
		if (hostfree) {
			free (host);
		}
		clear_arglist(&cmd);
		return (EXIT_FAILURE);
	}
	if (hostfree) {
		free (host);
	}
	clear_arglist(&cmd);

	return (EXIT_SUCCESS);
}

int cmd_disconnect (char* arg)
{
	if (session == NULL) {
		ERROR("disconnect", "not connected to any NETCONF server.");
	} else {
		nc_session_close (session);
		session = NULL;
	}

	return (EXIT_SUCCESS);
}

int cmd_quit (char* arg)
{
	done = 1;
	if (session != NULL) {
		cmd_disconnect (NULL);
	}
	return (0);
}

int cmd_verbose (char *arg)
{
	if (verb_level != 1) {
		verb_level = 1;
		nc_verbosity (NC_VERB_VERBOSE);
	} else {
		verb_level = 0;
		nc_verbosity (NC_VERB_ERROR);
	}

	return (EXIT_SUCCESS);
}

int cmd_debug (char *arg)
{
	if (verb_level != 2) {
		verb_level = 2;
		nc_verbosity (NC_VERB_DEBUG);
	} else {
		verb_level = 0;
		nc_verbosity (NC_VERB_ERROR);
	}

	return (EXIT_SUCCESS);
}

int cmd_help (char* arg)
{
	int i;

	print_version ();
	INSTRUCTION("Available commands:\n");

	for (i = 0; commands[i].name; i++) {
		if (commands[i].helpstring != NULL) {
			fprintf (stdout, "  %-15s %s\n", commands[i].name, commands[i].helpstring);
		}
	}

	return (0);
}
