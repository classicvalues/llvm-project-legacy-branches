; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt -S %s -scalarize-masked-mem-intrin -mtriple=x86_64-linux-gnu | FileCheck %s

define <2 x i64> @scalarize_v2i64(<2 x i64>* %p, <2 x i1> %mask, <2 x i64> %passthru) {
; CHECK-LABEL: @scalarize_v2i64(
; CHECK-NEXT:    [[TMP1:%.*]] = bitcast <2 x i64>* [[P:%.*]] to i64*
; CHECK-NEXT:    [[TMP2:%.*]] = extractelement <2 x i1> [[MASK:%.*]], i32 0
; CHECK-NEXT:    br i1 [[TMP2]], label [[COND_LOAD:%.*]], label [[ELSE:%.*]]
; CHECK:       cond.load:
; CHECK-NEXT:    [[TMP3:%.*]] = getelementptr inbounds i64, i64* [[TMP1]], i32 0
; CHECK-NEXT:    [[TMP4:%.*]] = load i64, i64* [[TMP3]], align 8
; CHECK-NEXT:    [[TMP5:%.*]] = insertelement <2 x i64> [[PASSTHRU:%.*]], i64 [[TMP4]], i32 0
; CHECK-NEXT:    br label [[ELSE]]
; CHECK:       else:
; CHECK-NEXT:    [[RES_PHI_ELSE:%.*]] = phi <2 x i64> [ [[TMP5]], [[COND_LOAD]] ], [ [[PASSTHRU]], [[TMP0:%.*]] ]
; CHECK-NEXT:    [[TMP6:%.*]] = extractelement <2 x i1> [[MASK]], i32 1
; CHECK-NEXT:    br i1 [[TMP6]], label [[COND_LOAD1:%.*]], label [[ELSE2:%.*]]
; CHECK:       cond.load1:
; CHECK-NEXT:    [[TMP7:%.*]] = getelementptr inbounds i64, i64* [[TMP1]], i32 1
; CHECK-NEXT:    [[TMP8:%.*]] = load i64, i64* [[TMP7]], align 8
; CHECK-NEXT:    [[TMP9:%.*]] = insertelement <2 x i64> [[RES_PHI_ELSE]], i64 [[TMP8]], i32 1
; CHECK-NEXT:    br label [[ELSE2]]
; CHECK:       else2:
; CHECK-NEXT:    [[RES_PHI_ELSE3:%.*]] = phi <2 x i64> [ [[TMP9]], [[COND_LOAD1]] ], [ [[RES_PHI_ELSE]], [[ELSE]] ]
; CHECK-NEXT:    ret <2 x i64> [[RES_PHI_ELSE3]]
;
  %ret = call <2 x i64> @llvm.masked.load.v2i64.p0v2i64(<2 x i64>* %p, i32 128, <2 x i1> %mask, <2 x i64> %passthru)
  ret <2 x i64> %ret
}

define <2 x i64> @scalarize_v2i64_ones_mask(<2 x i64>* %p, <2 x i64> %passthru) {
; CHECK-LABEL: @scalarize_v2i64_ones_mask(
; CHECK-NEXT:    [[TMP1:%.*]] = load <2 x i64>, <2 x i64>* [[P:%.*]], align 8
; CHECK-NEXT:    ret <2 x i64> [[TMP1]]
;
  %ret = call <2 x i64> @llvm.masked.load.v2i64.p0v2i64(<2 x i64>* %p, i32 8, <2 x i1> <i1 true, i1 true>, <2 x i64> %passthru)
  ret <2 x i64> %ret
}

define <2 x i64> @scalarize_v2i64_zero_mask(<2 x i64>* %p, <2 x i64> %passthru) {
; CHECK-LABEL: @scalarize_v2i64_zero_mask(
; CHECK-NEXT:    [[TMP1:%.*]] = bitcast <2 x i64>* [[P:%.*]] to i64*
; CHECK-NEXT:    ret <2 x i64> [[PASSTHRU:%.*]]
;
  %ret = call <2 x i64> @llvm.masked.load.v2i64.p0v2i64(<2 x i64>* %p, i32 8, <2 x i1> <i1 false, i1 false>, <2 x i64> %passthru)
  ret <2 x i64> %ret
}

define <2 x i64> @scalarize_v2i64_const_mask(<2 x i64>* %p, <2 x i64> %passthru) {
; CHECK-LABEL: @scalarize_v2i64_const_mask(
; CHECK-NEXT:    [[TMP1:%.*]] = bitcast <2 x i64>* [[P:%.*]] to i64*
; CHECK-NEXT:    [[TMP2:%.*]] = getelementptr inbounds i64, i64* [[TMP1]], i32 1
; CHECK-NEXT:    [[TMP3:%.*]] = load i64, i64* [[TMP2]], align 8
; CHECK-NEXT:    [[TMP4:%.*]] = insertelement <2 x i64> [[PASSTHRU:%.*]], i64 [[TMP3]], i32 1
; CHECK-NEXT:    ret <2 x i64> [[TMP4]]
;
  %ret = call <2 x i64> @llvm.masked.load.v2i64.p0v2i64(<2 x i64>* %p, i32 8, <2 x i1> <i1 false, i1 true>, <2 x i64> %passthru)
  ret <2 x i64> %ret
}

declare <2 x i64> @llvm.masked.load.v2i64.p0v2i64(<2 x i64>*, i32, <2 x i1>, <2 x i64>)