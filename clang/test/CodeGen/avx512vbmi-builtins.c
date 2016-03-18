// RUN: %clang_cc1 %s -triple=x86_64-apple-darwin -target-feature +avx512vbmi -emit-llvm -o - -Werror | FileCheck %s

// Don't include mm_malloc.h, it's system specific.
#define __MM_MALLOC_H

#include <immintrin.h>

__m512i test_mm512_mask2_permutex2var_epi8(__m512i __A, __m512i __I, __mmask64 __U, __m512i __B) {
  // CHECK-LABEL: @test_mm512_mask2_permutex2var_epi8
  // CHECK: @llvm.x86.avx512.mask.vpermi2var.qi.512
  return _mm512_mask2_permutex2var_epi8(__A, __I, __U, __B); 
}

__m512i test_mm512_permutex2var_epi8(__m512i __A, __m512i __I, __m512i __B) {
  // CHECK-LABEL: @test_mm512_permutex2var_epi8
  // CHECK: @llvm.x86.avx512.mask.vpermt2var.qi.512
  return _mm512_permutex2var_epi8(__A, __I, __B); 
}

__m512i test_mm512_mask_permutex2var_epi8(__m512i __A, __mmask64 __U, __m512i __I, __m512i __B) {
  // CHECK-LABEL: @test_mm512_mask_permutex2var_epi8
  // CHECK: @llvm.x86.avx512.mask.vpermt2var.qi.512
  return _mm512_mask_permutex2var_epi8(__A, __U, __I, __B); 
}

__m512i test_mm512_maskz_permutex2var_epi8(__mmask64 __U, __m512i __A, __m512i __I, __m512i __B) {
  // CHECK-LABEL: @test_mm512_maskz_permutex2var_epi8
  // CHECK: @llvm.x86.avx512.mask.vpermt2var.qi.512
  return _mm512_maskz_permutex2var_epi8(__U, __A, __I, __B); 
}

