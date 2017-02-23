mod helpers;
mod struct_layout;

use self::helpers::{BlobTyBuilder, attributes};
use self::struct_layout::{align_to, bytes_from_bits};
use self::struct_layout::{bytes_from_bits_pow2, StructLayoutTracker};
use aster;

use ir::annotations::FieldAccessorKind;
use ir::comp::{Base, CompInfo, CompKind, Field, Method, MethodKind};
use ir::context::{BindgenContext, ItemId};
use ir::derive::{CanDeriveCopy, CanDeriveDebug, CanDeriveDefault};
use ir::enum_ty::{Enum, EnumVariant, EnumVariantValue};
use ir::function::{Function, FunctionSig};
use ir::int::IntKind;
use ir::item::{Item, ItemAncestors, ItemCanonicalName, ItemCanonicalPath,
               ItemSet};
use ir::item_kind::ItemKind;
use ir::layout::Layout;
use ir::module::Module;
use ir::objc::ObjCInterface;
use ir::ty::{Type, TypeKind};
use ir::var::Var;

use std::borrow::Cow;
use std::cell::Cell;
use std::cmp;
use std::collections::{HashSet, VecDeque};
use std::collections::hash_map::{Entry, HashMap};
use std::fmt::Write;
use std::mem;
use std::ops;
use syntax::abi::Abi;
use syntax::ast;
use syntax::codemap::{Span, respan};
use syntax::ptr::P;

fn root_import_depth(ctx: &BindgenContext, item: &Item) -> usize {
    if !ctx.options().enable_cxx_namespaces {
        return 0;
    }

    item.ancestors(ctx)
        .filter(|id| ctx.resolve_item(*id).is_module())
        .fold(1, |i, _| i + 1)
}

fn top_level_path(ctx: &BindgenContext, item: &Item) -> Vec<ast::Ident> {
    let mut path = vec![ctx.rust_ident_raw("self")];

    if ctx.options().enable_cxx_namespaces {
        let super_ = ctx.rust_ident_raw("super");

        for _ in 0..root_import_depth(ctx, item) {
            path.push(super_.clone());
        }
    }

    path
}

fn root_import(ctx: &BindgenContext, module: &Item) -> P<ast::Item> {
    assert!(ctx.options().enable_cxx_namespaces, "Somebody messed it up");
    assert!(module.is_module());

    let mut path = top_level_path(ctx, module);

    let root = ctx.root_module().canonical_name(ctx);
    let root_ident = ctx.rust_ident(&root);
    path.push(root_ident);

    let use_root = aster::AstBuilder::new()
        .item()
        .use_()
        .ids(path)
        .build()
        .build();

    quote_item!(ctx.ext_cx(), #[allow(unused_imports)] $use_root).unwrap()
}

struct CodegenResult<'a> {
    items: Vec<P<ast::Item>>,

    /// A monotonic counter used to add stable unique id's to stuff that doesn't
    /// need to be referenced by anything.
    codegen_id: &'a Cell<usize>,

    /// Whether an union has been generated at least once.
    saw_union: bool,

    /// Whether an incomplete array has been generated at least once.
    saw_incomplete_array: bool,

    /// Whether Objective C types have been seen at least once.
    saw_objc: bool,

    items_seen: HashSet<ItemId>,
    /// The set of generated function/var names, needed because in C/C++ is
    /// legal to do something like:
    ///
    /// ```c++
    /// extern "C" {
    ///   void foo();
    ///   extern int bar;
    /// }
    ///
    /// extern "C" {
    ///   void foo();
    ///   extern int bar;
    /// }
    /// ```
    ///
    /// Being these two different declarations.
    functions_seen: HashSet<String>,
    vars_seen: HashSet<String>,

    /// Used for making bindings to overloaded functions. Maps from a canonical
    /// function name to the number of overloads we have already codegen'd for
    /// that name. This lets us give each overload a unique suffix.
    overload_counters: HashMap<String, u32>,
}

impl<'a> CodegenResult<'a> {
    fn new(codegen_id: &'a Cell<usize>) -> Self {
        CodegenResult {
            items: vec![],
            saw_union: false,
            saw_incomplete_array: false,
            saw_objc: false,
            codegen_id: codegen_id,
            items_seen: Default::default(),
            functions_seen: Default::default(),
            vars_seen: Default::default(),
            overload_counters: Default::default(),
        }
    }

    fn next_id(&mut self) -> usize {
        self.codegen_id.set(self.codegen_id.get() + 1);
        self.codegen_id.get()
    }

    fn saw_union(&mut self) {
        self.saw_union = true;
    }

    fn saw_incomplete_array(&mut self) {
        self.saw_incomplete_array = true;
    }

    fn saw_objc(&mut self) {
        self.saw_objc = true;
    }

    fn seen(&self, item: ItemId) -> bool {
        self.items_seen.contains(&item)
    }

    fn set_seen(&mut self, item: ItemId) {
        self.items_seen.insert(item);
    }

    fn seen_function(&self, name: &str) -> bool {
        self.functions_seen.contains(name)
    }

    fn saw_function(&mut self, name: &str) {
        self.functions_seen.insert(name.into());
    }

    /// Get the overload number for the given function name. Increments the
    /// counter internally so the next time we ask for the overload for this
    /// name, we get the incremented value, and so on.
    fn overload_number(&mut self, name: &str) -> u32 {
        let mut counter =
            self.overload_counters.entry(name.into()).or_insert(0);
        let number = *counter;
        *counter += 1;
        number
    }

    fn seen_var(&self, name: &str) -> bool {
        self.vars_seen.contains(name)
    }

    fn saw_var(&mut self, name: &str) {
        self.vars_seen.insert(name.into());
    }

    fn inner<F>(&mut self, cb: F) -> Vec<P<ast::Item>>
        where F: FnOnce(&mut Self),
    {
        let mut new = Self::new(self.codegen_id);

        cb(&mut new);

        self.saw_union |= new.saw_union;
        self.saw_incomplete_array |= new.saw_incomplete_array;
        self.saw_objc |= new.saw_objc;

        new.items
    }
}

impl<'a> ops::Deref for CodegenResult<'a> {
    type Target = Vec<P<ast::Item>>;

    fn deref(&self) -> &Self::Target {
        &self.items
    }
}

impl<'a> ops::DerefMut for CodegenResult<'a> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.items
    }
}

struct ForeignModBuilder {
    inner: ast::ForeignMod,
}

impl ForeignModBuilder {
    fn new(abi: Abi) -> Self {
        ForeignModBuilder {
            inner: ast::ForeignMod {
                abi: abi,
                items: vec![],
            },
        }
    }

    fn with_foreign_item(mut self, item: ast::ForeignItem) -> Self {
        self.inner.items.push(item);
        self
    }

    #[allow(dead_code)]
    fn with_foreign_items<I>(mut self, items: I) -> Self
        where I: IntoIterator<Item = ast::ForeignItem>,
    {
        self.inner.items.extend(items.into_iter());
        self
    }

    fn build(self, ctx: &BindgenContext) -> P<ast::Item> {
        use syntax::codemap::DUMMY_SP;
        P(ast::Item {
            ident: ctx.rust_ident(""),
            id: ast::DUMMY_NODE_ID,
            node: ast::ItemKind::ForeignMod(self.inner),
            vis: ast::Visibility::Public,
            attrs: vec![],
            span: DUMMY_SP,
        })
    }
}

/// A trait to convert a rust type into a pointer, optionally const, to the same
/// type.
///
/// This is done due to aster's lack of pointer builder, I guess I should PR
/// there.
trait ToPtr {
    fn to_ptr(self, is_const: bool, span: Span) -> P<ast::Ty>;
}

impl ToPtr for P<ast::Ty> {
    fn to_ptr(self, is_const: bool, span: Span) -> Self {
        let ty = ast::TyKind::Ptr(ast::MutTy {
            ty: self,
            mutbl: if is_const {
                ast::Mutability::Immutable
            } else {
                ast::Mutability::Mutable
            },
        });
        P(ast::Ty {
            id: ast::DUMMY_NODE_ID,
            node: ty,
            span: span,
        })
    }
}

trait CodeGenerator {
    /// Extra information from the caller.
    type Extra;

    fn codegen<'a>(&self,
                   ctx: &BindgenContext,
                   result: &mut CodegenResult<'a>,
                   whitelisted_items: &ItemSet,
                   extra: &Self::Extra);
}

impl CodeGenerator for Item {
    type Extra = ();

    fn codegen<'a>(&self,
                   ctx: &BindgenContext,
                   result: &mut CodegenResult<'a>,
                   whitelisted_items: &ItemSet,
                   _extra: &()) {
        if self.is_hidden(ctx) || result.seen(self.id()) {
            debug!("<Item as CodeGenerator>::codegen: Ignoring hidden or seen: \
                   self = {:?}",
                   self);
            return;
        }

        debug!("<Item as CodeGenerator>::codegen: self = {:?}", self);
        if !whitelisted_items.contains(&self.id()) {
            // TODO(emilio, #453): Figure out what to do when this happens
            // legitimately, we could track the opaque stuff and disable the
            // assertion there I guess.
            error!("Found non-whitelisted item in code generation: {:?}", self);
        }

        result.set_seen(self.id());

        match *self.kind() {
            ItemKind::Module(ref module) => {
                module.codegen(ctx, result, whitelisted_items, self);
            }
            ItemKind::Function(ref fun) => {
                if ctx.options().codegen_config.functions {
                    fun.codegen(ctx, result, whitelisted_items, self);
                }
            }
            ItemKind::Var(ref var) => {
                if ctx.options().codegen_config.vars {
                    var.codegen(ctx, result, whitelisted_items, self);
                }
            }
            ItemKind::Type(ref ty) => {
                if ctx.options().codegen_config.types {
                    ty.codegen(ctx, result, whitelisted_items, self);
                }
            }
        }
    }
}

impl CodeGenerator for Module {
    type Extra = Item;

    fn codegen<'a>(&self,
                   ctx: &BindgenContext,
                   result: &mut CodegenResult<'a>,
                   whitelisted_items: &ItemSet,
                   item: &Item) {
        debug!("<Module as CodeGenerator>::codegen: item = {:?}", item);

        let codegen_self = |result: &mut CodegenResult,
                            found_any: &mut bool| {
            for child in self.children() {
                if whitelisted_items.contains(child) {
                    *found_any = true;
                    ctx.resolve_item(*child)
                        .codegen(ctx, result, whitelisted_items, &());
                }
            }

            if item.id() == ctx.root_module() {
                if result.saw_union && !ctx.options().unstable_rust {
                    utils::prepend_union_types(ctx, &mut *result);
                }
                if result.saw_incomplete_array {
                    utils::prepend_incomplete_array_types(ctx, &mut *result);
                }
                if ctx.need_bindegen_complex_type() {
                    utils::prepend_complex_type(ctx, &mut *result);
                }
                if result.saw_objc {
                    utils::prepend_objc_header(ctx, &mut *result);
                }
            }
        };

        if !ctx.options().enable_cxx_namespaces ||
           (self.is_inline() && !ctx.options().conservative_inline_namespaces) {
            codegen_self(result, &mut false);
            return;
        }

        let mut found_any = false;
        let inner_items = result.inner(|result| {
            result.push(root_import(ctx, item));
            codegen_self(result, &mut found_any);
        });

        // Don't bother creating an empty module.
        if !found_any {
            return;
        }

        let module = ast::ItemKind::Mod(ast::Mod {
            inner: ctx.span(),
            items: inner_items,
        });

        let name = item.canonical_name(ctx);
        let item = aster::AstBuilder::new()
            .item()
            .pub_()
            .build_item_kind(name, module);

        result.push(item);
    }
}

