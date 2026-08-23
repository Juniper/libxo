/*
 * Copyright (c) 2025, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, August 2025
 *
 * xo_validate.cc: clang plugin that validates libxo format strings.
 *
 * Load with: clang -fplugin=/path/to/xo_validate.so ...
 *
 * Checks performed:
 *   1. Format string syntax (malformed field descriptors)
 *   2. Argument count (too few or too many va_args)
 *   3. Argument type: precise — length modifier checked (%ld expects long,
 *      %zu expects size_t, etc.) via ASTContext canonical types.  Falls back
 *      to coarse category (integer/float/pointer/string) for conversion
 *      characters not covered by the precise path.
 *
 * Design notes:
 *   - Only public ASTContext/QualType/Expr APIs are used; no internal
 *     clang headers that change across LLVM versions.
 *   - Varargs promotion is handled by using arg->getType() (which already
 *     reflects the promotion: char/short→int, float→double) for type
 *     matching.  arg->IgnoreImpCasts()->getType() is used only in the
 *     error message to show the programmer's source type.
 *   - %h/%hh modifiers map to int/unsigned int (the promoted types) so
 *     short/char arguments don't false-positive.
 */

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <llvm/Support/CommandLine.h>

#include <cctype>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

static llvm::cl::opt<bool> ErrorsAsWarnings(
    "xo-validate-errors-as-warnings",
    llvm::cl::desc("Treat xo_validate errors as warnings (do not fail compilation)"),
    llvm::cl::init(false));

#include "xo_parse_shim.h"

using namespace clang;

/*
 * Table of libxo emit functions: name and the 0-based index of the
 * format-string argument.  Functions taking a va_list (xo_emit_hv) are
 * omitted — we can validate the format string but cannot inspect the
 * argument list at compile time.
 */
struct XoEmitEntry {
    const char *name;
    unsigned    fmt_arg;
};

static const XoEmitEntry xo_emit_table[] = {
    { "xo_emit",        0 },
    { "xo_emit_h",      1 },
    { nullptr,          0 },
};

/*
 * Coarse type category (kept as fallback for conversions not in the
 * precise path and for the XFF_ARGUMENT name-slot check).
 */

enum class FmtExpect { String, Integer, Float, Pointer, Name, Unknown };

static FmtExpect
parse_fmt_expect (const char *fmt, unsigned fmtlen)
{
    if (!fmt || fmtlen == 0)
        return FmtExpect::Name;

    const char *p = fmt, *end = fmt + fmtlen;
    if (*p != '%')
        return FmtExpect::Unknown;
    p++;

    /* flags */
    while (p < end && (*p == '-' || *p == '+' || *p == ' ' ||
                        *p == '0' || *p == '#' || *p == '\''))
        p++;

    /* width */
    if (p < end && *p == '*')
        p++;
    else
        while (p < end && isdigit((unsigned char) *p))
            p++;

    /* precision groups */
    while (p < end && *p == '.') {
        p++;
        if (p < end && *p == '*')
            p++;
        else
            while (p < end && isdigit((unsigned char) *p))
                p++;
    }

    /* length modifiers — skip for coarse check */
    while (p < end && (*p == 'l' || *p == 'h' || *p == 'L' ||
                        *p == 'z' || *p == 't' || *p == 'j' || *p == 'q'))
        p++;

    if (p >= end)
        return FmtExpect::Unknown;

    switch (*p) {
    case 's':
        return FmtExpect::String;
    case 'd': case 'i': case 'u': case 'c':
    case 'x': case 'X': case 'o': case 'b':
        return FmtExpect::Integer;
    case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
        return FmtExpect::Float;
    case 'p':
        return FmtExpect::Pointer;
    default:
        return FmtExpect::Unknown;
    }
}

static bool
arg_type_ok (const Expr *arg, FmtExpect expect)
{
    QualType qt = arg->getType().getCanonicalType();

    switch (expect) {
    case FmtExpect::Name:
    case FmtExpect::String:
    case FmtExpect::Pointer:
        return qt->isPointerType();
    case FmtExpect::Integer:
        return qt->isIntegerType();
    case FmtExpect::Float:
        return qt->isFloatingType();
    case FmtExpect::Unknown:
        return true;
    }
    return true;
}

static const char *
expect_name (FmtExpect e)
{
    switch (e) {
    case FmtExpect::Name:
    case FmtExpect::String:  return "string (char *)";
    case FmtExpect::Integer: return "integer";
    case FmtExpect::Float:   return "floating-point";
    case FmtExpect::Pointer: return "pointer";
    default:                 return "unknown";
    }
}

