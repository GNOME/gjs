FROM fedora:latest
MAINTAINER Claudio André (c) 2018 V1.0

LABEL architecture="x86_64"
LABEL version="1.0"
LABEL description="Docker image to run CI for GNOME GJS (JavaScript bindings for GNOME)."

RUN dnf -y --nogpgcheck upgrade && \
    dnf -y install \
                   git cppcheck tokei nodejs python-devel && \
    mkdir -p /cwd && \
    pip install cpplint && \
    npm install -g eslint && \
    dnf -y clean all && \
    rm -rf /var/cache/dnf

CMD ["/bin/bash"]

