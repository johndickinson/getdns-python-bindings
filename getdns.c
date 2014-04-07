/**
 *
 * \file getdns.c
 * @brief pygetdns core functions and classes
 *
 */


/*
 * Copyright (c) 2014, Verisign, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * * Neither the name of the <organization> nor the
 * names of its contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Verisign, Include. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */



#include <Python.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <getdns/getdns.h>
#include <getdns/getdns_ext_libevent.h>
#include <event2/event.h>
#include "pygetdns.h"

#define UNUSED_PARAM(x) ((void)(x))

/*
 *  A shim to sit between the event callback function
 *  and the python callback.  This is a wee bit hacky
 */


void         
callback_shim(getdns_context *context, getdns_callback_type_t type, getdns_dict *resp,
  void *u, getdns_transaction_t tid)
{
    PyObject *main_module;
    PyObject *main_dict;
    PyObject *getdns_runner;
    pygetdns_libevent_callback_data *callback_data;
    PyObject *response;

    if ((main_module = PyImport_AddModule("__main__")) == 0)  {
        PyErr_SetString(getdns_error, "No __main__");
        /* need to throw an error here */
    }
    callback_data = (pygetdns_libevent_callback_data *)u;
    main_dict = PyModule_GetDict(main_module);
    if ((getdns_runner = PyDict_GetItemString(main_dict, callback_data->callback_func)) == 0)  {
        PyErr_SetString(getdns_error, "callback not found");
        /* need to throw an error here XXX */
    }
    /* Python callback prototype: */
    /* callback(context, callback_type, response, userarg, tid) */

    if ((response = decode_getdns_replies_tree_response(resp)) == 0)  {
        PyErr_SetString(getdns_error, "Unable to decode response");
        /* need to throw exceptiion XXX */
    }
    PyObject_CallFunction(getdns_runner, "OHOsl", context, type, response,
                          callback_data->userarg, tid);
}


void 
destroy_context(PyObject *context_capsule)
{
    struct getdns_context *context = 0;

    context = PyCapsule_GetPointer(context_capsule, "context");
    free(context);
    Py_DECREF(context_capsule);
}


/**
 * create context, return context dict
 */

static PyObject *
context_create(PyObject *self, PyObject *args, PyObject *keywds)
{
    static char *kwlist[] = {
        "set_from_os",
        0
    };
    struct getdns_context *context = 0;
    int  set_from_os = 1;       /* default to True */
    getdns_return_t ret;
    char err_buf[256];          
    PyObject *py_context;

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "|i", kwlist,
                                     &set_from_os))  {
        PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
        return NULL;    
    }
    if ((ret = getdns_context_create(&context, set_from_os)) != GETDNS_RETURN_GOOD)  {
        getdns_strerror(ret, err_buf, sizeof err_buf);
        PyErr_SetString(getdns_error, err_buf);
        return NULL;
    }

    py_context = PyCapsule_New(context, "context", destroy_context);
    Py_INCREF(py_context);
    return py_context;
}


PyObject *
do_query(PyObject *context_capsule,
         void *name,
         uint16_t request_type,
         PyDictObject *extensions_obj,
         void *userarg,
         long tid,
         char *callback)

