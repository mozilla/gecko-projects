/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "frontend/BinSource.h"

#include "mozilla/ArrayUtils.h"
#include "mozilla/Casting.h"
#include "mozilla/Maybe.h"
#include "mozilla/Move.h"
#include "mozilla/PodOperations.h"
#include "mozilla/Vector.h"

#include "frontend/BinSource-macros.h"
#include "frontend/BinTokenReaderTester.h"
#include "frontend/FullParseHandler.h"
#include "frontend/Parser.h"
#include "frontend/SharedContext.h"

#include "js/Result.h"
#include "vm/RegExpObject.h"

#include "frontend/ParseContext-inl.h"
#include "frontend/ParseNode-inl.h"
#include "vm/JSContext-inl.h"

// # About compliance with EcmaScript
//
// For the moment, this parser implements ES5. Future versions will be extended
// to ES6 and further on.
//
// By design, it does NOT implement Annex B.3.3. If possible, we would like
// to avoid going down that rabbit hole.
//
//
// # About the AST
//
// At this stage of experimentation, the AST specifications change often. This
// version of the parser attempts to implement
// https://gist.github.com/Yoric/2390f0367515c079172be2526349b294
//
//
// # About validating the AST
//
// Normally, this implementation validates all properties of the AST *except* the
// order of fields, which is partially constrained by the AST spec (e.g. in a block,
// field `scope` must appear before field `body`, etc.).
//
//
// # About names and scopes
//
// One of the key objectives of the BinAST syntax is to be able to entirely skip
// parsing inner functions until they are needed. With a purely syntactic AST,
// this is generally impossible, as we would need to walk the AST to find
// lexically-bound/var-bound variables, instances of direct eval, etc.
//
// To achieve this, BinAST files contain scope data, as instances of
// `BinJS:Scope` nodes. Rather than walking the AST to assign bindings
// to scopes, we extract data from the `BinJS:Scope` and check it lazily,
// once we actually need to walk the AST.
//
// WARNING: The current implementation DOES NOT perform the check yet. It
// is therefore unsafe.
//
// # About directives
//
// Currently, directives are ignored and treated as regular strings.
//
// They should be treated lazily (whenever we open a subscope), like bindings.

