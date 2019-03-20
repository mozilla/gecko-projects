/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This checker implements the "can run script" analysis.  The idea is to detect
 * functions that can run script that are being passed reference-counted
 * arguments (including "this") whose refcount might go to zero as a result of
 * the script running.  We want to prevent that.
 *
 * The approach is to attempt to enforce the following invariants on the call
 * graph:
 *
 * 1) Any caller of a MOZ_CAN_RUN_SCRIPT function is itself MOZ_CAN_RUN_SCRIPT.
 * 2) If a virtual MOZ_CAN_RUN_SCRIPT method overrides a base class method,
 *    that base class method is also MOZ_CAN_RUN_SCRIPT.
 *
 * Invariant 2 ensures that we don't accidentally call a MOZ_CAN_RUN_SCRIPT
 * function via a base-class virtual call.  Invariant 1 ensures that
 * the property of being able to run script propagates up the callstack.  There
 * is an opt-out for invariant 1: A function (declaration _or_ implementation)
 * can be decorated with MOZ_CAN_RUN_SCRIPT_BOUNDARY to indicate that we do not
 * require it or any of its callers to be MOZ_CAN_RUN_SCRIPT even if it calls
 * MOZ_CAN_RUN_SCRIPT functions.
 *
 * There are two known holes in invariant 1, apart from the
 * MOZ_CAN_RUN_SCRIPT_BOUNDARY opt-out:
 *
 *  - Functions called via function pointers can be MOZ_CAN_RUN_SCRIPT even if
 *    their caller is not, because we have no way to determine from the function
 *    pointer what function is being called.
 *  - MOZ_CAN_RUN_SCRIPT destructors can happen in functions that are not
 *    MOZ_CAN_RUN_SCRIPT.
 *    https://bugzilla.mozilla.org/show_bug.cgi?id=1535523 tracks this.
 *
 * Given those invariants we then require that when calling a MOZ_CAN_RUN_SCRIPT
 * function all refcounted arguments (including "this") satisfy one of three
 * conditions:
 *  a) The argument is held via a strong pointer on the stack.
 *  b) The argument is an argument of the caller (and hence held by a strong
 *     pointer somewhere higher up the callstack).
 *  c) The argument is explicitly annotated with MOZ_KnownLive, which indicates
 *     that something is guaranteed to keep it alive (e.g. it's rooted via a JS
 *     reflector).
 */

#include "CanRunScriptChecker.h"
#include "CustomMatchers.h"