impl CodeGenerator for Var {
    type Extra = Item;
    fn codegen<'a>(&self,
                   ctx: &BindgenContext,
                   result: &mut CodegenResult<'a>,
                   _whitelisted_items: &ItemSet,
                   item: &Item) {
        use ir::var::VarType;
        debug!("<Var as CodeGenerator>::codegen: item = {:?}", item);

        let canonical_name = item.canonical_name(ctx);

        if result.seen_var(&canonical_name) {
            return;
        }
        result.saw_var(&canonical_name);

        let ty = self.ty().to_rust_ty(ctx);

        if let Some(val) = self.val() {
            let const_item = aster::AstBuilder::new()
                .item()
                .pub_()
                .const_(canonical_name)
                .expr();
            let item = match *val {
                VarType::Bool(val) => {
                    const_item.build(helpers::ast_ty::bool_expr(val))
                        .build(ty)
                }
                VarType::Int(val) => {
                    const_item.build(helpers::ast_ty::int_expr(val)).build(ty)
                }
                VarType::String(ref bytes) => {
                    // Account the trailing zero.
                    //
                    // TODO: Here we ignore the type we just made up, probably
                    // we should refactor how the variable type and ty id work.
                    let len = bytes.len() + 1;
                    let ty = quote_ty!(ctx.ext_cx(), [u8; $len]);

                    match String::from_utf8(bytes.clone()) {
                        Ok(string) => {
                            const_item.build(helpers::ast_ty::cstr_expr(string))
                                .build(quote_ty!(ctx.ext_cx(), &'static $ty))
                        }
                        Err(..) => {
                            const_item
                                .build(helpers::ast_ty::byte_array_expr(bytes))
                                .build(ty)
                        }
                    }
                }
                VarType::Float(f) => {
                    const_item.build(helpers::ast_ty::float_expr(f))
                        .build(ty)
                }
                VarType::Char(c) => {
                    const_item
                        .build(aster::AstBuilder::new().expr().lit().byte(c))
                        .build(ty)
                }
            };

            result.push(item);
        } else {
            let mut attrs = vec![];
            if let Some(mangled) = self.mangled_name() {
                attrs.push(attributes::link_name(mangled));
            } else if canonical_name != self.name() {
                attrs.push(attributes::link_name(self.name()));
            }

            let item = ast::ForeignItem {
                ident: ctx.rust_ident_raw(&canonical_name),
                attrs: attrs,
                node: ast::ForeignItemKind::Static(ty, !self.is_const()),
                id: ast::DUMMY_NODE_ID,
                span: ctx.span(),
                vis: ast::Visibility::Public,
            };

            let item = ForeignModBuilder::new(Abi::C)
                .with_foreign_item(item)
                .build(ctx);
            result.push(item);
        }
    }
}

impl CodeGenerator for Type {
    type Extra = Item;

    fn codegen<'a>(&self,
                   ctx: &BindgenContext,
                   result: &mut CodegenResult<'a>,
                   whitelisted_items: &ItemSet,
                   item: &Item) {
        debug!("<Type as CodeGenerator>::codegen: item = {:?}", item);

        match *self.kind() {
            TypeKind::Void |
            TypeKind::NullPtr |
            TypeKind::Int(..) |
            TypeKind::Float(..) |
            TypeKind::Complex(..) |
            TypeKind::Array(..) |
            TypeKind::Pointer(..) |
            TypeKind::BlockPointer |
            TypeKind::Reference(..) |
            TypeKind::TemplateInstantiation(..) |
            TypeKind::Function(..) |
            TypeKind::ResolvedTypeRef(..) |
            TypeKind::Named => {
                // These items don't need code generation, they only need to be
                // converted to rust types in fields, arguments, and such.
                return;
            }
            TypeKind::Comp(ref ci) => {
                ci.codegen(ctx, result, whitelisted_items, item)
            }
            // NB: The code below will pick the correct
            // applicable_template_args.
            TypeKind::TemplateAlias(inner, _) |
            TypeKind::Alias(inner) => {
                let inner_item = ctx.resolve_item(inner);
                let name = item.canonical_name(ctx);

                // Try to catch the common pattern:
                //
                // typedef struct foo { ... } foo;
                //
                // here.
                //
                if inner_item.canonical_name(ctx) == name {
                    return;
                }

                // If this is a known named type, disallow generating anything
                // for it too.
                let spelling = self.name().expect("Unnamed alias?");
                if utils::type_from_named(ctx, spelling, inner).is_some() {
                    return;
                }

                let mut applicable_template_args =
                    item.applicable_template_args(ctx);
                let inner_rust_type = if item.is_opaque(ctx) {
                    applicable_template_args.clear();
                    // Pray if there's no layout.
                    let layout = self.layout(ctx).unwrap_or_else(Layout::zero);
                    BlobTyBuilder::new(layout).build()
                } else {
                    inner_item.to_rust_ty(ctx)
                };

                {
                    // FIXME(emilio): This is a workaround to avoid generating
                    // incorrect type aliases because of types that we haven't
                    // been able to resolve (because, eg, they depend on a
                    // template parameter).
                    //
                    // It's kind of a shame not generating them even when they
                    // could be referenced, but we already do the same for items
                    // with invalid template parameters, and at least this way
                    // they can be replaced, instead of generating plain invalid
                    // code.
                    let inner_canon_type = inner_item.expect_type()
                        .canonical_type(ctx);
                    if inner_canon_type.is_invalid_named_type() {
                        warn!("Item contained invalid named type, skipping: \
                              {:?}, {:?}",
                              item,
                              inner_item);
                        return;
                    }
                }

                let rust_name = ctx.rust_ident(&name);
                let mut typedef = aster::AstBuilder::new().item().pub_();

                if ctx.options().generate_comments {
                    if let Some(comment) = item.comment() {
                        typedef = typedef.attr().doc(comment);
                    }
                }

                // We prefer using `pub use` over `pub type` because of:
                // https://github.com/rust-lang/rust/issues/26264
                let simple_enum_path = match inner_rust_type.node {
                    ast::TyKind::Path(None, ref p) => {
                        if applicable_template_args.is_empty() &&
                           inner_item.expect_type()
                            .canonical_type(ctx)
                            .is_enum() &&
                           p.segments.iter().all(|p| p.parameters.is_none()) {
                            Some(p.clone())
                        } else {
                            None
                        }
                    }
                    _ => None,
                };

                let typedef = if let Some(mut p) = simple_enum_path {
                    for ident in top_level_path(ctx, item).into_iter().rev() {
                        p.segments.insert(0,
                                          ast::PathSegment {
                                              identifier: ident,
                                              parameters: None,
                                          });
                    }
                    typedef.use_().build(p).as_(rust_name)
                } else {
                    let mut generics = typedef.type_(rust_name).generics();
                    for template_arg in applicable_template_args.iter() {
                        let template_arg = ctx.resolve_type(*template_arg);
                        if template_arg.is_named() {
                            if template_arg.is_invalid_named_type() {
                                warn!("Item contained invalid template \
                                       parameter: {:?}",
                                      item);
                                return;
                            }
                            generics =
                                generics.ty_param_id(template_arg.name().unwrap());
                        }
                    }
                    generics.build().build_ty(inner_rust_type)
                };
                result.push(typedef)
            }
            TypeKind::Enum(ref ei) => {
                ei.codegen(ctx, result, whitelisted_items, item)
            }
            TypeKind::ObjCInterface(ref interface) => {
                interface.codegen(ctx, result, whitelisted_items, item)
            }
            ref u @ TypeKind::UnresolvedTypeRef(..) => {
                unreachable!("Should have been resolved after parsing {:?}!", u)
            }
        }
    }
}

struct Vtable<'a> {
    item_id: ItemId,
    #[allow(dead_code)]
    methods: &'a [Method],
    #[allow(dead_code)]
    base_classes: &'a [Base],
}

impl<'a> Vtable<'a> {
    fn new(item_id: ItemId,
           methods: &'a [Method],
           base_classes: &'a [Base])
           -> Self {
        Vtable {
            item_id: item_id,
            methods: methods,
            base_classes: base_classes,
        }
    }
}

impl<'a> CodeGenerator for Vtable<'a> {
    type Extra = Item;

    fn codegen<'b>(&self,
                   ctx: &BindgenContext,
                   result: &mut CodegenResult<'b>,
                   _whitelisted_items: &ItemSet,
                   item: &Item) {
        assert_eq!(item.id(), self.item_id);
        // For now, generate an empty struct, later we should generate function
        // pointers and whatnot.
        let mut attributes = vec![attributes::repr("C")];

        if ctx.options().derive_default {
            attributes.push(attributes::derives(&["Default"]))
        }

        let vtable = aster::AstBuilder::new()
            .item()
            .pub_()
            .with_attrs(attributes)
            .struct_(self.canonical_name(ctx))
            .build();
        result.push(vtable);
    }
}

impl<'a> ItemCanonicalName for Vtable<'a> {
    fn canonical_name(&self, ctx: &BindgenContext) -> String {
        format!("{}__bindgen_vtable", self.item_id.canonical_name(ctx))
    }
}

impl<'a> ItemToRustTy for Vtable<'a> {
    fn to_rust_ty(&self, ctx: &BindgenContext) -> P<ast::Ty> {
        aster::ty::TyBuilder::new().id(self.canonical_name(ctx))
    }
}

struct Bitfield<'a> {
    index: &'a mut usize,
    fields: Vec<&'a Field>,
}

impl<'a> Bitfield<'a> {
    fn new(index: &'a mut usize, fields: Vec<&'a Field>) -> Self {
        Bitfield {
            index: index,
            fields: fields,
        }
    }

