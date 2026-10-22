FROM glancelot-fs-lease-qt
COPY . /probe
RUN cmake -S /probe -B /probe-build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build /probe-build --parallel 2
CMD ["/probe-build/lease_probe"]
