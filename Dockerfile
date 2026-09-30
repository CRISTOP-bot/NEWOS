FROM debian:trixie-slim

# Toolchain del host de build. El codigo de newos NO se copia en la imagen:
# entra por bind-mount en /src, asi la imagen se reusa entre builds.
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates git curl unzip bzip2 \
      gcc g++ make binutils nasm python3 \
      qemu-system-x86 \
      grub-common grub-pc-bin grub-efi-amd64-bin xorriso mtools \
      libgmp-dev libmpfr-dev libmpc-dev flex bison \
    && rm -rf /var/lib/apt/lists/*

# scripts/fetch-limine.sh necesita red la primera vez (limine-binary.zip) y
# scripts/fetch-font.sh descarga la fuente 8x8; ambos cachean bajo build/.
RUN git config --system --add safe.directory '*'

WORKDIR /src
ENTRYPOINT ["make"]
CMD ["all"]
