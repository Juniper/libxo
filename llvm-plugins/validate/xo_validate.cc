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
 *   3. Argument type: precise - length modifier checked (%ld expects long,
 *      %zu expects size_t, etc.) via ASTContext canonical types.  Falls back
 *      to coarse category (integer/float/pointer/string) for conversion
 *      characters not covered by the precise path.
 *
 * Design notes:
 *   - Only public ASTContext/QualType/Expr APIs are used; no internal
 *     clang headers that change across LLVM versions.
 *   - Varargs promotion is handled by using arg->getType() (which already
 *     reflects the promotion: char/short->int, float->double) for type
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

static llvm::cl::opt<bool> LintWarnings(
    "xo-validate-lint",
    llvm::cl::desc("xo_validate should give lint errors (minor, non-fatal)"),
    llvm::cl::init(false));

#include "xo_parse_shim.h"

using namespace clang;

/*
 * Table of libxo emit functions: name and the 0-based index of the
 * format-string argument.  Functions taking a va_list (xo_emit_hv,
 * xo_emit_hvf) are omitted - we can validate the format string but
 * cannot inspect the argument list at compile time.
 */
struct XoEmitEntry {
    const char *name;
    unsigned    fmt_arg;
};

static const XoEmitEntry xo_emit_table[] = {
    { "xo_emit",        0 },
    { "xo_emitr",       0 },
    { "xo_emit_h",      1 },
    { "xo_emit_f",      1 },
    { "xo_emit_hf",     2 },
    { nullptr,          0 },
};

/*
 * Return the QualType the va_arg must have (after varargs promotion) for
 * a parsed fspec.  xf_arg_type says which argument libxo pulls; the
 * conversion character only picks the signedness shown in diagnostics,
 * since type_matches() ignores sign.  Returns a null QualType for an
 * fspec that consumes no argument.
 */
static bool
fmt_is_signed (const xo_fspec_t *xfp)
{
    int fc = xfp->xf_fc;

    return fc == 'd' || fc == 'i' || fc == 'D' || fc == 'c';
}

static QualType
fmt_expected_type (ASTContext &C, const xo_fspec_t *xfp)
{
    int fc = xfp->xf_fc;
    bool is_signed = fmt_is_signed(xfp);

    switch (xfp->xf_arg_type) {
    case XO_AT_INT:
        return is_signed ? C.IntTy : C.UnsignedIntTy;

    case XO_AT_LONG:
        return is_signed ? C.LongTy : C.UnsignedLongTy;

    case XO_AT_LONG_LONG:
    case XO_AT_QUAD:
        return is_signed ? C.LongLongTy : C.UnsignedLongLongTy;

    case XO_AT_INT64:
        return C.getIntTypeForBitwidth(64, is_signed);

    case XO_AT_INTMAX:
        return is_signed ? C.getIntMaxType() : C.getUIntMaxType();

    case XO_AT_PTRDIFF:
        return C.getPointerDiffType();

    case XO_AT_SIZE:
        return is_signed ? C.getSignedSizeType() : C.getSizeType();

    case XO_AT_DOUBLE:
        return C.DoubleTy;      /* float promotes to double in varargs */

    case XO_AT_LONG_DOUBLE:
        return C.LongDoubleTy;

    case XO_AT_WINT:
        return C.getWIntType();

    case XO_AT_STRING:
        if (xfp->xf_lflag || fc == 'S')
            return C.getPointerType(C.WCharTy);
        return C.getPointerType(C.CharTy);

    case XO_AT_POINTER:
        return C.VoidPtrTy;

    default:
        return QualType();
    }
}

/*
 * Return the typedef name the C library uses for an fspec's type, or
 * nullptr if the builtin type name is already the natural one.
 * fmt_expected_type() returns canonical builtin types (intmax_t is
 * just "long"), so without this a "%jd" diagnostic would name a type
 * the format never mentions.
 */
static const char *
fmt_expected_name (const xo_fspec_t *xfp)
{
    bool is_signed = fmt_is_signed(xfp);

    switch (xfp->xf_arg_type) {
    case XO_AT_INT64:
        return is_signed ? "int64_t" : "uint64_t";

    case XO_AT_INTMAX:
        return is_signed ? "intmax_t" : "uintmax_t";

    case XO_AT_PTRDIFF:
        return "ptrdiff_t";

    case XO_AT_SIZE:
        return is_signed ? "ssize_t" : "size_t";

    case XO_AT_WINT:
        return "wint_t";

    default:
        return nullptr;
    }
}

/*
 * Describe a type the way clang's own -Wformat does: the written name,
 * followed by the underlying type when they differ ("'uintmax_t' (aka
 * 'unsigned long')").
 */
