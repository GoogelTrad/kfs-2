#!/bin/bash
set -e

IMAGE=kfs-builder

# --- Faut-il (re)construire l'image ?
needs_build() {
    # 1. L'image n'existe pas encore
    if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
        echo "Image '$IMAGE' absente."
        return 0
    fi

    # 2. Le Dockerfile a été modifié après la création de l'image
    image_created=$(docker image inspect -f '{{.Created}}' "$IMAGE")
    image_epoch=$(date -d "$image_created" +%s)
    dockerfile_epoch=$(stat -c %Y Dockerfile)
    if [ "$dockerfile_epoch" -gt "$image_epoch" ]; then
        echo "Dockerfile modifié depuis le dernier build."
        return 0
    fi

    return 1
}

if needs_build; then
    echo ">>> Build de l'image $IMAGE"
    docker build -t "$IMAGE" .
else
    echo ">>> Image $IMAGE déjà à jour, build ignoré."
fi

# --- Build du kernel
docker run --rm -it -v "$(pwd)":/kfs "$IMAGE" make restart

# --- Vérification du résultat
if [ -f mykernel.iso ]; then
    echo ">>> mykernel.iso généré avec succès."
    qemu-system-i386 -cdrom mykernel.iso -display default,show-cursor=on
else
    echo "!!! mykernel.iso introuvable, le build a échoué."
    exit 1
fi
