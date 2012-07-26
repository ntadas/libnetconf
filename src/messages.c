/**
 * \file messages.c
 * \author Radek Krejci <rkrejci@cesnet.cz>
 * \brief Implementation of functions to create NETCONF messages.
 *
 * Copyright (C) 2012 CESNET, z.s.p.o.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is, and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 *
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <libxml/tree.h>

#include "messages.h"
#include "netconf_internal.h"
#include "error.h"
#include "messages_internal.h"

struct nc_filter *nc_filter_new(NC_FILTER_TYPE type, const char* filter)
{
	struct nc_filter *retval;

	retval = malloc(sizeof(struct nc_filter));
	if (retval == NULL) {
		ERROR("Memory allocation failed - %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	switch (type) {
	case NC_FILTER_SUBTREE:
		retval->type_string = strdup("subtree");
		break;
	default:
		ERROR("nc_filter_new: Invalid filter type specified.");
		free(retval);
		return (NULL);
	}
	retval->type = type;
	retval->content = strdup(filter);

	return (retval);
}

void nc_filter_free(struct nc_filter *filter)
{
	if (filter != NULL) {
		if (filter->content != NULL) {
			free(filter->content);
		}
		if (filter->type_string != NULL) {
			free(filter->type_string);
		}
		free(filter);
	}
}

char* nc_msg_dump(const struct nc_msg *msg)
{
	xmlChar *buf;
	int len;

	if (msg == NULL || msg->doc == NULL) {
		ERROR("%s: invalid input parameter.", __func__);
		return (NULL);
	}

	xmlDocDumpFormatMemory(msg->doc, &buf, &len, 1);
	return ((char*) buf);
}

char* nc_reply_dump(const nc_reply *reply)
{
	return (nc_msg_dump((struct nc_msg*)reply));
}

char* nc_rpc_dump(const nc_rpc *rpc)
{
	return (nc_msg_dump((struct nc_msg*)rpc));
}

struct nc_msg * nc_msg_build (const char * msg_dump)
{
	struct nc_msg * msg;

	if ((msg = malloc (sizeof(struct nc_msg))) == NULL) {
		return NULL;
	}

	if ((msg->doc = xmlReadMemory (msg_dump, strlen(msg_dump), NULL, NULL, XML_PARSE_NOBLANKS|XML_PARSE_NSCLEAN)) == NULL) {
		free (msg);
		return NULL;
	}

	msg->msgid = nc_msg_parse_msgid (msg);
	msg->error = NULL;
	
	return msg;
}

nc_rpc * nc_rpc_build (const char * rpc_dump)
{
	nc_rpc * rpc;
	NC_OP op;

	if ((rpc = nc_msg_build (rpc_dump)) == NULL) {
		return NULL;
	}

	op = nc_rpc_get_op (rpc);

	switch (op) {
	case (NC_OP_GETCONFIG):
	case (NC_OP_GET):
		rpc->type.rpc = NC_RPC_DATASTORE_READ;
		break;
	case (NC_OP_EDITCONFIG):
	case (NC_OP_COPYCONFIG):
	case (NC_OP_DELETECONFIG):
	case (NC_OP_LOCK): 
	case (NC_OP_UNLOCK):
		rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
		break;
	case (NC_OP_CLOSESESSION):
	case (NC_OP_KILLSESSION):
		rpc->type.rpc = NC_RPC_SESSION;
		break;
	default:
		rpc->type.rpc = NC_RPC_UNKNOWN;
		break;
	}

	return rpc;
}

nc_reply * nc_reply_build (const char * reply_dump)
{
	nc_reply * reply;
	xmlNodePtr root;

	if ((reply = nc_msg_build (reply_dump)) == NULL) {
		return NULL;
	}

	root = xmlDocGetRootElement (reply->doc);
	
	if (xmlStrEqual (root->children->name, BAD_CAST "ok")) {
		reply->type.reply = NC_REPLY_OK;
	} else if (xmlStrEqual (root->children->name, BAD_CAST "data")) {
		reply->type.reply = NC_REPLY_DATA;
	} else if (xmlStrEqual (root->children->name, BAD_CAST "error")) {
		reply->type.reply = NC_REPLY_ERROR;
	} else {
		reply->type.reply = NC_REPLY_UNKNOWN;
	}

	return reply;
}

nc_msgid nc_reply_get_msgid(const nc_reply *reply)
{
	if (reply != NULL) {
		return (reply->msgid);
	} else {
		return (0);
	}
}

nc_msgid nc_rpc_get_msgid(const nc_rpc *rpc)
{
	if (rpc != NULL) {
		return (rpc->msgid);
	} else {
		return (0);
	}
}

NC_OP nc_rpc_get_op(const nc_rpc *rpc)
{
	if (rpc == NULL || rpc->doc == NULL) {
		WARN("Invalid parameter for nc_rpc_get_operation().")
		return (NC_OP_UNKNOWN);
	}

	if (xmlStrcmp(rpc->doc->children->name, BAD_CAST "rpc") == 0) {
		if (xmlStrcmp(rpc->doc->children->children->name, BAD_CAST "copy-config") == 0) {
			return (NC_OP_COPYCONFIG);
		} else if (xmlStrcmp(rpc->doc->children->children->name, BAD_CAST "delete-config") == 0) {
			return (NC_OP_DELETECONFIG);
		} else if (xmlStrcmp(rpc->doc->children->children->name, BAD_CAST "edit-config") == 0) {
			return (NC_OP_EDITCONFIG);
		} else if (xmlStrcmp(rpc->doc->children->children->name, BAD_CAST "get") == 0) {
			return (NC_OP_GET);
		} else if (xmlStrcmp(rpc->doc->children->children->name, BAD_CAST "get-config") == 0) {
			return (NC_OP_GETCONFIG);
		} else if (xmlStrcmp(rpc->doc->children->children->name, BAD_CAST "lock") == 0) {
			return (NC_OP_LOCK);
		} else if (xmlStrcmp(rpc->doc->children->children->name, BAD_CAST "unlock") == 0) {
			return (NC_OP_UNLOCK);
		} else if (xmlStrcmp(rpc->doc->children->children->name, BAD_CAST "kill-session") == 0) {
			return (NC_OP_KILLSESSION);
		} else if (xmlStrcmp(rpc->doc->children->children->name, BAD_CAST "close-session") == 0) {
			return (NC_OP_CLOSESESSION);
		} else {
			return (NC_OP_UNKNOWN);
		}
	} else {
		WARN("Invalid rpc message for nc_rpc_get_operation - not a <rpc> message.");
		return (NC_OP_UNKNOWN);
	}
}

char * nc_rpc_get_op_content (const nc_rpc * rpc)
{
	char * retval;
	xmlNodePtr root;
	xmlBufferPtr buffer;

	if (rpc == NULL || rpc->doc == NULL) {
		return NULL;
	}

	if ((root = xmlDocGetRootElement (rpc->doc)) == NULL) {
		return NULL;
	}

	buffer = xmlBufferCreate ();
	xmlNodeDump (buffer, rpc->doc, root->children, 1, 1);
	retval = (char *)xmlBufferContent (buffer);
	xmlBufferFree (buffer);

	return retval;
}

NC_RPC_TYPE nc_rpc_get_type(const nc_rpc *rpc)
{
	if (rpc != NULL) {
		return (rpc->type.rpc);
	} else {
		return (NC_RPC_UNKNOWN);
	}
}

/**
 * @brief Get source or target datastore type
 * @param rpc RPC message
 * @param ds_type 'target' or 'source'
 */
