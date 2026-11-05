ARG BASE=debian:trixie-slim@sha256:7378260c0a098d27ac206225db5e7d92b8c7c84ea7afccc86eeb513756bbd392
FROM ${BASE} AS workload
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    bash ca-certificates curl git build-essential cmake ninja-build \
    python3 python3-venv python3-pip nodejs npm procps iproute2 \
    && rm -rf /var/lib/apt/lists/*
ARG CODEX_VERSION=0.159.2
RUN npm install --global @openai/codex@${CODEX_VERSION} && npm cache clean --force
RUN mkdir -p /workspace /root/.codex /aicage /run/aicage \
    && printf 'sandbox_mode = "danger-full-access"\napproval_policy = "never"\ncli_auth_credentials_store = "file"\n' > /root/.codex/config.toml \
    && chmod 700 /root/.codex
COPY build-linux/cage-broker /aicage/broker
COPY build-linux/cage-probe /usr/local/bin/cage-probe
WORKDIR /workspace
ENTRYPOINT ["/aicage/broker", "serve"]

FROM ${BASE} AS proxy
RUN apt-get update && apt-get install -y --no-install-recommends squid ca-certificates \
    && rm -rf /var/lib/apt/lists/*
USER proxy
ENTRYPOINT ["/usr/sbin/squid", "-N", "-d", "1", "-f", "/etc/aicage/squid.conf"]
