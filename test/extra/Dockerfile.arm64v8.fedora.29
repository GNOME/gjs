FROM arm64v8/fedora:rawhide
MAINTAINER Claudio André (c) 2018 V1.0

LABEL architecture="aarch64"
LABEL version="1.0"
LABEL description="Multiarch docker image to run CI for GNOME GJS."

ADD x86_64_qemu-aarch64-static.tar.gz /usr/bin

CMD ["/bin/bash"]

