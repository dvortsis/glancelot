# Docker browser desktop

Glancelot can run on an x86-64 Docker server and be viewed in a browser. This is the native Qt app streamed through TigerVNC/noVNC, not a separate web photo library. All clients share one desktop, cursor and viewer session. Photo paths refer to files on the server; only mounted host directories are available. Browser file upload/drop is not implemented.

## Start

From a checkout on a Linux Docker host (including an Unraid terminal), set up a password file outside the checkout or in the ignored `.secrets/` directory:

```sh
mkdir -p .secrets
chmod 700 .secrets
# VNC uses eight password bytes; enter exactly eight ASCII non-space characters.
read -r -s -p 'Desktop password (8 characters): ' password; printf '\n'
printf '%s' "$password" > .secrets/vnc-password
unset password
chmod 600 .secrets/vnc-password
# The container runs as UID 1000. Grant that UID read access to the secret.
# When running these commands as root (e.g. Unraid):
# chown 1000:1000 .secrets/vnc-password
# Otherwise use sudo chown or grant UID 1000 read access with your host ACL tools.
export GLANCELOT_PASSWORD_FILE="$PWD/.secrets/vnc-password"
export GLANCELOT_PHOTOS=/mnt/user/photos
# Default is localhost only. Use the server's LAN address to allow LAN clients.
export GLANCELOT_BIND_ADDRESS=192.168.1.10
export GLANCELOT_PORT=6080
docker compose up -d --build
```

Use your actual photo directory and server address. Open `http://SERVER:6080/`, click Connect, and enter the password. The initial image installs prebuilt Qt 6.8.3, builds Glancelot and runs all CTest targets; allow time and disk space for the build. The supplied image targets Linux amd64; ARM builds are not qualified.

Photos are mounted at `/photos` **read-only**. Settings and per-window locations live in the named `glancelot-config` volume. The process runs as UID/GID 1000; photo directories must be readable/searchable by that account. The secret must also be readable by it. Do not make the config volume read-only. Add additional read-only bind mounts to Compose if needed. Linux mount points appear in the folder browser. Because `/photos` is read-only, delete, rename and moving files into it are refused; copy only into a writable mount you add yourself.

Formats: JPEG, PNG, BMP, GIF (including animation) and embedded RAW previews decode in the container. WebP, TIFF and TGA do not yet: the image installs Qt without its `qtimageformats` module, which packet EN-02 adds. The native Windows build has them. See [current capabilities](SUPPORTED-CAPABILITIES.md#image-formats).

Click **Folders** (or Ctrl+B), expand `/`, then `/photos`. You can browse before opening an image. Disclosure arrows only expand folders; clicking a drive or `/` only expands it (so a click never roots navigation at a whole volume); clicking any other folder name opens the first supported image found in that folder or its descendants, with the existing bounded search. Empty/unreadable folders leave the current photo unchanged. **All locations** refreshes filesystem roots; **Parent** widens the photo traversal root; **Pin** keeps the panel visible. Mapped Windows drives appear as separate locations in the native app; an explicitly opened UNC location is retained, but network-share discovery is not provided.

`GLANCELOT_DESKTOP_SIZE=1440x900` controls the fixed remote desktop (560x400 through 3840x2160). noVNC's scaling controls fit it to your browser; resizing the browser does not change the server desktop. Rendering defaults to Mesa software OpenGL for hosts without a GPU. This route does not qualify physical-pixel zoom, ICC/HDR accuracy, or high-refresh presentation, and the single virtual display has no second-monitor output. Use the native app for display-sensitive inspection.

## Operations

```sh
docker compose logs --tail 100
docker compose down          # retain settings volume
docker compose up -d         # reopen desktop and restore last successful photo
```

The supervisor closes the Qt window through the window manager before stopping its display, allowing normal preference flush and slot release. Closing Glancelot itself ends the container; the Compose restart policy brings it back. Stopping Compose does not erase the settings volume or alter photos.

Only the browser port is published; raw VNC listens inside the container on loopback. Classic VNC password authentication does not encrypt the browser connection: use a trusted LAN/VPN, or put it behind an authenticated HTTPS reverse proxy with WebSocket support. Do not expose plain HTTP directly to the Internet. No cloud service, telemetry or automatic image updates are involved.

## Build and verification

`docker build -t glancelot-browser .` builds the app with the pinned Qt toolkit and runs all tests before creating the runtime image. The runtime contains its own Qt libraries/plugins and the decoder beside the viewer. Use `docker compose config` to validate local environment settings before starting. Native Windows builds remain documented in [BUILDING.md](../BUILDING.md).
