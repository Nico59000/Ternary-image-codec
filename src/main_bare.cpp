// ============================================================================
//  File: src/main_bare.cpp
//  Minimal self-test runner for core codec.
// ============================================================================
#include <cstdio>
#include "ternary_image_codec_v6_min.hpp"

int main(){ bool ok1=selftest_rs_unit(); bool ok2=selftest_api_roundtrip(); std::printf("RS:%s API:%s\n", ok1?"OK":"FAIL", ok2?"OK":"FAIL"); return (ok1&&ok2)?0:1; }
