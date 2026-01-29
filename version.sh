#!/bin/sh
#
# This script extracts the 0MQ version from include/zlink.h, which is the master
# location for this information.
#
if [ ! -f include/zlink.h ]; then
    echo "version.sh: error: include/zlink.h does not exist" 1>&2
    exit 1
fi
MAJOR=`grep -E '^#define +ZLINK_VERSION_MAJOR +[0-9]+$' include/zlink.h`
MINOR=`grep -E '^#define +ZLINK_VERSION_MINOR +[0-9]+$' include/zlink.h`
PATCH=`grep -E '^#define +ZLINK_VERSION_PATCH +[0-9]+$' include/zlink.h`
if [ -z "$MAJOR" -o -z "$MINOR" -o -z "$PATCH" ]; then
    echo "version.sh: error: could not extract version from include/zlink.h" 1>&2
    exit 1
fi
MAJOR=`echo $MAJOR | awk '{ print $3 }'`
MINOR=`echo $MINOR | awk '{ print $3 }'`
PATCH=`echo $PATCH | awk '{ print $3 }'`
echo $MAJOR.$MINOR.$PATCH | tr -d '\n'

