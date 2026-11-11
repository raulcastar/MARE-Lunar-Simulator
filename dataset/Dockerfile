# The dataset generator in a container (Linux): Python, Terrain Studio's
# dependencies and the Docker CLI, which drives the GNC containers through the
# host's Docker socket. run_docker.sh builds and runs it.
#   docker build -t mare/dataset:dev --build-context studio=<Terrain Studio repo> dataset
FROM docker:29-cli AS docker-cli

FROM python:3.14-slim
COPY --from=docker-cli /usr/local/bin/docker /usr/local/bin/docker
COPY --from=studio requirements.txt /tmp/req/studio.txt
COPY requirements.txt /tmp/req/dataset.txt
RUN pip install --no-cache-dir --disable-pip-version-check -r /tmp/req/studio.txt -r /tmp/req/dataset.txt \
 && rm -rf /tmp/req
ENV PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1 HOME=/tmp MPLBACKEND=Agg
