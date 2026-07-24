; ModuleID = '../../../tests/validate/precompile_01.c'
source_filename = "../../../tests/validate/precompile_01.c"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx13.0.0"

@.str = private unnamed_addr constant [5 x i8] c"data\00", align 1, !dbg !0
@.str.1 = private unnamed_addr constant [12 x i8] c"{:name/%s}\0A\00", align 1, !dbg !7
@.str.2 = private unnamed_addr constant [6 x i8] c"alice\00", align 1, !dbg !12
@.str.3 = private unnamed_addr constant [13 x i8] c"{:count/%d}\0A\00", align 1, !dbg !17
@.str.4 = private unnamed_addr constant [24 x i8] c"{:first/%s} {:last/%s}\0A\00", align 1, !dbg !22
@.str.5 = private unnamed_addr constant [5 x i8] c"john\00", align 1, !dbg !27
@.str.6 = private unnamed_addr constant [4 x i8] c"doe\00", align 1, !dbg !29
@.str.7 = private unnamed_addr constant [22 x i8] c"{e:id/%d}{:label/%s}\0A\00", align 1, !dbg !34
@.str.8 = private unnamed_addr constant [5 x i8] c"item\00", align 1, !dbg !39
@.str.9 = private unnamed_addr constant [24 x i8] c"{[:/%d}{:value/%s}{]:}\0A\00", align 1, !dbg !41
@.str.10 = private unnamed_addr constant [7 x i8] c"padded\00", align 1, !dbg !43
@.xo_fields._________tests_validate_precompile_01_c.0 = private unnamed_addr constant [2 x { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 }] [{ i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 86, i16 1, i16 2, i16 7, i16 -1, i16 10, i16 8, i16 4, i16 2, i16 0, i32 0, i32 0 }, { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 10, i16 10, i16 -1, i16 -1, i16 -1, i16 -1, i16 1, i16 0, i16 0, i16 0, i32 0, i32 0 }]
@.xo_fcache._________tests_validate_precompile_01_c.0 = private unnamed_addr constant { i32, i32, ptr } { i32 1, i32 2, ptr @.xo_fields._________tests_validate_precompile_01_c.0 }
@.xo_fields._________tests_validate_precompile_01_c.1 = private unnamed_addr constant [2 x { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 }] [{ i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 86, i16 1, i16 2, i16 8, i16 -1, i16 11, i16 9, i16 5, i16 2, i16 0, i32 0, i32 0 }, { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 10, i16 11, i16 -1, i16 -1, i16 -1, i16 -1, i16 1, i16 0, i16 0, i16 0, i32 0, i32 0 }]
@.xo_fcache._________tests_validate_precompile_01_c.1 = private unnamed_addr constant { i32, i32, ptr } { i32 1, i32 2, ptr @.xo_fields._________tests_validate_precompile_01_c.1 }
@.xo_fields._________tests_validate_precompile_01_c.2 = private unnamed_addr constant [4 x { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 }] [{ i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 86, i16 1, i16 2, i16 8, i16 -1, i16 11, i16 9, i16 5, i16 2, i16 0, i32 0, i32 0 }, { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 43, i16 11, i16 11, i16 -1, i16 -1, i16 12, i16 0, i16 1, i16 0, i16 0, i32 0, i32 0 }, { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 86, i16 13, i16 14, i16 19, i16 -1, i16 22, i16 8, i16 4, i16 2, i16 0, i32 0, i32 0 }, { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 10, i16 22, i16 -1, i16 -1, i16 -1, i16 -1, i16 1, i16 0, i16 0, i16 0, i32 0, i32 0 }]
@.xo_fcache._________tests_validate_precompile_01_c.2 = private unnamed_addr constant { i32, i32, ptr } { i32 1, i32 4, ptr @.xo_fields._________tests_validate_precompile_01_c.2 }
@.xo_fields._________tests_validate_precompile_01_c.3 = private unnamed_addr constant [3 x { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 }] [{ i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 8, i32 86, i16 1, i16 3, i16 6, i16 -1, i16 9, i16 7, i16 2, i16 2, i16 0, i32 0, i32 0 }, { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 86, i16 10, i16 11, i16 17, i16 -1, i16 20, i16 9, i16 5, i16 2, i16 0, i32 0, i32 0 }, { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 10, i16 20, i16 -1, i16 -1, i16 -1, i16 -1, i16 1, i16 0, i16 0, i16 0, i32 0, i32 0 }]
@.xo_fcache._________tests_validate_precompile_01_c.3 = private unnamed_addr constant { i32, i32, ptr } { i32 1, i32 3, ptr @.xo_fields._________tests_validate_precompile_01_c.3 }
@.xo_fields._________tests_validate_precompile_01_c.4 = private unnamed_addr constant [4 x { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 }] [{ i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 91, i16 1, i16 -1, i16 4, i16 -1, i16 7, i16 5, i16 0, i16 2, i16 0, i32 0, i32 0 }, { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 86, i16 8, i16 9, i16 15, i16 -1, i16 18, i16 9, i16 5, i16 2, i16 0, i32 0, i32 0 }, { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 93, i16 19, i16 -1, i16 -1, i16 -1, i16 22, i16 2, i16 0, i16 0, i16 0, i32 0, i32 0 }, { i64, i32, i16, i16, i16, i16, i16, i16, i16, i16, i16, i32, i32 } { i64 0, i32 10, i16 22, i16 -1, i16 -1, i16 -1, i16 -1, i16 1, i16 0, i16 0, i16 0, i32 0, i32 0 }]
@.xo_fcache._________tests_validate_precompile_01_c.4 = private unnamed_addr constant { i32, i32, ptr } { i32 1, i32 4, ptr @.xo_fields._________tests_validate_precompile_01_c.4 }