{
    struct getdns_context *context;
    struct getdns_dict *extensions_dict = 0;
    struct getdns_dict *resp = 0;
    getdns_return_t ret;
    char *query_name;

    context = PyCapsule_GetPointer(context_capsule, "context");
    if (extensions_obj)  {
        if ((extensions_dict = extensions_to_getdnsdict(extensions_obj)) == 0)  {
            PyErr_SetString(getdns_error, "Dictionary parse failure");
            return NULL;
        }
    }
    if (request_type == GETDNS_RRTYPE_PTR)  {
        PyObject *address = (PyObject *)name;
        getdns_dict *addr_dict;
        getdns_bindata addr_data;
        getdns_bindata addr_type;
        PyObject *str;
        int domain;
        unsigned char buf[sizeof(struct in6_addr)];

        if (!PyDict_Check(address))  {
            PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
            return NULL;    
        }
        if (PyDict_Size(address) != 2)  {
            PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
            return NULL;    
        }
        addr_dict = getdns_dict_create();
        if ((str = PyDict_GetItemString(address, "address_type")) == NULL)  {
            PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
            return NULL;    
        }
        if (!PyString_Check(str))  {
            PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
            return NULL;
        }
        addr_type.data = (uint8_t *)strdup(PyString_AsString(str));
        addr_type.size = strlen((char *)addr_type.data);
        if (strlen((char *)addr_type.data) != 4)  {
            PyErr_SetString(getdns_error, GETDNS_RETURN_WRONG_TYPE_REQUESTED_TEXT);
            return NULL;
        }
        if (!strncasecmp((char *)addr_type.data, "IPv4", 4))
            domain = AF_INET;
        else if (!strncasecmp((char *)addr_type.data, "IPv6", 4))
            domain = AF_INET6;
        else  {
            PyErr_SetString(getdns_error,  GETDNS_RETURN_INVALID_PARAMETER_TEXT);
            return NULL;
        }
        getdns_dict_set_bindata(addr_dict, "address_type", &addr_type);

        if ((str = PyDict_GetItemString(address, "address_data")) == NULL)  {
            PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
            return NULL;            
        }
        if (!PyString_Check(str))  {
            PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
            return NULL;
        }
        if (inet_pton(domain, PyString_AsString(str), buf) <= 0)  {
            PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
            return NULL;
        }
        addr_data.data = (uint8_t *)buf;
        addr_data.size = (domain == AF_INET ? 4 : 16);
        getdns_dict_set_bindata(addr_dict, "address_data", &addr_data);
        if ((query_name = reverse_address(&addr_data)) == NULL)  {
            PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
            return NULL;
        }
    }  else
        query_name = (char *)name;

    if (callback)  {
        struct event_base *gen_event_base;
        int dispatch_ret;
        pygetdns_libevent_callback_data callback_data;

        if ((gen_event_base = event_base_new()) == NULL)  {
            PyErr_SetString(getdns_error, GETDNS_RETURN_GENERIC_ERROR_TEXT);
            return NULL;
        }
        
        callback_data.callback_func = callback;
        callback_data.userarg = userarg;
        if ((ret = getdns_extension_set_libevent_base(context, gen_event_base)) != GETDNS_RETURN_GOOD)  {
            PyErr_SetString(getdns_error, GETDNS_RETURN_GENERIC_ERROR_TEXT);
            return NULL;
        }

        if ((ret = getdns_general(context, query_name, request_type,
                                  extensions_dict, (void *)&callback_data,
                                  (getdns_transaction_t *)&tid, callback_shim)) != GETDNS_RETURN_GOOD)  {
            PyErr_SetString(getdns_error, GETDNS_RETURN_GENERIC_ERROR_TEXT);
            event_base_free(gen_event_base);
            return NULL;
        }
        dispatch_ret = event_base_dispatch(gen_event_base);
        UNUSED_PARAM(dispatch_ret);
            
        event_base_free(gen_event_base);
        return Py_None;
    }
    if ((ret = getdns_general_sync(context, query_name, request_type,
                                   extensions_dict, &resp)) != GETDNS_RETURN_GOOD)  {
        PyErr_SetString(getdns_error, GETDNS_RETURN_GENERIC_ERROR_TEXT);
        return NULL;
    }
    return getFullResponse(resp);
#if 0
    return decode_getdns_replies_tree_response(resp);
#endif

}


static PyObject *
cancel_callback(PyObject *self, PyObject *args, PyObject *keywds)
{
    static char *kwlist[] = {
        "context",
        "transaction_id",
        0
    };
    PyObject *context_capsule;
    struct getdns_context *context;
    getdns_return_t ret;
    long tid;

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "Ol", kwlist,
                                     &context_capsule, &tid))  {
        return NULL;
    }
    context = PyCapsule_GetPointer(context_capsule, "context");
    if ((ret = getdns_cancel_callback(context, (getdns_transaction_t)tid)) !=
        GETDNS_RETURN_GOOD)  {
        char err_buf[256];
        getdns_strerror(ret, err_buf, sizeof err_buf);
        PyErr_SetString(getdns_error, err_buf);
        return NULL;
    }
    return Py_None;
}


