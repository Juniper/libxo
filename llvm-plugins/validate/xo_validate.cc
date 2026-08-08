/*
 * Copyright (c) 2025, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, 2025
 *
 * xo_validate.cc: clang plugin that validates libxo format strings.
 *
 * Load with: clang -fplugin=/path/to/xo_validate.so ...
 *
 * Checks performed:
 *   1. Format string syntax (malformed field descriptors)
 *   2. Argument count (too few or too many va_args)
 *   3. Argument type category mismatch (%s vs non-pointer, %d vs non-integer,
 *      %f vs non-float, %p vs non-pointer).  Length-modifier precision (e.g.
 *      %lu vs unsigned long vs unsigned int) is intentionally not checked here
 *      to avoid depending on internal Clang APIs that have hidden visibility
 *      and change across LLVM versions.
 */

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
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
 * Expected type category derived from a printf format spec.
 * Intentionally coarse — we check category (integer/float/pointer/string),
 * not precision (int vs long vs long long).  This keeps us independent of
 * internal Clang type-checking APIs.
 */
enum class FmtExpect { String, Integer, Float, Pointer, Name, Unknown };

/*
 * Parse a printf format spec (not NUL-terminated, length fmtlen) and return
 * the expected argument type category.  NULL fmt means this field's name
 * comes from a va_arg (XFF_ARGUMENT), which must be const char *.
 *
 * scan_format_args() in xo_parse_shim.c already emits a separate "%d" entry
 * for each '*' width/precision before emitting the full specifier, so by the
 * time we see "%*d" here the '*' is just part of the spec string and we skip
 * it while scanning for the conversion character.
 */
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
    /* width: digits or '*' (scan_format_args already emitted a separate %d) */
    if (p < end && *p == '*')
        p++;
    else
        while (p < end && isdigit((unsigned char)*p))
            p++;
    /* precision */
    if (p < end && *p == '.') {
        p++;
        if (p < end && *p == '*')
            p++;
        else
            while (p < end && isdigit((unsigned char)*p))
                p++;
    }
    /* length modifiers — skip, we don't check precision */
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

/*
 * Check whether the type of 'arg' is compatible with 'expect'.
 * Returns true if compatible or if we cannot determine (Unknown).
 */
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

/* State collected by the xo_shim_arg_cb_t callback */
struct ArgCollector {
    std::vector<std::pair<std::string, unsigned>> args; /* (spec, speclen) */

    static void callback(void *data, const char *fmt, unsigned fmtlen) {
        auto *ac = static_cast<ArgCollector *>(data);
        ac->args.emplace_back(fmt ? std::string(fmt, fmtlen) : std::string(),
                               fmtlen);
    }
};

/* State for the error/warn callback passed to xo_shim_parse_args */
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
    unsigned           SyntaxDiagID;
    unsigned           CountDiagID;
    unsigned           TypeDiagID;
    unsigned           WarnDiagID;

public:
    explicit XoValidateVisitor(CompilerInstance &CI)
        : Diags(CI.getDiagnostics())
    {
        auto errLevel = ErrorsAsWarnings
            ? DiagnosticsEngine::Warning : DiagnosticsEngine::Error;

        SyntaxDiagID = Diags.getCustomDiagID(errLevel,
                           "libxo format: %0");
        CountDiagID  = Diags.getCustomDiagID(errLevel,
                           "libxo: format expects %0 argument(s) but %1 provided");
        TypeDiagID   = Diags.getCustomDiagID(errLevel,
                           "libxo: argument %0 type mismatch: format expects %1");
        WarnDiagID   = Diags.getCustomDiagID(DiagnosticsEngine::Warning,
                           "libxo format: %0");
    }

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

        for (unsigned i = 0; i < expected; i++) {
            const auto &a = ac.args[i];
            FmtExpect expect = parse_fmt_expect(
                a.first.empty() ? nullptr : a.first.c_str(), a.second);

            const Expr *arg = CE->getArg(fmt_arg + 1 + i);
            if (!arg_type_ok(arg, expect)) {
                Diags.Report(arg->getBeginLoc(), TypeDiagID)
                    << (i + 1) << expect_name(expect);
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
