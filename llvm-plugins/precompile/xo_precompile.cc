/*
 * Copyright (c) 2025-2026, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, 2026
 *
 * xo_precompile.cc: LLVM New-PM module pass that rewrites xo_emit() calls
 * with constant format strings into xo_emit_cached() calls, supplying a
 * pre-parsed xo_format_cache_t global so the runtime skips format parsing.
 *
 * Load with:  clang -fpass-plugin=/path/to/xo_precompile.so ...
 *
 * The pass runs at PipelineStart (before any optimisations) so that string
 * constants are still in their original global variables.  At -O0 with the
 * New PM, PipelineStart EP callbacks do fire.
 */

#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "../validate/xo_parse_shim.h"

using namespace llvm;

/* Keep in sync with xo.h XO_EMIT_CACHE_VERSION */
#define XO_EMIT_CACHE_VERSION 1

/* ---------- compatibility helpers ---------------------------------------- */

/*
 * LLVM 15+ uses opaque pointers; earlier versions use typed pointers.
 * We abstract the difference with small helpers.
 */
#if LLVM_VERSION_MAJOR >= 15
#  define XO_OPAQUE_PTRS 1
static inline Type *voidPtrTy(LLVMContext &Ctx, Type * = nullptr) {
    return PointerType::getUnqual(Ctx);
}
static inline Constant *toVoidPtr(Constant *C, Type *) { return C; }
#else
#  define XO_OPAQUE_PTRS 0
static inline Type *voidPtrTy(LLVMContext &Ctx, Type *El = nullptr) {
    return El ? PointerType::getUnqual(El) : Type::getInt8PtrTy(Ctx);
}
static inline Constant *toVoidPtr(Constant *C, Type *PtrTy) {
    return ConstantExpr::getBitCast(C, PtrTy);
}
#endif

/* ---------- emit-function dispatch table --------------------------------- */

struct EmitTarget {
    const char *cached_name; /* replacement function name */
    unsigned    fmt_idx;     /* which arg index holds the format string */
    /*
     * All args before fmt_idx are passed through unchanged (handle, flags, …).
     * The cache pointer is inserted at fmt_idx; fmt and value args follow.
     */
};

/*
 * Every xo_emit*() variant with a potentially-constant format argument.
 * fmt_idx encodes the position: args[0..fmt_idx-1] are kept, then cache is
 * inserted, then args[fmt_idx..] (fmt + value args) are appended.
 *
 * The _p inline wrappers in xo.h normally expand to xo_emit_hv / xo_emit_hvf
 * calls, but may appear under their own names at -O0; both names are listed.
 */
static const struct {
    const char *name;
    EmitTarget  target;
} kEmitTable[] = {
    /* no prefix args */
    {"xo_emit",       {"xo_emit_cached",       0}},
    {"xo_emitr",      {"xo_emit_cachedr",      0}},
    {"xo_emit_p",     {"xo_emit_cached_p",     0}},
    /* one prefix arg (handle or flags) */
    {"xo_emit_h",     {"xo_emit_cached_h",     1}},
    {"xo_emit_hv",    {"xo_emit_cached_hv",    1}},
    {"xo_emit_f",     {"xo_emit_cached_f",     1}},
    {"xo_emit_hvp",   {"xo_emit_cached_hvp",   1}},
    {"xo_emit_hp",    {"xo_emit_cached_hp",    1}},
    {"xo_emit_fp",    {"xo_emit_cached_fp",    1}},
    /* two prefix args (handle + flags) */
    {"xo_emit_hf",    {"xo_emit_cached_hf",    2}},
    {"xo_emit_hvf",   {"xo_emit_cached_hvf",   2}},
    {"xo_emit_hfp",   {"xo_emit_cached_hfp",   2}},
    {"xo_emit_hvfp",  {"xo_emit_cached_hvfp",  2}},
};

static const EmitTarget *lookupEmitTarget(StringRef name)
{
    for (auto &e : kEmitTable)
        if (name == e.name) return &e.target;
    return nullptr;
}

/* ---------- format-string resolution ------------------------------------- */

