FROM          ubuntu:16.04
MAINTAINER    Johan Lorenzo <jlorenzo+tc@mozilla.com>

RUN mkdir /builds
RUN useradd -d /builds/worker -s /bin/bash -m worker

RUN apt-get update
RUN apt-get install --yes git python3-setuptools build-essential libssl-dev libffi-dev python3-dev

RUN git clone https://github.com/mozilla-releng/mozapkpublisher
WORKDIR /builds/worker/mozapkpublisher
RUN python3 setup.py develop

RUN chown -R worker:worker /builds/worker

CMD ["python3", "./mozapkpublisher/get_l10n_strings.py", "--package-name", "${PACKAGE_NAME}", "--output-file", "${GOOGLE_PLAY_STRING_FILE}"]