static NC_DATASTORE nc_rpc_get_ds (const nc_rpc *rpc, char* ds_type)
{
	xmlNodePtr root, ds_node;

	if (rpc == NULL || rpc->doc == NULL) {
		return NC_DATASTORE_NONE;
	}

	if ((root = xmlDocGetRootElement (rpc->doc)) == NULL || !xmlStrEqual (root->name, BAD_CAST "rpc") || root->children == NULL) {
		return NC_DATASTORE_NONE;
	}

	ds_node = root->children->children;
	while (ds_node) {
		if (xmlStrEqual (ds_node->name, BAD_CAST ds_type)) {
			break;
		}
		ds_node = ds_node->next;
	}

	if (ds_node == NULL || ds_node->children == NULL) {
		return NC_DATASTORE_NONE;
	}

	if (xmlStrEqual (ds_node->children->name, BAD_CAST "candidate")) {
		return NC_DATASTORE_CANDIDATE;
	} else if (xmlStrEqual (ds_node->children->name, BAD_CAST "running")) {
		return NC_DATASTORE_RUNNING;
	} else if (xmlStrEqual (ds_node->children->name, BAD_CAST "startup")) {
		return NC_DATASTORE_STARTUP;
	}

	return NC_DATASTORE_NONE;
}

NC_DATASTORE nc_rpc_get_source (const nc_rpc *rpc)
{
	return (nc_rpc_get_ds(rpc, "source"));
}