static PyObject *
general(PyObject *self, PyObject *args, PyObject *keywds)
{
    static char *kwlist[] = {
        "context",
        "name",
        "request_type",
        "extensions",
        "userarg",
        "transaction_id",
        "callback",
        0
    };

    PyObject *context_capsule;
    char *name;
    uint16_t  request_type;
    PyDictObject *extensions_obj;
    void *userarg;
    long tid = 0;
    char *callback = 0;
    PyObject *my_ret;

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "OsH|Osls", kwlist,
                                     &context_capsule, &name, &request_type,
                                     &extensions_obj, &userarg, &tid, &callback))  {
        return NULL;
    }
    if ((my_ret = do_query(context_capsule, name, request_type, extensions_obj, userarg,
                           (long)tid, callback)) == 0)
        PyErr_SetString(getdns_error, GETDNS_RETURN_GENERIC_ERROR_TEXT);
        return NULL;
    return my_ret;
}


static PyObject *
service(PyObject *self, PyObject *args, PyObject *keywds)
{
    static char *kwlist[] = {
        "context",
        "name",
        "extensions",
        "userarg",
        "transaction_id",
        "callback",
        0
    };
    PyObject *context_capsule;
    char *name;
    PyDictObject *extensions_obj = 0;
    void *userarg;
    long tid;
    char * callback = 0;
    PyObject *my_ret;

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "Os|Osls", kwlist,
                                     &context_capsule, &name, 
                                     &extensions_obj, &userarg, &tid, &callback))  {
        PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
        return NULL;            
    }
    if ((my_ret = do_query(context_capsule, name, (uint16_t)GETDNS_RRTYPE_SRV, extensions_obj, userarg,
                           tid, callback)) == 0)  {
        PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
        return NULL;
    }
    return my_ret;

    
}


static PyObject *
address(PyObject *self, PyObject *args, PyObject *keywds)
{
    static char *kwlist[] = {
        "context",
        "name",
        "extensions",
        "userarg",
        "transaction_id",
        "callback",
        0
    };
    PyObject *context_capsule;
    char *name;
    PyDictObject *extensions_obj = 0;
    void *userarg;
    long tid;
    char * callback = 0;
    PyObject *my_ret;

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "Os|OsHs", kwlist,
                                     &context_capsule, &name, 
                                     &extensions_obj, &userarg, &tid, &callback))  {
        PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
    }
    if ((my_ret = do_query(context_capsule, name, GETDNS_RRTYPE_A, extensions_obj, userarg,
                           tid, callback)) == 0)  {
        PyErr_SetString(getdns_error, GETDNS_RETURN_GENERIC_ERROR_TEXT);
        return NULL;
    }
    return my_ret;
}

static PyObject *
hostname(PyObject *self, PyObject *args, PyObject *keywds)
{
    static char *kwlist[] = {
        "context",
        "address",
        "extensions",
        "userarg",
        "transaction_id",
        "callback",
        0
    };
    PyObject *context_capsule;
    void *address;
    PyDictObject *extensions_obj = 0;
    void *userarg;
    long tid;
    char * callback = 0;
    PyObject *my_ret;

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "OO|Osls", kwlist,
                                     &context_capsule, &address, 
                                     &extensions_obj, &userarg, &tid, &callback))  {
        PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
        return NULL; 
    }
    if ((my_ret = do_query(context_capsule, address, GETDNS_RRTYPE_PTR, extensions_obj, userarg,
                           tid, callback)) == 0)
        PyErr_SetString(getdns_error, GETDNS_RETURN_GENERIC_ERROR_TEXT);
        return NULL;
    return my_ret;
}




/*
 * Implements the replies_tree for the getDns API
 * Returns a PyObject with the response.
 */
