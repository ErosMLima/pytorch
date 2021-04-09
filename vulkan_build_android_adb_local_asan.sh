rm -rf build_android
CXX_FLAGS="-pthread" ANDROID_ABI=arm64-v8a BUILD_MOBILE_TEST=0 \
  ./scripts/build_android.sh \
  -DANDROID_CCACHE=/usr/local/bin/ccache \
  -DANDROID_DEBUG_SYMBOLS=ON \
  -DBUILD_BINARY=ON \
  -DUSE_ASAN=ON \
  -DUSE_VULKAN=ON \
  -DUSE_VULKAN_API=ON \
  -DUSE_VULKAN_SHADERC_RUNTIME=OFF \
  -DUSE_VULKAN_WRAPPER=ON 

#adb push ./build_android/bin/speed_benchmark_torch /data/local/tmp