static std::string
type_desc (const std::string &name, const std::string &canon)
{
    if (name == canon)
        return "'" + name + "'";

    return "'" + name + "' (aka '" + canon + "')";
}

/*
 * Return true if the actual argument type is compatible with the expected type.
 * Uses arg->getType() (the promoted type seen by the callee) for matching so
 * that varargs promotions (char->int, float->double) are already applied.
 *
 * Matching rules:
 *  - Integer: same bit width, sign ignored (long == unsigned long long
 *    when both are 64-bit).  This is looser than clang's own -Wformat
 *    (which requires exact kind match) by design: fixed-width typedefs
 *    like uint64_t/int64_t alias different builtin kinds across
 *    platforms (unsigned long on FreeBSD/Linux, unsigned long long on
 *    macOS), and a libxo format string that is correct on one platform
 *    must not warn on another.
 *  - Float:   exact canonical type (long double != double even if same size).
 *  - %s:      any char pointer.
 *  - %p:      any pointer.
 *  - NULL:    only when null_ok is set, since clang's -Wformat rejects
 *             a NULL "%s" and libxo only defines it for "%JNs".
 */
static bool
type_matches (ASTContext &ctxt, QualType expected, const Expr *arg,
              bool null_ok)
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
        if (null_ok && arg->isNullPointerConstant(ctxt,
                                       Expr::NPC_ValueDependentIsNotNull))
            return true;

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

        /* %s/%hs: any char kind (char *, unsigned char *, char[N], ...) */
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

/*
 * Collects a copy of each argument's fspec, since the shim's fspecs
 * don't outlive the parse.  The name of an XFF_ARGUMENT field (a NULL
 * fspec) is a string, so it's recorded as "%s".
 */
struct ArgCollector {
    std::vector<xo_fspec_t> args;

    static void callback(void *data, const xo_fspec_t *xfp) {
        auto *ac = static_cast<ArgCollector *>(data);

        if (xfp) {
            ac->args.push_back(*xfp);
        } else {
            xo_fspec_t name = {};
            name.xf_fc = 's';
            name.xf_arg_type = XO_AT_STRING;
            ac->args.push_back(name);
        }
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
    unsigned           TypeDiagID;
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
                           "libxo: argument %0: format specifies type %1"
                           " but the argument has type %2");
        WarnDiagID   = Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                           "libxo: %0");
    }

    void setContext(ASTContext &Ctx) { Ctx_ = &Ctx; }

    bool VisitCallExpr(CallExpr *CE)
    {
        const FunctionDecl *FD = CE->getDirectCallee();
        if (!FD)
            return true;

        /*
         * getName() asserts the callee's DeclarationName is a simple
         * identifier; operator overloads, conversion functions, etc.
         * are not, and none of them can ever be a libxo emit call.
         */
        if (!FD->getIdentifier())
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

	xo_parse_flags_t flags = XPF_STRICT;
	if (LintWarnings)
	    flags |= XPF_LINT;

        int rc = xo_shim_parse_args(fmt.c_str(),
				    emit_diag, &dc_err,
				    emit_diag, &dc_warn,
				    ArgCollector::callback, &ac,
				    flags);
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
            const Expr *arg = CE->getArg(fmt_arg + 1 + i);

            const xo_fspec_t *xfp = &ac.args[i];
            QualType exp_type = fmt_expected_type(*Ctx_, xfp);
            bool null_ok = (xfp->xf_extflags & XXF_NULL_AS_EMPTY) != 0;

            if (!exp_type.isNull()) {
                if (!type_matches(*Ctx_, exp_type, arg, null_ok)) {
                    /*
                     * Newer clang (LLVM 21+, https://github.com/llvm/llvm-project/pull/143653)
                     * made getSizeType()/getPointerDiffType() return a
                     * PredefinedSugarType ("__size_t"/"__ptrdiff_t") instead
                     * of the canonical builtin.  Desugar explicitly so the
                     * printed name (e.g. "unsigned long") is stable across
                     * clang versions.
                     */
                    std::string exp_canon = exp_type.getCanonicalType()
                                                   .getAsString(PP);
                    const char *exp_name = fmt_expected_name(xfp);
                    std::string exp_str = type_desc(exp_name ? exp_name
                                                    : exp_canon, exp_canon);

                    QualType act_type = arg->IgnoreImpCasts()->getType();
                    std::string act_str = type_desc(act_type.getAsString(PP),
                                    act_type.getCanonicalType().getAsString(PP));

                    Diags.Report(arg->getBeginLoc(), TypeDiagID)
                        << (i + 1) << exp_str << act_str;
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
