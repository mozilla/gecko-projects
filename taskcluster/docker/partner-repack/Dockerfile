# %ARG DOCKER_IMAGE_PARENT
FROM $DOCKER_IMAGE_PARENT
MAINTAINER Ben Hearsum <bhearsum@mozilla.com>

VOLUME /builds/worker/checkouts
VOLUME /builds/worker/workspace

RUN dpkg --add-architecture amd64

RUN apt-get update && \
    apt-get install \
      bzip2 \
      git \
      gzip \
      openssh-client \
      unzip \
      zip && \
    apt-get clean

RUN mkdir /root/.ssh
COPY known_hosts /root/.ssh/known_hosts