NC_DATASTORE nc_rpc_get_target (const nc_rpc *rpc)
{
	return (nc_rpc_get_ds(rpc, "target"));
}

char * nc_rpc_get_config (const nc_rpc *rpc)
{
	xmlNodePtr rpc_root, op, config;
	xmlDocPtr config_doc;
	char * retval = NULL;
	int len;

	rpc_root = xmlDocGetRootElement (rpc->doc);
	if (rpc_root == NULL || !xmlStrEqual(rpc_root->name, BAD_CAST "rpc")) {
		return NULL;
	}

	if ((op = rpc_root->children) == NULL) {
		return NULL;
	}

	config = op->children;
	while (config != NULL && !xmlStrEqual(config->name, BAD_CAST "config")) {
		config = config->next;
	}

	if (config != NULL) {
		config_doc = xmlNewDoc (BAD_CAST "1.0");
		config_doc->children = xmlDocCopyNode (config->children, config_doc, 1);
		xmlDocSetRootElement (config_doc, config_doc->children);
		xmlDocDumpFormatMemory (config_doc, (xmlChar**)&retval, &len, 1);
		xmlFreeDoc (config_doc);
	}
	return retval;
}

NC_EDIT_DEFOP_TYPE nc_rpc_get_defop (const nc_rpc *rpc)
{
	xmlNodePtr rpc_root, op, defop;
	NC_EDIT_DEFOP_TYPE retval = NC_EDIT_DEFOP_MERGE;

	/* only applicable on edit-config */
	if (nc_rpc_get_op(rpc) != NC_OP_EDITCONFIG) {
		return NC_EDIT_DEFOP_ERROR;
	}

	rpc_root = xmlDocGetRootElement (rpc->doc);
	if (rpc_root == NULL || !xmlStrEqual(rpc_root->name, BAD_CAST "rpc")) {
		return NC_EDIT_DEFOP_ERROR;
	}

	if ((op = rpc_root->children) == NULL) {
		return NC_EDIT_DEFOP_ERROR;
	}

	defop = op->children;
	while (defop != NULL && !xmlStrEqual(defop->name, BAD_CAST "default-operation")) {
		defop = defop->next;
	}

	if (defop != NULL) {
		if (xmlStrEqual(defop->children->content, BAD_CAST "merge")) {
			retval = NC_EDIT_DEFOP_MERGE;
		} else if (xmlStrEqual(defop->children->content, BAD_CAST "replace")) {
			retval = NC_EDIT_DEFOP_REPLACE;
		} else if (xmlStrEqual(defop->children->content, BAD_CAST "none")) {
			retval = NC_EDIT_DEFOP_NONE;
		}
	}

	return retval;
}

NC_EDIT_ERROPT_TYPE nc_rpc_get_erropt (const nc_rpc *rpc)
{
	xmlNodePtr rpc_root, op, erropt;
	NC_EDIT_ERROPT_TYPE retval = NC_EDIT_ERROPT_STOP;

	/* only applicable on edit-config */
	if (nc_rpc_get_op(rpc) != NC_OP_EDITCONFIG) {
		return NC_EDIT_ERROPT_ERROR;
	}

	rpc_root = xmlDocGetRootElement (rpc->doc);
	if (rpc_root == NULL || !xmlStrEqual(rpc_root->name, BAD_CAST "rpc")) {
		return NC_EDIT_ERROPT_ERROR;
	}

	if ((op = rpc_root->children) == NULL) {
		return NC_EDIT_ERROPT_ERROR;
	}

	erropt = op->children;
	while (erropt != NULL && !xmlStrEqual(erropt->name, BAD_CAST "error-option")) {
		erropt = erropt->next;
	}

	if (erropt != NULL) {
		if (xmlStrEqual(erropt->children->content, BAD_CAST "stop-on-error")) {
			retval = NC_EDIT_ERROPT_STOP;
		} else if (xmlStrEqual(erropt->children->content, BAD_CAST "continue-on-error")) {
			retval = NC_EDIT_ERROPT_CONT;
		} else if (xmlStrEqual(erropt->children->content, BAD_CAST "rollback-on-error")) {
			retval = NC_EDIT_ERROPT_ROLLBACK;
		}
	}

	return retval;
}

NC_REPLY_TYPE nc_reply_get_type(const nc_reply *reply)
{
	if (reply != NULL) {
		return (reply->type.reply);
	} else {
		return (NC_REPLY_UNKNOWN);
	}
}