    fn codegen_fields(self,
                      ctx: &BindgenContext,
                      fields: &mut Vec<ast::StructField>,
                      _methods: &mut Vec<ast::ImplItem>)
                      -> Layout {
        use aster::struct_field::StructFieldBuilder;

        // NOTE: What follows is reverse-engineered from LLVM's
        // lib/AST/RecordLayoutBuilder.cpp
        //
        // FIXME(emilio): There are some differences between Microsoft and the
        // Itanium ABI, but we'll ignore those and stick to Itanium for now.
        //
        // Also, we need to handle packed bitfields and stuff.
        // TODO(emilio): Take into account C++'s wide bitfields, and
        // packing, sigh.
        let mut total_size_in_bits = 0;
        let mut max_align = 0;
        let mut unfilled_bits_in_last_unit = 0;
        let mut field_size_in_bits = 0;
        *self.index += 1;
        let mut last_field_name = format!("_bitfield_{}", self.index);
        let mut last_field_align = 0;

        for field in self.fields {
            let width = field.bitfield().unwrap();
            let field_item = ctx.resolve_item(field.ty());
            let field_ty_layout = field_item.kind()
                .expect_type()
                .layout(ctx)
                .expect("Bitfield without layout? Gah!");

            let field_align = field_ty_layout.align;

            if field_size_in_bits != 0 &&
                (width == 0 || width as usize > unfilled_bits_in_last_unit) {
                field_size_in_bits = align_to(field_size_in_bits, field_align);
                // Push the new field.
                let ty =
                    BlobTyBuilder::new(Layout::new(bytes_from_bits_pow2(field_size_in_bits),
                                                   bytes_from_bits_pow2(last_field_align)))
                        .build();

                let field = StructFieldBuilder::named(&last_field_name)
                    .pub_()
                    .build_ty(ty);
                fields.push(field);

                // TODO(emilio): dedup this.
                *self.index += 1;
                last_field_name = format!("_bitfield_{}", self.index);

                // Now reset the size and the rest of stuff.
                // unfilled_bits_in_last_unit = 0;
                field_size_in_bits = 0;
                last_field_align = 0;
            }

            // TODO(emilio): Create the accessors. Problem here is that we still
            // don't know which one is going to be the final alignment of the
            // bitfield, and whether we have to index in it. Thus, we don't know
            // which integer type do we need.
            //
            // We could push them to a Vec or something, but given how buggy
            // they where maybe it's not a great idea?
            field_size_in_bits += width as usize;
            total_size_in_bits += width as usize;


            let data_size = align_to(field_size_in_bits, field_align * 8);

            max_align = cmp::max(max_align, field_align);

            // NB: The width here is completely, absolutely intentional.
            last_field_align = cmp::max(last_field_align, width as usize);

            unfilled_bits_in_last_unit = data_size - field_size_in_bits;
        }

        if field_size_in_bits != 0 {
            // Push the last field.
            let ty =
                BlobTyBuilder::new(Layout::new(bytes_from_bits_pow2(field_size_in_bits),
                                               bytes_from_bits_pow2(last_field_align)))
                    .build();

            let field = StructFieldBuilder::named(&last_field_name)
                .pub_()
                .build_ty(ty);
            fields.push(field);
        }

        Layout::new(bytes_from_bits(total_size_in_bits), max_align)
    }
}

impl CodeGenerator for CompInfo {
    type Extra = Item;

