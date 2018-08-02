FROM ubuntu:18.04
MAINTAINER release@mozilla.com

RUN apt-get update && apt-get install -qy wget gettext-base curl && \
    apt-get install -y software-properties-common && \
    add-apt-repository -y ppa:alexlarsson/flatpak && apt update && \
    apt install -y flatpak-builder && apt-get clean

RUN flatpak remote-add --from flathub https://dl.flathub.org/repo/flathub.flatpakrepo && \
    apt-get clean

# Set a default command useful for debugging
CMD ["/bin/bash", "--login"]