namespace js {
namespace frontend {

using UsedNamePtr = UsedNameTracker::UsedNameMap::Ptr;

BinASTParserBase::BinASTParserBase(JSContext* cx, LifoAlloc& alloc, UsedNameTracker& usedNames)
  : AutoGCRooter(cx, AutoGCRooter::Tag::BinParser)
  , cx_(cx)
  , alloc_(alloc)
  , traceListHead_(nullptr)
  , usedNames_(usedNames)
  , nodeAlloc_(cx, alloc)
  , keepAtoms_(cx)
  , parseContext_(nullptr)
  , factory_(cx, alloc, nullptr, SourceKind::Binary)
{
    cx->frontendCollectionPool().addActiveCompilation();
    tempPoolMark_ = alloc.mark();
}

BinASTParserBase::~BinASTParserBase()
{
    alloc_.release(tempPoolMark_);

    /*
     * The parser can allocate enormous amounts of memory for large functions.
     * Eagerly free the memory now (which otherwise won't be freed until the
     * next GC) to avoid unnecessary OOMs.
     */
    alloc_.freeAllIfHugeAndUnused();

    cx_->frontendCollectionPool().removeActiveCompilation();
}

// ------------- Toplevel constructions

template<typename Tok> JS::Result<ParseNode*>
BinASTParser<Tok>::parse(GlobalSharedContext* globalsc, const Vector<uint8_t>& data)
{
    return parse(globalsc, data.begin(), data.length());
}

template<typename Tok> JS::Result<ParseNode*>
BinASTParser<Tok>::parse(GlobalSharedContext* globalsc, const uint8_t* start, const size_t length)
{
    auto result = parseAux(globalsc, start, length);
    poison(); // Make sure that the parser is never used again accidentally.
    return result;
}


template<typename Tok> JS::Result<ParseNode*>
BinASTParser<Tok>::parseAux(GlobalSharedContext* globalsc,
                            const uint8_t* start, const size_t length)
{
    MOZ_ASSERT(globalsc);

    tokenizer_.emplace(cx_, start, length);

    BinParseContext globalpc(cx_, this, globalsc, /* newDirectives = */ nullptr);
    if (!globalpc.init()) {
        return cx_->alreadyReportedError();
    }

    ParseContext::VarScope varScope(cx_, &globalpc, usedNames_);
    if (!varScope.init(&globalpc)) {
        return cx_->alreadyReportedError();
    }

    MOZ_TRY(tokenizer_->readHeader());

    ParseNode* result(nullptr);
    MOZ_TRY_VAR(result, parseProgram());

    mozilla::Maybe<GlobalScope::Data*> bindings = NewGlobalScopeData(cx_, varScope, alloc_,
                                                                     parseContext_);
    if (!bindings) {
        return cx_->alreadyReportedError();
    }
    globalsc->bindings = *bindings;

    return result; // Magic conversion to Ok.
}


template<typename Tok> JS::Result<FunctionBox*>
BinASTParser<Tok>::buildFunctionBox(GeneratorKind generatorKind,
    FunctionAsyncKind functionAsyncKind,
    FunctionSyntaxKind syntax,
    ParseNode* name)
{
    RootedAtom atom(cx_);
    if (name) {
        atom = name->name();
    }

    // Allocate the function before walking down the tree.
    RootedFunction fun(cx_);
    BINJS_TRY_VAR(fun, AllocNewFunction(cx_, atom, syntax, generatorKind, functionAsyncKind, nullptr));

    auto* funbox = alloc_.new_<FunctionBox>(cx_, traceListHead_, fun, /* toStringStart = */ 0,
                                            Directives(parseContext_), /* extraWarning = */ false,
                                            generatorKind, functionAsyncKind);
    if (!funbox) {
        return raiseOOM();
    }

    traceListHead_ = funbox;
    funbox->initWithEnclosingParseContext(parseContext_, syntax);
    return funbox;
}

template<typename Tok> JS::Result<ParseNode*>
BinASTParser<Tok>::buildFunction(const size_t start, const BinKind kind, ParseNode* name,
                                 ListNode* params, ParseNode* body, FunctionBox* funbox)
{
    TokenPos pos = tokenizer_->pos(start);

    // Set the argument count for building argument packets. Function.length is handled
    // by setting the appropriate funbox field during argument parsing.
    funbox->function()->setArgCount(params ? uint16_t(params->count()) : 0);

    // ParseNode represents the body as concatenated after the params.
    params->appendWithoutOrderAssumption(body);

    bool isStatement = kind == BinKind::EagerFunctionDeclaration ||
                       kind == BinKind::LazyFunctionDeclaration;

    BINJS_TRY_DECL(result, isStatement
                     ? factory_.newFunctionStatement(pos)
                     : factory_.newFunctionExpression(pos));

    factory_.setFunctionBox(result, funbox);
    factory_.setFunctionFormalParametersAndBody(result, params);

    HandlePropertyName dotThis = cx_->names().dotThis;
    const bool declareThis = hasUsedName(dotThis) ||
                             funbox->bindingsAccessedDynamically() ||
                             funbox->isDerivedClassConstructor();

    if (declareThis) {
        ParseContext::Scope& funScope = parseContext_->functionScope();
        ParseContext::Scope::AddDeclaredNamePtr p = funScope.lookupDeclaredNameForAdd(dotThis);
        MOZ_ASSERT(!p);
        BINJS_TRY(funScope.addDeclaredName(parseContext_, p, dotThis, DeclarationKind::Var,
                                     DeclaredNameInfo::npos));
        funbox->setHasThisBinding();

        // TODO (efaust): This capture will have to come from encoder side for arrow functions.
    }

    // Check all our bindings after maybe adding function This.
    MOZ_TRY(checkFunctionClosedVars());

    BINJS_TRY_DECL(bindings,
             NewFunctionScopeData(cx_, parseContext_->functionScope(),
                                  /* hasParameterExprs = */ false, alloc_, parseContext_));

    funbox->functionScopeBindings().set(*bindings);

    if (funbox->function()->isNamedLambda()) {
        BINJS_TRY_DECL(recursiveBinding,
                 NewLexicalScopeData(cx_, parseContext_->namedLambdaScope(), alloc_, parseContext_));

        funbox->namedLambdaBindings().set(*recursiveBinding);
    }

    return result;
}

template<typename Tok> JS::Result<Ok>
BinASTParser<Tok>::addScopeName(AssertedScopeKind scopeKind, HandleAtom name,
                                ParseContext::Scope* scope, DeclarationKind declKind,
                                bool isCaptured)
{
    auto ptr = scope->lookupDeclaredNameForAdd(name);
    if (ptr) {
        return raiseError("Variable redeclaration");
    }

    BINJS_TRY(scope->addDeclaredName(parseContext_, ptr, name.get(), declKind,
                                     tokenizer_->offset()));

    if (isCaptured) {
        auto declaredPtr = scope->lookupDeclaredName(name);
        MOZ_ASSERT(declaredPtr);
        declaredPtr->value()->setClosedOver();
    }

    return Ok();
}

template<typename Tok> void
BinASTParser<Tok>::captureFunctionName()
{
    MOZ_ASSERT(parseContext_->isFunctionBox());
    MOZ_ASSERT(parseContext_->functionBox()->function()->isNamedLambda());

    RootedAtom funName(cx_, parseContext_->functionBox()->function()->explicitName());
    MOZ_ASSERT(funName);

    auto ptr = parseContext_->namedLambdaScope().lookupDeclaredName(funName);
    MOZ_ASSERT(ptr);
    ptr->value()->setClosedOver();
}

template<typename Tok> JS::Result<Ok>
BinASTParser<Tok>::getDeclaredScope(AssertedScopeKind scopeKind, AssertedDeclaredKind kind,
                                    ParseContext::Scope*& scope, DeclarationKind& declKind)
{
    MOZ_ASSERT(scopeKind == AssertedScopeKind::Block ||
               scopeKind == AssertedScopeKind::Global ||
               scopeKind == AssertedScopeKind::Var);
    switch (kind) {
      case AssertedDeclaredKind::Var:
        if (scopeKind == AssertedScopeKind::Block) {
            return raiseError("AssertedBlockScope cannot contain 'var' binding");
        }
        declKind = DeclarationKind::Var;
        scope = &parseContext_->varScope();
        break;
      case AssertedDeclaredKind::NonConstLexical:
        declKind = DeclarationKind::Let;
        scope = parseContext_->innermostScope();
        break;
      case AssertedDeclaredKind::ConstLexical:
        declKind = DeclarationKind::Const;
        scope = parseContext_->innermostScope();
        break;
    }

    return Ok();
}

template<typename Tok> JS::Result<Ok>
BinASTParser<Tok>::getBoundScope(AssertedScopeKind scopeKind,
                                 ParseContext::Scope*& scope, DeclarationKind& declKind)
{
    MOZ_ASSERT(scopeKind == AssertedScopeKind::Catch ||
               scopeKind == AssertedScopeKind::Parameter);
    switch (scopeKind) {
      case AssertedScopeKind::Catch:
        declKind = DeclarationKind::CatchParameter;
        scope = parseContext_->innermostScope();
        break;
      case AssertedScopeKind::Parameter:
        MOZ_ASSERT(parseContext_->isFunctionBox());
        declKind = DeclarationKind::PositionalFormalParameter;
        scope = &parseContext_->functionScope();
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Unexpected AssertedScopeKind");
        break;
    }

    return Ok();
}

template<typename Tok> JS::Result<Ok>
BinASTParser<Tok>::checkBinding(JSAtom* name)
{
    // Check that the variable appears in the corresponding scope.
    ParseContext::Scope& scope =
        variableDeclarationKind_ == VariableDeclarationKind::Var
        ? parseContext_->varScope()
        : *parseContext_->innermostScope();

    auto ptr = scope.lookupDeclaredName(name->asPropertyName());
    if (!ptr) {
        return raiseMissingVariableInAssertedScope(name);
    }

    return Ok();
}

// Binary AST (revision 8eab67e0c434929a66ff6abe99ff790bca087dda)
// 3.1.5 CheckPositionalParameterIndices.
template<typename Tok> JS::Result<Ok>
BinASTParser<Tok>::checkPositionalParameterIndices(Handle<GCVector<JSAtom*>> positionalParams,
                                                   ListNode* params)
{
    MOZ_ASSERT(positionalParams.get().length() == params->count());

    uint32_t i = 0;
    for (ParseNode* param : params->contents()) {
        if (param->isKind(ParseNodeKind::Assign)) {
            param = param->as<AssignmentNode>().left();
        }
        MOZ_ASSERT(param->isKind(ParseNodeKind::Name) ||
                   param->isKind(ParseNodeKind::Object) ||
                   param->isKind(ParseNodeKind::Array) ||
                   param->isKind(ParseNodeKind::Spread));

        if (JSAtom* name = positionalParams.get()[i]) {
            // Simple or default parameter.
            if (param->isKind(ParseNodeKind::Object) || param->isKind(ParseNodeKind::Array)) {
                return raiseError("AssertedPositionalParameterName: expected positional parameter, got destructuring parameter");
            }
            if (param->isKind(ParseNodeKind::Spread)) {
                return raiseError("AssertedPositionalParameterName: expected positional parameter, got rest parameter");
            }

            if (param->name() != name) {
                return raiseError("AssertedPositionalParameterName: name mismatch");
            }
        } else {
            // Destructuring or rest parameter.
            if (param->isKind(ParseNodeKind::Name)) {
                return raiseError("AssertedParameterName/AssertedRestParameterName: expected destructuring/rest parameter, got positional parameter");
            }
        }

        i++;
    }

    return Ok();
}

// Binary AST (revision 8eab67e0c434929a66ff6abe99ff790bca087dda)
// 3.1.13 CheckFunctionLength.
template<typename Tok> JS::Result<Ok>
BinASTParser<Tok>::checkFunctionLength(uint32_t expectedLength)
{
    if (parseContext_->functionBox()->length != expectedLength) {
        return raiseError("Function length does't match");
    }
    return Ok();
}

template<typename Tok> JS::Result<Ok>
BinASTParser<Tok>::checkClosedVars(ParseContext::Scope& scope)
{
    for (ParseContext::Scope::BindingIter bi = scope.bindings(parseContext_); bi; bi++) {
        if (UsedNamePtr p = usedNames_.lookup(bi.name())) {
            bool closedOver;
            p->value().noteBoundInScope(parseContext_->scriptId(), scope.id(), &closedOver);
            if (closedOver && !bi.closedOver()) {
                return raiseInvalidClosedVar(bi.name());
            }
        }
    }

    return Ok();
}

template<typename Tok> JS::Result<Ok>
BinASTParser<Tok>::checkFunctionClosedVars()
{
    MOZ_ASSERT(parseContext_->isFunctionBox());

    MOZ_TRY(checkClosedVars(*parseContext_->innermostScope()));
    MOZ_TRY(checkClosedVars(parseContext_->functionScope()));
    if (parseContext_->functionBox()->function()->isNamedLambda()) {
        MOZ_TRY(checkClosedVars(parseContext_->namedLambdaScope()));
    }

    return Ok();
}

template<typename Tok> JS::Result<ParseNode*>
BinASTParser<Tok>::appendDirectivesToBody(ListNode* body, ListNode* directives)
{
    if (!directives) {
        return body;
    }

    ParseNode* result = body;
    if (!directives->empty()) {
        // Convert directive list to a list of strings.
        auto pos = directives->head()->pn_pos;
        BINJS_TRY_DECL(prefix, factory_.newStatementList(pos));
        for (ParseNode* iter : directives->contents()) {
            BINJS_TRY_DECL(statement, factory_.newExprStatement(iter, iter->pn_pos.end));
            prefix->appendWithoutOrderAssumption(statement);
        }

        // Prepend to the body.
        ParseNode* iter = body->head();
        while (iter) {
            ParseNode* next = iter->pn_next;
            prefix->appendWithoutOrderAssumption(iter);
            iter = next;
        }
        prefix->setKind(body->getKind());
        prefix->setOp(body->getOp());
        result = prefix;
    }

    return result;
}

template<typename Tok> mozilla::GenericErrorResult<JS::Error&>
BinASTParser<Tok>::raiseInvalidClosedVar(JSAtom* name)
{
    return raiseError("Captured variable was not declared as captured");
}

template<typename Tok> mozilla::GenericErrorResult<JS::Error&>
BinASTParser<Tok>::raiseMissingVariableInAssertedScope(JSAtom* name)
{
    // For the moment, we don't trust inputs sufficiently to put the name
    // in an error message.
    return raiseError("Missing variable in AssertedScope");
}

template<typename Tok> mozilla::GenericErrorResult<JS::Error&>
BinASTParser<Tok>::raiseMissingDirectEvalInAssertedScope()
{
    return raiseError("Direct call to `eval` was not declared in AssertedScope");
}

template<typename Tok> mozilla::GenericErrorResult<JS::Error&>
BinASTParser<Tok>::raiseInvalidKind(const char* superKind, const BinKind kind)
{
    Sprinter out(cx_);
    BINJS_TRY(out.init());
    BINJS_TRY(out.printf("In %s, invalid kind %s", superKind, describeBinKind(kind)));
    return raiseError(out.string());
}

template<typename Tok> mozilla::GenericErrorResult<JS::Error&>
BinASTParser<Tok>::raiseInvalidVariant(const char* kind, const BinVariant value)
{
    Sprinter out(cx_);
    BINJS_TRY(out.init());
    BINJS_TRY(out.printf("In %s, invalid variant '%s'", kind, describeBinVariant(value)));

    return raiseError(out.string());
}

template<typename Tok> mozilla::GenericErrorResult<JS::Error&>
BinASTParser<Tok>::raiseMissingField(const char* kind, const BinField field)
{
    Sprinter out(cx_);
    BINJS_TRY(out.init());
    BINJS_TRY(out.printf("In %s, missing field '%s'", kind, describeBinField(field)));

    return raiseError(out.string());
}

template<typename Tok> mozilla::GenericErrorResult<JS::Error&>
BinASTParser<Tok>::raiseEmpty(const char* description)
{
    Sprinter out(cx_);
    BINJS_TRY(out.init());
    BINJS_TRY(out.printf("Empty %s", description));

    return raiseError(out.string());
}

template<typename Tok> mozilla::GenericErrorResult<JS::Error&>
BinASTParser<Tok>::raiseOOM()
{
    return tokenizer_->raiseOOM();
}

template<typename Tok> mozilla::GenericErrorResult<JS::Error&>
BinASTParser<Tok>::raiseError(BinKind kind, const char* description)
{
    Sprinter out(cx_);
    BINJS_TRY(out.init());
    BINJS_TRY(out.printf("In %s, ", description));
    return tokenizer_->raiseError(out.string());
}

template<typename Tok> mozilla::GenericErrorResult<JS::Error&>
BinASTParser<Tok>::raiseError(const char* description)
{
    return tokenizer_->raiseError(description);
}

template<typename Tok> void
BinASTParser<Tok>::poison()
{
    tokenizer_.reset();
}

template<typename Tok> void
BinASTParser<Tok>::reportErrorNoOffsetVA(unsigned errorNumber, va_list args)
{
    ErrorMetadata metadata;
    metadata.filename = getFilename();
    metadata.lineNumber = 0;
    metadata.columnNumber = offset();
    metadata.isMuted = options().mutedErrors();
    ReportCompileError(cx_, std::move(metadata), nullptr, JSREPORT_ERROR, errorNumber, args);
}

template<typename Tok> void
BinASTParser<Tok>::errorAtVA(uint32_t offset, unsigned errorNumber, va_list* args)
{
    ErrorMetadata metadata;
    metadata.filename = getFilename();
    metadata.lineNumber = 0;
    metadata.columnNumber = offset;
    metadata.isMuted = options().mutedErrors();
    ReportCompileError(cx_, std::move(metadata), nullptr, JSREPORT_ERROR, errorNumber, *args);
}

template<typename Tok> bool
BinASTParser<Tok>::reportExtraWarningErrorNumberVA(UniquePtr<JSErrorNotes> notes, uint32_t offset, unsigned errorNumber, va_list* args)
{
    if (!options().extraWarningsOption) {
        return true;
    }

    ErrorMetadata metadata;
    metadata.filename = getFilename();
    metadata.lineNumber = 0;
    metadata.columnNumber = offset;
    metadata.isMuted = options().mutedErrors();

    if (options().werrorOption) {
        ReportCompileError(cx_, std::move(metadata), std::move(notes), JSREPORT_STRICT, errorNumber, *args);
        return false;
    }

    return ReportCompileWarning(cx_, std::move(metadata), std::move(notes), JSREPORT_STRICT | JSREPORT_WARNING, errorNumber, *args);
}

bool
BinASTParserBase::hasUsedName(HandlePropertyName name)
{
    if (UsedNamePtr p = usedNames_.lookup(name)) {
        return p->value().isUsedInScript(parseContext_->scriptId());
    }

    return false;
}

void
TraceBinParser(JSTracer* trc, JS::AutoGCRooter* parser)
{
    static_cast<BinASTParserBase*>(parser)->trace(trc);
}


// Force class instantiation.
// This ensures that the symbols are built, without having to export all our
// code (and its baggage of #include and macros) in the header.
template class BinASTParser<BinTokenReaderMultipart>;
template class BinASTParser<BinTokenReaderTester>;

} // namespace frontend
} // namespace js