    fn codegen<'a>(&self,
                   ctx: &BindgenContext,
                   result: &mut CodegenResult<'a>,
                   whitelisted_items: &ItemSet,
                   item: &Item) {
        use aster::struct_field::StructFieldBuilder;

        debug!("<CompInfo as CodeGenerator>::codegen: item = {:?}", item);

        // Don't output classes with template parameters that aren't types, and
        // also don't output template specializations, neither total or partial.
        if self.has_non_type_template_params() {
            return;
        }

        let applicable_template_args = item.applicable_template_args(ctx);

        // generate tuple struct if struct or union is a forward declaration,
        // skip for now if template parameters are needed.
        if self.is_forward_declaration() &&
           applicable_template_args.is_empty() {
            let struct_name = item.canonical_name(ctx);
            let struct_name = ctx.rust_ident_raw(&struct_name);
            let tuple_struct = quote_item!(ctx.ext_cx(),
                                           #[repr(C)]
                                           #[derive(Debug, Copy, Clone)]
                                           pub struct $struct_name([u8; 0]);
                                          )
                .unwrap();
            result.push(tuple_struct);
            return;
        }

        if self.is_template_specialization() {
            let layout = item.kind().expect_type().layout(ctx);

            if let Some(layout) = layout {
                let fn_name = format!("__bindgen_test_layout_template_{}",
                                      result.next_id());
                let fn_name = ctx.rust_ident_raw(&fn_name);
                let ident = item.to_rust_ty(ctx);
                let prefix = ctx.trait_prefix();
                let size_of_expr = quote_expr!(ctx.ext_cx(),
                                ::$prefix::mem::size_of::<$ident>());
                let align_of_expr = quote_expr!(ctx.ext_cx(),
                                ::$prefix::mem::align_of::<$ident>());
                let size = layout.size;
                let align = layout.align;
                let item = quote_item!(ctx.ext_cx(),
                                       #[test]
                                       fn $fn_name() {
                                           assert_eq!($size_of_expr, $size,
                                                      concat!("Size of template specialization: ", stringify!($ident)));
                                           assert_eq!($align_of_expr, $align,
                                                      concat!("Alignment of template specialization: ", stringify!($ident)));
                                       })
                    .unwrap();
                result.push(item);
            }
            return;
        }

        let mut attributes = vec![];
        let mut needs_clone_impl = false;
        let mut needs_default_impl = false;
        if ctx.options().generate_comments {
            if let Some(comment) = item.comment() {
                attributes.push(attributes::doc(comment));
            }
        }
        if self.packed() {
            attributes.push(attributes::repr_list(&["C", "packed"]));
        } else {
            attributes.push(attributes::repr("C"));
        }

        let is_union = self.kind() == CompKind::Union;
        let mut derives = vec![];
        if item.can_derive_debug(ctx, ()) {
            derives.push("Debug");
        }

        if item.can_derive_default(ctx, ()) {
            derives.push("Default");
        } else {
            needs_default_impl = ctx.options().derive_default;
        }

        if item.can_derive_copy(ctx, ()) &&
           !item.annotations().disallow_copy() {
            derives.push("Copy");
            if !applicable_template_args.is_empty() {
                // FIXME: This requires extra logic if you have a big array in a
                // templated struct. The reason for this is that the magic:
                //     fn clone(&self) -> Self { *self }
                // doesn't work for templates.
                //
                // It's not hard to fix though.
                derives.push("Clone");
            } else {
                needs_clone_impl = true;
            }
        }

        if !derives.is_empty() {
            attributes.push(attributes::derives(&derives))
        }

        let mut template_args_used =
            vec![false; applicable_template_args.len()];
        let canonical_name = item.canonical_name(ctx);
        let builder = if is_union && ctx.options().unstable_rust {
            aster::AstBuilder::new()
                .item()
                .pub_()
                .with_attrs(attributes)
                .union_(&canonical_name)
        } else {
            aster::AstBuilder::new()
                .item()
                .pub_()
                .with_attrs(attributes)
                .struct_(&canonical_name)
        };

        // Generate the vtable from the method list if appropriate.
        //
        // TODO: I don't know how this could play with virtual methods that are
        // not in the list of methods found by us, we'll see. Also, could the
        // order of the vtable pointers vary?
        //
        // FIXME: Once we generate proper vtables, we need to codegen the
        // vtable, but *not* generate a field for it in the case that
        // needs_explicit_vtable is false but has_vtable is true.
        //
        // Also, we need to generate the vtable in such a way it "inherits" from
        // the parent too.
        let mut fields = vec![];
        let mut struct_layout = StructLayoutTracker::new(ctx, self);
        if self.needs_explicit_vtable(ctx) {
            let vtable =
                Vtable::new(item.id(), self.methods(), self.base_members());
            vtable.codegen(ctx, result, whitelisted_items, item);

            let vtable_type = vtable.to_rust_ty(ctx).to_ptr(true, ctx.span());

            let vtable_field = StructFieldBuilder::named("vtable_")
                .pub_()
                .build_ty(vtable_type);

            struct_layout.saw_vtable();

            fields.push(vtable_field);
        }

        for (i, base) in self.base_members().iter().enumerate() {
            // Virtual bases are already taken into account by the vtable
            // pointer.
            //
            // FIXME(emilio): Is this always right?
            if base.is_virtual() {
                continue;
            }

            let base_ty = ctx.resolve_type(base.ty);
            // NB: We won't include unsized types in our base chain because they
            // would contribute to our size given the dummy field we insert for
            // unsized types.
            if base_ty.is_unsized(ctx) {
                continue;
            }

            for (i, ty_id) in applicable_template_args.iter().enumerate() {
                let template_arg_ty = ctx.resolve_type(*ty_id);
                if base_ty.signature_contains_named_type(ctx, template_arg_ty) {
                    template_args_used[i] = true;
                }
            }

            let inner = base.ty.to_rust_ty(ctx);
            let field_name = if i == 0 {
                "_base".into()
            } else {
                format!("_base_{}", i)
            };

            struct_layout.saw_base(base_ty);

            let field = StructFieldBuilder::named(field_name)
                .pub_()
                .build_ty(inner);
            fields.push(field);
        }
        if is_union {
            result.saw_union();
        }

        let layout = item.kind().expect_type().layout(ctx);

        let mut current_bitfield_width = None;
        let mut current_bitfield_layout: Option<Layout> = None;
        let mut current_bitfield_fields = vec![];
        let mut bitfield_count = 0;
        let struct_fields = self.fields();
        let fields_should_be_private = item.annotations()
            .private_fields()
            .unwrap_or(false);
        let struct_accessor_kind = item.annotations()
            .accessor_kind()
            .unwrap_or(FieldAccessorKind::None);

        let mut methods = vec![];
        let mut anonymous_field_count = 0;
        for field in struct_fields {
            debug_assert_eq!(current_bitfield_width.is_some(),
                             current_bitfield_layout.is_some());
            debug_assert_eq!(current_bitfield_width.is_some(),
                             !current_bitfield_fields.is_empty());

            let field_ty = ctx.resolve_type(field.ty());

            // Try to catch a bitfield contination early.
            if let (Some(ref mut bitfield_width), Some(width)) =
                (current_bitfield_width, field.bitfield()) {
                let layout = current_bitfield_layout.unwrap();
                debug!("Testing bitfield continuation {} {} {:?}",
                       *bitfield_width,
                       width,
                       layout);
                if *bitfield_width + width <= (layout.size * 8) as u32 {
                    *bitfield_width += width;
                    current_bitfield_fields.push(field);
                    continue;
                }
            }

            // Flush the current bitfield.
            if current_bitfield_width.is_some() {
                debug_assert!(!current_bitfield_fields.is_empty());
                let bitfield_fields =
                    mem::replace(&mut current_bitfield_fields, vec![]);
                let bitfield_layout = Bitfield::new(&mut bitfield_count,
                                                    bitfield_fields)
                    .codegen_fields(ctx, &mut fields, &mut methods);
                struct_layout.saw_bitfield_batch(bitfield_layout);

                current_bitfield_width = None;
                current_bitfield_layout = None;
            }
            debug_assert!(current_bitfield_fields.is_empty());

            if let Some(width) = field.bitfield() {
                let layout = field_ty.layout(ctx)
                    .expect("Bitfield type without layout?");
                current_bitfield_width = Some(width);
                current_bitfield_layout = Some(layout);
                current_bitfield_fields.push(field);
                continue;
            }

            for (i, ty_id) in applicable_template_args.iter().enumerate() {
                let template_arg = ctx.resolve_type(*ty_id);
                if field_ty.signature_contains_named_type(ctx, template_arg) {
                    template_args_used[i] = true;
                }
            }

            let ty = field.ty().to_rust_ty(ctx);

            // NB: In unstable rust we use proper `union` types.
            let ty = if is_union && !ctx.options().unstable_rust {
                if ctx.options().enable_cxx_namespaces {
                    quote_ty!(ctx.ext_cx(), root::__BindgenUnionField<$ty>)
                } else {
                    quote_ty!(ctx.ext_cx(), __BindgenUnionField<$ty>)
                }
            } else if let Some(item) = field_ty.is_incomplete_array(ctx) {
                result.saw_incomplete_array();

                let inner = item.to_rust_ty(ctx);

                if ctx.options().enable_cxx_namespaces {
                    quote_ty!(ctx.ext_cx(), root::__IncompleteArrayField<$inner>)
                } else {
                    quote_ty!(ctx.ext_cx(), __IncompleteArrayField<$inner>)
                }
            } else {
                ty
            };

            let mut attrs = vec![];
            if ctx.options().generate_comments {
                if let Some(comment) = field.comment() {
                    attrs.push(attributes::doc(comment));
                }
            }
            let field_name = match field.name() {
                Some(name) => ctx.rust_mangle(name).into_owned(),
                None => {
                    anonymous_field_count += 1;
                    format!("__bindgen_anon_{}", anonymous_field_count)
                }
            };

            if let Some(padding_field) =
                struct_layout.pad_field(&field_name, field_ty, field.offset()) {
                fields.push(padding_field);
            }

            let is_private = field.annotations()
                .private_fields()
                .unwrap_or(fields_should_be_private);

            let accessor_kind = field.annotations()
                .accessor_kind()
                .unwrap_or(struct_accessor_kind);

            let mut field = StructFieldBuilder::named(&field_name);

            if !is_private {
                field = field.pub_();
            }

            let field = field.with_attrs(attrs)
                .build_ty(ty.clone());

            fields.push(field);

            // TODO: Factor the following code out, please!
            if accessor_kind == FieldAccessorKind::None {
                continue;
            }

            let getter_name =
                ctx.rust_ident_raw(&format!("get_{}", field_name));
            let mutable_getter_name =
                ctx.rust_ident_raw(&format!("get_{}_mut", field_name));
            let field_name = ctx.rust_ident_raw(&field_name);

            let accessor_methods_impl = match accessor_kind {
                FieldAccessorKind::None => unreachable!(),
                FieldAccessorKind::Regular => {
                    quote_item!(ctx.ext_cx(),
                        impl X {
                            #[inline]
                            pub fn $getter_name(&self) -> &$ty {
                                &self.$field_name
                            }

                            #[inline]
                            pub fn $mutable_getter_name(&mut self) -> &mut $ty {
                                &mut self.$field_name
                            }
                        }
                    )
                }
                FieldAccessorKind::Unsafe => {
                    quote_item!(ctx.ext_cx(),
                        impl X {
                            #[inline]
                            pub unsafe fn $getter_name(&self) -> &$ty {
                                &self.$field_name
                            }

                            #[inline]
                            pub unsafe fn $mutable_getter_name(&mut self)
                                -> &mut $ty {
                                &mut self.$field_name
                            }
                        }
                    )
                }
                FieldAccessorKind::Immutable => {
                    quote_item!(ctx.ext_cx(),
                        impl X {
                            #[inline]
                            pub fn $getter_name(&self) -> &$ty {
                                &self.$field_name
                            }
                        }
                    )
                }
            };

            match accessor_methods_impl.unwrap().node {
                ast::ItemKind::Impl(_, _, _, _, _, ref items) => {
                    methods.extend(items.clone())
                }
                _ => unreachable!(),
            }
        }

        // Flush the last bitfield if any.
        //
        // FIXME: Reduce duplication with the loop above.
        // FIXME: May need to pass current_bitfield_layout too.
        if current_bitfield_width.is_some() {
            debug_assert!(!current_bitfield_fields.is_empty());
            let bitfield_fields = mem::replace(&mut current_bitfield_fields,
                                               vec![]);
            let bitfield_layout = Bitfield::new(&mut bitfield_count,
                                                bitfield_fields)
                .codegen_fields(ctx, &mut fields, &mut methods);
            struct_layout.saw_bitfield_batch(bitfield_layout);
        }
        debug_assert!(current_bitfield_fields.is_empty());

        if is_union && !ctx.options().unstable_rust {
            let layout = layout.expect("Unable to get layout information?");
            let ty = BlobTyBuilder::new(layout).build();
            let field = StructFieldBuilder::named("bindgen_union_field")
                .pub_()
                .build_ty(ty);

            struct_layout.saw_union(layout);

            fields.push(field);
        }

        // Yeah, sorry about that.
        if item.is_opaque(ctx) {
            fields.clear();
            methods.clear();
            for i in 0..template_args_used.len() {
                template_args_used[i] = false;
            }

            match layout {
                Some(l) => {
                    let ty = BlobTyBuilder::new(l).build();
                    let field =
                        StructFieldBuilder::named("_bindgen_opaque_blob")
                            .pub_()
                            .build_ty(ty);
                    fields.push(field);
                }
                None => {
                    warn!("Opaque type without layout! Expect dragons!");
                }
            }
        } else if !is_union && !self.is_unsized(ctx) {
            if let Some(padding_field) =
                layout.and_then(|layout| struct_layout.pad_struct(&canonical_name, layout)) {
                fields.push(padding_field);
            }

            if let Some(align_field) =
                layout.and_then(|layout| struct_layout.align_struct(layout)) {
                fields.push(align_field);
            }
        }

        // C requires every struct to be addressable, so what C compilers do is
        // making the struct 1-byte sized.
        //
        // NOTE: This check is conveniently here to avoid the dummy fields we
        // may add for unused template parameters.
        if self.is_unsized(ctx) {
            let ty = BlobTyBuilder::new(Layout::new(1, 1)).build();
            let field = StructFieldBuilder::named("_address")
                .pub_()
                .build_ty(ty);
            fields.push(field);
        }

        // Append any extra template arguments that nobody has used so far.
        for (i, ty) in applicable_template_args.iter().enumerate() {
            if !template_args_used[i] {
                let name = ctx.resolve_type(*ty).name().unwrap();
                let ident = ctx.rust_ident(name);
                let prefix = ctx.trait_prefix();
                let phantom = quote_ty!(ctx.ext_cx(),
                                        ::$prefix::marker::PhantomData<$ident>);
                let field = StructFieldBuilder::named(format!("_phantom_{}",
                                                              i))
                    .pub_()
                    .build_ty(phantom);
                fields.push(field)
            }
        }


        let mut generics = aster::AstBuilder::new().generics();
        for template_arg in applicable_template_args.iter() {
            // Take into account that here only arrive named types, not
            // template specialisations that would need to be
            // instantiated.
            //
            // TODO: Add template args from the parent, here and in
            // `to_rust_ty`!!
            let template_arg = ctx.resolve_type(*template_arg);
            generics = generics.ty_param_id(template_arg.name().unwrap());
        }

        let generics = generics.build();

        let rust_struct = builder.with_generics(generics.clone())
            .with_fields(fields)
            .build();
        result.push(rust_struct);

        // Generate the inner types and all that stuff.
        //
        // TODO: In the future we might want to be smart, and use nested
        // modules, and whatnot.
        for ty in self.inner_types() {
            let child_item = ctx.resolve_item(*ty);
            // assert_eq!(child_item.parent_id(), item.id());
            child_item.codegen(ctx, result, whitelisted_items, &());
        }

        // NOTE: Some unexposed attributes (like alignment attributes) may
        // affect layout, so we're bad and pray to the gods for avoid sending
        // all the tests to shit when parsing things like max_align_t.
        if self.found_unknown_attr() {
            warn!("Type {} has an unkown attribute that may affect layout",
                  canonical_name);
        }

        if applicable_template_args.is_empty() {
            for var in self.inner_vars() {
                ctx.resolve_item(*var)
                    .codegen(ctx, result, whitelisted_items, &());
            }

            if let Some(layout) = layout {
                let fn_name = format!("bindgen_test_layout_{}", canonical_name);
                let fn_name = ctx.rust_ident_raw(&fn_name);
                let type_name = ctx.rust_ident_raw(&canonical_name);
                let prefix = ctx.trait_prefix();
                let size_of_expr = quote_expr!(ctx.ext_cx(),
                                ::$prefix::mem::size_of::<$type_name>());
                let align_of_expr = quote_expr!(ctx.ext_cx(),
                                ::$prefix::mem::align_of::<$type_name>());
                let size = layout.size;
                let align = layout.align;

                let check_struct_align = if align > mem::size_of::<*mut ()>() {
                    // FIXME when [RFC 1358](https://github.com/rust-lang/rust/issues/33626) ready
                    None
                } else {
                    quote_item!(ctx.ext_cx(),
                        assert_eq!($align_of_expr,
                                   $align,
                                   concat!("Alignment of ", stringify!($type_name)));
                    )
                };

                // FIXME when [issue #465](https://github.com/servo/rust-bindgen/issues/465) ready
                let too_many_base_vtables = self.base_members()
                    .iter()
                    .filter(|base| {
                        ctx.resolve_type(base.ty).has_vtable(ctx)
                    })
                    .count() > 1;

                let should_skip_field_offset_checks = item.is_opaque(ctx) ||
                                                      too_many_base_vtables;

                let check_field_offset = if should_skip_field_offset_checks {
                    None
                } else {
                    let asserts = self.fields()
                    .iter()
                    .filter(|field| field.bitfield().is_none())
                    .flat_map(|field| {
                        field.name().and_then(|name| {
                            field.offset().and_then(|offset| {
                                let field_offset = offset / 8;
                                let field_name = ctx.rust_ident(name);

                                quote_item!(ctx.ext_cx(),
                                    assert_eq!(unsafe { &(*(0 as *const $type_name)).$field_name as *const _ as usize },
                                               $field_offset,
                                               concat!("Alignment of field: ", stringify!($type_name), "::", stringify!($field_name)));
                                )
                            })
                        })
                    }).collect::<Vec<P<ast::Item>>>();

                    Some(asserts)
                };

                let item = quote_item!(ctx.ext_cx(),
                    #[test]
                    fn $fn_name() {
                        assert_eq!($size_of_expr,
                                   $size,
                                   concat!("Size of: ", stringify!($type_name)));

                        $check_struct_align
                        $check_field_offset
                    })
                    .unwrap();
                result.push(item);
            }

            let mut method_names = Default::default();
            if ctx.options().codegen_config.methods {
                for method in self.methods() {
                    assert!(method.kind() != MethodKind::Constructor);
                    method.codegen_method(ctx,
                                          &mut methods,
                                          &mut method_names,
                                          result,
                                          whitelisted_items,
                                          self);
                }
            }

            if ctx.options().codegen_config.constructors {
                for sig in self.constructors() {
                    Method::new(MethodKind::Constructor,
                                *sig,
                                /* const */
                                false)
                        .codegen_method(ctx,
                                        &mut methods,
                                        &mut method_names,
                                        result,
                                        whitelisted_items,
                                        self);
                }
            }
        }

        // NB: We can't use to_rust_ty here since for opaque types this tries to
        // use the specialization knowledge to generate a blob field.
        let ty_for_impl = aster::AstBuilder::new()
            .ty()
            .path()
            .segment(&canonical_name)
            .with_generics(generics.clone())
            .build()
            .build();

        if needs_clone_impl {
            let impl_ = quote_item!(ctx.ext_cx(),
                impl X {
                    fn clone(&self) -> Self { *self }
                }
            );

            let impl_ = match impl_.unwrap().node {
                ast::ItemKind::Impl(_, _, _, _, _, ref items) => items.clone(),
                _ => unreachable!(),
            };

            let clone_impl = aster::AstBuilder::new()
                .item()
                .impl_()
                .trait_()
                .id("Clone")
                .build()
                .with_generics(generics.clone())
                .with_items(impl_)
                .build_ty(ty_for_impl.clone());

            result.push(clone_impl);
        }

        if needs_default_impl {
            let prefix = ctx.trait_prefix();
            let impl_ = quote_item!(ctx.ext_cx(),
                impl X {
                    fn default() -> Self { unsafe { ::$prefix::mem::zeroed() } }
                }
            );

            let impl_ = match impl_.unwrap().node {
                ast::ItemKind::Impl(_, _, _, _, _, ref items) => items.clone(),
                _ => unreachable!(),
            };

            let default_impl = aster::AstBuilder::new()
                .item()
                .impl_()
                .trait_()
                .id("Default")
                .build()
                .with_generics(generics.clone())
                .with_items(impl_)
                .build_ty(ty_for_impl.clone());

            result.push(default_impl);
        }

        if !methods.is_empty() {
            let methods = aster::AstBuilder::new()
                .item()
                .impl_()
                .with_generics(generics)
                .with_items(methods)
                .build_ty(ty_for_impl);
            result.push(methods);
        }
    }
}

