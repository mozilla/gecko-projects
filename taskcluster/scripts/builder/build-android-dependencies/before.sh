#!/bin/bash -vex

set -x -e

echo "running as" $(id)

: WORKSPACE ${WORKSPACE:=/home/worker/workspace}

set -v

# Populate /home/worker/workspace/build/src/java_home.
. $WORKSPACE/build/src/taskcluster/scripts/builder/build-android-dependencies/repackage-jdk-centos.sh

mv $WORKSPACE/java/usr/lib/jvm/java_home $WORKSPACE/build/src/java_home

export JAVA_HOME=$WORKSPACE/build/src/java_home
export PATH=$PATH:$JAVA_HOME/bin

# Populate /home/worker/.mozbuild/android-sdk-linux.
python2.7 /home/worker/workspace/build/src/python/mozboot/mozboot/android.py --artifact-mode --no-interactive

RUN_AS_USER=worker NEXUS_WORK=$WORKSPACE/nexus /opt/sonatype/nexus/bin/nexus restart

# Wait "a while" for Nexus to actually start.  Don't fail if this fails.
wget --quiet --retry-connrefused --waitretry=2 --tries=100 \
  http://localhost:8081/nexus/service/local/status || true
rm -rf status

# It's helpful when debugging to see the "latest state".
curl http://localhost:8081/nexus/service/local/status || true

# Verify Nexus has actually started.  Fail if this fails.
curl --fail --silent --location http://localhost:8081/nexus/service/local/status | grep '<state>STARTED</state>'
