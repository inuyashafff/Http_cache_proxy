FROM ubuntu:16.04
RUN mkdir /code
WORKDIR /code
RUN apt-get update
RUN apt-get -y install libboost-all-dev g++ make
ADD . /code/
RUN make -j -B
RUN chmod +s server