/*
 * Precise type mapping: format spec → ASTContext QualType.
 */

enum LenMod {
    LM_NONE,
    LM_H,       /* h  — maps to int/unsigned int (varargs-promoted) */
    LM_HH,      /* hh — maps to int/unsigned int (varargs-promoted) */
    LM_L,       /* l  */
    LM_LL,      /* ll */
    LM_L_BIG,   /* L  — only for floating-point */
    LM_Z,       /* z  */
    LM_T,       /* t  */
    LM_J,       /* j  */
};

/*
 * Return the QualType the va_arg must have (after varargs promotion) for the
 * given printf-style format spec.  Returns a null QualType for specs that
 * need no type check (%m, %n, unknown) or are not yet handled.
 */
static QualType
fmt_expected_type (ASTContext &C, const char *spec, unsigned len)
{
    if (!spec || len == 0)
        return QualType();

    const char *p = spec, *end = spec + len;
    if (p >= end || *p != '%')
        return QualType();
    p++;

    /* flags */
    while (p < end && (*p == '-' || *p == '+' || *p == ' ' ||
                        *p == '0' || *p == '#' || *p == '\''))
        p++;
    /* width (already split as a separate "%d" by scan_format_args) */
    if (p < end && *p == '*')
        p++;
    else
        while (p < end && isdigit((unsigned char) *p))
            p++;
    /* precision groups (libxo allows %.*.*s) */
    while (p < end && *p == '.') {
        p++;
        if (p < end && *p == '*')
            p++;
        else
            while (p < end && isdigit((unsigned char) *p))
                p++;
    }

    /* length modifier */
    LenMod lm = LM_NONE;
    if (p < end) {
        switch (*p) {
        case 'h':
            p++;
            if (p < end && *p == 'h') {
                lm = LM_HH;
                p++;
            } else {
                lm = LM_H;
            }
            break;
        case 'l':
            p++;
            if (p < end && *p == 'l') {
                lm = LM_LL;
                p++;
            } else {
                lm = LM_L;
            }
            break;
        case 'L': lm = LM_L_BIG; p++; break;
        case 'z': lm = LM_Z;     p++; break;
        case 't': lm = LM_T;     p++; break;
        case 'j': lm = LM_J;     p++; break;
        case 'q': lm = LM_LL;    p++; break;   /* BSD %q = long long */
        default:  break;
        }
    }

    if (p >= end)
        return QualType();

    switch (*p) {
    case 'd': case 'i':
        switch (lm) {
        case LM_NONE: case LM_H: case LM_HH:
            return C.IntTy;
        case LM_L:
            return C.LongTy;
        case LM_LL:
            return C.LongLongTy;
        case LM_Z: case LM_T:
            return C.getPointerDiffType();
        case LM_J:
            return C.getIntMaxType();
        default:
            return QualType();
        }
    case 'u': case 'x': case 'X': case 'o': case 'b':
        switch (lm) {
        case LM_NONE: case LM_H: case LM_HH:
            return C.UnsignedIntTy;
        case LM_L:
            return C.UnsignedLongTy;
        case LM_LL:
            return C.UnsignedLongLongTy;
        case LM_Z:
            return C.getSizeType();
        case LM_J:
            return C.getUIntMaxType();
        default:
            return QualType();
        }
    case 'c':
        return C.IntTy;     /* char/short promote to int in varargs */
    case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
        if (lm == LM_L_BIG)
            return C.LongDoubleTy;
        return C.DoubleTy;  /* float promotes to double in varargs */
    case 's':
        if (lm == LM_L)
            return C.getPointerType(C.WCharTy);
        return C.getPointerType(C.CharTy);
    case 'p':
        return C.VoidPtrTy;
    default:
        return QualType();
    }
}

/*
 * Return true if the actual argument type is compatible with the expected type.
 * Uses arg->getType() (the promoted type seen by the callee) for matching so
 * that varargs promotions (char→int, float→double) are already applied.
 *
 * Matching rules:
 *  - Integer: same bit width, sign ignored (long == unsigned long long
 *    when both are 64-bit).  This is looser than clang's own -Wformat
 *    (which requires exact kind match) by design: fixed-width typedefs
 *    like uint64_t/int64_t alias different builtin kinds across
 *    platforms (unsigned long on FreeBSD/Linux, unsigned long long on
 *    macOS), and a libxo format string that is correct on one platform
 *    must not warn on another.
 *  - Float:   exact canonical type (long double ≠ double even if same size).
 *  - %s:      any char pointer.
 *  - %p:      any pointer.
 */
