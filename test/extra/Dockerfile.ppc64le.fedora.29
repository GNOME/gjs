FROM ppc64le/fedora:rawhide
MAINTAINER Claudio André (c) 2018 V1.0

LABEL architecture="ppc64le"
LABEL version="1.0"
LABEL description="Multiarch docker image to run CI for GNOME GJS."

ADD x86_64_qemu-ppc64le-static.tar.gz /usr/bin

CMD ["/bin/bash"]

