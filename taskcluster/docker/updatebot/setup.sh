#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

set -vex

export DEBIAN_FRONTEND=noninteractive

# Update apt-get lists
apt-get update -y

# Install dependencies
apt-get install -y --no-install-recommends \
    arcanist \
    ca-certificates \
    curl \
    python-requests \
    python-requests-unixsocket \
    python3.5 \
    python3-minimal \
    python3-requests \
    python3-requests-unixsocket \
    python3-pymysql \
    fzf \
    openssh-client

mkdir -p /home/worker/.mozbuild
chown -R worker:worker /home/worker/

rm -rf /setup