static PyObject *
replies_tree(PyObject *self, PyObject *args, PyObject *keywds)
{
    static char *kwlist[] = {
        "context",
        "name",
        "request_type",
        "extensions",
        "callback",
        0
    };

    PyObject *context_capsule;
    struct getdns_context *context;
    char *name;
    uint16_t  request_type;
    PyDictObject *extensions_obj;
    struct getdns_dict *extensions_dict;
    int callback = 0;
    getdns_return_t ret;
    struct getdns_dict *resp = 0;

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "OsH|OO" , kwlist,
                                     &context_capsule, &name, &request_type,
                                     &extensions_obj, &callback))  {
        PyErr_SetString(getdns_error, GETDNS_RETURN_INVALID_PARAMETER_TEXT);
        return NULL;
    }
    context = PyCapsule_GetPointer(context_capsule, "context");
    if ((extensions_dict = extensions_to_getdnsdict(extensions_obj)) == 0)  {
        PyErr_SetString(getdns_error, "Dictionary parse failure");
        return NULL;
    }
    if ((ret = getdns_general_sync(context, name, request_type,
                                   extensions_dict, &resp))
    		                       != GETDNS_RETURN_GOOD)  {
    	//TODO: refine error handling consistently thru the app, a error handler
    	// with helpful messages.
    	char err_buf[255];
        getdns_strerror(ret, err_buf, sizeof err_buf);
        PyErr_SetString(getdns_error, err_buf);
        return NULL;
    }
#if 0 
    int list_len;
    (void)getdns_list_get_length(resp, &list_len);
    printf("%d answers\n", list_len);
    int i = 0;
    struct getdns_bindata *resp_item;
    for ( i = 0 ; i < list_len ; i++ )  {
        getdns_list_get_bindata(resp, i, &resp_item);
        printf("Item %s\n", resp_item->data);
    }
#endif 
#if 0
    return decode_getdns_replies_tree_response(resp);
#endif
    return getFullResponse(resp);

}



static struct PyMethodDef getdns_methods[] = {
    { "context_create", (PyCFunction)context_create, METH_KEYWORDS },
    { "general", (PyCFunction)general, METH_KEYWORDS },
    { "address", (PyCFunction)address, METH_KEYWORDS },
    { "service", (PyCFunction)service, METH_KEYWORDS },
    { "hostname", (PyCFunction)hostname, METH_KEYWORDS },
    { "replies_tree", (PyCFunction)replies_tree, METH_KEYWORDS },
    { "cancel_callback", (PyCFunction)cancel_callback, METH_KEYWORDS },
    { 0, 0 }
};