trait MethodCodegen {
    fn codegen_method<'a>(&self,
                          ctx: &BindgenContext,
                          methods: &mut Vec<ast::ImplItem>,
                          method_names: &mut HashMap<String, usize>,
                          result: &mut CodegenResult<'a>,
                          whitelisted_items: &ItemSet,
                          parent: &CompInfo);
}

impl MethodCodegen for Method {
    fn codegen_method<'a>(&self,
                          ctx: &BindgenContext,
                          methods: &mut Vec<ast::ImplItem>,
                          method_names: &mut HashMap<String, usize>,
                          result: &mut CodegenResult<'a>,
                          whitelisted_items: &ItemSet,
                          _parent: &CompInfo) {
        if self.is_virtual() {
            return; // FIXME
        }
        // First of all, output the actual function.
        let function_item = ctx.resolve_item(self.signature());
        function_item.codegen(ctx, result, whitelisted_items, &());

        let function = function_item.expect_function();
        let signature_item = ctx.resolve_item(function.signature());
        let mut name = match self.kind() {
            MethodKind::Constructor => "new".into(),
            _ => function.name().to_owned(),
        };

        let signature = match *signature_item.expect_type().kind() {
            TypeKind::Function(ref sig) => sig,
            _ => panic!("How in the world?"),
        };

        // Do not generate variadic methods, since rust does not allow
        // implementing them, and we don't do a good job at it anyway.
        if signature.is_variadic() {
            return;
        }

        let count = {
            let mut count = method_names.entry(name.clone())
                .or_insert(0);
            *count += 1;
            *count - 1
        };

        if count != 0 {
            name.push_str(&count.to_string());
        }

        let function_name = function_item.canonical_name(ctx);
        let mut fndecl = utils::rust_fndecl_from_signature(ctx, signature_item)
            .unwrap();
        if !self.is_static() && !self.is_constructor() {
            let mutability = if self.is_const() {
                ast::Mutability::Immutable
            } else {
                ast::Mutability::Mutable
            };

            assert!(!fndecl.inputs.is_empty());

            // FIXME: use aster here.
            fndecl.inputs[0] = ast::Arg {
                ty: P(ast::Ty {
                    id: ast::DUMMY_NODE_ID,
                    node: ast::TyKind::Rptr(None, ast::MutTy {
                        ty: P(ast::Ty {
                            id: ast::DUMMY_NODE_ID,
                            node: ast::TyKind::ImplicitSelf,
                            span: ctx.span()
                        }),
                        mutbl: mutability,
                    }),
                    span: ctx.span(),
                }),
                pat: P(ast::Pat {
                    id: ast::DUMMY_NODE_ID,
                    node: ast::PatKind::Ident(
                        ast::BindingMode::ByValue(ast::Mutability::Immutable),
                        respan(ctx.span(), ctx.ext_cx().ident_of("self")),
                        None
                    ),
                    span: ctx.span(),
                }),
                id: ast::DUMMY_NODE_ID,
            };
        }

        // If it's a constructor, we always return `Self`, and we inject the
        // "this" parameter, so there's no need to ask the user for it.
        //
        // Note that constructors in Clang are represented as functions with
        // return-type = void.
        if self.is_constructor() {
            fndecl.inputs.remove(0);
            fndecl.output = ast::FunctionRetTy::Ty(quote_ty!(ctx.ext_cx(),
                                                             Self));
        }

        let sig = ast::MethodSig {
            unsafety: ast::Unsafety::Unsafe,
            abi: Abi::Rust,
            decl: P(fndecl),
            generics: ast::Generics::default(),
            constness: respan(ctx.span(), ast::Constness::NotConst),
        };

        let mut exprs = helpers::ast_ty::arguments_from_signature(&signature,
                                                                  ctx);

        let mut stmts = vec![];

        // If it's a constructor, we need to insert an extra parameter with a
        // variable called `__bindgen_tmp` we're going to create.
        if self.is_constructor() {
            let prefix = ctx.trait_prefix();
            let tmp_variable_decl =
                quote_stmt!(ctx.ext_cx(),
                            let mut __bindgen_tmp = ::$prefix::mem::uninitialized())
                .unwrap();
            stmts.push(tmp_variable_decl);
            exprs[0] = quote_expr!(ctx.ext_cx(), &mut __bindgen_tmp);
        } else if !self.is_static() {
            assert!(!exprs.is_empty());
            exprs[0] = if self.is_const() {
                quote_expr!(ctx.ext_cx(), &*self)
            } else {
                quote_expr!(ctx.ext_cx(), &mut *self)
            };
        };

        let call = aster::expr::ExprBuilder::new()
            .call()
            .id(function_name)
            .with_args(exprs)
            .build();

        stmts.push(ast::Stmt {
            id: ast::DUMMY_NODE_ID,
            node: ast::StmtKind::Expr(call),
            span: ctx.span(),
        });

        if self.is_constructor() {
            stmts.push(quote_stmt!(ctx.ext_cx(), __bindgen_tmp).unwrap());
        }

        let block = ast::Block {
            stmts: stmts,
            id: ast::DUMMY_NODE_ID,
            rules: ast::BlockCheckMode::Default,
            span: ctx.span(),
        };

        let mut attrs = vec![];
        attrs.push(attributes::inline());

        let item = ast::ImplItem {
            id: ast::DUMMY_NODE_ID,
            ident: ctx.rust_ident(&name),
            vis: ast::Visibility::Public,
            attrs: attrs,
            node: ast::ImplItemKind::Method(sig, P(block)),
            defaultness: ast::Defaultness::Final,
            span: ctx.span(),
        };

        methods.push(item);
    }
}

/// A helper type to construct enums, either bitfield ones or rust-style ones.
enum EnumBuilder<'a> {
    Rust(aster::item::ItemEnumBuilder<aster::invoke::Identity>),
    Bitfield {
        canonical_name: &'a str,
        aster: P<ast::Item>,
    },
    Consts { aster: P<ast::Item> },
}

impl<'a> EnumBuilder<'a> {
    /// Create a new enum given an item builder, a canonical name, a name for
    /// the representation, and whether it should be represented as a rust enum.
    fn new(aster: aster::item::ItemBuilder<aster::invoke::Identity>,
           name: &'a str,
           repr: P<ast::Ty>,
           bitfield_like: bool,
           constify: bool)
           -> Self {
        if bitfield_like {
            EnumBuilder::Bitfield {
                canonical_name: name,
                aster: aster.tuple_struct(name)
                    .field()
                    .pub_()
                    .build_ty(repr)
                    .build(),
            }
        } else if constify {
            EnumBuilder::Consts {
                aster: aster.type_(name).build_ty(repr),
            }
        } else {
            EnumBuilder::Rust(aster.enum_(name))
        }
    }

    /// Add a variant to this enum.
    fn with_variant<'b>(self,
                        ctx: &BindgenContext,
                        variant: &EnumVariant,
                        mangling_prefix: Option<&String>,
                        rust_ty: P<ast::Ty>,
                        result: &mut CodegenResult<'b>)
                        -> Self {
        let variant_name = ctx.rust_mangle(variant.name());
        let expr = aster::AstBuilder::new().expr();
        let expr = match variant.val() {
            EnumVariantValue::Signed(v) => helpers::ast_ty::int_expr(v),
            EnumVariantValue::Unsigned(v) => expr.uint(v),
        };

        match self {
            EnumBuilder::Rust(b) => {
                EnumBuilder::Rust(b.with_variant_(ast::Variant_ {
                    name: ctx.rust_ident(&*variant_name),
                    attrs: vec![],
                    data: ast::VariantData::Unit(ast::DUMMY_NODE_ID),
                    disr_expr: Some(expr),
                }))
            }
            EnumBuilder::Bitfield { canonical_name, .. } => {
                let constant_name = match mangling_prefix {
                    Some(prefix) => {
                        Cow::Owned(format!("{}_{}", prefix, variant_name))
                    }
                    None => variant_name,
                };

                let constant = aster::AstBuilder::new()
                    .item()
                    .pub_()
                    .const_(&*constant_name)
                    .expr()
                    .call()
                    .id(canonical_name)
                    .arg()
                    .build(expr)
                    .build()
                    .build(rust_ty);
                result.push(constant);
                self
            }
            EnumBuilder::Consts { .. } => {
                let constant_name = match mangling_prefix {
                    Some(prefix) => {
                        Cow::Owned(format!("{}_{}", prefix, variant_name))
                    }
                    None => variant_name,
                };

                let constant = aster::AstBuilder::new()
                    .item()
                    .pub_()
                    .const_(&*constant_name)
                    .expr()
                    .build(expr)
                    .build(rust_ty);

                result.push(constant);
                self
            }
        }
    }

    fn build<'b>(self,
                 ctx: &BindgenContext,
                 rust_ty: P<ast::Ty>,
                 result: &mut CodegenResult<'b>)
                 -> P<ast::Item> {
        match self {
            EnumBuilder::Rust(b) => b.build(),
            EnumBuilder::Bitfield { canonical_name, aster } => {
                let rust_ty_name = ctx.rust_ident_raw(canonical_name);
                let prefix = ctx.trait_prefix();

                let impl_ = quote_item!(ctx.ext_cx(),
                    impl ::$prefix::ops::BitOr<$rust_ty> for $rust_ty {
                        type Output = Self;

                        #[inline]
                        fn bitor(self, other: Self) -> Self {
                            $rust_ty_name(self.0 | other.0)
                        }
                    }
                )
                    .unwrap();

                result.push(impl_);
                aster
            }
            EnumBuilder::Consts { aster, .. } => aster,
        }
    }
}

