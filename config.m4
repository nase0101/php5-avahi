PHP_ARG_ENABLE(avahi,
        [Whether to enable the "avahi" extension],
        [  --enable-avahi       Enable "avahi" extension support])


if test $PHP_AVAHI != "no"; then

 if test "$PHP_AVAHI" != "yes"; then
    AVAHI_SEARCH_DIRS=$PHP_AVAHI
  else
    AVAHI_SEARCH_DIRS="/usr/local /usr"
  fi

  for i in $AVAHI_SEARCH_DIRS; do
    if test -f $i/include/avahi-client/client.h; then
      AVAHI_DIR=$i
      AVAHI_INCDIR=$i/include/avahi-client
    fi
  done

  if test -z "$AVAHI_DIR"; then
    AC_MSG_ERROR(Cannot find avahi-client)
  fi

  AVAHI_LIBDIR=$AVAHI_DIR/$AVAHI_LIBDIR

  PHP_ADD_LIBRARY_WITH_PATH(avahi-client, $AVAHI_LIBDIR, AVAHI_SHARED_LIBADD)
  PHP_ADD_INCLUDE($AVAHI_INCDIR)

  PHP_SUBST(AVAHI_SHARED_LIBADD)
  PHP_NEW_EXTENSION(avahi, avahi.c, $ext_shared)
fi