PyMODINIT_FUNC
initgetdns(void)
{
    PyObject *g;

    static PyObject *getdns_error;
    if ((g = Py_InitModule("getdns", getdns_methods)) == NULL)
        return;
    getdns_error = PyErr_NewException("getdns.error", NULL, NULL);
    Py_INCREF(getdns_error);
    PyModule_AddObject(g, "error", getdns_error);

/*
 * return value constants
 */

    PyModule_AddIntConstant(g, "GETDNS_RETURN_GOOD", 0);
    PyModule_AddIntConstant(g, "GETDNS_RETURN_GENERIC_ERROR", 1);
    PyModule_AddIntConstant(g, "GETDNS_RETURN_BAD_DOMAIN_NAME", 300);
    PyModule_AddIntConstant(g, "GETDNS_RETURN_BAD_CONTEXT", 301);
    PyModule_AddIntConstant(g, "GETDNS_RETURN_CONTEXT_UPDATE_FAIL", 302);
    PyModule_AddIntConstant(g, "GETDNS_RETURN_UNKNOWN_TRANSACTION", 303);
    PyModule_AddIntConstant(g, "GETDNS_RETURN_NO_SUCH_LIST_ITEM", 304);
    PyModule_AddIntConstant(g, "GETDNS_RETURN_NO_SUCH_DICT_NAME", 305);
    PyModule_AddIntConstant(g, "GETDNS_RETURN_WRONG_TYPE_REQUESTED", 306);
    PyModule_AddIntConstant(g, "GETDNS_RETURN_NO_SUCH_EXTENSION", 307);
    PyModule_AddIntConstant(g, "GETDNS_RETURN_EXTENSION_MISFORMAT", 308);
    PyModule_AddIntConstant(g, "GETDNS_RETURN_DNSSEC_WITH_STUB_DISALLOWED", 309);
    PyModule_AddIntConstant(g, "GETDNS_RETURN_MEMORY_ERROR", 310);
    PyModule_AddIntConstant(g, "GETDNS_RETURN_INVALID_PARAMETER", 311);

/*
 * dnssec values
 */

    PyModule_AddIntConstant(g, "GETDNS_DNSSEC_SECURE", 400);
    PyModule_AddIntConstant(g, "GETDNS_DNSSEC_BOGUS", 401);
    PyModule_AddIntConstant(g, "GETDNS_DNSSEC_INDETERINATE", 402);
    PyModule_AddIntConstant(g, "GETDNS_DNSSEC_INSECURE", 403);
    PyModule_AddIntConstant(g, "GETDNS_DNSSEC_NOT_PERFORMED", 404);

/*
 * namespace types
 */

    PyModule_AddIntConstant(g, "GETDNS_NAMESPACE_DNS", 500);
    PyModule_AddIntConstant(g, "GETDNS_NAMESPACE_LOCALNAMES", 501);
    PyModule_AddIntConstant(g, "GETDNS_NAMESPACE_NETBIOS", 502);
    PyModule_AddIntConstant(g, "GETDNS_NAMESPACE_MDNS", 503);
    PyModule_AddIntConstant(g, "GETDNS_NAMESPACE_NIS", 504);

/*
 * resolution types
 */

    PyModule_AddIntConstant(g, "GETDNS_RESOLUTION_STUB", 520);
    PyModule_AddIntConstant(g, "GETDNS_RESOLUTION_RECURSING", 521);

/*
 * redirect policies
 */

    PyModule_AddIntConstant(g, "GETDNS_REDIRECTS_FOLLOW", 530);
    PyModule_AddIntConstant(g, "GETDNS_REDIRECTS_DO_NOTFOLLOW", 531);

/*
 * transport arrangements
 */

    PyModule_AddIntConstant(g, "GETDNS_TRANSPORT_UDP_FIRST_AND_FALL_BACK_TO_TCP", 540);
    PyModule_AddIntConstant(g, "GETDNS_TRANSPORT_UDP_ONLY", 541);
    PyModule_AddIntConstant(g, "GETDNS_TRANSPORT_TCP_ONLY", 542);
    PyModule_AddIntConstant(g, "GETDNS_TRANSPORT_TCP_ONLY_KEEP_CONNECTIONS_OPEN", 543);

/*
 * suffix appending methods
 */

    PyModule_AddIntConstant(g, "GETDNS_APPEND_NAME_ALWAYS", 550);
    PyModule_AddIntConstant(g, "GETDNS_APPEND_NAME_ONLY_TO_SINGLE_LABEL_AFTER_FAILURE", 551);
    PyModule_AddIntConstant(g, "GETDNS_APPEND_NAME_ONLY_TO_MULTIPLE_LABEL_NAME_AFTER_FAILURE", 552);
    PyModule_AddIntConstant(g, "GETDNS_APPEND_NAME_NEVER", 553);

/*
 * context codes
 */
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_NAMESPACES", 600);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_RESOLUTION_TYPE", 601);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_FOLLOW_REDIRECTS", 602);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_UPSTREAM_RECURSIVE_SERVERS", 603);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_DNS_ROOT_SERVERS", 604);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_DNS_TRANSPORT", 605);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_LIMIT_OUTSTANDING_QUERIES", 606);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_APPEND_NAME", 607);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_SUFFIX", 608);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_DNSSEC_TRUST_ANCHORS", 609);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_EDNS_MAXIMUM_UDP_PAYLOAD_SIZE", 610);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_EDNS_EXTENDED_RCODE", 611);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_EDNS_VERSION", 612);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_EDNS_DO_BIT", 613);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_DNSSEC_ALLOWED_SKEW", 614);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_MEMORY_FUNCTIONS", 615);
    PyModule_AddIntConstant(g, "GETDNS_CONTEXT_CODE_TIMEOUT", 61);

