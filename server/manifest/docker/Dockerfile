# SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
# SPDX-License-Identifier: MIT

FROM alpine:3.20

ENV WORKDIR=/app \
    TZ=Asia/Shanghai

WORKDIR ${WORKDIR}

RUN apk add --no-cache ca-certificates tzdata \
    && mkdir -p ${WORKDIR}/file ${WORKDIR}/logs ${WORKDIR}/tmp ${WORKDIR}/data

COPY ./stackChan ${WORKDIR}/stackChan
COPY ./config.yaml ${WORKDIR}/config.yaml
COPY ./web ${WORKDIR}/web

RUN chmod +x ${WORKDIR}/stackChan

EXPOSE 12800
STOPSIGNAL SIGTERM

CMD ["./stackChan"]