static GlobalVariable *resolveStringGlobal(Value *v)
{
    v = v->stripPointerCasts();
    if (auto *GV = dyn_cast<GlobalVariable>(v))
        return GV;
#if !XO_OPAQUE_PTRS
    /* typed-pointer mode: string literals appear as GEP constant expressions */
    if (auto *CE = dyn_cast<ConstantExpr>(v)) {
        if (CE->getOpcode() == Instruction::GetElementPtr)
            return dyn_cast<GlobalVariable>(
                CE->getOperand(0)->stripPointerCasts());
    }
#endif
    return nullptr;
}

static bool extractCString(GlobalVariable *GV, std::string &out)
{
    if (!GV || !GV->isConstant() || !GV->hasDefinitiveInitializer())
        return false;
    auto *CDA = dyn_cast<ConstantDataArray>(GV->getInitializer());
    if (!CDA || !CDA->isCString())
        return false;
    out = CDA->getAsCString().str();
    return true;
}

static void
parse_error_cb (void *data, const char *, ...)
{
    *static_cast<bool *>(data) = true;
}

/* ---------- the pass ----------------------------------------------------- */

struct XoPrecompile : PassInfoMixin<XoPrecompile> {
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &)
    {
        LLVMContext &Ctx = M.getContext();

        Type *i64 = Type::getInt64Ty(Ctx);
        Type *i32 = Type::getInt32Ty(Ctx);
        Type *i16 = Type::getInt16Ty(Ctx);

        /*
         * LLVM StructType mirroring xo_field_info_t (13 logical members).
         * The DataLayout inserts the 2-byte pad between xfi_elen and xfi_fnum
         * automatically (same target as the C compiler).
         */
        StructType *FieldTy = StructType::get(Ctx, {
            i64,                                /* xfi_flags */
            i32,                                /* xfi_ftype */
            i16, i16, i16, i16, i16,           /* start, content, format, encoding, next */
            i16, i16, i16, i16,                 /* len, clen, flen, elen */
            i32, i32                            /* fnum, renum */
        });

        Type *PtrTy = voidPtrTy(Ctx);

        /* StructType matching xo_format_cache_t: { version, num_fields, *fields } */
        StructType *CacheTy = StructType::get(Ctx, {i32, i32, PtrTy});

        /* Sanitize module name for use in global symbol names */
        std::string ModSlug = M.getName().str();
        for (char &c : ModSlug)
            if (!isalnum((unsigned char) c)) c = '_';
        if (ModSlug.empty()) ModSlug = "anon";
        if (ModSlug.size() > 40)
            ModSlug = ModSlug.substr(ModSlug.size() - 40);

        /*
         * Collect candidates before modifying the IR.
         * Erasing a CallInst while iterating over instructions is unsafe.
         */
        SmallVector<std::pair<CallInst *, EmitTarget>, 16> ToRewrite;

        for (auto &F : M) {
            for (auto &BB : F) {
                for (auto &I : BB) {
                    auto *CI = dyn_cast<CallInst>(&I);
                    if (!CI) continue;
                    Function *Callee = CI->getCalledFunction();
                    if (!Callee) continue;
                    const EmitTarget *T = lookupEmitTarget(Callee->getName());
                    if (!T) continue;
                    if (CI->arg_size() <= T->fmt_idx) continue;
                    ToRewrite.push_back({CI, *T});
                }
            }
        }

        if (ToRewrite.empty())
            return PreservedAnalyses::all();

        bool Changed = false;
        unsigned Counter = 0;

        for (auto &[CI, Target] : ToRewrite) {
            /* Resolve the format string to a C string */
            Value *FmtArg = CI->getArgOperand(Target.fmt_idx);
            GlobalVariable *FmtGV = resolveStringGlobal(FmtArg);
            std::string FmtStr;
            if (!extractCString(FmtGV, FmtStr)) continue;

            /* Parse fields via the C shim */
            struct ParseCtx {
                SmallVector<xo_shim_field_t, 8> fields;
                bool error = false;
            } PCtx;

            int rc = xo_shim_parse_fields(
                FmtStr.c_str(),
                parse_error_cb,
                &PCtx.error,
                [](void *d, const xo_shim_field_t *f) {
                    static_cast<ParseCtx *>(d)->fields.push_back(*f);
                },
                &PCtx);

            if (rc < 0 || PCtx.error) continue;

            /* Build per-call name suffix */
            std::string Suffix = "." + ModSlug + "." + std::to_string(Counter++);
            unsigned N = (unsigned) PCtx.fields.size();

            /* Build const xo_field_info_t[] global */
            SmallVector<Constant *, 32> Elems;
            for (auto &f : PCtx.fields) {
                Elems.push_back(ConstantStruct::get(FieldTy, {
                    ConstantInt::get(i64, f.xsf_flags),
                    ConstantInt::get(i32, f.xsf_ftype),
                    ConstantInt::getSigned(i16, f.xsf_start),
                    ConstantInt::getSigned(i16, f.xsf_content),
                    ConstantInt::getSigned(i16, f.xsf_format),
                    ConstantInt::getSigned(i16, f.xsf_encoding),
                    ConstantInt::getSigned(i16, f.xsf_next),
                    ConstantInt::getSigned(i16, f.xsf_len),
                    ConstantInt::getSigned(i16, f.xsf_clen),
                    ConstantInt::getSigned(i16, f.xsf_flen),
                    ConstantInt::getSigned(i16, f.xsf_elen),
                    ConstantInt::get(i32, f.xsf_fnum),
                    ConstantInt::get(i32, f.xsf_renum),
                }));
            }

            ArrayType *ArrTy = ArrayType::get(FieldTy, N);
            auto *FieldsGV = new GlobalVariable(M, ArrTy, /*isConst*/ true,
                GlobalValue::PrivateLinkage,
                ConstantArray::get(ArrTy, Elems),
                ".xo_fields" + Suffix);
            FieldsGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

            /* Build const xo_format_cache_t global */
            Constant *FieldsPtr = toVoidPtr(FieldsGV, PtrTy);
            auto *CacheGV = new GlobalVariable(M, CacheTy, /*isConst*/ true,
                GlobalValue::PrivateLinkage,
                ConstantStruct::get(CacheTy, {
                    ConstantInt::get(i32, XO_EMIT_CACHE_VERSION),
                    ConstantInt::get(i32, N),
                    FieldsPtr,
                }),
                ".xo_fcache" + Suffix);
            CacheGV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

            /*
             * Build the FunctionType for the cached replacement:
             *   [ param types before fmt_idx ]  (handle, flags, …)
             *   + ptr  (cache)
             *   + [ param types from fmt_idx ]   (fmt + fixed typed params)
             *   + vararg flag inherited from original
             *
             * Deriving types from the original call avoids hard-coding
             * handle/flags widths and handles both vararg and va_list forms.
             */
            FunctionType *OrigFT = CI->getFunctionType();
            SmallVector<Type *, 8> ParamTypes;
            for (unsigned i = 0; i < Target.fmt_idx; ++i)
                ParamTypes.push_back(OrigFT->getParamType(i));
            ParamTypes.push_back(PtrTy); /* cache */
            for (unsigned i = Target.fmt_idx; i < OrigFT->getNumParams(); ++i)
                ParamTypes.push_back(OrigFT->getParamType(i));

            FunctionCallee CachedFn = M.getOrInsertFunction(
                Target.cached_name,
                FunctionType::get(OrigFT->getReturnType(),
                                  ParamTypes, OrigFT->isVarArg()));

            /* Build the replacement argument list */
            SmallVector<Value *, 16> Args;
            /* prefix args before fmt (handle, flags, …) */
            for (unsigned i = 0; i < Target.fmt_idx; ++i)
                Args.push_back(CI->getArgOperand(i));
            /* cache */
            Args.push_back(toVoidPtr(CacheGV, PtrTy));
            /* fmt and all remaining args (value args / va_list) */
            for (unsigned i = Target.fmt_idx; i < CI->arg_size(); ++i)
                Args.push_back(CI->getArgOperand(i));

            IRBuilder<> Builder(CI);
            CallInst *NewCI = Builder.CreateCall(CachedFn, Args);
            NewCI->setCallingConv(CI->getCallingConv());
            NewCI->setDebugLoc(CI->getDebugLoc());

            CI->replaceAllUsesWith(NewCI);
            CI->eraseFromParent();
            Changed = true;
        }

        return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }
};

/* ---------- plugin registration ------------------------------------------ */

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo()
{
    return {
        LLVM_PLUGIN_API_VERSION, "xo_precompile", "1.0",
        [](PassBuilder &PB) {
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel) {
                    MPM.addPass(XoPrecompile());
                });
        }
    };
}