/*
 * name service types
 */

    PyModule_AddIntConstant(g, "GETDNS_NAMETYPE_DNS", 800);
    PyModule_AddIntConstant(g, "GETDNS_NAMETYPE_WINS", 801);

    PyModule_AddIntConstant(g, "GETDNS_EXTENSION_TRUE", 1000);
    PyModule_AddIntConstant(g, "GETDNS_EXTENSION_FALSE", 1001);

    PyModule_AddIntConstant(g, "GETDNS_CALLBACK_COMPLETE", 700);
    PyModule_AddIntConstant(g, "GETDNS_CALLBACK_CANCEL", 701);
    PyModule_AddIntConstant(g, "GETDNS_CALLBACK_TIMEOUT", 702);
    PyModule_AddIntConstant(g, "GETDNS_CALLBACK_ERROR", 703);

    PyModule_AddIntConstant(g, "GETDNS_RESPSTATUS_GOOD", 900);
    PyModule_AddIntConstant(g, "GETDNS_RESPSTATUS_NO_NAME", 901);
    PyModule_AddIntConstant(g, "GETDNS_RESPSTATUS_ALL_TIMEOUT", 902);
    PyModule_AddIntConstant(g, "GETDNS_RESPSTATUS_NO_SECURE_ANSWERS", 903);

    PyModule_AddIntConstant(g, "GETDNS_BAD_DNS_CNAME_IN_TARGET", 1100);
    PyModule_AddIntConstant(g, "GETDNS_BAD_DNS_ALL_NUMERIC_LABEL", 1101);
    PyModule_AddIntConstant(g, "GETDNS_BAD_DNS_CNAME_RETURNED_FOR_OTHER_TYPE", 1102);

/*
 * rr type constants
 */

    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_A", 1);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_NS", 2);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_MD", 3);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_MF", 4);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_CNAME", 5);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_SOA", 6);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_MB", 7);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_MG", 8);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_MR", 9);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_NULL", 10);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_WKS", 11);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_PTR", 12);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_HINFO", 13);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_MINFO", 14);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_MX", 15);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_TXT", 16);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_RP", 17);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_AFSDB", 18);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_X25", 19);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_ISDN", 20);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_RT", 21);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_NSAP", 22);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_SIG", 24);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_KEY", 25);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_PX", 26);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_GPOS", 27);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_AAAA", 28);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_LOC", 29);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_NXT", 30);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_EID", 31);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_NIMLOC", 32);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_SRV", 33);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_ATMA", 34);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_NAPTR", 35);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_KX", 36);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_CERT", 37);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_A6", 38);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_DNAME", 39);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_SINK", 40);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_OPT", 41);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_APL", 42);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_DS", 43);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_SSHFP", 44);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_IPSECKEY", 45);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_RRSIG", 46);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_NSEC", 47);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_DNSKEY", 48);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_DHCID", 49);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_NSEC3", 50);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_NSEC3PARAM", 51);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_TLSA", 52);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_HIP", 55);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_NINFO", 56);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_RKEY", 57);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_TALINK", 58);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_CDS", 59);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_SPF", 99);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_UINFO", 100);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_UID", 101);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_GID", 102);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_UNSPEC", 103);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_NID", 104);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_L32", 105);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_L64", 106);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_LP", 107);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_EUI48", 108);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_EUI64", 109);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_TKEY", 249);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_TSIG", 250);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_IXFR", 251);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_AXFR", 252);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_MAILB", 253);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_MAILA", 254);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_URI", 256);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_CAA", 257);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_TA", 32768);
    PyModule_AddIntConstant(g, "GETDNS_RRTYPE_DLV", 32769);

}