char *nc_reply_get_data(const nc_reply *reply)
{
	xmlDocPtr doc;
	xmlNodePtr node, root;
	xmlChar *buf;
	int len;

	if (reply == NULL ||
			reply->type.reply != NC_REPLY_DATA ||
			reply->doc == NULL ||
			reply->doc->children == NULL || /* <rpc-reply> */
			reply->doc->children->children == NULL /* <data> */) {
		/* some part of the reply is corrupted */
		ERROR("nc_reply_get_data: invalid input parameter.");
		return (NULL);
	}
	if (reply->doc->children->children->children == NULL) { /* content */
		/*
		 * Returned data content is empty, so return empty
		 * string without any error message. This can be a valid
		 * content of the reply, e.g. in case of filtering.
		 */
		return (strdup(""));
	}

	if ((doc = xmlNewDoc(BAD_CAST XML_VERSION)) == NULL) {
		ERROR("nc_reply_get_data: xmlNewDoc failed.");
		return (NULL);
	}
	doc->encoding = xmlStrdup(BAD_CAST UTF8);
	xmlDocSetRootElement(doc, root = xmlCopyNode(node = reply->doc->children->children->children, 1));
	for (node = node->next; node != NULL; node = node->next) {
		xmlAddNextSibling(root, xmlCopyNode(node, 1));
	}
	xmlDocDumpFormatMemory(doc, &buf, &len, 1);
	xmlFreeDoc(doc);

	return ((char*) buf);
}