impl CodeGenerator for Enum {
    type Extra = Item;

    fn codegen<'a>(&self,
                   ctx: &BindgenContext,
                   result: &mut CodegenResult<'a>,
                   _whitelisted_items: &ItemSet,
                   item: &Item) {
        debug!("<Enum as CodeGenerator>::codegen: item = {:?}", item);

        let name = item.canonical_name(ctx);
        let enum_ty = item.expect_type();
        let layout = enum_ty.layout(ctx);

        let repr = self.repr().map(|repr| ctx.resolve_type(repr));
        let repr = match repr {
            Some(repr) => {
                match *repr.canonical_type(ctx).kind() {
                    TypeKind::Int(int_kind) => int_kind,
                    _ => panic!("Unexpected type as enum repr"),
                }
            }
            None => {
                warn!("Guessing type of enum! Forward declarations of enums \
                      shouldn't be legal!");
                IntKind::Int
            }
        };

        let signed = repr.is_signed();
        let size = layout.map(|l| l.size)
            .or_else(|| repr.known_size())
            .unwrap_or(0);

        let repr_name = match (signed, size) {
            (true, 1) => "i8",
            (false, 1) => "u8",
            (true, 2) => "i16",
            (false, 2) => "u16",
            (true, 4) => "i32",
            (false, 4) => "u32",
            (true, 8) => "i64",
            (false, 8) => "u64",
            _ => {
                warn!("invalid enum decl: signed: {}, size: {}", signed, size);
                "i32"
            }
        };

        let mut builder = aster::AstBuilder::new().item().pub_();

        // FIXME(emilio): These should probably use the path so it can
        // disambiguate between namespaces, just like is_opaque etc.
        let is_bitfield = {
            ctx.options().bitfield_enums.matches(&name) ||
            (enum_ty.name().is_none() &&
             self.variants()
                .iter()
                .any(|v| ctx.options().bitfield_enums.matches(&v.name())))
        };

        let is_constified_enum = {
            ctx.options().constified_enums.matches(&name) ||
            (enum_ty.name().is_none() &&
             self.variants()
                .iter()
                .any(|v| ctx.options().constified_enums.matches(&v.name())))
        };

        let is_rust_enum = !is_bitfield && !is_constified_enum;

        // FIXME: Rust forbids repr with empty enums. Remove this condition when
        // this is allowed.
        //
        // TODO(emilio): Delegate this to the builders?
        if is_rust_enum {
            if !self.variants().is_empty() {
                builder = builder.with_attr(attributes::repr(repr_name));
            }
        } else if is_bitfield {
            builder = builder.with_attr(attributes::repr("C"));
        }

        if ctx.options().generate_comments {
            if let Some(comment) = item.comment() {
                builder = builder.with_attr(attributes::doc(comment));
            }
        }

        if !is_constified_enum {
            let derives = attributes::derives(&["Debug",
                                                "Copy",
                                                "Clone",
                                                "PartialEq",
                                                "Eq",
                                                "Hash"]);

            builder = builder.with_attr(derives);
        }

        fn add_constant<'a>(enum_: &Type,
                            // Only to avoid recomputing every time.
                            enum_canonical_name: &str,
                            // May be the same as "variant" if it's because the
                            // enum is unnamed and we still haven't seen the
                            // value.
                            variant_name: &str,
                            referenced_name: &str,
                            enum_rust_ty: P<ast::Ty>,
                            result: &mut CodegenResult<'a>) {
            let constant_name = if enum_.name().is_some() {
                format!("{}_{}", enum_canonical_name, variant_name)
            } else {
                variant_name.into()
            };

            let constant = aster::AstBuilder::new()
                .item()
                .pub_()
                .const_(constant_name)
                .expr()
                .path()
                .ids(&[&*enum_canonical_name, referenced_name])
                .build()
                .build(enum_rust_ty);
            result.push(constant);
        }

        let repr = self.repr()
            .map(|repr| repr.to_rust_ty(ctx))
            .unwrap_or_else(|| helpers::ast_ty::raw_type(ctx, repr_name));

        let mut builder = EnumBuilder::new(builder,
                                           &name,
                                           repr,
                                           is_bitfield,
                                           is_constified_enum);

        // A map where we keep a value -> variant relation.
        let mut seen_values = HashMap::<_, String>::new();
        let enum_rust_ty = item.to_rust_ty(ctx);
        let is_toplevel = item.is_toplevel(ctx);

        // Used to mangle the constants we generate in the unnamed-enum case.
        let parent_canonical_name = if is_toplevel {
            None
        } else {
            Some(item.parent_id().canonical_name(ctx))
        };

        let constant_mangling_prefix = if enum_ty.name().is_none() {
            parent_canonical_name.as_ref().map(|n| &*n)
        } else {
            Some(&name)
        };

        // NB: We defer the creation of constified variants, in case we find
        // another variant with the same value (which is the common thing to
        // do).
        let mut constified_variants = VecDeque::new();

        let mut iter = self.variants().iter().peekable();
        while let Some(variant) = iter.next()
            .or_else(|| constified_variants.pop_front()) {
            if variant.hidden() {
                continue;
            }

            if variant.force_constification() && iter.peek().is_some() {
                constified_variants.push_back(variant);
                continue;
            }

            match seen_values.entry(variant.val()) {
                Entry::Occupied(ref entry) => {
                    if is_rust_enum {
                        let variant_name = ctx.rust_mangle(variant.name());
                        let mangled_name = if is_toplevel ||
                                              enum_ty.name().is_some() {
                            variant_name
                        } else {
                            let parent_name = parent_canonical_name.as_ref()
                                .unwrap();

                            Cow::Owned(format!("{}_{}",
                                               parent_name,
                                               variant_name))
                        };

                        let existing_variant_name = entry.get();
                        add_constant(enum_ty,
                                     &name,
                                     &*mangled_name,
                                     existing_variant_name,
                                     enum_rust_ty.clone(),
                                     result);
                    } else {
                        builder = builder.with_variant(ctx,
                                          variant,
                                          constant_mangling_prefix,
                                          enum_rust_ty.clone(),
                                          result);
                    }
                }
                Entry::Vacant(entry) => {
                    builder = builder.with_variant(ctx,
                                                   variant,
                                                   constant_mangling_prefix,
                                                   enum_rust_ty.clone(),
                                                   result);

                    let variant_name = ctx.rust_mangle(variant.name());

                    // If it's an unnamed enum, or constification is enforced,
                    // we also generate a constant so it can be properly
                    // accessed.
                    if (is_rust_enum && enum_ty.name().is_none()) ||
                       variant.force_constification() {
                        let mangled_name = if is_toplevel {
                            variant_name.clone()
                        } else {
                            let parent_name = parent_canonical_name.as_ref()
                                .unwrap();

                            Cow::Owned(format!("{}_{}",
                                               parent_name,
                                               variant_name))
                        };

                        add_constant(enum_ty,
                                     &name,
                                     &mangled_name,
                                     &variant_name,
                                     enum_rust_ty.clone(),
                                     result);
                    }

                    entry.insert(variant_name.into_owned());
                }
            }
        }

        let enum_ = builder.build(ctx, enum_rust_ty, result);
        result.push(enum_);
    }
}

trait ToRustTy {
    type Extra;

    fn to_rust_ty(&self,
                  ctx: &BindgenContext,
                  extra: &Self::Extra)
                  -> P<ast::Ty>;
}

trait ItemToRustTy {
    fn to_rust_ty(&self, ctx: &BindgenContext) -> P<ast::Ty>;
}

// Convenience implementation.
impl ItemToRustTy for ItemId {
    fn to_rust_ty(&self, ctx: &BindgenContext) -> P<ast::Ty> {
        ctx.resolve_item(*self).to_rust_ty(ctx)
    }
}

impl ItemToRustTy for Item {
    fn to_rust_ty(&self, ctx: &BindgenContext) -> P<ast::Ty> {
        self.kind().expect_type().to_rust_ty(ctx, self)
    }
}

impl ToRustTy for Type {
    type Extra = Item;