void CanRunScriptChecker::registerMatchers(MatchFinder *AstMatcher) {
  auto Refcounted = qualType(hasDeclaration(cxxRecordDecl(isRefCounted())));
  auto StackSmartPtr =
    ignoreTrivials(
      declRefExpr(to(varDecl(hasAutomaticStorageDuration())),
                  hasType(isSmartPtrToRefCounted())));
  auto MozKnownLiveCall =
    callExpr(callee(functionDecl(hasName("MOZ_KnownLive"))));

  auto InvalidArg =
      // We want to find any expression,
      ignoreTrivials(expr(
          // which has a refcounted pointer type,
          anyOf(
            hasType(Refcounted),
            hasType(pointsTo(Refcounted)),
            hasType(references(Refcounted)),
            hasType(isSmartPtrToRefCounted())
          ),
          // and which is not this,
          unless(cxxThisExpr()),
          // and which is not a stack smart ptr
          unless(StackSmartPtr),
          // and which is not a method call on a stack smart ptr,
          unless(cxxMemberCallExpr(on(StackSmartPtr))),
          // and which is not calling operator* on a stack smart ptr.
          unless(
            allOf(
              cxxOperatorCallExpr(hasOverloadedOperatorName("*")),
              callExpr(allOf(
                hasAnyArgument(StackSmartPtr),
                argumentCountIs(1)
              ))
            )
          ),
          // and which is not a parameter of the parent function,
          unless(declRefExpr(to(parmVarDecl()))),
          // and which is not a default arg with value nullptr, since those are
          // always safe.
          unless(cxxDefaultArgExpr(isNullDefaultArg())),
          // and which is not a dereference of a parameter of the parent
          // function (including "this"),
          unless(
            unaryOperator(
              unaryDereferenceOperator(),
              hasUnaryOperand(
                anyOf(
                  // If we're doing *someArg, the argument of the dereference is
                  // an ImplicitCastExpr LValueToRValue which has the
                  // DeclRefExpr as an argument.  We could try to match that
                  // explicitly with a custom matcher (none of the built-in
                  // matchers seem to match on the thing being cast for an
                  // implicitCastExpr), but it's simpler to just use
                  // ignoreTrivials to strip off the cast.
                  ignoreTrivials(declRefExpr(to(parmVarDecl()))),
                  cxxThisExpr()
                )
              )
            )
          ),
          // and which is not a MOZ_KnownLive wrapped value.
          unless(
            anyOf(
              MozKnownLiveCall,
              // MOZ_KnownLive applied to a RefPtr or nsCOMPtr just returns that
              // same RefPtr/nsCOMPtr type which causes us to have a conversion
              // operator applied after the MOZ_KnownLive.
              cxxMemberCallExpr(on(allOf(hasType(isSmartPtrToRefCounted()),
                                         MozKnownLiveCall)))
            )
          ),
          expr().bind("invalidArg")));

  auto OptionalInvalidExplicitArg = anyOf(
      // We want to find any argument which is invalid.
      hasAnyArgument(InvalidArg),

      // This makes this matcher optional.
      anything());

  // Please note that the hasCanRunScriptAnnotation() matchers are not present
  // directly in the cxxMemberCallExpr, callExpr and constructExpr matchers
  // because we check that the corresponding functions can run script later in
  // the checker code.
  AstMatcher->addMatcher(
      expr(
          anyOf(
              // We want to match a method call expression,
              cxxMemberCallExpr(
                  // which optionally has an invalid arg,
                  OptionalInvalidExplicitArg,
                  // or which optionally has an invalid implicit this argument,
                  anyOf(
                      // which derefs into an invalid arg,
                      on(cxxOperatorCallExpr(
                          anyOf(hasAnyArgument(InvalidArg), anything()))),
                      // or is an invalid arg.
                      on(InvalidArg),

                      anything()),
                  expr().bind("callExpr")),
              // or a regular call expression,
              callExpr(
                  // which optionally has an invalid arg.
                  OptionalInvalidExplicitArg, expr().bind("callExpr")),
              // or a construct expression,
              cxxConstructExpr(
                  // which optionally has an invalid arg.
                  OptionalInvalidExplicitArg, expr().bind("constructExpr"))),

          anyOf(
              // We want to match the parent function.
              forFunction(functionDecl().bind("nonCanRunScriptParentFunction")),

              // ... optionally.
              anything())),
      this);
}

void CanRunScriptChecker::onStartOfTranslationUnit() {
  IsFuncSetBuilt = false;
  CanRunScriptFuncs.clear();
}

namespace {
/// This class is a callback used internally to match function declarations with
/// the MOZ_CAN_RUN_SCRIPT annotation, adding these functions to the
/// can-run-script function set and making sure the functions they override (if
/// any) also have the annotation.
class FuncSetCallback : public MatchFinder::MatchCallback {
public:
  FuncSetCallback(CanRunScriptChecker& Checker,
                  std::unordered_set<const FunctionDecl *> &FuncSet)
      : CanRunScriptFuncs(FuncSet),
        Checker(Checker) {}

  void run(const MatchFinder::MatchResult &Result) override;

private:
  /// This method checks the methods overriden by the given parameter.
  void checkOverriddenMethods(const CXXMethodDecl *Method);

  std::unordered_set<const FunctionDecl *> &CanRunScriptFuncs;
  CanRunScriptChecker &Checker;
};

void FuncSetCallback::run(const MatchFinder::MatchResult &Result) {
  const FunctionDecl *Func;
  if (auto *Lambda = Result.Nodes.getNodeAs<LambdaExpr>("lambda")) {
    Func = Lambda->getCallOperator();
    if (!Func || !hasCustomAttribute<moz_can_run_script>(Func))
      return;
  } else {
    Func = Result.Nodes.getNodeAs<FunctionDecl>("canRunScriptFunction");
  }

  CanRunScriptFuncs.insert(Func);

  // If this is a method, we check the methods it overrides.
  if (auto *Method = dyn_cast<CXXMethodDecl>(Func)) {
    checkOverriddenMethods(Method);
  }
}

void FuncSetCallback::checkOverriddenMethods(const CXXMethodDecl *Method) {
  for (auto OverriddenMethod : Method->overridden_methods()) {
    if (!hasCustomAttribute<moz_can_run_script>(OverriddenMethod)) {
      const char *ErrorNonCanRunScriptOverridden =
          "functions marked as MOZ_CAN_RUN_SCRIPT cannot override functions "
          "that are not marked MOZ_CAN_RUN_SCRIPT";
      const char* NoteNonCanRunScriptOverridden =
          "overridden function declared here";

      Checker.diag(Method->getLocation(), ErrorNonCanRunScriptOverridden,
                   DiagnosticIDs::Error);
      Checker.diag(OverriddenMethod->getLocation(),
                   NoteNonCanRunScriptOverridden,
                   DiagnosticIDs::Note);
    }
  }
}
} // namespace

