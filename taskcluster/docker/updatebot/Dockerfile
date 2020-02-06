# %ARG DOCKER_IMAGE_PARENT
FROM $DOCKER_IMAGE_PARENT
MAINTAINER Tom Ritter <tom@mozilla.com>

VOLUME /builds/worker/checkouts

COPY	setup.sh 	/setup/setup.sh
RUN 	cd /setup && ./setup.sh

COPY	run.py 	/home/worker/run.py

COPY	hgrc	/etc/mercurial/hgrc.d/updatebot.rc

ENV		HOME          		/home/worker
ENV		SHELL         		/bin/bash
ENV		USER          		worker
ENV		LOGNAME       		worker
ENV		PYTHONUNBUFFERED	1