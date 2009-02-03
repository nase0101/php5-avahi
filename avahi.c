#include "php_avahi.h"

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <locale.h>

#include <avahi-common/simple-watch.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/domain.h>
#include <avahi-common/llist.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>


typedef struct PublishConfig {
    int verbose, no_fail;
    char *name, *stype, *domain, *host;
    uint16_t port;
    AvahiStringList *txt, *subtypes;
    AvahiAddress address;
} PublishConfig;


typedef struct Config {
    int verbose;
    int terminate_on_all_for_now;
    int terminate_on_cache_exhausted;
    char *domain;
    char *stype;
    int ignore_local;
    int resolve;
    int no_fail;
    int parsable;
} Config;


typedef struct ServiceInfo ServiceInfo;

struct ServiceInfo {
    AvahiIfIndex interface;
    AvahiProtocol protocol;
    char *name, *type, *domain;

    AvahiServiceResolver *resolver;
    Config *config;

    AVAHI_LLIST_FIELDS(ServiceInfo, info);
};


static AvahiClient *client = NULL;
static AvahiSimplePoll *simple_poll = NULL;
static AvahiEntryGroup *entry_group = NULL;
static ServiceInfo *services = NULL;

static void service_resolver_callback(
    AvahiServiceResolver *r,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiResolverEvent event,
    const char *name,
    const char *type,
    const char *domain,
    const char *host_name,
    const AvahiAddress *a,
    uint16_t port,
    AvahiStringList *txt,
    AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
    void *userdata) {

    ServiceInfo *i = userdata;

    assert(r);
    assert(i);

    switch (event) {
        case AVAHI_RESOLVER_FOUND:
           break;

        case AVAHI_RESOLVER_FAILURE:
            php_printf("Failed to resolve service '%s' of type '%s' in domain '%s': %s\n", name, type, domain, avahi_strerror(avahi_client_errno(client)));
            break;
    }

    avahi_service_resolver_free(i->resolver);
    i->resolver = NULL;

}

static void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata) {
    PublishConfig *config = userdata;

    assert(g);
    assert(config);

    switch (state) {

        case AVAHI_ENTRY_GROUP_ESTABLISHED:
            php_printf("Established under name '%s'\n", config->name);
            break;

        case AVAHI_ENTRY_GROUP_FAILURE:

            php_printf("Failed to register: %s\n", avahi_strerror(avahi_client_errno(client)));
            break;

        case AVAHI_ENTRY_GROUP_COLLISION: {
            char *n;

	    /*
            if (config->command == COMMAND_PUBLISH_SERVICE)
                n = avahi_alternative_service_name(config->name);
            else {
                assert(config->command == COMMAND_PUBLISH_ADDRESS);
                n = avahi_alternative_host_name(config->name);
            }
	    */

            php_printf("Name collision, picking new name '%s'.\n", n);
            avahi_free(config->name);
            config->name = n;

            register_stuff(config);

            break;
        }

        case AVAHI_ENTRY_GROUP_UNCOMMITED:
        case AVAHI_ENTRY_GROUP_REGISTERING:
            break;
    }
}


static ServiceInfo *find_service(AvahiIfIndex interface, AvahiProtocol protocol, const char *name, const char *type, const char *domain) {
    ServiceInfo *i;

    for (i = services; i; i = i->info_next)
        if (i->interface == interface &&
            i->protocol == protocol &&
            strcasecmp(i->name, name) == 0 &&
            avahi_domain_equal(i->type, type) &&
            avahi_domain_equal(i->domain, domain))

            return i;

    return NULL;
}

static void remove_service(ServiceInfo *i) {
    assert(i);

    AVAHI_LLIST_REMOVE(ServiceInfo, info, services, i);

    if (i->resolver) {
        avahi_service_resolver_free(i->resolver);
    }

    avahi_free(i->name);
    avahi_free(i->type);
    avahi_free(i->domain);
    avahi_free(i);
}