nc_rpc *nc_msg_client_hello(char **cpblts)
{
	nc_rpc *msg;
	xmlNodePtr node;
	int i;

	if (cpblts == NULL || cpblts[0] == NULL) {
		ERROR("hello: no capability specified");
		return (NULL);
	}

	msg = malloc(sizeof(nc_rpc));
	if (msg == NULL) {
		ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	msg->error = NULL;
	msg->doc = xmlNewDoc(BAD_CAST "1.0");
	msg->doc->encoding = xmlStrdup(BAD_CAST UTF8);

	/* create root element */
	msg->doc->children = xmlNewDocNode(msg->doc, NULL, BAD_CAST NC_HELLO_MSG, NULL);

	/* set namespace */
	xmlNewNs(msg->doc->children, (xmlChar *) NC_NS_BASE10, NULL);

	/* create capabilities node */
	node = xmlNewChild(msg->doc->children, NULL, BAD_CAST "capabilities", NULL);
	for (i = 0; cpblts[i] != NULL; i++) {
		xmlNewChild(node, NULL, BAD_CAST "capability", BAD_CAST cpblts[i]);
	}

	return (msg);
}

void nc_msg_free(struct nc_msg *msg)
{
	if (msg != NULL) {
		if (msg->doc != NULL) {
			xmlFreeDoc(msg->doc);
		}
		if (msg->error != NULL) {
			nc_err_free(msg->error);
		}
		free(msg);
	}
}

void nc_rpc_free(nc_rpc *rpc)
{
	nc_msg_free((struct nc_msg*) rpc);
}

void nc_reply_free(nc_reply *reply)
{
	nc_msg_free((struct nc_msg*) reply);
}

struct nc_msg *nc_msg_dup(struct nc_msg *msg)
{
	struct nc_msg *dupmsg;

	if (msg == NULL || msg->doc == NULL) {
		return (NULL);
	}

	dupmsg = malloc(sizeof(struct nc_msg));
	dupmsg->doc = xmlCopyDoc(msg->doc, 1);
	dupmsg->msgid = msg->msgid;
	dupmsg->type = msg->type;
	if (msg->error != NULL) {
		dupmsg->error = nc_err_dup(msg->error);
	} else {
		dupmsg->error = NULL;
	}

	return (dupmsg);
}

nc_rpc *nc_msg_server_hello(char **cpblts, char* session_id)
{
	nc_rpc *msg;

	msg = nc_msg_client_hello(cpblts);
	if (msg == NULL) {
		return (NULL);
	}
	msg->error = NULL;

	/* assign session-id */
	/* check if session-id is prepared */
	if (session_id == NULL || strlen(session_id) == 0) {
		/* no session-id set */
		ERROR("Hello: session ID is empty");
		xmlFreeDoc(msg->doc);
		free(msg);
		return (NULL);
	}

	/* create <session-id> node */
	xmlNewChild(msg->doc->children, NULL, BAD_CAST "session-id", BAD_CAST session_id);

	return (msg);
}

/**
 * @brief Create generic NETCONF message envelope according to given type (rpc or rpc-reply) and insert given data
 *
 * @param[in] content pointer to xml node containing data
 * @param[in] msgtype string of the envelope element (rpc, rpc-reply)
 *
 * @return Prepared nc_msg structure.
 */
struct nc_msg* nc_msg_create(xmlNodePtr content, char* msgtype)
{
	struct nc_msg* msg;

	xmlDocPtr xmlmsg = NULL;

	if ((xmlmsg = xmlNewDoc(BAD_CAST XML_VERSION)) == NULL) {
		ERROR("xmlNewDoc failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return NULL;
	}
	xmlmsg->encoding = xmlStrdup(BAD_CAST UTF8);

	if ((xmlmsg->children = xmlNewDocNode(xmlmsg, NULL, BAD_CAST msgtype, NULL)) == NULL) {
		ERROR("xmlNewDocNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		xmlFreeDoc(xmlmsg);
		return NULL;
	}

	if (xmlNewProp(xmlmsg->children, BAD_CAST "message-id", BAD_CAST "") == NULL) {
		ERROR("xmlNewProp failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		xmlFreeDoc(xmlmsg);
		return NULL;
	}

	if (xmlAddChild(xmlmsg->children, xmlCopyNode(content, 1)) == NULL) {
		ERROR("xmlAddChild failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		xmlFreeDoc(xmlmsg);
		return NULL;
	}

	msg = malloc(sizeof(nc_rpc));
	if (msg == NULL) {
		ERROR("Memory reallocation failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}
	msg->doc = xmlmsg;
	msg->error = NULL;

	return (msg);
}

/**
 * @brief Create \<rpc\> envelope and insert given data
 *
 * @param[in] content pointer to xml node containing data
 *
 * @return Prepared nc_rpc structure.
 */
nc_rpc* nc_rpc_create(xmlNodePtr content)
{
	return ((nc_rpc*)nc_msg_create(content,"rpc"));
}

/**
 * @brief Create \<rpc-reply\> envelope and insert given data
 *
 * @param[in] content pointer to xml node containing data
 *
 * @return Prepared nc_reply structure.
 */
nc_reply* nc_reply_create(xmlNodePtr content)
{
	return ((nc_reply*)nc_msg_create(content,"rpc-reply"));
}

nc_reply *nc_reply_ok()
{
	nc_reply *reply;
	xmlNodePtr content;

	if ((content = xmlNewNode(NULL, BAD_CAST "ok")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	reply = nc_reply_create(content);
	reply->type.reply = NC_REPLY_OK;
	xmlFreeNode(content);

	return (reply);
}

nc_reply *nc_reply_data(const char* data)
{
	nc_reply *reply;
	xmlDocPtr doc_data;
	char* data_env;
	struct nc_err* e;

	if (data != NULL) {
		asprintf(&data_env, "<data>%s</data>", data);
	} else {
		asprintf(&data_env, "<data/>");
	}

	/* prepare XML structure from given data */
	doc_data = xmlReadMemory(data_env, strlen(data_env), NULL, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (doc_data == NULL) {
		ERROR("xmlReadMemory failed (%s:%d)", __FILE__, __LINE__);
		free(data_env);
		e = nc_err_new(NC_ERR_OP_FAILED);
		nc_err_set(e, NC_ERR_PARAM_MSG, "Configuration data seems to be corrupted.");
		return (nc_reply_error(e));
	}

	reply = nc_reply_create(doc_data->children);
	reply->type.reply = NC_REPLY_DATA;
	xmlFreeDoc(doc_data);
	free(data_env);

	return (reply);
}

nc_reply *nc_reply_error(struct nc_err* error)
{
	nc_reply *reply;
	xmlNodePtr content, einfo;

	if (error == NULL) {
		ERROR("Empty error structure to create rpc-error reply.");
		return (NULL);
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "rpc-error")) == NULL) {
		ERROR("xmlNewNode failed (%s:%d).", __FILE__, __LINE__);
		return (NULL);
	}

	/* prepare error-info if needed */
	if (error->sid != NULL || error->attribute != NULL || error->element != NULL || error->ns != NULL) {
		if ((einfo = xmlNewChild(content, NULL, BAD_CAST "error-info", NULL)) == NULL) {
			ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	if (error->tag != NULL) {
		if (xmlNewChild(content, NULL, BAD_CAST "error-tag", BAD_CAST error->tag) == NULL) {
			ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	if (error->type != NULL) {
		if (xmlNewChild(content, NULL, BAD_CAST "error-type", BAD_CAST error->type) == NULL) {
			ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	if (error->severity != NULL) {
		if (xmlNewChild(content, NULL, BAD_CAST "error-severity", BAD_CAST error->severity) == NULL) {
			ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	if (error->apptag != NULL) {
		if (xmlNewChild(content, NULL, BAD_CAST "error-app-tag", BAD_CAST error->apptag) == NULL) {
			ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	if (error->path != NULL) {
		if (xmlNewChild(content, NULL, BAD_CAST "error-path", BAD_CAST error->path) == NULL) {
			ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	if (error->message != NULL) {
		if (xmlNewChild(content, NULL, BAD_CAST "error-message", BAD_CAST error->message) == NULL) {
			ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	/* error-info items */
	if (error->attribute != NULL) {
		if (xmlNewChild(einfo, NULL, BAD_CAST "attribute", BAD_CAST error->attribute) == NULL) {
			ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	if (error->element != NULL) {
		if (xmlNewChild(einfo, NULL, BAD_CAST "element", BAD_CAST error->element) == NULL) {
			ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	if (error->ns != NULL) {
		if (xmlNewChild(einfo, NULL, BAD_CAST "ns", BAD_CAST error->ns) == NULL) {
			ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	if (error->sid != NULL) {
		if (xmlNewChild(einfo, NULL, BAD_CAST "session-id", BAD_CAST error->sid) == NULL) {
			ERROR("xmlNewChild failed (%s:%d).", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	reply = nc_reply_create(content);
	reply->error = error;
	reply->type.reply = NC_REPLY_ERROR;
	xmlFreeNode(content);

	return (reply);
}

nc_rpc *nc_rpc_closesession()
{
	nc_rpc *rpc;
	xmlNodePtr content;

	if ((content = xmlNewNode(NULL, BAD_CAST "close-session")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	rpc->type.rpc = NC_RPC_SESSION;
	xmlFreeNode(content);

	return (rpc);
}

static int process_filter_param (xmlNodePtr content, struct nc_filter* filter)
{
	char* aux_string;
	xmlDocPtr doc_filter = NULL;

	if (filter != NULL) {
		if (filter->type == NC_FILTER_SUBTREE) {
			/* process Subtree filter type */

			/*
			 * prepare the filter specification - the filter content
			 * given by caller is enveloped by <filter> element to
			 * allow setting multiple filter elements corresponding
			 * to the configuration data model's root elements.
			 * Without this hack, libxml2 will not read given filter
			 * correctly when it contains multiple root elements.
			 */
			asprintf (&aux_string, "<filter type=\"%s\">%s</filter>", filter->type_string, filter->content);

			/* convert string to the libxml2 format */
			doc_filter = xmlReadMemory(aux_string, strlen(aux_string), NULL, NULL, 0);
			if (doc_filter == NULL) {
				ERROR("xmlReadMemory failed (%s:%d)", __FILE__, __LINE__);
				return (EXIT_FAILURE);
			}
			if (xmlAddChild(content, xmlCopyNode(doc_filter->children, 1)) == NULL) {
				ERROR("xmlAddChild failed (%s:%d)", __FILE__, __LINE__);
				xmlFreeDoc(doc_filter);
				return (EXIT_FAILURE);
			}
		}

		/* check that the filter was processed correctly */
		if (doc_filter != NULL) {
			xmlFreeDoc(doc_filter);
		} else {
			WARN("Unknown filter type used - skipping filter.");
		}
	}
	return (EXIT_SUCCESS);
}

nc_rpc *nc_rpc_getconfig(NC_DATASTORE source, struct nc_filter *filter)
{
	nc_rpc *rpc;
	xmlNodePtr content, node_source;
	char* datastore;


	switch (source) {
	case NC_DATASTORE_RUNNING:
		datastore = "running";
		break;
	case NC_DATASTORE_STARTUP:
		datastore = "startup";
		break;
	case NC_DATASTORE_CANDIDATE:
		datastore = "candidate";
		break;
	default:
		ERROR("Unknown source datastore for <get-config>.");
		return (NULL);
		break;
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "get-config")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}
	node_source = xmlNewChild(content, NULL, BAD_CAST "source", NULL);
	if (node_source == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_source, NULL, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	/* add filter specification if any required */
	if (process_filter_param(content, filter) != 0) {
		xmlFreeNode(content);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	rpc->type.rpc = NC_RPC_DATASTORE_READ;
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_get(struct nc_filter *filter)
{
	nc_rpc *rpc;
	xmlNodePtr content;

	if ((content = xmlNewNode(NULL, BAD_CAST "get")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	/* add filter specification if any required */
	if (process_filter_param(content, filter) != 0) {
		xmlFreeNode(content);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	rpc->type.rpc = NC_RPC_DATASTORE_READ;
	xmlFreeNode(content);

	return (rpc);

}

nc_rpc *nc_rpc_deleteconfig(NC_DATASTORE target)
{
	nc_rpc *rpc;
	xmlNodePtr content, node_target;
	char* datastore;

	switch (target) {
	case NC_DATASTORE_RUNNING:
		ERROR("Running datastore cannot be deleted.");
		return (NULL);
		break;
	case NC_DATASTORE_STARTUP:
		datastore = "startup";
		break;
	case NC_DATASTORE_CANDIDATE:
		datastore = "candidate";
		break;
	default:
		ERROR("Unknown target datastore for <delete-config>.");
		return (NULL);
		break;
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "delete-config")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	node_target = xmlNewChild(content, NULL, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_target, NULL, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_lock(NC_DATASTORE target)
{
	nc_rpc *rpc;
	xmlNodePtr content, node_target;
	char* datastore;

	switch (target) {
	case NC_DATASTORE_RUNNING:
		datastore = "running";
		break;
	case NC_DATASTORE_STARTUP:
		datastore = "startup";
		break;
	case NC_DATASTORE_CANDIDATE:
		datastore = "candidate";
		break;
	default:
		ERROR("Unknown target datastore for <lock>.");
		return (NULL);
		break;
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "lock")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	node_target = xmlNewChild(content, NULL, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_target, NULL, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_unlock(NC_DATASTORE target)
{
	nc_rpc *rpc;
	xmlNodePtr content, node_target;
	char* datastore;

	switch (target) {
	case NC_DATASTORE_RUNNING:
		datastore = "running";
		break;
	case NC_DATASTORE_STARTUP:
		datastore = "startup";
		break;
	case NC_DATASTORE_CANDIDATE:
		datastore = "candidate";
		break;
	default:
		ERROR("Unknown target datastore for <unlock>.");
		return (NULL);
		break;
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "unlock")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	node_target = xmlNewChild(content, NULL, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_target, NULL, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_copyconfig(NC_DATASTORE source, NC_DATASTORE target, const char *data)
{
	nc_rpc *rpc;
	xmlDocPtr doc_data;
	xmlNodePtr content, node_target, node_source, config;
	NC_DATASTORE params[2] = {source, target};
	char *datastores[2]; /* 0 - source, 1 - target */
	int i;

	if (target == source) {
		ERROR("<copy-config>'s source and target parameters identify the same datastore.");
		return (NULL);
	}

	for (i = 0; i < 2; i++) {
		switch (params[i]) {
		case NC_DATASTORE_RUNNING:
			datastores[i] = "running";
			break;
		case NC_DATASTORE_STARTUP:
			datastores[i] = "startup";
			break;
		case NC_DATASTORE_CANDIDATE:
			datastores[i] = "candidate";
			break;
		case NC_DATASTORE_NONE:
			if (i == 0) {
				if (data != NULL) {
					/* source configuration data are specified as given data */
					datastores[i] = NULL;
				} else {
					ERROR("Missing source configuration data for <copy-config>.");
					return (NULL);
				}
			} else {
				ERROR("Unknown target datastore for <copy-config>.");
				return (NULL);
			}
			break;
		default:
			ERROR("Unknown %s datastore for <copy-config>.", (i == 0) ? "source" : "target");
			return (NULL);
			break;
		}
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "copy-config")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	/* <source> */
	node_source = xmlNewChild(content, NULL, BAD_CAST "source", NULL);
	if (node_source == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (datastores[0] == NULL) {
		/* source configuration is given as data parameter */

		/* prepare covering element in rpc request */
		if ((config = xmlNewChild(node_source, NULL, BAD_CAST "config", NULL)) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}

		/* prepare XML structure from given data */
		doc_data = xmlReadMemory(data, strlen(data), NULL, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
		if (doc_data == NULL) {
			ERROR("xmlReadMemory failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}

		/* connect given configuration data with the rpc request */
		if (xmlAddChild(config, xmlCopyNode(doc_data->children, 1)) == NULL) {
			ERROR("xmlAddChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			xmlFreeDoc(doc_data);
			return (NULL);
		}

		/* free no more needed structure */
		xmlFreeDoc(doc_data);
	} else {
		/* source is one of the standard datastores */
		if (xmlNewChild(node_source, NULL, BAD_CAST datastores[0], NULL) == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	/* <target> */
	node_target = xmlNewChild(content, NULL, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_target, NULL, BAD_CAST datastores[1], NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
	xmlFreeNode(content);

	return (rpc);
}

nc_rpc *nc_rpc_editconfig(NC_DATASTORE target, NC_EDIT_DEFOP_TYPE default_operation, NC_EDIT_ERROPT_TYPE error_option, const char *data)
{
	nc_rpc *rpc;
	xmlDocPtr doc_data;
	xmlNodePtr content, node_target, node_defop, node_erropt, node_config;
	char* datastore, *defop = NULL, *erropt = NULL;

	if (data == NULL || strlen(data) == 0) {
		ERROR("Invalid configuration data for <edit-config>");
		return (NULL);
	}

	/* detect target datastore */
	switch (target) {
	case NC_DATASTORE_RUNNING:
		datastore = "running";
		break;
	case NC_DATASTORE_STARTUP:
		datastore = "startup";
		break;
	case NC_DATASTORE_CANDIDATE:
		datastore = "candidate";
		break;
	default:
		ERROR("Unknown target datastore for <edit-config>.");
		return (NULL);
		break;
	}

	/* detect default-operation parameter */
	if (default_operation != 0) {
		switch (default_operation) {
		case NC_EDIT_DEFOP_MERGE:
			defop = "merge";
			break;
		case NC_EDIT_DEFOP_NONE:
			defop = "none";
			break;
		case NC_EDIT_DEFOP_REPLACE:
			defop = "replace";
			break;
		default:
			ERROR("Unknown default-operation parameter for <edit-config>.");
			return (NULL);
			break;
		}
	}

	/* detect error-option parameter */
	if (error_option != 0) {
		switch (error_option) {
		case NC_EDIT_ERROPT_STOP:
			erropt = "stop-on-error";
			break;
		case NC_EDIT_ERROPT_CONT:
			erropt = "continue-on-error";
			break;
		case NC_EDIT_ERROPT_ROLLBACK:
			erropt = "rollback-on-error";
			break;
		default:
			ERROR("Unknown error-option parameter for <edit-config>.");
			return (NULL);
			break;
		}
	}

	/* create edit-config envelope */
	if ((content = xmlNewNode(NULL, BAD_CAST "edit-config")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	/* set <target> element */
	node_target = xmlNewChild(content, NULL, BAD_CAST "target", NULL);
	if (node_target == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}
	if (xmlNewChild(node_target, NULL, BAD_CAST datastore, NULL) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	/* set <default-operation> element */
	if (default_operation != 0) {
		node_defop = xmlNewChild(content, NULL, BAD_CAST "default-operation", BAD_CAST defop);
		if (node_defop == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	/* set <error-option> element */
	if (error_option != 0) {
		node_erropt = xmlNewChild(content, NULL, BAD_CAST "error-option", BAD_CAST erropt);
		if (node_erropt == NULL) {
			ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
			xmlFreeNode(content);
			return (NULL);
		}
	}

	/* set <config> element */
	/* prepare covering element in rpc request */
	if ((node_config = xmlNewChild(content, NULL, BAD_CAST "config", NULL)) == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	/* prepare XML structure from given data */
	doc_data = xmlReadMemory(data, strlen(data), NULL, NULL, XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (doc_data == NULL) {
		ERROR("xmlReadMemory failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	/* connect given configuration data with the rpc request */
	if (xmlAddChild(node_config, xmlCopyNode(doc_data->children, 1)) == NULL) {
		ERROR("xmlAddChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		xmlFreeDoc(doc_data);
		return (NULL);
	}
	xmlFreeDoc(doc_data);

	rpc = nc_rpc_create(content);
	rpc->type.rpc = NC_RPC_DATASTORE_WRITE;
	xmlFreeNode(content);

	return (rpc);
}


nc_rpc *nc_rpc_killsession(const char *kill_sid)
{
	nc_rpc *rpc;
	xmlNodePtr content, node_sid;

	/* check input parameter */
	if (kill_sid == NULL || strlen(kill_sid) == 0) {
		ERROR("Invalid session id for <kill-session> rpc message specified.");
		return (NULL);
	}

	if ((content = xmlNewNode(NULL, BAD_CAST "kill-session")) == NULL) {
		ERROR("xmlNewNode failed: %s (%s:%d).", strerror (errno), __FILE__, __LINE__);
		return (NULL);
	}

	node_sid = xmlNewChild(content, NULL, BAD_CAST "session-id", BAD_CAST kill_sid);
	if (node_sid == NULL) {
		ERROR("xmlNewChild failed (%s:%d)", __FILE__, __LINE__);
		xmlFreeNode(content);
		return (NULL);
	}

	rpc = nc_rpc_create(content);
	rpc->type.rpc = NC_RPC_SESSION;
	xmlFreeNode(content);

	return (rpc);
}
