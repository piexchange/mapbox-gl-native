#!/usr/bin/env bash

CXX11ABI=${CXX11ABI:-$(scripts/check-cxx11abi.sh)}

UNIQUE_RESOURCE_VERSION=dev
PROTOZERO_VERSION=1.3.0
BOOST_VERSION=1.60.0
BOOST_LIBPROGRAM_OPTIONS_VERSION=1.60.0
LIBCURL_VERSION=system
GLFW_VERSION=3.1.2
LIBPNG_VERSION=1.6.20
LIBJPEG_TURBO_VERSION=1.4.2
SQLITE_VERSION=3.9.1
LIBUV_VERSION=1.7.5
ZLIB_VERSION=system
NUNICODE_VERSION=1.6
GEOMETRY_VERSION=0.8.0
GEOJSON_VERSION=0.2.0${CXX11ABI:-}
GEOJSONVT_VERSION=6.1.0
VARIANT_VERSION=1.1.0
RAPIDJSON_VERSION=1.0.2
GTEST_VERSION=1.7.0${CXX11ABI:-}
PIXELMATCH_VERSION=0.9.0
WEBP_VERSION=0.5.0
EARCUT_VERSION=0.11
BENCHMARK_VERSION=1.0.0

function print_opengl_flags {
    CONFIG+="    'opengl_cflags%': $(quote_flags $(pkg-config gl x11 --cflags)),"$LN
    CONFIG+="    'opengl_ldflags%': $(quote_flags $(pkg-config gl x11 --libs)),"$LN
}
