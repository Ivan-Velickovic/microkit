source ./build.sh
mq.sh run -c 'kek' -l log.txt -s zcu102 -f "$BUILD_DIR/build/images/capdl-loader-image-arm-zynqmp"