; Function Attrs: nounwind ssp uwtable(sync)
define range(i32 0, 2) i32 @main(i32 noundef %0, ptr noundef %1) local_unnamed_addr #0 !dbg !58 {
    #dbg_value(i32 %0, !65, !DIExpression(), !67)
    #dbg_value(ptr %1, !66, !DIExpression(), !67)
  %3 = tail call i32 @xo_parse_args(i32 noundef %0, ptr noundef %1) #2, !dbg !68
    #dbg_value(i32 %3, !65, !DIExpression(), !67)
  %4 = icmp slt i32 %3, 0, !dbg !69
  br i1 %4, label %14, label %5, !dbg !71

5:                                                ; preds = %2
  %6 = tail call i64 @xo_open_container(ptr noundef nonnull @.str) #2, !dbg !72
  %7 = tail call i64 (ptr, ptr, ...) @xo_emit_cached(ptr nonnull @.xo_fcache._________tests_validate_precompile_01_c.0, ptr nonnull @.str.1, ptr nonnull @.str.2) #2, !dbg !73
  %8 = tail call i64 (ptr, ptr, ...) @xo_emit_cached(ptr nonnull @.xo_fcache._________tests_validate_precompile_01_c.1, ptr nonnull @.str.3, i32 42) #2, !dbg !78
  %9 = tail call i64 (ptr, ptr, ...) @xo_emit_cached(ptr nonnull @.xo_fcache._________tests_validate_precompile_01_c.2, ptr nonnull @.str.4, ptr nonnull @.str.5, ptr nonnull @.str.6) #2, !dbg !79
  %10 = tail call i64 (ptr, ptr, ...) @xo_emit_cached(ptr nonnull @.xo_fcache._________tests_validate_precompile_01_c.3, ptr nonnull @.str.7, i32 7, ptr nonnull @.str.8) #2, !dbg !80
  %11 = tail call i64 (ptr, ptr, ...) @xo_emit_cached(ptr nonnull @.xo_fcache._________tests_validate_precompile_01_c.4, ptr nonnull @.str.9, i32 20, ptr nonnull @.str.10) #2, !dbg !81
  %12 = tail call i64 @xo_close_container(ptr noundef nonnull @.str) #2, !dbg !82
  %13 = tail call i64 @xo_finish() #2, !dbg !83
  br label %14, !dbg !84

14:                                               ; preds = %2, %5
  %15 = phi i32 [ 0, %5 ], [ 1, %2 ], !dbg !67
  ret i32 %15, !dbg !85
}

