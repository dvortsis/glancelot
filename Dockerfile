# syntax=docker/dockerfile:1
FROM ubuntu:24.04 AS qt
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates python3-venv build-essential cmake ninja-build libgl1-mesa-dev libxkbcommon-dev libfontconfig1 libdbus-1-3 libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-shape0 libxcb-xinerama0 libxcb-image0 libxcb-render-util0 libxkbcommon-x11-0 libegl1 && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/aqt && /opt/aqt/bin/pip install --no-cache-dir aqtinstall==3.3.0 && /opt/aqt/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O /opt/Qt -m qtshadertools qtimageformats --archives qtbase qtdeclarative qtshadertools qtimageformats
ENV PATH=/opt/Qt/6.8.3/gcc_64/bin:$PATH CMAKE_PREFIX_PATH=/opt/Qt/6.8.3/gcc_64
RUN /opt/aqt/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O /opt/Qt --archives icu
RUN apt-get update && apt-get install -y --no-install-recommends libglib2.0-0t64 && rm -rf /var/lib/apt/lists/*
FROM qt AS build
RUN apt-get update && apt-get install -y --no-install-recommends xvfb libgl1-mesa-dri && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake cmake
COPY app app
COPY assets assets
COPY include include
COPY src src
COPY ui ui
COPY tests tests
COPY scripts scripts
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON && cmake --build /build --parallel 4
RUN QT_QPA_PLATFORM=offscreen ctest --test-dir /build --output-on-failure

FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends python3 ca-certificates novnc websockify tigervnc-standalone-server tigervnc-tools openbox wmctrl tini fonts-noto-core libgl1-mesa-dri libegl1 libgl1 libxkbcommon-x11-0 libfontconfig1 libdbus-1-3 libxcb-cursor0 libxcb-icccm4 libxcb-keysyms1 libxcb-shape0 libxcb-xinerama0 libxcb-image0 libxcb-render-util0 libasound2t64 && rm -rf /var/lib/apt/lists/* && userdel -r ubuntu && useradd --create-home --uid 1000 viewer && mkdir -p /config /photos && chown viewer:viewer /config
RUN apt-get update && apt-get install -y --no-install-recommends libopengl0 && rm -rf /var/lib/apt/lists/*
COPY --from=qt /opt/Qt/6.8.3/gcc_64 /opt/Qt
COPY --from=build /build/glancelot /build/glancelot-decoder /opt/glancelot/
COPY docker/start.py /opt/glancelot/start.py
COPY LICENSE THIRD_PARTY_NOTICES.md README.md /opt/glancelot/
COPY assets/fonts/geist/OFL.txt /opt/glancelot/Geist-OFL.txt
RUN printf '[Paths]\nPrefix=/opt/Qt\n' > /opt/glancelot/qt.conf && cp /usr/share/novnc/vnc.html /usr/share/novnc/index.html
ENV PATH=/opt/Qt/bin:$PATH LD_LIBRARY_PATH=/opt/Qt/lib DISPLAY=:0 QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 GLANCELOT_CONFIG_DIR=/config HOME=/home/viewer
RUN ! LD_LIBRARY_PATH=/opt/Qt/lib ldd /opt/glancelot/glancelot | grep -q "not found" && ! LD_LIBRARY_PATH=/opt/Qt/lib ldd /opt/glancelot/glancelot-decoder | grep -q "not found" && ! LD_LIBRARY_PATH=/opt/Qt/lib ldd /opt/Qt/plugins/platforms/libqxcb.so | grep -q "not found"
# WebP, TIFF and TGA come from qtimageformats (as on Windows); every image-format plugin must load at runtime.
RUN for format in qwebp qtiff qtga; do test -f /opt/Qt/plugins/imageformats/lib$format.so || { echo "image-format plugin missing: /opt/Qt/plugins/imageformats/lib$format.so" >&2; exit 1; }; done && for plugin in /opt/Qt/plugins/imageformats/*.so; do if LD_LIBRARY_PATH=/opt/Qt/lib ldd "$plugin" | grep "not found"; then echo "image-format plugin has unresolved libraries (listed above): $plugin" >&2; exit 1; fi; done
USER viewer
WORKDIR /photos
EXPOSE 6080
HEALTHCHECK --interval=30s --timeout=3s --start-period=20s CMD python3 -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:6080/vnc.html', timeout=2).read(1)"
ENTRYPOINT ["/usr/bin/tini", "--", "python3", "/opt/glancelot/start.py"]
