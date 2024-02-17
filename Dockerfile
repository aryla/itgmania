FROM ubuntu:latest

WORKDIR /root

RUN DEBIAN_FRONTEND=noninteractive \
	apt-get update \
	&& apt-get install -y \
	g++ gcc git make nasm cmake cmake-curses-gui \
	libgtk-3-dev libasound2-dev libbz2-dev libgl1-mesa-dev libglu1-mesa-dev libjack-dev libpulse-dev libssl-dev libudev-dev libva-dev libxinerama-dev libxrandr-dev libxtst-dev libusb-dev \
	&& apt-get clean \
	&& rm -rf /var/lib/apt/lists/*

RUN mkdir /root/code

VOLUME /root/code

CMD ["/bin/bash"]