declare !dbg !86 i32 @xo_parse_args(i32 noundef, ptr noundef) local_unnamed_addr #1

declare !dbg !88 i64 @xo_open_container(ptr noundef) local_unnamed_addr #1

declare !dbg !99 i64 @xo_close_container(ptr noundef) local_unnamed_addr #1

declare !dbg !100 i64 @xo_finish() local_unnamed_addr #1

declare i64 @xo_emit_cached(ptr, ptr, ...) local_unnamed_addr

attributes #0 = { nounwind ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a,+zcm,+zcz" }
attributes #1 = { "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a,+zcm,+zcz" }
attributes #2 = { nounwind }

!llvm.module.flags = !{!48, !49, !50, !51, !52, !53, !54}
!llvm.dbg.cu = !{!55}
!llvm.ident = !{!57}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(scope: null, file: !2, line: 41, type: !3, isLocal: true, isDefinition: true)
!2 = !DIFile(filename: "../../../tests/validate/precompile_01.c", directory: "/Volumes/case/work/new/libxo/build/tests/validate")
!3 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 40, elements: !5)
!4 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
!5 = !{!6}
!6 = !DISubrange(count: 5)
!7 = !DIGlobalVariableExpression(var: !8, expr: !DIExpression())
!8 = distinct !DIGlobalVariable(scope: null, file: !2, line: 19, type: !9, isLocal: true, isDefinition: true)
!9 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 96, elements: !10)
!10 = !{!11}
!11 = !DISubrange(count: 12)
!12 = !DIGlobalVariableExpression(var: !13, expr: !DIExpression())
!13 = distinct !DIGlobalVariable(scope: null, file: !2, line: 19, type: !14, isLocal: true, isDefinition: true)
!14 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 48, elements: !15)
!15 = !{!16}
!16 = !DISubrange(count: 6)
!17 = !DIGlobalVariableExpression(var: !18, expr: !DIExpression())
!18 = distinct !DIGlobalVariable(scope: null, file: !2, line: 22, type: !19, isLocal: true, isDefinition: true)
!19 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 104, elements: !20)
!20 = !{!21}
!21 = !DISubrange(count: 13)
!22 = !DIGlobalVariableExpression(var: !23, expr: !DIExpression())
!23 = distinct !DIGlobalVariable(scope: null, file: !2, line: 25, type: !24, isLocal: true, isDefinition: true)
!24 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 192, elements: !25)
!25 = !{!26}
!26 = !DISubrange(count: 24)
!27 = !DIGlobalVariableExpression(var: !28, expr: !DIExpression())
!28 = distinct !DIGlobalVariable(scope: null, file: !2, line: 25, type: !3, isLocal: true, isDefinition: true)
!29 = !DIGlobalVariableExpression(var: !30, expr: !DIExpression())
!30 = distinct !DIGlobalVariable(scope: null, file: !2, line: 25, type: !31, isLocal: true, isDefinition: true)
!31 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 32, elements: !32)
!32 = !{!33}
!33 = !DISubrange(count: 4)
!34 = !DIGlobalVariableExpression(var: !35, expr: !DIExpression())
!35 = distinct !DIGlobalVariable(scope: null, file: !2, line: 28, type: !36, isLocal: true, isDefinition: true)
!36 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 176, elements: !37)
!37 = !{!38}
!38 = !DISubrange(count: 22)
!39 = !DIGlobalVariableExpression(var: !40, expr: !DIExpression())
!40 = distinct !DIGlobalVariable(scope: null, file: !2, line: 28, type: !3, isLocal: true, isDefinition: true)
!41 = !DIGlobalVariableExpression(var: !42, expr: !DIExpression())
!42 = distinct !DIGlobalVariable(scope: null, file: !2, line: 31, type: !24, isLocal: true, isDefinition: true)
!43 = !DIGlobalVariableExpression(var: !44, expr: !DIExpression())
!44 = distinct !DIGlobalVariable(scope: null, file: !2, line: 31, type: !45, isLocal: true, isDefinition: true)
!45 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 56, elements: !46)
!46 = !{!47}
!47 = !DISubrange(count: 7)
!48 = !{i32 2, !"SDK Version", [2 x i32] [i32 13, i32 3]}
!49 = !{i32 7, !"Dwarf Version", i32 4}
!50 = !{i32 2, !"Debug Info Version", i32 3}
!51 = !{i32 1, !"wchar_size", i32 4}
!52 = !{i32 8, !"PIC Level", i32 2}
!53 = !{i32 7, !"uwtable", i32 1}
!54 = !{i32 7, !"frame-pointer", i32 1}
!55 = distinct !DICompileUnit(language: DW_LANG_C11, file: !2, producer: "clang version 19.1.7", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, globals: !56, splitDebugInlining: false, nameTableKind: Apple, sysroot: "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk", sdk: "MacOSX.sdk")
!56 = !{!0, !7, !12, !17, !22, !27, !29, !34, !39, !41, !43}
!57 = !{!"clang version 19.1.7"}
!58 = distinct !DISubprogram(name: "main", scope: !2, file: !2, line: 35, type: !59, scopeLine: 36, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !55, retainedNodes: !64)
!59 = !DISubroutineType(types: !60)
!60 = !{!61, !61, !62}
!61 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!62 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !63, size: 64)
!63 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64)
!64 = !{!65, !66}
!65 = !DILocalVariable(name: "argc", arg: 1, scope: !58, file: !2, line: 35, type: !61)
!66 = !DILocalVariable(name: "argv", arg: 2, scope: !58, file: !2, line: 35, type: !62)
!67 = !DILocation(line: 0, scope: !58)
!68 = !DILocation(line: 37, column: 12, scope: !58)
!69 = !DILocation(line: 38, column: 14, scope: !70)
!70 = distinct !DILexicalBlock(scope: !58, file: !2, line: 38, column: 9)
!71 = !DILocation(line: 38, column: 9, scope: !58)
!72 = !DILocation(line: 41, column: 5, scope: !58)
!73 = !DILocation(line: 19, column: 5, scope: !74, inlinedAt: !77)
!74 = distinct !DISubprogram(name: "emit_fields", scope: !2, file: !2, line: 16, type: !75, scopeLine: 17, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !55)
!75 = !DISubroutineType(types: !76)
!76 = !{null}
!77 = distinct !DILocation(line: 42, column: 5, scope: !58)
!78 = !DILocation(line: 22, column: 5, scope: !74, inlinedAt: !77)
!79 = !DILocation(line: 25, column: 5, scope: !74, inlinedAt: !77)
!80 = !DILocation(line: 28, column: 5, scope: !74, inlinedAt: !77)
!81 = !DILocation(line: 31, column: 5, scope: !74, inlinedAt: !77)
!82 = !DILocation(line: 43, column: 5, scope: !58)
!83 = !DILocation(line: 44, column: 5, scope: !58)
!84 = !DILocation(line: 45, column: 5, scope: !58)
!85 = !DILocation(line: 46, column: 1, scope: !58)
!86 = !DISubprogram(name: "xo_parse_args", scope: !87, file: !87, line: 820, type: !59, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!87 = !DIFile(filename: "/Users/phil/work/root/include/libxo/xo.h", directory: "")
!88 = !DISubprogram(name: "xo_open_container", scope: !87, file: !87, line: 520, type: !89, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!89 = !DISubroutineType(types: !90)
!90 = !{!91, !97}
!91 = !DIDerivedType(tag: DW_TAG_typedef, name: "xo_ssize_t", file: !87, line: 244, baseType: !92)
!92 = !DIDerivedType(tag: DW_TAG_typedef, name: "ssize_t", file: !93, line: 31, baseType: !94)
!93 = !DIFile(filename: "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/sys/_types/_ssize_t.h", directory: "")
!94 = !DIDerivedType(tag: DW_TAG_typedef, name: "__darwin_ssize_t", file: !95, line: 97, baseType: !96)
!95 = !DIFile(filename: "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/arm/_types.h", directory: "")
!96 = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
!97 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !98, size: 64)
!98 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !4)
!99 = !DISubprogram(name: "xo_close_container", scope: !87, file: !87, line: 532, type: !89, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!100 = !DISubprogram(name: "xo_finish", scope: !87, file: !87, line: 643, type: !101, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!101 = !DISubroutineType(types: !102)
!102 = !{!91}