void CanRunScriptChecker::buildFuncSet(ASTContext *Context) {
  // We create a match finder.
  MatchFinder Finder;
  // We create the callback which will be called when we find a function with
  // a MOZ_CAN_RUN_SCRIPT annotation.
  FuncSetCallback Callback(*this, CanRunScriptFuncs);
  // We add the matcher to the finder, linking it to our callback.
  Finder.addMatcher(
      functionDecl(hasCanRunScriptAnnotation()).bind("canRunScriptFunction"),
      &Callback);
  Finder.addMatcher(
      lambdaExpr().bind("lambda"),
      &Callback);
  // We start the analysis, given the ASTContext our main checker is in.
  Finder.matchAST(*Context);
}

void CanRunScriptChecker::check(const MatchFinder::MatchResult &Result) {

  // If the set of functions which can run script is not yet built, then build
  // it.
  if (!IsFuncSetBuilt) {
    buildFuncSet(Result.Context);
    IsFuncSetBuilt = true;
  }

  const char *ErrorInvalidArg =
      "arguments must all be strong refs or parent parameters when calling a "
      "function marked as MOZ_CAN_RUN_SCRIPT (including the implicit object "
      "argument)";

  const char *ErrorNonCanRunScriptParent =
      "functions marked as MOZ_CAN_RUN_SCRIPT can only be called from "
      "functions also marked as MOZ_CAN_RUN_SCRIPT";
  const char *NoteNonCanRunScriptParent = "caller function declared here";

  const Expr *InvalidArg = Result.Nodes.getNodeAs<Expr>("invalidArg");

  const CallExpr *Call = Result.Nodes.getNodeAs<CallExpr>("callExpr");
  // If we don't find the FunctionDecl linked to this call or if it's not marked
  // as can-run-script, consider that we didn't find a match.
  if (Call && (!Call->getDirectCallee() ||
               !CanRunScriptFuncs.count(Call->getDirectCallee()))) {
    Call = nullptr;
  }

  const CXXConstructExpr *Construct =
      Result.Nodes.getNodeAs<CXXConstructExpr>("constructExpr");

  // If we don't find the CXXConstructorDecl linked to this construct expression
  // or if it's not marked as can-run-script, consider that we didn't find a
  // match.
  if (Construct && (!Construct->getConstructor() ||
                    !CanRunScriptFuncs.count(Construct->getConstructor()))) {
    Construct = nullptr;
  }

  const FunctionDecl *ParentFunction =
      Result.Nodes.getNodeAs<FunctionDecl>("nonCanRunScriptParentFunction");
  // If the parent function can run script, consider that we didn't find a match
  // because we only care about parent functions which can't run script.
  //
  // In addition, If the parent function is annotated as a
  // CAN_RUN_SCRIPT_BOUNDARY, we don't want to complain about it calling a
  // CAN_RUN_SCRIPT function. This is a mechanism to opt out of the infectious
  // nature of CAN_RUN_SCRIPT which is necessary in some tricky code like
  // Bindings.
  if (ParentFunction &&
      (CanRunScriptFuncs.count(ParentFunction) ||
       hasCustomAttribute<moz_can_run_script_boundary>(ParentFunction))) {
    ParentFunction = nullptr;
  }

  // Get the call range from either the CallExpr or the ConstructExpr.
  SourceRange CallRange;
  if (Call) {
    CallRange = Call->getSourceRange();
  } else if (Construct) {
    CallRange = Construct->getSourceRange();
  } else {
    // If we have neither a Call nor a Construct, we have nothing do to here.
    return;
  }

  // If we have an invalid argument in the call, we emit the diagnostic to
  // signal it.
  if (InvalidArg) {
    diag(InvalidArg->getExprLoc(), ErrorInvalidArg, DiagnosticIDs::Error)
        << CallRange;
  }

  // If the parent function is not marked as MOZ_CAN_RUN_SCRIPT, we emit an
  // error and a not indicating it.
  if (ParentFunction) {
    assert(!hasCustomAttribute<moz_can_run_script>(ParentFunction) &&
           "Matcher missed something");

    diag(CallRange.getBegin(), ErrorNonCanRunScriptParent, DiagnosticIDs::Error)
        << CallRange;

    diag(ParentFunction->getCanonicalDecl()->getLocation(),
	 NoteNonCanRunScriptParent, DiagnosticIDs::Note);
  }
}