    fn to_rust_ty(&self, ctx: &BindgenContext, item: &Item) -> P<ast::Ty> {
        use self::helpers::ast_ty::*;

        match *self.kind() {
            TypeKind::Void => raw_type(ctx, "c_void"),
            // TODO: we should do something smart with nullptr, or maybe *const
            // c_void is enough?
            TypeKind::NullPtr => {
                raw_type(ctx, "c_void").to_ptr(true, ctx.span())
            }
            TypeKind::Int(ik) => {
                match ik {
                    IntKind::Bool => aster::ty::TyBuilder::new().bool(),
                    IntKind::Char => raw_type(ctx, "c_char"),
                    IntKind::UChar => raw_type(ctx, "c_uchar"),
                    IntKind::Short => raw_type(ctx, "c_short"),
                    IntKind::UShort => raw_type(ctx, "c_ushort"),
                    IntKind::Int => raw_type(ctx, "c_int"),
                    IntKind::UInt => raw_type(ctx, "c_uint"),
                    IntKind::Long => raw_type(ctx, "c_long"),
                    IntKind::ULong => raw_type(ctx, "c_ulong"),
                    IntKind::LongLong => raw_type(ctx, "c_longlong"),
                    IntKind::ULongLong => raw_type(ctx, "c_ulonglong"),

                    IntKind::I8 => aster::ty::TyBuilder::new().i8(),
                    IntKind::U8 => aster::ty::TyBuilder::new().u8(),
                    IntKind::I16 => aster::ty::TyBuilder::new().i16(),
                    IntKind::U16 => aster::ty::TyBuilder::new().u16(),
                    IntKind::I32 => aster::ty::TyBuilder::new().i32(),
                    IntKind::U32 => aster::ty::TyBuilder::new().u32(),
                    IntKind::I64 => aster::ty::TyBuilder::new().i64(),
                    IntKind::U64 => aster::ty::TyBuilder::new().u64(),
                    IntKind::Custom { name, .. } => {
                        let ident = ctx.rust_ident_raw(name);
                        quote_ty!(ctx.ext_cx(), $ident)
                    }
                    // FIXME: This doesn't generate the proper alignment, but we
                    // can't do better right now. We should be able to use
                    // i128/u128 when they're available.
                    IntKind::U128 | IntKind::I128 => {
                        aster::ty::TyBuilder::new().array(2).u64()
                    }
                }
            }
            TypeKind::Float(fk) => float_kind_rust_type(ctx, fk),
            TypeKind::Complex(fk) => {
                let float_path = float_kind_rust_type(ctx, fk);

                ctx.generated_bindegen_complex();
                if ctx.options().enable_cxx_namespaces {
                    quote_ty!(ctx.ext_cx(), root::__BindgenComplex<$float_path>)
                } else {
                    quote_ty!(ctx.ext_cx(), __BindgenComplex<$float_path>)
                }
            }
            TypeKind::Function(ref fs) => {
                let ty = fs.to_rust_ty(ctx, item);
                let prefix = ctx.trait_prefix();
                quote_ty!(ctx.ext_cx(), ::$prefix::option::Option<$ty>)
            }
            TypeKind::Array(item, len) => {
                let ty = item.to_rust_ty(ctx);
                aster::ty::TyBuilder::new().array(len).build(ty)
            }
            TypeKind::Enum(..) => {
                let path = item.namespace_aware_canonical_path(ctx);
                aster::AstBuilder::new().ty().path().ids(path).build()
            }
            TypeKind::TemplateInstantiation(inner, ref template_args) => {
                // PS: Sorry for the duplication here.
                let mut inner_ty = inner.to_rust_ty(ctx).unwrap();

                if let ast::TyKind::Path(_, ref mut path) = inner_ty.node {
                    let template_args = template_args.iter()
                        .map(|arg| arg.to_rust_ty(ctx))
                        .collect::<Vec<_>>();

                    path.segments.last_mut().unwrap().parameters = if
                        template_args.is_empty() {
                        None
                    } else {
                        Some(P(ast::PathParameters::AngleBracketed(
                            ast::AngleBracketedParameterData {
                                lifetimes: vec![],
                                types: P::from_vec(template_args),
                                bindings: P::from_vec(vec![]),
                            }
                        )))
                    }
                }

                P(inner_ty)
            }
            TypeKind::ResolvedTypeRef(inner) => inner.to_rust_ty(ctx),
            TypeKind::TemplateAlias(inner, _) |
            TypeKind::Alias(inner) => {
                let applicable_named_args = item.applicable_template_args(ctx)
                    .into_iter()
                    .filter(|arg| ctx.resolve_type(*arg).is_named())
                    .collect::<Vec<_>>();

                let spelling = self.name().expect("Unnamed alias?");
                if item.is_opaque(ctx) && !applicable_named_args.is_empty() {
                    // Pray if there's no available layout.
                    let layout = self.layout(ctx).unwrap_or_else(Layout::zero);
                    BlobTyBuilder::new(layout).build()
                } else if let Some(ty) = utils::type_from_named(ctx,
                                                                spelling,
                                                                inner) {
                    ty
                } else {
                    utils::build_templated_path(item,
                                                ctx,
                                                applicable_named_args)
                }
            }
            TypeKind::Comp(ref info) => {
                let template_args = item.applicable_template_args(ctx);
                if info.has_non_type_template_params() ||
                   (item.is_opaque(ctx) && !template_args.is_empty()) {
                    return match self.layout(ctx) {
                        Some(layout) => BlobTyBuilder::new(layout).build(),
                        None => {
                            warn!("Couldn't compute layout for a type with non \
                                  type template params or opaque, expect \
                                  dragons!");
                            aster::AstBuilder::new().ty().unit()
                        }
                    };
                }

                utils::build_templated_path(item, ctx, template_args)
            }
            TypeKind::BlockPointer => {
                let void = raw_type(ctx, "c_void");
                void.to_ptr(/* is_const = */
                            false,
                            ctx.span())
            }
            TypeKind::Pointer(inner) |
            TypeKind::Reference(inner) => {
                let inner = ctx.resolve_item(inner);
                let inner_ty = inner.expect_type();
                let ty = inner.to_rust_ty(ctx);

                // Avoid the first function pointer level, since it's already
                // represented in Rust.
                if inner_ty.canonical_type(ctx).is_function() {
                    ty
                } else {
                    let is_const = self.is_const() ||
                                   inner.expect_type().is_const();
                    ty.to_ptr(is_const, ctx.span())
                }
            }
            TypeKind::Named => {
                let name = item.canonical_name(ctx);
                let ident = ctx.rust_ident(&name);
                quote_ty!(ctx.ext_cx(), $ident)
            }
            TypeKind::ObjCInterface(..) => quote_ty!(ctx.ext_cx(), id),
            ref u @ TypeKind::UnresolvedTypeRef(..) => {
                unreachable!("Should have been resolved after parsing {:?}!", u)
            }
        }
    }
}

impl ToRustTy for FunctionSig {
    type Extra = Item;

    fn to_rust_ty(&self, ctx: &BindgenContext, _item: &Item) -> P<ast::Ty> {
        // TODO: we might want to consider ignoring the reference return value.
        let ret = utils::fnsig_return_ty(ctx, &self);
        let arguments = utils::fnsig_arguments(ctx, &self);

        let decl = P(ast::FnDecl {
            inputs: arguments,
            output: ret,
            variadic: self.is_variadic(),
        });

        let fnty = ast::TyKind::BareFn(P(ast::BareFnTy {
            unsafety: ast::Unsafety::Unsafe,
            abi: self.abi().expect("Invalid abi for function!"),
            lifetimes: vec![],
            decl: decl,
        }));

        P(ast::Ty {
            id: ast::DUMMY_NODE_ID,
            node: fnty,
            span: ctx.span(),
        })
    }
}

impl CodeGenerator for Function {
    type Extra = Item;

    fn codegen<'a>(&self,
                   ctx: &BindgenContext,
                   result: &mut CodegenResult<'a>,
                   _whitelisted_items: &ItemSet,
                   item: &Item) {
        debug!("<Function as CodeGenerator>::codegen: item = {:?}", item);

        let name = self.name();
        let mut canonical_name = item.canonical_name(ctx);
        let mangled_name = self.mangled_name();

        {
            let seen_symbol_name = mangled_name.unwrap_or(&canonical_name);

            // TODO: Maybe warn here if there's a type/argument mismatch, or
            // something?
            if result.seen_function(seen_symbol_name) {
                return;
            }
            result.saw_function(seen_symbol_name);
        }

        let signature_item = ctx.resolve_item(self.signature());
        let signature = signature_item.kind().expect_type().canonical_type(ctx);
        let signature = match *signature.kind() {
            TypeKind::Function(ref sig) => sig,
            _ => panic!("Signature kind is not a Function: {:?}", signature),
        };

        let fndecl = utils::rust_fndecl_from_signature(ctx, signature_item);

        let mut attributes = vec![];

        if ctx.options().generate_comments {
            if let Some(comment) = item.comment() {
                attributes.push(attributes::doc(comment));
            }
        }

        if let Some(mangled) = mangled_name {
            attributes.push(attributes::link_name(mangled));
        } else if name != canonical_name {
            attributes.push(attributes::link_name(name));
        }

        let foreign_item_kind =
            ast::ForeignItemKind::Fn(fndecl, ast::Generics::default());

        // Handle overloaded functions by giving each overload its own unique
        // suffix.
        let times_seen = result.overload_number(&canonical_name);
        if times_seen > 0 {
            write!(&mut canonical_name, "{}", times_seen).unwrap();
        }

        let foreign_item = ast::ForeignItem {
            ident: ctx.rust_ident_raw(&canonical_name),
            attrs: attributes,
            node: foreign_item_kind,
            id: ast::DUMMY_NODE_ID,
            span: ctx.span(),
            vis: ast::Visibility::Public,
        };

        let item = ForeignModBuilder::new(signature.abi()
                .expect("Invalid abi for function!"))
            .with_foreign_item(foreign_item)
            .build(ctx);

        result.push(item);
    }
}

impl CodeGenerator for ObjCInterface {
    type Extra = Item;
    fn codegen<'a>(&self,
                   ctx: &BindgenContext,
                   result: &mut CodegenResult<'a>,
                   _whitelisted_items: &ItemSet,
                   _: &Item) {
        let mut impl_items = vec![];
        let mut trait_items = vec![];

        for method in self.methods() {
            let signature = method.signature();
            let fn_args = utils::fnsig_arguments(ctx, signature);
            let fn_ret = utils::fnsig_return_ty(ctx, signature);
            let sig = aster::AstBuilder::new()
                .method_sig()
                .unsafe_()
                .fn_decl()
                .self_()
                .build(ast::SelfKind::Value(ast::Mutability::Immutable))
                .with_args(fn_args.clone())
                .build(fn_ret);

            // Collect the actual used argument names
            let arg_names: Vec<_> = fn_args.iter()
                .map(|ref arg| match arg.pat.node {
                    ast::PatKind::Ident(_, ref spanning, _) => {
                        spanning.node.name.as_str().to_string()
                    }
                    _ => {
                        panic!("odd argument!");
                    }
                })
                .collect();

            let methods_and_args =
                ctx.rust_ident(&method.format_method_call(&arg_names));
            let body = quote_stmt!(ctx.ext_cx(),
                                   msg_send![self, $methods_and_args])
                .unwrap();
            let block = ast::Block {
                stmts: vec![body],
                id: ast::DUMMY_NODE_ID,
                rules: ast::BlockCheckMode::Default,
                span: ctx.span(),
            };

            let attrs = vec![];

            let impl_item = ast::ImplItem {
                id: ast::DUMMY_NODE_ID,
                ident: ctx.rust_ident(method.rust_name()),
                vis: ast::Visibility::Inherited, // Public,
                attrs: attrs.clone(),
                node: ast::ImplItemKind::Method(sig.clone(), P(block)),
                defaultness: ast::Defaultness::Final,
                span: ctx.span(),
            };

            let trait_item = ast::TraitItem {
                id: ast::DUMMY_NODE_ID,
                ident: ctx.rust_ident(method.rust_name()),
                attrs: attrs,
                node: ast::TraitItemKind::Method(sig, None),
                span: ctx.span(),
            };

            impl_items.push(impl_item);
            trait_items.push(trait_item)
        }


        let trait_block = aster::AstBuilder::new()
            .item()
            .pub_()
            .trait_(self.name())
            .with_items(trait_items)
            .build();

        let ty_for_impl = quote_ty!(ctx.ext_cx(), id);
        let impl_block = aster::AstBuilder::new()
            .item()
            .impl_()
            .trait_()
            .id(self.name())
            .build()
            .with_items(impl_items)
            .build_ty(ty_for_impl);

        result.push(trait_block);
        result.push(impl_block);
        result.saw_objc();
    }
}



pub fn codegen(context: &mut BindgenContext) -> Vec<P<ast::Item>> {
    context.gen(|context| {
        let counter = Cell::new(0);
        let mut result = CodegenResult::new(&counter);

        debug!("codegen: {:?}", context.options());

        let whitelisted_items: ItemSet = context.whitelisted_items().collect();

        if context.options().emit_ir {
            for &id in whitelisted_items.iter() {
                let item = context.resolve_item(id);
                println!("ir: {:?} = {:#?}", id, item);
            }
        }

        if let Some(path) = context.options().emit_ir_graphviz.as_ref() {
            match context.emit_ir_graphviz(path.clone()) {
                Ok(()) => info!("Your dot file was generated successfully into: {}", path),
                Err(e) => error!("{}", e),
            }
        }

        context.resolve_item(context.root_module())
            .codegen(context, &mut result, &whitelisted_items, &());

        result.items
    })
}

mod utils {
    use super::ItemToRustTy;
    use aster;
    use ir::context::{BindgenContext, ItemId};
    use ir::function::FunctionSig;
    use ir::item::{Item, ItemCanonicalPath};
    use ir::ty::TypeKind;
    use std::mem;
    use syntax::ast;
    use syntax::ptr::P;

