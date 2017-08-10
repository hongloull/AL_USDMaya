#!/usr/bin/env bash
cd ~/workspace/AL_USDMaya
rm -rf build
mkdir build
cd build

# add custom include files
export USD_ROOT=/apps/shared/usd/0.8.1
cp ~/workspace/USD/include/*.h $USD_ROOT/include

/data/share/usd/dependency/cmake-3.6.3/bin/cmake \
      -DBUILD_USDMAYA_SCHEMAS=OFF \
      -DBUILD_USDMAYA_TRANSLATORS=OFF \
      -DCMAKE_INSTALL_PREFIX='/apps/shared/AL_USDMaya/0.0.1' \
      -DCMAKE_MODULE_PATH='/data/share/usd/dependency/cmake-3.6.3' \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DBOOST_ROOT='/data/share/usd/dependency/boost_1_55_0' \
      -DMAYA_LOCATION='/apps/shared/maya/2016/platform-linux' \
      -DOPENEXR_LOCATION='/data/share/usd/dependency/openexr-2.2.0'\
      -DOPENGL_gl_LIBRARY='/data/share/usd/dependency/glfw-3.1.1/lib/libglfw3.a'\
      -DGLEW_LOCATION='/data/share/usd/dependency/glew-1.10.0'\
      -DUSD_CONFIG_FILE='/apps/shared/usd/0.8.1/pxrConfig.cmake'\
      -DGTEST_ROOT='/data/share/usd/dependency/googletest/1.8.0'\
      -DCMAKE_PREFIX_PATH='/home/mjun/workspace/USD/build/third_party/maya/lib'\
      ..

make -j 6 install
