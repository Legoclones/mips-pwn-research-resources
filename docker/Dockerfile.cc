FROM ubuntu:24.04

ARG build=mipsel32r6-uClibc

# install dependencies
RUN apt-get update
RUN apt-get upgrade -y
RUN apt-get install -y libmpc3 libmpfr6
RUN rm -rf /var/lib/apt/lists/*

# copy files
COPY ./docker/compiling/${build} /compiling

# add to PATH
ENV PATH="${PATH}:/compiling/bin"

# workdir
RUN mkdir /workdir
WORKDIR /workdir

CMD ["bash"]