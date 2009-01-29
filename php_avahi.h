#ifndef PHP_AVAHI_H
/* Prevent double inclusion */
#define PHP_AVAHI_H

/* Define extension properties */
#define PHP_AVAHI_EXTNAME "avahi"
#define PHP_AVAHI_EXTVER "1.0"

/* Import configure options
 *  * when building outside of the
 *   * PHP source tree */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Include PHP standard Header */
#include "php.h"
/*
 *  * define the entry point symbole
 *   * Zend will use when loading this module
 *    */
extern zend_module_entry avahi_module_entry;
#define phpext_avahi_ptr &avahi_module_entry

#endif /* PHP_AVAHI_H */