static bool
type_matches (ASTContext &ctxt, QualType expected, const Expr *arg)
{
    QualType act = arg->getType().getCanonicalType().getUnqualifiedType();
    QualType exp = expected.getCanonicalType().getUnqualifiedType();

    if (act == exp)
        return true;

    /* Resolve enum to its underlying integer type before further checks */
    if (const auto *ET = act->getAs<EnumType>()) {
        act = ET->getDecl()->getIntegerType()
                 .getCanonicalType().getUnqualifiedType();
    }
    if (act == exp)
        return true;

    /*
     * Integers: same bit width, sign ignored (int vs unsigned int is
     * fine, as is unsigned long vs unsigned long long when both are
     * 64-bit).  See the note above type_matches() for why cross-kind
     * matches are accepted here.
     */
    if (exp->isIntegerType() && act->isIntegerType())
        return ctxt.getTypeSize(act) == ctxt.getTypeSize(exp);

    /*
     * Also handle array-to-pointer conversion (T[N] for T*).  Arrays
     * may not be converted to pointers.
     */
    if (exp->isPointerType()) {
        /* Unwrap pointer types */
        QualType ap;
        if (act->isPointerType())
            ap = act->getPointeeType().getCanonicalType().getUnqualifiedType();
        else if (act->isArrayType())
            ap = ctxt.getAsArrayType(act)->getElementType()
                  .getCanonicalType().getUnqualifiedType();
        else
            return false;

        QualType ep = exp->getPointeeType().getCanonicalType()
	                   .getUnqualifiedType();

        if (ap == ep)
            return true;

        /* %s/%hs: any char kind (char *, unsigned char *, char[N], …) */
        if (ep->isCharType() && ap->isCharType())
            return true;

        /*
	 * %ls: in C mode the wchar_t typedef resolves to an integer
         * type (e.g. int on macOS) while ctxt.WCharTy may be a
         * distinct built-in BuiltinType::WChar_S.  Accept any
         * same-sized integer as wchar_t.
	 */
        if (ep->isWideCharType() && ap->isIntegerType()
	        && ctxt.getTypeSize(ap) == ctxt.getTypeSize(ep))
            return true;

        /* %p: void * accepts any pointer-or-array argument */
        if (ep->isVoidType())
            return true;

        return false;
    }

    /*
     * Float: exact canonical type match.  On macOS ARM long double
     * and double are both 64-bit, but they are distinct types and
     * clang warns when they are mixed.
     */
    if (exp->isFloatingType() && act->isFloatingType())
        return act == exp;

    return false;
}

/*
 * Diagnostic callbacks and visitor.
 */

struct ArgCollector {
    std::vector<std::pair<std::string, unsigned>> args; /* (spec, speclen) */

    static void callback(void *data, const char *fmt, unsigned fmtlen) {
        auto *ac = static_cast<ArgCollector *>(data);
        ac->args.emplace_back(fmt ? std::string(fmt, fmtlen) : std::string(),
                               fmtlen);
    }
};

struct DiagCb {
    DiagnosticsEngine *diags;
    unsigned           id;
    SourceLocation     loc;
};

static void
emit_diag (void *data, const char *fmt, ...)
{
    auto *dc = static_cast<DiagCb *>(data);
    char buf[512];
    va_list vap;
    va_start(vap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vap);
    va_end(vap);
    dc->diags->Report(dc->loc, dc->id) << buf;
}