static ServiceInfo *add_service(Config *c, AvahiIfIndex interface, AvahiProtocol protocol, const char *name, const char *type, const char *domain) {
    ServiceInfo *i;

    i = avahi_new(ServiceInfo, 1);

    /*
    if (c->resolve) {
        if (!(i->resolver = avahi_service_resolver_new(client, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC, 0, service_resolver_callback, i))) {
            avahi_free(i);
            php_printf("Failed to resolve service '%s' of type '%s' in domain '%s': %s\n", name, type, domain, avahi_strerror(avahi_client_errno(client)));
            return NULL;
        }

    } else
    */

    i->resolver = NULL;

    i->interface = interface;
    i->protocol = protocol;
    i->name = avahi_strdup(name);
    i->type = avahi_strdup(type);
    i->domain = avahi_strdup(domain);
    i->config = c;

    AVAHI_LLIST_PREPEND(ServiceInfo, info, services, i);

    return i;
}

static browse_callback(

    AvahiServiceBrowser *b,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *name,
    const char *type,
    const char *domain,
    AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
    void* userdata) {
    Config *c = userdata;
    assert(b);


    switch (event) {
        case AVAHI_BROWSER_FAILURE:
            php_printf("(Browser) %s\n", avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
            avahi_simple_poll_quit(simple_poll);
            return;

        case AVAHI_BROWSER_NEW:
	    add_service(c, interface, protocol, name, type, domain);
            break;

        case AVAHI_BROWSER_REMOVE:
            php_printf("(Browser) REMOVE: service '%s' of type '%s' in domain '%s'\n", name, type, domain);
            break;

        case AVAHI_BROWSER_ALL_FOR_NOW:
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
            //php_printf("(Browser) %s\n", event == AVAHI_BROWSER_CACHE_EXHAUSTED ? "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
	    avahi_simple_poll_quit(simple_poll);


	    return 3;
            break;
    }
}



static void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata) {
    assert(c);

    switch (state) {
	case  AVAHI_CLIENT_FAILURE:
    	    php_printf("Server connection failure: %s\n", avahi_strerror(avahi_client_errno(c)));
            avahi_simple_poll_quit(simple_poll);
	    break;    
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

PHP_FUNCTION(avahi_publish_service)
{
	PublishConfig *config;

        if (avahi_entry_group_add_address(entry_group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, config->name, &config->address) < 0) {
            php_printf("Failed to add address: %s\n", avahi_strerror(avahi_client_errno(client)));
            RETURN_NULL();
        }
}

PHP_FUNCTION(avahi_browse_service)
{
    AvahiServiceBrowser *sb = NULL;
    Config config;
    int error;
    ServiceInfo *i;
    char *type;
    int type_len;
    char *domain;
    int domain_len;
	
    int argc = ZEND_NUM_ARGS();

    zval *mysubarray;
    array_init(return_value);

    if (zend_parse_parameters(argc TSRMLS_CC, "s|s", &type, &type_len, &domain, &domain_len) == FAILURE) {
        RETURN_NULL();
    }
    
    if (argc==1) {
	domain = "local";
    }

    /* Allocate main loop object */
    if (!(simple_poll = avahi_simple_poll_new())) {
        php_printf("Failed to create simple poll object.\n");
	return;	    
    }

    /* Allocate a new client */
    client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, client_callback, &config, &error);

    /* Check wether creating the client object succeeded */
    if (!client) {
        php_printf("Failed to create client: %s\n", avahi_strerror(error));
	return;    
    }

    /* Create the service browser */
    if (!(sb = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, type, NULL, 0, browse_callback, NULL))) {
        php_printf("Failed to create service browser: %s\n", avahi_strerror(avahi_client_errno(client)));
        return;
    }

    while (services) {
        remove_service(services);
    }
 
    /* Run the main loop */
    avahi_simple_poll_loop(simple_poll);
    
    for (i = services; i; i = i->info_next) {
	ALLOC_INIT_ZVAL(mysubarray);
	array_init(mysubarray);
        add_assoc_string(mysubarray, "service", i->name, 1);
        add_assoc_string(mysubarray, "type", i->type, 1);
        add_assoc_string(mysubarray, "domain", i->domain, 1);

        add_next_index_zval(return_value, mysubarray);    
    } 
    
}



PHP_FUNCTION(avahi_browse_array)
{
}

static function_entry php_avahi_functions[] = {
	PHP_FE(avahi_publish_service, NULL)
	PHP_FE(avahi_remove_service, NULL)
	PHP_FE(avahi_browse_service, NULL)
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

