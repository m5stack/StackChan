FROM alpine:latest
ENV WORKDIR=/app
WORKDIR $WORKDIR
COPY ./stackChan $WORKDIR/stackChan
COPY ./config.yaml $WORKDIR/config.yaml
RUN chmod +x $WORKDIR/stackChan
CMD ["./stackChan"]