class XoValidateVisitor : public RecursiveASTVisitor<XoValidateVisitor> {
    DiagnosticsEngine &Diags;
    ASTContext        *Ctx_;          /* set by setContext() before traversal */
    unsigned           SyntaxDiagID;
    unsigned           CountDiagID;
    unsigned           TypeDiagID;    /* coarse fallback */
    unsigned           TypePreciseDiagID;
    unsigned           WarnDiagID;

public:
    explicit XoValidateVisitor(CompilerInstance &CI)
        : Diags(CI.getDiagnostics()), Ctx_(nullptr)
    {
        auto errLevel = ErrorsAsWarnings
            ? DiagnosticsEngine::Warning : DiagnosticsEngine::Error;

        SyntaxDiagID = Diags.getCustomDiagID(errLevel,
                           "libxo: %0");
        CountDiagID  = Diags.getCustomDiagID(errLevel,
                           "libxo: format expects %0 argument(s) but %1 provided");
        TypeDiagID   = Diags.getCustomDiagID(errLevel,
                           "libxo: argument %0 type mismatch: format expects %1");
        TypePreciseDiagID = Diags.getCustomDiagID(errLevel,
                           "libxo: argument %0: format specifies type '%1'"
                           " but the argument has type '%2'");
        WarnDiagID   = Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                           "libxo: %0");
    }

    void setContext(ASTContext &Ctx) { Ctx_ = &Ctx; }

    bool VisitCallExpr(CallExpr *CE)
    {
        const FunctionDecl *FD = CE->getDirectCallee();
        if (!FD)
            return true;

        StringRef name = FD->getName();
        unsigned fmt_arg = UINT_MAX;
        for (const XoEmitEntry *e = xo_emit_table; e->name; e++) {
            if (name == e->name) {
                fmt_arg = e->fmt_arg;
                break;
            }
        }
        if (fmt_arg == UINT_MAX || fmt_arg >= CE->getNumArgs())
            return true;

        const Expr *fmtexpr = CE->getArg(fmt_arg)->IgnoreParenCasts();
        const auto *SL = dyn_cast<StringLiteral>(fmtexpr);
        if (!SL)
            return true;    /* non-literal format strings: skip */

        std::string fmt = SL->getString().str();
        DiagCb dc_err  { &Diags, SyntaxDiagID, SL->getBeginLoc() };
        DiagCb dc_warn { &Diags, WarnDiagID,   SL->getBeginLoc() };
        ArgCollector ac;

        int rc = xo_shim_parse_args(fmt.c_str(),
                                     emit_diag, &dc_err,
                                     emit_diag, &dc_warn,
                                     ArgCollector::callback, &ac);
        if (rc < 0)
            return true;    /* parse error already reported */

        unsigned expected = (unsigned) ac.args.size();
        unsigned actual   = CE->getNumArgs() - fmt_arg - 1;

        if (expected != actual) {
            Diags.Report(SL->getBeginLoc(), CountDiagID) << expected << actual;
            return true;
        }

        if (!Ctx_)
            return true;

        PrintingPolicy PP = Ctx_->getPrintingPolicy();

        for (unsigned i = 0; i < expected; i++) {
            const auto &a    = ac.args[i];
            const char *spec = a.first.empty() ? nullptr : a.first.c_str();
            unsigned speclen = a.second;
            const Expr *arg  = CE->getArg(fmt_arg + 1 + i);

            QualType exp_type = fmt_expected_type(*Ctx_, spec, speclen);
            if (!exp_type.isNull()) {
                if (!type_matches(*Ctx_, exp_type, arg)) {
                    std::string exp_str = exp_type.getAsString(PP);
                    std::string act_str = arg->IgnoreImpCasts()->getType()
                                             .getAsString(PP);
                    Diags.Report(arg->getBeginLoc(), TypePreciseDiagID)
                        << (i + 1) << exp_str << act_str;
                }
            } else {
                FmtExpect expect = parse_fmt_expect(spec, speclen);
                if (!arg_type_ok(arg, expect)) {
                    Diags.Report(arg->getBeginLoc(), TypeDiagID)
                        << (i + 1) << expect_name(expect);
                }
            }
        }

        return true;
    }
};

class XoValidateConsumer : public ASTConsumer {
    XoValidateVisitor Visitor;
public:
    explicit XoValidateConsumer(CompilerInstance &CI) : Visitor(CI) {}

    void HandleTranslationUnit(ASTContext &Ctx) override
    {
        Visitor.setContext(Ctx);
        Visitor.TraverseDecl(Ctx.getTranslationUnitDecl());
    }
};

class XoValidateAction : public PluginASTAction {
protected:
    std::unique_ptr<ASTConsumer>
    CreateASTConsumer(CompilerInstance &CI, StringRef) override
    {
        return std::make_unique<XoValidateConsumer>(CI);
    }

    bool ParseArgs(const CompilerInstance &,
                   const std::vector<std::string> &) override
    {
        return true;
    }

    ActionType getActionType() override { return AddAfterMainAction; }
};

static FrontendPluginRegistry::Add<XoValidateAction>
    X("xo-validate", "validate libxo format strings and argument types");
