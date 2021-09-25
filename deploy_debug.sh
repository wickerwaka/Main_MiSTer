#! /bin/sh

# defines
TARGET=mister-dev
REMOTEPATH=/media/fat/MiSTer-debug
EXECUTABLE=MiSTer-debug

# remove old executable on target
ssh root@$TARGET rm $REMOTEPATH
# copy over new executable
scp $EXECUTABLE root@$TARGET:$REMOTEPATH
