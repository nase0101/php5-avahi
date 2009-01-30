#include "php_avahi.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>
#include <avahi-common/alternative.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/timeval.h>

static AvahiClient *client = NULL;
static AvahiSimplePoll *simple_poll = NULL;

static void resolve_callback(

    AvahiServiceResolver *r,
    AVAHI_GCC_UNUSED AvahiIfIndex interface,
    AVAHI_GCC_UNUSED AvahiProtocol protocol,
    AvahiResolverEvent event,
    const char *name,
    const char *type,
    const char *domain,
    const char *host_name,
    const AvahiAddress *address,
    uint16_t port,
    AvahiStringList *txt,
    AvahiLookupResultFlags flags,
    AVAHI_GCC_UNUSED void* userdata) {

    assert(r);

    switch (event) {
        case AVAHI_RESOLVER_FAILURE:
            //fprintf(stderr, "(Resolver) Failed to resolve service '%s' of type '%s' in domain '%s': %s\n", name, type, domain, avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r))));
            break;

        case AVAHI_RESOLVER_FOUND: {
            char a[AVAHI_ADDRESS_STR_MAX], *t;

            php_printf("Service '%s' of type '%s' in domain '%s':\n", name, type, domain);

            avahi_address_snprint(a, sizeof(a), address);
            t = avahi_string_list_to_string(txt);
	    
            php_printf(
                    "\t%s:%u (%s)\n"
                    "\tTXT=%s\n"
                    "\tcookie is %u\n"
                    "\tis_local: %i\n"
                    "\tour_own: %i\n"
                    "\twide_area: %i\n"
                    "\tmulticast: %i\n"
                    "\tcached: %i\n",
                    host_name, port, a,
                    t,
                    avahi_string_list_get_service_cookie(txt),
                    !!(flags & AVAHI_LOOKUP_RESULT_LOCAL),
                    !!(flags & AVAHI_LOOKUP_RESULT_OUR_OWN),
                    !!(flags & AVAHI_LOOKUP_RESULT_WIDE_AREA),
                    !!(flags & AVAHI_LOOKUP_RESULT_MULTICAST),
                    !!(flags & AVAHI_LOOKUP_RESULT_CACHED));
	    
            avahi_free(t);
        }
    }

    avahi_service_resolver_free(r);
}


static int browse_callback(

    AvahiServiceBrowser *b,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *name,
    const char *type,
    const char *domain,
    AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
    void* userdata) {
    AvahiClient *c = userdata;
    assert(b);

    switch (event) {
        case AVAHI_BROWSER_FAILURE:
            php_printf("(Browser) %s\n", avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
            avahi_simple_poll_quit(simple_poll);
            return;

        case AVAHI_BROWSER_NEW:
	    php_printf("%s %s %s" , name,type,domain);

            if (!(avahi_service_resolver_new(c, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC, 0, resolve_callback, c))) {
                php_printf("Failed to resolve service '%s': %s\n", name, avahi_strerror(avahi_client_errno(c)));
	    }

            break;

        case AVAHI_BROWSER_REMOVE:
            php_printf("(Browser) REMOVE: service '%s' of type '%s' in domain '%s'\n", name, type, domain);
            break;

        case AVAHI_BROWSER_ALL_FOR_NOW:
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
            php_printf("(Browser) %s\n", event == AVAHI_BROWSER_CACHE_EXHAUSTED ? "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
	    avahi_simple_poll_quit(simple_poll);


	    return 3;
            break;
    }
}


static void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata) {
    assert(c);

    if (state == AVAHI_CLIENT_FAILURE) {
        php_printf("Server connection failure: %s\n", avahi_strerror(avahi_client_errno(c)));
        avahi_simple_poll_quit(simple_poll);
    }
}

PHP_MINIT_FUNCTION(avahi)
{
	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(avahi)
{
        return SUCCESS;
}

PHP_RINIT_FUNCTION(avahi)
{
    return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(avahi)
{
    return SUCCESS;
}


PHP_MINFO_FUNCTION(avahi)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "avahi support", "enabled");
    php_info_print_table_end();

}

PHP_FUNCTION(avahi_remove_service)
{
}

PHP_FUNCTION(avahi_create_service)
{
    AvahiClient *client = NULL;
    int error;
    int ret = 1;
    struct timeval tv;
    char *name;
    int name_len;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &name, &name_len) == FAILURE) {
       	RETURN_NULL();
    }

    /* Allocate main loop object */
    if (!(simple_poll = avahi_simple_poll_new())) {
        php_printf("Failed to create simple poll object.\n");
	return;    
    }

    name = avahi_strdup(name);

    /* Allocate a new client */
    client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, client_callback, NULL, &error);

    /* Check wether creating the client object succeeded */
    if (!client) {
       php_printf(stderr, "Failed to create client: %s\n", avahi_strerror(error));
       return;
    }

    /* Run the main loop */
    avahi_simple_poll_loop(simple_poll);
    
    php_printf("Hello %s ", name);
	
	
}

PHP_FUNCTION(avahi_browse_service)
{
    AvahiClient *client = NULL;
    AvahiServiceBrowser *sb = NULL;
    int error;
    int ret = 1;

    zval *mysubarray;
    array_init(return_value);

    ALLOC_INIT_ZVAL(mysubarray);
    array_init(mysubarray);
    add_next_index_string(mysubarray, "hello", 1);


    /* Allocate main loop object */
    if (!(simple_poll = avahi_simple_poll_new())) {
        php_printf("Failed to create simple poll object.\n");
	return;	    
    }

    /* Allocate a new client */
    client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, client_callback, NULL, &error);

    /* Check wether creating the client object succeeded */
    if (!client) {
        php_printf("Failed to create client: %s\n", avahi_strerror(error));
	return;    
    }

    /* Create the service browser */
    if (!(sb = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_http._tcp", NULL, 0, browse_callback, client))) {
        php_printf("Failed to create service browser: %s\n", avahi_strerror(avahi_client_errno(client)));
        return;
    }

    /* Run the main loop */
    avahi_simple_poll_loop(simple_poll);

}

PHP_FUNCTION(avahi_browse_array)
{
}

static function_entry php_avahi_functions[] = {
	PHP_FE(avahi_create_service, NULL)
	PHP_FE(avahi_remove_service, NULL)
	PHP_FE(avahi_browse_service, NULL)
        PHP_FE(avahi_browse_array, NULL)
        { NULL, NULL, NULL }
};

zend_module_entry avahi_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
        STANDARD_MODULE_HEADER,
#endif
        PHP_AVAHI_EXTNAME,
        php_avahi_functions,
    	PHP_MINIT(avahi),
    	PHP_MSHUTDOWN(avahi),
    	PHP_RINIT(avahi),
    	PHP_RSHUTDOWN(avahi),
    	PHP_MINFO(avahi),
#if ZEND_MODULE_API_NO >= 20010901
        PHP_AVAHI_EXTVER,
#endif
        STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_AVAHI
ZEND_GET_MODULE(avahi)
#endif