    pub fn prepend_objc_header(ctx: &BindgenContext,
                               result: &mut Vec<P<ast::Item>>) {
        let use_objc = if ctx.options().objc_extern_crate {
            quote_item!(ctx.ext_cx(),
                use objc;
            )
                .unwrap()
        } else {
            quote_item!(ctx.ext_cx(),
                #[macro_use]
                extern crate objc;
            )
                .unwrap()
        };


        let id_type = quote_item!(ctx.ext_cx(),
            #[allow(non_camel_case_types)]
            pub type id = *mut objc::runtime::Object;
        )
            .unwrap();

        let items = vec![use_objc, id_type];
        let old_items = mem::replace(result, items);
        result.extend(old_items.into_iter());
    }

    pub fn prepend_union_types(ctx: &BindgenContext,
                               result: &mut Vec<P<ast::Item>>) {
        let prefix = ctx.trait_prefix();

        // TODO(emilio): The fmt::Debug impl could be way nicer with
        // std::intrinsics::type_name, but...
        let union_field_decl = quote_item!(ctx.ext_cx(),
            #[repr(C)]
            pub struct __BindgenUnionField<T>(
                ::$prefix::marker::PhantomData<T>);
        )
            .unwrap();

        let union_field_impl = quote_item!(&ctx.ext_cx(),
            impl<T> __BindgenUnionField<T> {
                #[inline]
                pub fn new() -> Self {
                    __BindgenUnionField(::$prefix::marker::PhantomData)
                }

                #[inline]
                pub unsafe fn as_ref(&self) -> &T {
                    ::$prefix::mem::transmute(self)
                }

                #[inline]
                pub unsafe fn as_mut(&mut self) -> &mut T {
                    ::$prefix::mem::transmute(self)
                }
            }
        )
            .unwrap();

        let union_field_default_impl = quote_item!(&ctx.ext_cx(),
            impl<T> ::$prefix::default::Default for __BindgenUnionField<T> {
                #[inline]
                fn default() -> Self {
                    Self::new()
                }
            }
        )
            .unwrap();

        let union_field_clone_impl = quote_item!(&ctx.ext_cx(),
            impl<T> ::$prefix::clone::Clone for __BindgenUnionField<T> {
                #[inline]
                fn clone(&self) -> Self {
                    Self::new()
                }
            }
        )
            .unwrap();

        let union_field_copy_impl = quote_item!(&ctx.ext_cx(),
            impl<T> ::$prefix::marker::Copy for __BindgenUnionField<T> {}
        )
            .unwrap();

        let union_field_debug_impl = quote_item!(ctx.ext_cx(),
            impl<T> ::$prefix::fmt::Debug for __BindgenUnionField<T> {
                fn fmt(&self, fmt: &mut ::$prefix::fmt::Formatter)
                       -> ::$prefix::fmt::Result {
                    fmt.write_str("__BindgenUnionField")
                }
            }
        )
            .unwrap();

        let items = vec![union_field_decl,
                         union_field_impl,
                         union_field_default_impl,
                         union_field_clone_impl,
                         union_field_copy_impl,
                         union_field_debug_impl];

        let old_items = mem::replace(result, items);
        result.extend(old_items.into_iter());
    }

    pub fn prepend_incomplete_array_types(ctx: &BindgenContext,
                                          result: &mut Vec<P<ast::Item>>) {
        let prefix = ctx.trait_prefix();

        let incomplete_array_decl = quote_item!(ctx.ext_cx(),
            #[repr(C)]
            #[derive(Default)]
            pub struct __IncompleteArrayField<T>(
                ::$prefix::marker::PhantomData<T>);
        )
            .unwrap();

        let incomplete_array_impl = quote_item!(&ctx.ext_cx(),
            impl<T> __IncompleteArrayField<T> {
                #[inline]
                pub fn new() -> Self {
                    __IncompleteArrayField(::$prefix::marker::PhantomData)
                }

                #[inline]
                pub unsafe fn as_ptr(&self) -> *const T {
                    ::$prefix::mem::transmute(self)
                }

                #[inline]
                pub unsafe fn as_mut_ptr(&mut self) -> *mut T {
                    ::$prefix::mem::transmute(self)
                }

                #[inline]
                pub unsafe fn as_slice(&self, len: usize) -> &[T] {
                    ::$prefix::slice::from_raw_parts(self.as_ptr(), len)
                }

                #[inline]
                pub unsafe fn as_mut_slice(&mut self, len: usize) -> &mut [T] {
                    ::$prefix::slice::from_raw_parts_mut(self.as_mut_ptr(), len)
                }
            }
        )
            .unwrap();

        let incomplete_array_debug_impl = quote_item!(ctx.ext_cx(),
            impl<T> ::$prefix::fmt::Debug for __IncompleteArrayField<T> {
                fn fmt(&self, fmt: &mut ::$prefix::fmt::Formatter)
                       -> ::$prefix::fmt::Result {
                    fmt.write_str("__IncompleteArrayField")
                }
            }
        )
            .unwrap();

        let incomplete_array_clone_impl = quote_item!(&ctx.ext_cx(),
            impl<T> ::$prefix::clone::Clone for __IncompleteArrayField<T> {
                #[inline]
                fn clone(&self) -> Self {
                    Self::new()
                }
            }
        )
            .unwrap();

        let incomplete_array_copy_impl = quote_item!(&ctx.ext_cx(),
            impl<T> ::$prefix::marker::Copy for __IncompleteArrayField<T> {}
        )
            .unwrap();

        let items = vec![incomplete_array_decl,
                         incomplete_array_impl,
                         incomplete_array_debug_impl,
                         incomplete_array_clone_impl,
                         incomplete_array_copy_impl];

        let old_items = mem::replace(result, items);
        result.extend(old_items.into_iter());
    }

    pub fn prepend_complex_type(ctx: &BindgenContext,
                                result: &mut Vec<P<ast::Item>>) {
        let complex_type = quote_item!(ctx.ext_cx(),
            #[derive(PartialEq, Copy, Clone, Hash, Debug, Default)]
            #[repr(C)]
            pub struct __BindgenComplex<T> {
                pub re: T,
                pub im: T
            }
        )
            .unwrap();

        let items = vec![complex_type];
        let old_items = mem::replace(result, items);
        result.extend(old_items.into_iter());
    }

    pub fn build_templated_path(item: &Item,
                                ctx: &BindgenContext,
                                template_args: Vec<ItemId>)
                                -> P<ast::Ty> {
        let path = item.namespace_aware_canonical_path(ctx);
        let builder = aster::AstBuilder::new().ty().path();

        let template_args = template_args.iter()
            .map(|arg| arg.to_rust_ty(ctx))
            .collect::<Vec<_>>();

        // XXX: I suck at aster.
        if path.len() == 1 {
            return builder.segment(&path[0])
                .with_tys(template_args)
                .build()
                .build();
        }

        let mut builder = builder.id(&path[0]);
        for (i, segment) in path.iter().skip(1).enumerate() {
            // Take into account the skip(1)
            builder = if i == path.len() - 2 {
                // XXX Extra clone courtesy of the borrow checker.
                builder.segment(&segment)
                    .with_tys(template_args.clone())
                    .build()
            } else {
                builder.segment(&segment).build()
            }
        }

        builder.build()
    }

    fn primitive_ty(ctx: &BindgenContext, name: &str) -> P<ast::Ty> {
        let ident = ctx.rust_ident_raw(&name);
        quote_ty!(ctx.ext_cx(), $ident)
    }

    pub fn type_from_named(ctx: &BindgenContext,
                           name: &str,
                           _inner: ItemId)
                           -> Option<P<ast::Ty>> {
        // FIXME: We could use the inner item to check this is really a
        // primitive type but, who the heck overrides these anyway?
        Some(match name {
            "int8_t" => primitive_ty(ctx, "i8"),
            "uint8_t" => primitive_ty(ctx, "u8"),
            "int16_t" => primitive_ty(ctx, "i16"),
            "uint16_t" => primitive_ty(ctx, "u16"),
            "int32_t" => primitive_ty(ctx, "i32"),
            "uint32_t" => primitive_ty(ctx, "u32"),
            "int64_t" => primitive_ty(ctx, "i64"),
            "uint64_t" => primitive_ty(ctx, "u64"),

            "uintptr_t" | "size_t" => primitive_ty(ctx, "usize"),

            "intptr_t" | "ptrdiff_t" | "ssize_t" => primitive_ty(ctx, "isize"),
            _ => return None,
        })
    }

    pub fn rust_fndecl_from_signature(ctx: &BindgenContext,
                                      sig: &Item)
                                      -> P<ast::FnDecl> {
        use codegen::ToRustTy;

        let signature = sig.kind().expect_type().canonical_type(ctx);
        let signature = match *signature.kind() {
            TypeKind::Function(ref sig) => sig,
            _ => panic!("How?"),
        };

        let decl_ty = signature.to_rust_ty(ctx, sig);
        match decl_ty.unwrap().node {
            ast::TyKind::BareFn(bare_fn) => bare_fn.unwrap().decl,
            _ => panic!("How did this happen exactly?"),
        }
    }

    pub fn fnsig_return_ty(ctx: &BindgenContext,
                           sig: &FunctionSig)
                           -> ast::FunctionRetTy {
        let return_item = ctx.resolve_item(sig.return_type());
        if let TypeKind::Void = *return_item.kind().expect_type().kind() {
            ast::FunctionRetTy::Default(ctx.span())
        } else {
            ast::FunctionRetTy::Ty(return_item.to_rust_ty(ctx))
        }
    }

    pub fn fnsig_arguments(ctx: &BindgenContext,
                           sig: &FunctionSig)
                           -> Vec<ast::Arg> {
        use super::ToPtr;
        let mut unnamed_arguments = 0;
        sig.argument_types().iter().map(|&(ref name, ty)| {
            let arg_item = ctx.resolve_item(ty);
            let arg_ty = arg_item.kind().expect_type();

            // From the C90 standard[1]:
            //
            //     A declaration of a parameter as "array of type" shall be
            //     adjusted to "qualified pointer to type", where the type
            //     qualifiers (if any) are those specified within the [ and ] of
            //     the array type derivation.
            //
            // [1]: http://c0x.coding-guidelines.com/6.7.5.3.html
            let arg_ty = match *arg_ty.canonical_type(ctx).kind() {
                TypeKind::Array(t, _) => {
                    t.to_rust_ty(ctx).to_ptr(ctx.resolve_type(t).is_const(), ctx.span())
                },
                TypeKind::Pointer(inner) => {
                    let inner = ctx.resolve_item(inner);
                    let inner_ty = inner.expect_type();
                    if let TypeKind::ObjCInterface(_) = *inner_ty.canonical_type(ctx).kind() {
                        quote_ty!(ctx.ext_cx(), id)
                    } else {
                        arg_item.to_rust_ty(ctx)
                    }
                },
                _ => {
                    arg_item.to_rust_ty(ctx)
                }
            };

            let arg_name = match *name {
                Some(ref name) => ctx.rust_mangle(name).into_owned(),
                None => {
                    unnamed_arguments += 1;
                    format!("arg{}", unnamed_arguments)
                }
            };

            assert!(!arg_name.is_empty());

            ast::Arg {
                ty: arg_ty,
                pat: aster::AstBuilder::new().pat().id(arg_name),
                id: ast::DUMMY_NODE_ID,
            }
        }).collect::<Vec<_>>()
    }
}
