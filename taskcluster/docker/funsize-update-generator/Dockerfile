FROM ubuntu:vivid
MAINTAINER Rail Aliiev <rail@mozilla.com>

VOLUME /home/worker/workspace
VOLUME /home/worker/tooltool-cache


# %include python/mozbuild/mozbuild/action/tooltool.py
ADD topsrcdir/python/mozbuild/mozbuild/action/tooltool.py /builds/tooltool.py
ADD topsrcdir/python/mozbuild/mozbuild/action/tooltool.py /setup/tooltool.py

# %include testing/mozharness/external_tools/robustcheckout.py
ADD topsrcdir/testing/mozharness/external_tools/robustcheckout.py /usr/local/mercurial/robustcheckout.py


# Required software
ENV DEBIAN_FRONTEND noninteractive
# Chain apt-get commands with apt-get clean in a single docker RUN
# to make sure that files are removed within a single docker layer
RUN apt-get update -q && \
    apt-get install -yyq --no-install-recommends \
    python python-setuptools python-cryptography  libgetopt-simple-perl \
    bzip2 clamav clamav-freshclam python-requests python-sh curl \
    python-dev gcc liblzma-dev xz-utils && \
    apt-get clean
RUN useradd -d /home/worker -s /bin/bash -m worker
COPY requirements.txt /tmp/

# Freshclam may be flaky, retry if it fails
RUN for i in 1 2 3 4 5; do freshclam --verbose && break || sleep 15; done

# python-pip installs a lot of dependencies increasing the size of an image
# drastically. Using easy_install saves us almost 200M.
RUN easy_install pip
RUN pip install -r /tmp/requirements.txt

# scripts
RUN mkdir /home/worker/bin
COPY scripts/* /home/worker/bin/

# %include taskcluster/docker/recipes/run-task
ADD topsrcdir/taskcluster/docker/recipes/run-task /home/worker/bin/run-task


# Stubbed out credentials; mozharness looks for this file an issues a WARNING
# if it's not found, which causes the build to fail.  Note that this needs to
# be in the parent of the workspace directory and in the directory where
# mozharness is run (not its --work-dir).  See Bug 1169652.
ADD           oauth.txt /home/worker/

# stubbed out buildprops, which keeps mozharness from choking
# Note that this needs to be in the parent of the workspace directory and in
# the directory where mozharness is run (not its --work-dir)
ADD           buildprops.json /home/worker/


COPY runme.sh /runme.sh
RUN chmod 755 /home/worker/bin/* /runme.sh
RUN mkdir /home/worker/keys
COPY *.pubkey /home/worker/keys/

ENV           HOME          /home/worker
ENV           SHELL         /bin/bash
ENV           USER          worker
ENV           LOGNAME       worker
