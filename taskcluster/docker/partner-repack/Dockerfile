# %ARG DOCKER_IMAGE_PARENT
FROM $DOCKER_IMAGE_PARENT
MAINTAINER Ben Hearsum <bhearsum@mozilla.com>

VOLUME /builds/worker/checkouts
VOLUME /builds/worker/workspace

RUN dpkg --add-architecture amd64

RUN apt-get update && \
    apt-get install \
      bzip2 \
      curl \
      git \
      gzip \
      openssh-client \
      unzip \
      zip && \
    apt-get clean

# Add wrapper scripts for xvfb allowing tasks to easily retry starting up xvfb
# %include taskcluster/docker/recipes/xvfb.sh
COPY topsrcdir/taskcluster/docker/recipes/xvfb.sh /builds/worker/scripts/xvfb.sh

RUN mkdir /root/.ssh
COPY known_hosts /root/.ssh/known_hosts
